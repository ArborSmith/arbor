#include "ArborAutomationTools.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/AutomationEvent.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ArborAutomationToolsInternal
{
	/** True iff TestPath either equals Filter or starts with Filter + "." (proper-prefix match). */
	static bool MatchesFilter(const FString& TestPath, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		if (TestPath == Filter)
		{
			return true;
		}
		if (TestPath.StartsWith(Filter + TEXT(".")))
		{
			return true;
		}
		return false;
	}

	/**
	 * Widen FAutomationTestFramework::RequestedTestFilter to every filter category.
	 *
	 * Why: GetValidTestNames filters by `(TestFlags & RequestedTestFilter) != 0`,
	 * and RequestedTestFilter defaults to `SmokeFilter` only — so project tests
	 * marked with EngineFilter (the IMPLEMENT_SIMPLE_AUTOMATION_TEST default)
	 * silently disappear from the list until something widens the filter (the
	 * editor's Automation panel does this when opened, but headless callers
	 * don't get that for free).
	 *
	 * No public getter exists on FAutomationTestFramework, so we can't
	 * save/restore — we just always set it. Same end-state the Automation panel
	 * leaves the framework in, so this doesn't surprise downstream callers.
	 */
	static void EnsureWideTestFilter()
	{
		FAutomationTestFramework::Get().SetRequestedTestFilter(EAutomationTestFlags::FilterMask);
	}

	/** Collect every valid test info whose full path matches Filter. */
	static TArray<FAutomationTestInfo> CollectMatchingTests(const FString& Filter)
	{
		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
		Framework.LoadTestModules();
		EnsureWideTestFilter();

		TArray<FAutomationTestInfo> All;
		Framework.GetValidTestNames(All);

		TArray<FAutomationTestInfo> Matches;
		Matches.Reserve(All.Num());
		for (const FAutomationTestInfo& Info : All)
		{
			if (MatchesFilter(Info.GetFullTestPath(), Filter))
			{
				Matches.Add(Info);
			}
		}
		return Matches;
	}

	/** Drives latent commands until the test signals completion or TimeoutSeconds elapses.
	 *  Returns true on natural completion, false on timeout. */
	static bool DrainLatentCommands(float TimeoutSeconds)
	{
		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();

		const double StartTime = FPlatformTime::Seconds();
		while (true)
		{
			const bool bDone = Framework.ExecuteLatentCommands();
			if (bDone)
			{
				return true;
			}
			Framework.ExecuteNetworkCommands();

			if (TimeoutSeconds > 0.0f
				&& (FPlatformTime::Seconds() - StartTime) > TimeoutSeconds)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.0f); // yield, but don't block long
		}
	}

	/** Convert an EAutomationEventType to a short tag for JSON. */
	static const TCHAR* EventTypeTag(EAutomationEventType T)
	{
		switch (T)
		{
			case EAutomationEventType::Info:    return TEXT("info");
			case EAutomationEventType::Warning: return TEXT("warning");
			case EAutomationEventType::Error:   return TEXT("error");
		}
		return TEXT("unknown");
	}
}

FString UArborAutomationTools::ListTests(const FString& Filter)
{
	using namespace ArborAutomationToolsInternal;

	const TArray<FAutomationTestInfo> Matches = CollectMatchingTests(Filter);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("count"), Matches.Num());
	Writer->WriteArrayStart(TEXT("tests"));
	for (const FAutomationTestInfo& Info : Matches)
	{
		Writer->WriteValue(Info.GetFullTestPath());
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}

FString UArborAutomationTools::RunTestsAndWait(const FString& Filter, float TimeoutSeconds, int32 MaxErrorsPerTest)
{
	using namespace ArborAutomationToolsInternal;

	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	const TArray<FAutomationTestInfo> Matches = CollectMatchingTests(Filter);

	const double SuiteStart = FPlatformTime::Seconds();

	int32 Passed = 0;
	int32 Failed = 0;

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteArrayStart(TEXT("tests"));

	for (const FAutomationTestInfo& Info : Matches)
	{
		Framework.StartTestByName(Info.GetTestName(), /*RoleIndex*/ 0, Info.GetFullTestPath());
		const bool bDrained = DrainLatentCommands(TimeoutSeconds);

		FAutomationTestExecutionInfo ExecInfo;
		// StopTest captures the per-test result and clears state for the next run.
		Framework.StopTest(ExecInfo);

		const bool bSuccess = bDrained && ExecInfo.bSuccessful;
		if (bSuccess)
		{
			++Passed;
		}
		else
		{
			++Failed;
		}

		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("path"), Info.GetFullTestPath());
		Writer->WriteValue(TEXT("success"), bSuccess);
		Writer->WriteValue(TEXT("duration_sec"), ExecInfo.Duration);
		Writer->WriteValue(TEXT("error_count"), ExecInfo.GetErrorTotal());
		Writer->WriteValue(TEXT("warning_count"), ExecInfo.GetWarningTotal());
		if (!bDrained)
		{
			Writer->WriteValue(TEXT("timeout"), true);
		}

		// Capture up to MaxErrorsPerTest error/warning messages so callers don't have
		// to scrape logs to know what failed. 0 disables the cap.
		int32 EventsWritten = 0;
		Writer->WriteArrayStart(TEXT("errors"));
		for (const FAutomationExecutionEntry& Entry : ExecInfo.GetEntries())
		{
			if (Entry.Event.Type != EAutomationEventType::Error
				&& Entry.Event.Type != EAutomationEventType::Warning)
			{
				continue;
			}
			if (MaxErrorsPerTest > 0 && EventsWritten >= MaxErrorsPerTest)
			{
				break;
			}
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("type"), EventTypeTag(Entry.Event.Type));
			Writer->WriteValue(TEXT("message"), Entry.Event.Message);
			if (!Entry.Filename.IsEmpty())
			{
				Writer->WriteValue(TEXT("file"), Entry.Filename);
				Writer->WriteValue(TEXT("line"), Entry.LineNumber);
			}
			Writer->WriteObjectEnd();
			++EventsWritten;
		}
		Writer->WriteArrayEnd();

		Writer->WriteObjectEnd();
	}

	Writer->WriteArrayEnd();

	const double SuiteDuration = FPlatformTime::Seconds() - SuiteStart;
	Writer->WriteObjectStart(TEXT("summary"));
	Writer->WriteValue(TEXT("total"), Matches.Num());
	Writer->WriteValue(TEXT("passed"), Passed);
	Writer->WriteValue(TEXT("failed"), Failed);
	Writer->WriteValue(TEXT("duration_sec"), SuiteDuration);
	Writer->WriteValue(TEXT("filter"), Filter);
	Writer->WriteObjectEnd();

	Writer->WriteValue(TEXT("success"), Failed == 0);
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
