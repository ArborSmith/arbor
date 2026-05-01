// Shared JSON ↔ FProperty helpers for Arbor UFUNCTION tools that need
// reflection-based property edits (UArborLevelTools, the SetActorProperty
// path on UArborActorTools, ...).
//
// Header-only namespace, private to the module. UBlueprintBuilder still
// has its own inline copy of similar logic for historical reasons; that
// can be folded in later if a refactor is wanted.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace Arbor::Json
{
	inline FString MakeJsonResult(bool bSuccess, const FString& Message = FString(), const TSharedPtr<FJsonObject>& Extra = nullptr)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), bSuccess);
		if (!Message.IsEmpty())
		{
			Root->SetStringField(bSuccess ? TEXT("message") : TEXT("error"), Message);
		}
		if (Extra.IsValid())
		{
			for (const auto& Pair : Extra->Values)
			{
				Root->SetField(Pair.Key, Pair.Value);
			}
		}
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	inline FString JsonError(const FString& Message)
	{
		return MakeJsonResult(false, Message);
	}

	inline UClass* LoadClassByPath(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty()) return nullptr;

		UClass* Cls = FindObject<UClass>(nullptr, *ClassPath);
		if (Cls) return Cls;

		Cls = LoadObject<UClass>(nullptr, *ClassPath);
		if (Cls) return Cls;

		// Try with _C suffix for blueprint classes
		if (!ClassPath.EndsWith(TEXT("_C")))
		{
			const FString WithSuffix = ClassPath + TEXT("_C");
			Cls = LoadObject<UClass>(nullptr, *WithSuffix);
		}
		return Cls;
	}

	inline bool ParseJsonValue(const FString& ValueJson, TSharedPtr<FJsonValue>& OutValue)
	{
		// JSON spec requires a top-level object/array; property values are
		// usually scalars. Wrap in an object to parse, then extract.
		const FString Wrapped = FString::Printf(TEXT("{\"v\": %s}"), *ValueJson);
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Wrapped);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return false;
		}
		OutValue = Obj->TryGetField(TEXT("v"));
		return OutValue.IsValid();
	}

	/**
	 * Apply a JSON value to a property on a UObject. Supported types:
	 *   scalars (bool/int32/float/double/FString/FName)
	 *   FGameplayTag (string), FGameplayTagContainer (array of tag strings)
	 *   FObjectProperty / FSoftObjectProperty (asset path string)
	 *   FClassProperty / FSoftClassProperty (class path string)
	 *
	 * Not yet supported: nested struct properties (other than the two tag
	 * structs above), TArray, TMap, TSet.
	 */
	inline bool ApplyJsonToProperty(UObject* Object, const FString& PropertyName,
		const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
	{
		if (!Object)
		{
			OutError = TEXT("null object");
			return false;
		}
		if (!JsonValue.IsValid())
		{
			OutError = TEXT("invalid JSON value");
			return false;
		}

		FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"),
				*PropertyName, *Object->GetClass()->GetName());
			return false;
		}
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Object);

		// Tag-related structs — must come before generic FStructProperty fallback.
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
			{
				const FString TagString = JsonValue->AsString();
				FGameplayTag Tag;
				if (!TagString.IsEmpty())
				{
					Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagString), false);
					if (!Tag.IsValid())
					{
						OutError = FString::Printf(TEXT("Gameplay tag '%s' is not registered"), *TagString);
						return false;
					}
				}
				*static_cast<FGameplayTag*>(Addr) = Tag;
				return true;
			}

			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
				if (!JsonValue->TryGetArray(JsonArray) || !JsonArray)
				{
					OutError = FString::Printf(
						TEXT("Property '%s' is FGameplayTagContainer; expected JSON array of tag strings"),
						*PropertyName);
					return false;
				}
				FGameplayTagContainer Container;
				for (const TSharedPtr<FJsonValue>& Elem : *JsonArray)
				{
					if (!Elem.IsValid()) continue;
					const FString TagString = Elem->AsString();
					if (TagString.IsEmpty()) continue;
					const FGameplayTag Tag = UGameplayTagsManager::Get()
						.RequestGameplayTag(FName(*TagString), false);
					if (!Tag.IsValid())
					{
						OutError = FString::Printf(TEXT("Gameplay tag '%s' is not registered"), *TagString);
						return false;
					}
					Container.AddTag(Tag);
				}
				*static_cast<FGameplayTagContainer*>(Addr) = Container;
				return true;
			}

			OutError = FString::Printf(TEXT("Struct property '%s' (%s) not supported"),
				*PropertyName, *StructProp->Struct->GetName());
			return false;
		}

		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			const FString ClassPath = JsonValue->AsString();
			UClass* Loaded = LoadClassByPath(ClassPath);
			if (!Loaded)
			{
				OutError = FString::Printf(TEXT("Could not load class '%s'"), *ClassPath);
				return false;
			}
			ClassProp->SetObjectPropertyValue(Addr, Loaded);
			return true;
		}

		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			const FString AssetPath = JsonValue->AsString();
			UObject* Loaded = AssetPath.IsEmpty() ? nullptr
				: StaticLoadObject(ObjProp->PropertyClass, nullptr, *AssetPath);
			if (!Loaded && !AssetPath.IsEmpty() && !AssetPath.Contains(TEXT(".")))
			{
				const FString FullPath = AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath);
				Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *FullPath);
			}
			if (!Loaded && !AssetPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Could not load object '%s'"), *AssetPath);
				return false;
			}
			ObjProp->SetObjectPropertyValue(Addr, Loaded);
			return true;
		}

		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			const FString AssetPath = JsonValue->AsString();
			*static_cast<FSoftObjectPtr*>(Addr) = FSoftObjectPath(AssetPath);
			return true;
		}

		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
		{
			const FString ClassPath = JsonValue->AsString();
			*static_cast<FSoftObjectPtr*>(Addr) = FSoftObjectPath(ClassPath);
			return true;
		}

		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue(Addr, static_cast<float>(JsonValue->AsNumber()));
			return true;
		}
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue(Addr, JsonValue->AsNumber());
			return true;
		}
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue(Addr, static_cast<int32>(JsonValue->AsNumber()));
			return true;
		}
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue(Addr, JsonValue->AsBool());
			return true;
		}
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			StrProp->SetPropertyValue(Addr, JsonValue->AsString());
			return true;
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			NameProp->SetPropertyValue(Addr, FName(*JsonValue->AsString()));
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported property type for '%s' on %s"),
			*PropertyName, *Object->GetClass()->GetName());
		return false;
	}

	/** Read property → JSON value (best-effort summary; same scope as ApplyToProperty). */
	inline TSharedPtr<FJsonValue> PropertyToJson(UObject* Object, FProperty* Prop)
	{
		if (!Object || !Prop) return MakeShared<FJsonValueNull>();
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Object);

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
			{
				const FGameplayTag* Tag = static_cast<const FGameplayTag*>(Addr);
				return MakeShared<FJsonValueString>(Tag->IsValid() ? Tag->ToString() : FString());
			}
			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				const FGameplayTagContainer* Container = static_cast<const FGameplayTagContainer*>(Addr);
				TArray<TSharedPtr<FJsonValue>> Tags;
				for (const FGameplayTag& Tag : Container->GetGameplayTagArray())
				{
					Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
				}
				return MakeShared<FJsonValueArray>(Tags);
			}
			return MakeShared<FJsonValueString>(FString::Printf(TEXT("<struct:%s>"), *StructProp->Struct->GetName()));
		}
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			UClass* Cls = Cast<UClass>(ClassProp->GetObjectPropertyValue(Addr));
			return MakeShared<FJsonValueString>(Cls ? Cls->GetPathName() : FString());
		}
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* Val = ObjProp->GetObjectPropertyValue(Addr);
			return MakeShared<FJsonValueString>(Val ? Val->GetPathName() : FString());
		}
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			const FSoftObjectPtr* Soft = static_cast<const FSoftObjectPtr*>(Addr);
			return MakeShared<FJsonValueString>(Soft->ToSoftObjectPath().ToString());
		}
		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
		{
			const FSoftObjectPtr* Soft = static_cast<const FSoftObjectPtr*>(Addr);
			return MakeShared<FJsonValueString>(Soft->ToSoftObjectPath().ToString());
		}
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			return MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(Addr));
		}
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			return MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(Addr));
		}
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			return MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(Addr));
		}
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(Addr));
		}
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(Addr));
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(Addr).ToString());
		}
		return MakeShared<FJsonValueString>(FString::Printf(TEXT("<unsupported:%s>"), *Prop->GetClass()->GetName()));
	}
}
