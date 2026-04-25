#include "ArborCompileTools.h"

#include "ILiveCodingModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace ArborCompileToolsInternal
{
	/** Singleton patch-complete observer. Lazily attached to ILiveCodingModule
	 *  on first use of any UArborCompileTools UFUNCTION so we don't touch the
	 *  Arbor module's StartupModule path.
	 */
	struct FPatchObserver
	{
		bool bAttached = false;
		bool bHasResult = false;
		FDateTime LastPatchUtc = FDateTime();
		FDelegateHandle Handle;

		void EnsureAttached()
		{
			if (bAttached)
			{
				return;
			}
			ILiveCodingModule* LC = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
			if (!LC)
			{
				return;
			}
			Handle = LC->GetOnPatchCompleteDelegate().AddLambda([this]()
			{
				bHasResult = true;
				LastPatchUtc = FDateTime::UtcNow();
			});
			bAttached = true;
		}
	};

	static FPatchObserver& GetObserver()
	{
		static FPatchObserver Observer;
		return Observer;
	}

	static const TCHAR* ResultToString(ELiveCodingCompileResult Result)
	{
		switch (Result)
		{
			case ELiveCodingCompileResult::Success:            return TEXT("Success");
			case ELiveCodingCompileResult::NoChanges:          return TEXT("NoChanges");
			case ELiveCodingCompileResult::InProgress:         return TEXT("InProgress");
			case ELiveCodingCompileResult::CompileStillActive: return TEXT("CompileStillActive");
			case ELiveCodingCompileResult::NotStarted:         return TEXT("NotStarted");
			case ELiveCodingCompileResult::Failure:            return TEXT("Failure");
			case ELiveCodingCompileResult::Cancelled:          return TEXT("Cancelled");
		}
		return TEXT("Unknown");
	}

	static FString MakeStatus(bool bSuccess, const FString& Message, TFunctionRef<void(TSharedRef<TJsonWriter<>>&)> Extras)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("success"), bSuccess);
		Writer->WriteValue(TEXT("message"), Message);
		Extras(Writer);
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	}

	static FString MakeStatus(bool bSuccess, const FString& Message)
	{
		return MakeStatus(bSuccess, Message, [](TSharedRef<TJsonWriter<>>&){});
	}

	/** Resolves the LiveCoding module + validates it's usable. Returns nullptr on failure
	 *  and writes a status JSON describing why to OutFailureJson. */
	static ILiveCodingModule* ResolveOrFail(FString& OutFailureJson)
	{
		ILiveCodingModule* LC = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		if (!LC)
		{
			OutFailureJson = MakeStatus(false, TEXT("LiveCoding module not loaded — is the plugin enabled and the editor a development build?"));
			return nullptr;
		}
		if (!LC->IsEnabledForSession())
		{
			OutFailureJson = MakeStatus(false, TEXT("LiveCoding is not enabled for this session — toggle Edit -> Editor Preferences -> General -> Live Coding."));
			return nullptr;
		}
		return LC;
	}
}

bool UArborCompileTools::IsLiveCodingCompiling()
{
	using namespace ArborCompileToolsInternal;
	GetObserver().EnsureAttached();
	ILiveCodingModule* LC = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	return LC && LC->IsCompiling();
}

FString UArborCompileTools::CompileAndWait()
{
	using namespace ArborCompileToolsInternal;
	GetObserver().EnsureAttached();

	FString Failure;
	ILiveCodingModule* LC = ResolveOrFail(Failure);
	if (!LC)
	{
		return Failure;
	}

	if (LC->IsCompiling())
	{
		return MakeStatus(false, TEXT("A compile is already in progress — wait for it to finish or call IsLiveCodingCompiling to poll."));
	}

	const FDateTime StartUtc = FDateTime::UtcNow();
	ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
	const bool bStarted = LC->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
	const double Duration = (FDateTime::UtcNow() - StartUtc).GetTotalSeconds();

	const bool bResultIsSuccess = (Result == ELiveCodingCompileResult::Success
		|| Result == ELiveCodingCompileResult::NoChanges);

	const FString ResultStr = ResultToString(Result);

	return MakeStatus(bResultIsSuccess && bStarted, ResultStr, [&](TSharedRef<TJsonWriter<>>& Writer)
	{
		Writer->WriteValue(TEXT("result_code"), ResultStr);
		Writer->WriteValue(TEXT("duration_sec"), Duration);
		Writer->WriteValue(TEXT("started"), bStarted);
	});
}

FString UArborCompileTools::StartLiveCodingCompile()
{
	using namespace ArborCompileToolsInternal;
	GetObserver().EnsureAttached();

	FString Failure;
	ILiveCodingModule* LC = ResolveOrFail(Failure);
	if (!LC)
	{
		return Failure;
	}

	if (LC->IsCompiling())
	{
		return MakeStatus(false, TEXT("A compile is already in progress."));
	}

	LC->Compile();
	return MakeStatus(true, TEXT("Compile triggered. Poll IsLiveCodingCompiling to wait."));
}

FString UArborCompileTools::GetLastCompileResult()
{
	using namespace ArborCompileToolsInternal;
	FPatchObserver& Observer = GetObserver();
	Observer.EnsureAttached();

	const bool bHas = Observer.bHasResult;
	const FString TimestampStr = bHas ? Observer.LastPatchUtc.ToIso8601() : FString();
	const double TimeSince = bHas ? (FDateTime::UtcNow() - Observer.LastPatchUtc).GetTotalSeconds() : -1.0;

	return MakeStatus(true, TEXT("OK"), [&](TSharedRef<TJsonWriter<>>& Writer)
	{
		Writer->WriteValue(TEXT("has_result"), bHas);
		Writer->WriteValue(TEXT("completed_at_utc"), TimestampStr);
		Writer->WriteValue(TEXT("time_since_seconds"), TimeSince);
	});
}
