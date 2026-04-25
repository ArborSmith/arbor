// Copyright Arbor Plugin. All Rights Reserved.

#include "FabHelper.h"

#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogFabHelper, Log, All);

// ---------------------------------------------------------------------------
// FindFabBrowserApi — locate the singleton UFabBrowserApi via reflection
// ---------------------------------------------------------------------------

// Persistent instance we create if the Fab browser panel hasn't spawned one
static UObject* CachedFabApi = nullptr;

UObject* UFabHelper::FindFabBrowserApi()
{
	// Find the UClass by its script path — no compile-time dependency on Fab module
	UClass* FabApiClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
	if (!FabApiClass)
	{
		UE_LOG(LogFabHelper, Warning, TEXT("Fab plugin not loaded — cannot find UFabBrowserApi class."));
		return nullptr;
	}

	// Try to find an existing instance (created by the Fab browser panel)
	TArray<UObject*> Instances;
	GetObjectsOfClass(FabApiClass, Instances, true);
	if (Instances.Num() > 0)
	{
		return Instances[0];
	}

	// No instance exists — create one ourselves.
	// GetAuthToken() only delegates to global EOS state, so a fresh instance works fine.
	if (!CachedFabApi || !CachedFabApi->IsValidLowLevel())
	{
		CachedFabApi = NewObject<UObject>(GetTransientPackage(), FabApiClass);
		CachedFabApi->AddToRoot(); // Prevent GC
		UE_LOG(LogFabHelper, Log, TEXT("Created transient UFabBrowserApi instance for auth token access."));
	}

	return CachedFabApi;
}

// ---------------------------------------------------------------------------
// GetFabAuthToken — call UFabBrowserApi::GetAuthToken() via ProcessEvent
// ---------------------------------------------------------------------------

FString UFabHelper::GetFabAuthToken()
{
	UObject* FabApi = FindFabBrowserApi();
	if (!FabApi)
	{
		return FString();
	}

	UFunction* GetAuthTokenFn = FabApi->FindFunction(TEXT("GetAuthToken"));
	if (!GetAuthTokenFn)
	{
		UE_LOG(LogFabHelper, Error, TEXT("UFabBrowserApi::GetAuthToken function not found via reflection."));
		return FString();
	}

	// GetAuthToken() takes no params and returns FString.
	// ProcessEvent params layout: just the return value.
	struct
	{
		FString ReturnValue;
	} Params;

	FabApi->ProcessEvent(GetAuthTokenFn, &Params);

	if (Params.ReturnValue.IsEmpty())
	{
		UE_LOG(LogFabHelper, Warning, TEXT("Fab auth token is empty — user may not be logged in. Use Fab.Login console command."));
	}
	else
	{
		UE_LOG(LogFabHelper, Log, TEXT("Successfully retrieved Fab auth token (%d chars)."), Params.ReturnValue.Len());
	}

	return Params.ReturnValue;
}

// ---------------------------------------------------------------------------
// IsFabLoggedIn
// ---------------------------------------------------------------------------

bool UFabHelper::IsFabLoggedIn()
{
	return !GetFabAuthToken().IsEmpty();
}

// ---------------------------------------------------------------------------
// Helper: set a string property on a struct in memory by name
// ---------------------------------------------------------------------------

static void SetStructStringProp(UStruct* Struct, void* StructMemory, const TCHAR* PropName, const FString& Value)
{
	FProperty* Prop = Struct->FindPropertyByName(PropName);
	if (Prop)
	{
		FStrProperty* StrProp = CastField<FStrProperty>(Prop);
		if (StrProp)
		{
			StrProp->SetPropertyValue_InContainer(StructMemory, Value);
		}
	}
}

static void SetStructBoolProp(UStruct* Struct, void* StructMemory, const TCHAR* PropName, bool Value)
{
	FProperty* Prop = Struct->FindPropertyByName(PropName);
	if (Prop)
	{
		FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop);
		if (BoolProp)
		{
			BoolProp->SetPropertyValue_InContainer(StructMemory, Value);
		}
	}
}

static void SetStructStringArrayProp(UStruct* Struct, void* StructMemory, const TCHAR* PropName, const TArray<FString>& Values)
{
	FProperty* Prop = Struct->FindPropertyByName(PropName);
	if (!Prop) return;

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
	if (!ArrayProp) return;

	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(StructMemory));
	ArrayHelper.EmptyValues();

	FStrProperty* InnerProp = CastField<FStrProperty>(ArrayProp->Inner);
	if (!InnerProp) return;

	for (const FString& Val : Values)
	{
		const int32 Idx = ArrayHelper.AddValue();
		InnerProp->SetPropertyValue(ArrayHelper.GetRawPtr(Idx), Val);
	}
}

// ---------------------------------------------------------------------------
// FabImportAsset — call UFabBrowserApi::AddToProject() via ProcessEvent
// ---------------------------------------------------------------------------

bool UFabHelper::FabImportAsset(const FString& MetadataJson)
{
	// Parse input JSON
	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetadataJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogFabHelper, Error, TEXT("FabImportAsset: failed to parse MetadataJson."));
		return false;
	}

	FString DownloadUrl, AssetId, AssetName, AssetType, ListingType, AssetNamespace;
	bool bIsQuixel = false;

	JsonObj->TryGetStringField(TEXT("download_url"), DownloadUrl);
	JsonObj->TryGetStringField(TEXT("asset_id"), AssetId);
	JsonObj->TryGetStringField(TEXT("asset_name"), AssetName);
	JsonObj->TryGetStringField(TEXT("asset_type"), AssetType);
	JsonObj->TryGetStringField(TEXT("listing_type"), ListingType);
	JsonObj->TryGetBoolField(TEXT("is_quixel"), bIsQuixel);
	JsonObj->TryGetStringField(TEXT("asset_namespace"), AssetNamespace);

	TArray<FString> DistributionUrls;
	const TArray<TSharedPtr<FJsonValue>>* UrlsArray;
	if (JsonObj->TryGetArrayField(TEXT("distribution_urls"), UrlsArray))
	{
		for (const auto& Val : *UrlsArray)
		{
			DistributionUrls.Add(Val->AsString());
		}
	}

	// Find the FabBrowserApi instance
	UObject* FabApi = FindFabBrowserApi();
	if (!FabApi)
	{
		return false;
	}

	// Find AddToProject function
	UFunction* AddToProjectFn = FabApi->FindFunction(TEXT("AddToProject"));
	if (!AddToProjectFn)
	{
		UE_LOG(LogFabHelper, Error, TEXT("UFabBrowserApi::AddToProject function not found via reflection."));
		return false;
	}

	// Allocate the ProcessEvent parameter buffer
	uint8* ParamsBuffer = static_cast<uint8*>(FMemory::Malloc(AddToProjectFn->ParmsSize));
	FMemory::Memzero(ParamsBuffer, AddToProjectFn->ParmsSize);
	AddToProjectFn->InitializeStruct(ParamsBuffer);

	// Iterate function parameters and set values
	for (TFieldIterator<FProperty> It(AddToProjectFn); It; ++It)
	{
		FProperty* Prop = *It;
		const FName PropName = Prop->GetFName();

		if (PropName == FName("DownloadUrl"))
		{
			FStrProperty* StrProp = CastField<FStrProperty>(Prop);
			if (StrProp)
			{
				StrProp->SetPropertyValue_InContainer(ParamsBuffer, DownloadUrl);
			}
		}
		else if (PropName == FName("AssetMetadata"))
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (StructProp)
			{
				// Get pointer to the FFabAssetMetadata struct within the params buffer
				void* MetadataPtr = StructProp->ContainerPtrToValuePtr<void>(ParamsBuffer);
				UScriptStruct* MetaStruct = StructProp->Struct;

				// Set each field using reflection
				SetStructStringProp(MetaStruct, MetadataPtr, TEXT("AssetId"), AssetId);
				SetStructStringProp(MetaStruct, MetadataPtr, TEXT("AssetName"), AssetName);
				SetStructStringProp(MetaStruct, MetadataPtr, TEXT("AssetType"), AssetType);
				SetStructStringProp(MetaStruct, MetadataPtr, TEXT("ListingType"), ListingType);
				SetStructBoolProp(MetaStruct, MetadataPtr, TEXT("IsQuixel"), bIsQuixel);
				SetStructStringProp(MetaStruct, MetadataPtr, TEXT("AssetNamespace"), AssetNamespace);
				SetStructStringArrayProp(MetaStruct, MetadataPtr, TEXT("DistributionPointBaseUrls"), DistributionUrls);
			}
		}
	}

	UE_LOG(LogFabHelper, Log, TEXT("Calling AddToProject for asset '%s' (type: %s)"), *AssetName, *AssetType);

	// Call the function
	FabApi->ProcessEvent(AddToProjectFn, ParamsBuffer);

	// Cleanup
	AddToProjectFn->DestroyStruct(ParamsBuffer);
	FMemory::Free(ParamsBuffer);

	UE_LOG(LogFabHelper, Log, TEXT("AddToProject dispatched successfully for '%s'."), *AssetName);
	return true;
}

// ---------------------------------------------------------------------------
// ListFabApiFunctions — enumerate all UFunctions on UFabBrowserApi
// ---------------------------------------------------------------------------

FString UFabHelper::ListFabApiFunctions()
{
	UClass* FabApiClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
	if (!FabApiClass)
	{
		UE_LOG(LogFabHelper, Warning, TEXT("Fab plugin not loaded — cannot enumerate API functions."));
		return TEXT("[]");
	}

	TArray<FString> FunctionNames;
	for (TFieldIterator<UFunction> It(FabApiClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Func = *It;
		if (Func && Func->GetOuter() == FabApiClass)
		{
			FunctionNames.Add(Func->GetName());
		}
	}

	// Build JSON array
	FString Result = TEXT("[");
	for (int32 i = 0; i < FunctionNames.Num(); ++i)
	{
		if (i > 0) Result += TEXT(",");
		Result += FString::Printf(TEXT("\"%s\""), *FunctionNames[i]);
	}
	Result += TEXT("]");

	UE_LOG(LogFabHelper, Log, TEXT("Found %d functions on UFabBrowserApi."), FunctionNames.Num());
	return Result;
}

// ---------------------------------------------------------------------------
// GetFabLibrary — retrieve user's owned/claimed Fab assets via reflection
// ---------------------------------------------------------------------------

FString UFabHelper::GetFabLibrary(const FString& ParamsJson)
{
	UObject* FabApi = FindFabBrowserApi();
	if (!FabApi)
	{
		return TEXT("{\"success\":false,\"error\":\"Fab plugin not loaded.\"}");
	}

	// Parse optional params
	int32 Page = 1;
	int32 PerPage = 20;

	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
	{
		JsonObj->TryGetNumberField(TEXT("page"), Page);
		JsonObj->TryGetNumberField(TEXT("per_page"), PerPage);
	}

	// Try known method names for retrieving user's library
	static const TCHAR* CandidateMethods[] = {
		TEXT("GetLibrary"),
		TEXT("GetOwnedListings"),
		TEXT("GetAcquiredContent"),
		TEXT("GetUserLibrary"),
		TEXT("GetMyLibrary"),
		TEXT("FetchLibrary"),
		TEXT("GetPurchasedListings"),
		TEXT("GetAcquisitions"),
		TEXT("GetOwnedContent"),
		TEXT("GetEntitlements"),
	};

	UFunction* LibraryFunc = nullptr;
	FString FoundMethodName;

	for (const TCHAR* MethodName : CandidateMethods)
	{
		UFunction* Func = FabApi->FindFunction(MethodName);
		if (Func)
		{
			LibraryFunc = Func;
			FoundMethodName = MethodName;
			UE_LOG(LogFabHelper, Log, TEXT("Found Fab library method: %s"), MethodName);
			break;
		}
	}

	if (!LibraryFunc)
	{
		// No known method found — list available functions for discovery
		FString AvailableFunctions = ListFabApiFunctions();
		UE_LOG(LogFabHelper, Warning, TEXT("No known library method found on UFabBrowserApi. "
			"Available functions: %s"), *AvailableFunctions);

		// Build informative error response
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("success"), false);
		ErrorObj->SetStringField(TEXT("error"),
			TEXT("No library method found on UFabBrowserApi. Use list_api_functions() to discover available methods."));
		ErrorObj->SetStringField(TEXT("available_functions"), AvailableFunctions);

		FString ErrorResult;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ErrorResult);
		FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
		return ErrorResult;
	}

	// Allocate ProcessEvent parameter buffer
	uint8* ParamsBuffer = static_cast<uint8*>(FMemory::Malloc(LibraryFunc->ParmsSize));
	FMemory::Memzero(ParamsBuffer, LibraryFunc->ParmsSize);
	LibraryFunc->InitializeStruct(ParamsBuffer);

	// Try to set Page/PerPage parameters if the function accepts them
	for (TFieldIterator<FProperty> It(LibraryFunc); It; ++It)
	{
		FProperty* Prop = *It;
		if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}

		const FName PropName = Prop->GetFName();
		const FString PropNameStr = PropName.ToString().ToLower();

		FIntProperty* IntProp = CastField<FIntProperty>(Prop);
		if (IntProp)
		{
			if (PropNameStr.Contains(TEXT("page")) && !PropNameStr.Contains(TEXT("size")) && !PropNameStr.Contains(TEXT("per")))
			{
				IntProp->SetPropertyValue_InContainer(ParamsBuffer, Page);
			}
			else if (PropNameStr.Contains(TEXT("perpage")) || PropNameStr.Contains(TEXT("per_page"))
				|| PropNameStr.Contains(TEXT("pagesize")) || PropNameStr.Contains(TEXT("page_size"))
				|| PropNameStr.Contains(TEXT("limit")) || PropNameStr.Contains(TEXT("count")))
			{
				IntProp->SetPropertyValue_InContainer(ParamsBuffer, PerPage);
			}
		}
	}

	// Call the function
	UE_LOG(LogFabHelper, Log, TEXT("Calling UFabBrowserApi::%s (page=%d, per_page=%d)"),
		*FoundMethodName, Page, PerPage);
	FabApi->ProcessEvent(LibraryFunc, ParamsBuffer);

	// Try to extract return value
	FString ResultString;
	for (TFieldIterator<FProperty> It(LibraryFunc); It; ++It)
	{
		FProperty* Prop = *It;
		if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			FStrProperty* StrProp = CastField<FStrProperty>(Prop);
			if (StrProp)
			{
				ResultString = StrProp->GetPropertyValue_InContainer(ParamsBuffer);
			}
			break;
		}
	}

	// Cleanup
	LibraryFunc->DestroyStruct(ParamsBuffer);
	FMemory::Free(ParamsBuffer);

	if (ResultString.IsEmpty())
	{
		// Method was called but returned empty/void — wrap in success response
		TSharedPtr<FJsonObject> SuccessObj = MakeShared<FJsonObject>();
		SuccessObj->SetBoolField(TEXT("success"), true);
		SuccessObj->SetStringField(TEXT("method"), FoundMethodName);
		SuccessObj->SetStringField(TEXT("note"),
			TEXT("Method called successfully but returned no string data. "
				"The method may use a delegate/callback pattern."));

		FString SuccessResult;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SuccessResult);
		FJsonSerializer::Serialize(SuccessObj.ToSharedRef(), Writer);
		return SuccessResult;
	}

	return ResultString;
}
