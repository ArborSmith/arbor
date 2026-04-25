#include "ArborTagTools.h"

#include "GameplayTagsManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** Build a JSON status object string. */
	FString MakeStatus(bool bSuccess, const FString& Message, TFunctionRef<void(TSharedRef<TJsonWriter<>>&)> ExtraFields)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("success"), bSuccess);
		Writer->WriteValue(TEXT("message"), Message);
		ExtraFields(Writer);
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	}

	FString MakeStatus(bool bSuccess, const FString& Message)
	{
		return MakeStatus(bSuccess, Message, [](TSharedRef<TJsonWriter<>>&) {});
	}

	/**
	 * Walk a dotted property path on an object/struct, returning a (PropertyOwnerPtr,
	 * Property*) pair pointing at the leaf. Supports:
	 *   - "Field"                         direct property on object
	 *   - "Struct.Sub"                    USTRUCT member access
	 *   - "Array.0", "Array.0.Sub"        TArray element access
	 *
	 * Returns false if any segment fails to resolve.
	 */
	struct FResolvedProperty
	{
		void* OwnerPtr = nullptr;     // Memory of the containing object/struct/array element.
		FProperty* Property = nullptr; // The leaf property (the one we'll write to).
	};

	bool ResolvePropertyPath(UObject* Target, const FString& Path, FResolvedProperty& OutResult, FString& OutError)
	{
		if (!Target)
		{
			OutError = TEXT("Target is null");
			return false;
		}

		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			OutError = TEXT("Empty property path");
			return false;
		}

		void* CurrentOwner = Target;
		UStruct* CurrentStruct = Target->GetClass();
		FProperty* CurrentProperty = nullptr;

		for (int32 i = 0; i < Segments.Num(); ++i)
		{
			const FString& Segment = Segments[i];
			const bool bIsLast = (i == Segments.Num() - 1);

			// Numeric segment = TArray index. Requires the previously-resolved property
			// to have been an FArrayProperty.
			int32 ArrayIndex = INDEX_NONE;
			if (Segment.IsNumeric())
			{
				ArrayIndex = FCString::Atoi(*Segment);
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(CurrentProperty);
				if (!ArrayProp)
				{
					OutError = FString::Printf(TEXT("Numeric segment '%s' but previous property is not an array"), *Segment);
					return false;
				}
				FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentOwner));
				if (!Helper.IsValidIndex(ArrayIndex))
				{
					OutError = FString::Printf(TEXT("Array index %d out of range (size=%d) at segment %d"),
						ArrayIndex, Helper.Num(), i);
					return false;
				}
				CurrentOwner = Helper.GetRawPtr(ArrayIndex);
				// The "current property" inside the array is the element type.
				CurrentProperty = ArrayProp->Inner;
				if (FStructProperty* InnerStruct = CastField<FStructProperty>(CurrentProperty))
				{
					CurrentStruct = InnerStruct->Struct;
				}
				if (bIsLast)
				{
					OutResult.OwnerPtr = CurrentOwner;
					OutResult.Property = CurrentProperty;
					return true;
				}
				continue;
			}

			// Named segment = FProperty lookup on CurrentStruct.
			if (!CurrentStruct)
			{
				OutError = FString::Printf(TEXT("No struct context to resolve '%s' at segment %d"), *Segment, i);
				return false;
			}
			FProperty* Found = CurrentStruct->FindPropertyByName(FName(*Segment));
			if (!Found)
			{
				OutError = FString::Printf(TEXT("Property '%s' not found on struct '%s'"),
					*Segment, *CurrentStruct->GetName());
				return false;
			}
			CurrentProperty = Found;

			if (bIsLast)
			{
				OutResult.OwnerPtr = CurrentOwner;
				OutResult.Property = CurrentProperty;
				return true;
			}

			// Descend into the value: array stays as-is (next segment indexes it),
			// struct unwraps to its inner UStruct.
			if (FArrayProperty* AsArray = CastField<FArrayProperty>(CurrentProperty))
			{
				// Don't advance CurrentOwner; next iteration's numeric segment indexes here.
				CurrentStruct = nullptr; // Will be re-set after the array index resolves.
				continue;
			}

			if (FStructProperty* AsStruct = CastField<FStructProperty>(CurrentProperty))
			{
				CurrentOwner = AsStruct->ContainerPtrToValuePtr<void>(CurrentOwner);
				CurrentStruct = AsStruct->Struct;
				continue;
			}

			OutError = FString::Printf(TEXT("Cannot descend into property '%s' (type %s) at segment %d"),
				*Segment, *CurrentProperty->GetCPPType(), i);
			return false;
		}

		OutError = TEXT("Path resolution fell through unexpectedly");
		return false;
	}

	/** Mark the package dirty if Target is or owns a package — so the editor saves it. */
	void MarkOwnerPackageDirty(UObject* Target)
	{
		if (Target)
		{
			Target->MarkPackageDirty();
		}
	}
}

FGameplayTag UArborTagTools::RequestGameplayTag(FName TagName, bool bErrorIfNotFound)
{
	if (TagName.IsNone())
	{
		return FGameplayTag();
	}
	return UGameplayTagsManager::Get().RequestGameplayTag(TagName, bErrorIfNotFound);
}

bool UArborTagTools::IsTagRegistered(FName TagName)
{
	if (TagName.IsNone())
	{
		return false;
	}
	return UGameplayTagsManager::Get().RequestGameplayTag(TagName, /*ErrorIfNotFound*/ false).IsValid();
}

FString UArborTagTools::SetGameplayTagOnObject(UObject* Target, const FString& PropertyPath, FName TagName)
{
	const FGameplayTag Tag = RequestGameplayTag(TagName, /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		return MakeStatus(false, FString::Printf(TEXT("Tag '%s' is not registered"), *TagName.ToString()));
	}

	FResolvedProperty Resolved;
	FString Error;
	if (!ResolvePropertyPath(Target, PropertyPath, Resolved, Error))
	{
		return MakeStatus(false, Error);
	}

	FStructProperty* StructProp = CastField<FStructProperty>(Resolved.Property);
	if (!StructProp || StructProp->Struct != FGameplayTag::StaticStruct())
	{
		return MakeStatus(false, FString::Printf(TEXT("Property '%s' is not an FGameplayTag (type=%s)"),
			*PropertyPath, Resolved.Property ? *Resolved.Property->GetCPPType() : TEXT("null")));
	}

	void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(Resolved.OwnerPtr);
	*static_cast<FGameplayTag*>(ValuePtr) = Tag;

	MarkOwnerPackageDirty(Target);

	return MakeStatus(true, TEXT("OK"), [&Tag](TSharedRef<TJsonWriter<>>& Writer)
	{
		Writer->WriteValue(TEXT("resolved_tag"), Tag.ToString());
	});
}

FString UArborTagTools::SetGameplayTagContainerOnObject(UObject* Target, const FString& PropertyPath, const TArray<FName>& TagNames)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	FGameplayTagContainer NewContainer;
	TArray<FString> Resolved;
	TArray<FString> Unresolved;
	for (const FName& Name : TagNames)
	{
		const FGameplayTag Tag = Manager.RequestGameplayTag(Name, /*ErrorIfNotFound*/ false);
		if (Tag.IsValid())
		{
			NewContainer.AddTag(Tag);
			Resolved.Add(Tag.ToString());
		}
		else
		{
			Unresolved.Add(Name.ToString());
		}
	}

	if (Unresolved.Num() > 0)
	{
		return MakeStatus(false,
			FString::Printf(TEXT("%d tag(s) not registered: %s"),
				Unresolved.Num(),
				*FString::Join(Unresolved, TEXT(", "))),
			[&Resolved, &Unresolved](TSharedRef<TJsonWriter<>>& Writer)
			{
				Writer->WriteArrayStart(TEXT("resolved_tags"));
				for (const FString& S : Resolved) Writer->WriteValue(S);
				Writer->WriteArrayEnd();
				Writer->WriteArrayStart(TEXT("unresolved"));
				for (const FString& S : Unresolved) Writer->WriteValue(S);
				Writer->WriteArrayEnd();
			});
	}

	FResolvedProperty ResolvedProp;
	FString Error;
	if (!ResolvePropertyPath(Target, PropertyPath, ResolvedProp, Error))
	{
		return MakeStatus(false, Error);
	}

	FStructProperty* StructProp = CastField<FStructProperty>(ResolvedProp.Property);
	if (!StructProp || StructProp->Struct != FGameplayTagContainer::StaticStruct())
	{
		return MakeStatus(false,
			FString::Printf(TEXT("Property '%s' is not an FGameplayTagContainer (type=%s)"),
				*PropertyPath,
				ResolvedProp.Property ? *ResolvedProp.Property->GetCPPType() : TEXT("null")));
	}

	void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(ResolvedProp.OwnerPtr);
	*static_cast<FGameplayTagContainer*>(ValuePtr) = NewContainer;

	MarkOwnerPackageDirty(Target);

	return MakeStatus(true, TEXT("OK"),
		[&Resolved](TSharedRef<TJsonWriter<>>& Writer)
		{
			Writer->WriteArrayStart(TEXT("resolved_tags"));
			for (const FString& S : Resolved) Writer->WriteValue(S);
			Writer->WriteArrayEnd();
		});
}

FString UArborTagTools::ListGameplayTags(const FString& Prefix)
{
	FGameplayTagContainer All;
	UGameplayTagsManager::Get().RequestAllGameplayTags(All, /*OnlyIncludeDictTags*/ false);

	TArray<FString> Names;
	Names.Reserve(All.Num());
	for (const FGameplayTag& Tag : All)
	{
		const FString S = Tag.ToString();
		if (Prefix.IsEmpty() || S.StartsWith(Prefix))
		{
			Names.Add(S);
		}
	}
	Names.Sort();

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("count"), Names.Num());
	Writer->WriteArrayStart(TEXT("tags"));
	for (const FString& S : Names)
	{
		Writer->WriteValue(S);
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
