#include "ArborAssetSearch.h"
#include "ArborRegistryHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Regex.h"

// ============================================================================
// Static state
// ============================================================================

TArray<UArborAssetSearch::FAssetEntry> UArborAssetSearch::CachedAssets;
bool UArborAssetSearch::bCacheValid = false;

// ============================================================================
// Constants
// ============================================================================

namespace
{

// Supported asset types (must match Python _SUPPORTED_TYPES).
const TSet<FString> SupportedTypes = {
	TEXT("StaticMesh"),
	TEXT("SkeletalMesh"),
	TEXT("Material"),
	TEXT("MaterialInstanceConstant"),
	TEXT("Blueprint"),
	TEXT("NiagaraSystem"),
	TEXT("NiagaraEmitter"),
	TEXT("Texture2D"),
	TEXT("SoundWave"),
	TEXT("SoundCue"),
	TEXT("BehaviorTree"),
	TEXT("BlackboardData"),
	TEXT("EnvironmentQuery"),
	TEXT("AnimSequence"),
	TEXT("AnimBlueprint"),
	TEXT("LevelSequence"),
	TEXT("FoliageType"),
};

// Internal → friendly type aliases.
const TMap<FString, FString> TypeAliases = {
	{TEXT("MaterialInstanceConstant"), TEXT("MaterialInstance")},
};

// Common UE5 asset name prefixes that add no search value.
const TSet<FString> NamePrefixes = {
	TEXT("sm"), TEXT("sk"), TEXT("m"), TEXT("mi"), TEXT("t"), TEXT("bp"),
	TEXT("w"), TEXT("a"), TEXT("abp"), TEXT("bt"), TEXT("bb"), TEXT("eqs"),
	TEXT("ns"), TEXT("ne"), TEXT("ft"), TEXT("ls"), TEXT("sc"), TEXT("sw"),
};

} // anonymous namespace

// ============================================================================
// Helpers
// ============================================================================

bool UArborAssetSearch::IsExcluded(const FString& Path)
{
	static const TArray<FString> ExcludePatterns = {
		TEXT("/Game/Developers/"),
		TEXT("/__ExternalActors__/"),
		TEXT("/__ExternalObjects__/"),
		TEXT("_BuiltData"),
	};

	for (const FString& Pattern : ExcludePatterns)
	{
		if (Path.Contains(Pattern))
		{
			return true;
		}
	}
	return false;
}

TArray<FString> UArborAssetSearch::BuildTags(const FString& Path, const FString& Name)
{
	TArray<FString> RawTokens;

	// Path segments (skip root Game/Engine).
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("/"), true);
	bool bFoundRoot = false;
	for (int32 I = 0; I < Parts.Num(); ++I)
	{
		if (!bFoundRoot)
		{
			if (Parts[I] == TEXT("Game") || Parts[I] == TEXT("Engine"))
			{
				bFoundRoot = true;
			}
			continue;
		}
		RawTokens.Add(Parts[I]);
	}

	// Split name on underscores.
	TArray<FString> NameParts;
	Name.ParseIntoArray(NameParts, TEXT("_"), true);
	RawTokens.Append(NameParts);

	// Expand CamelCase in every token.
	TArray<FString> Expanded;
	const FRegexPattern CamelPattern(TEXT("[A-Z][a-z]*|[a-z]+|[0-9]+"));
	for (const FString& Tok : RawTokens)
	{
		FRegexMatcher Matcher(CamelPattern, Tok);
		while (Matcher.FindNext())
		{
			Expanded.Add(Matcher.GetCaptureGroup(0));
		}
	}

	// Lowercase, filter, deduplicate.
	TSet<FString> Seen;
	TArray<FString> Result;
	for (const FString& Tok : Expanded)
	{
		FString Low = Tok.ToLower();
		if (NamePrefixes.Contains(Low))
		{
			continue;
		}
		if (Low.Len() < 2)
		{
			continue;
		}
		if (Seen.Contains(Low))
		{
			continue;
		}
		Seen.Add(Low);
		Result.Add(Low);
	}

	return Result;
}

int32 UArborAssetSearch::ScoreAsset(const FAssetEntry& Entry,
	const TArray<FString>& Tokens, const TSet<FString>& TypeSet)
{
	if (TypeSet.Num() > 0 && !TypeSet.Contains(Entry.Type))
	{
		return 0;
	}

	int32 Score = 0;
	const FString NameLower = Entry.Name.ToLower();
	const FString PathLower = Entry.Path.ToLower();

	for (const FString& Tok : Tokens)
	{
		// Exact name match.
		if (Tok == NameLower)
		{
			Score += 10;
		}
		// Substring in name.
		else if (NameLower.Contains(Tok))
		{
			Score += 5;
		}

		// Token matches a tag.
		if (Entry.Tags.Contains(Tok))
		{
			Score += 3;
		}

		// Substring in path.
		if (PathLower.Contains(Tok))
		{
			Score += 1;
		}
	}

	return Score;
}

// ============================================================================
// Cache management
// ============================================================================

void UArborAssetSearch::EnsureCache()
{
	if (bCacheValid)
	{
		// Check if C++ dirty flag is set (asset added/removed since last scan).
		bool bDirty = true;
		try
		{
			bDirty = UArborRegistryHelper::IsAssetRegistryDirty();
		}
		catch (...)
		{
		}

		if (!bDirty)
		{
			return;
		}
	}

	RebuildCache();
}

void UArborAssetSearch::RebuildCache()
{
	CachedAssets.Empty();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();

	// Scan /Game and common engine paths.
	static const TArray<FString> ScanRoots = {
		TEXT("/Game"),
		TEXT("/Engine/BasicShapes"),
		TEXT("/Engine/EngineMaterials"),
		TEXT("/Engine/Functions"),
		TEXT("/Engine/EditorMeshes"),
		TEXT("/Engine/MapTemplates"),
	};

	for (const FString& Root : ScanRoots)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Root));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		for (const FAssetData& AssetData : Assets)
		{
			const FString AssetPath = AssetData.GetObjectPathString();
			if (IsExcluded(AssetPath))
			{
				continue;
			}

			// Get class name (UE 5.1+ uses AssetClassPath).
			FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();

			if (!SupportedTypes.Contains(ClassName))
			{
				continue;
			}

			// Friendly type alias.
			const FString* Alias = TypeAliases.Find(ClassName);
			const FString FriendlyType = Alias ? *Alias : ClassName;

			// Extract short name.
			FString AssetName = AssetData.AssetName.ToString();

			FAssetEntry Entry;
			Entry.Path = AssetPath;
			Entry.Name = AssetName;
			Entry.Type = FriendlyType;
			Entry.Tags = BuildTags(AssetPath, AssetName);

			CachedAssets.Add(MoveTemp(Entry));
		}
	}

	bCacheValid = true;

	// Clear the C++ dirty flag.
	try
	{
		UArborRegistryHelper::ClearAssetRegistryDirty();
	}
	catch (...)
	{
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAssetSearch] RebuildCache: indexed %d assets"), CachedAssets.Num());
}

// ============================================================================
// Public API
// ============================================================================

FString UArborAssetSearch::FindAsset(const FString& Query, const FString& TypeFilter, int32 Limit)
{
	EnsureCache();

	// Parse type filter.
	TSet<FString> TypeSet;
	if (!TypeFilter.IsEmpty())
	{
		TArray<FString> Types;
		TypeFilter.ParseIntoArray(Types, TEXT(","), true);
		for (FString& T : Types)
		{
			T.TrimStartAndEndInline();
			TypeSet.Add(T);
		}
	}

	// Parse query tokens.
	TArray<FString> Tokens;
	Query.ToLower().ParseIntoArray(Tokens, TEXT(" "), true);

	// Browse mode: empty query + type filter → return all of that type.
	if (Tokens.Num() == 0 && TypeSet.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ResultArray;
		int32 Count = 0;
		for (const FAssetEntry& Entry : CachedAssets)
		{
			if (!TypeSet.Contains(Entry.Type))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("path"), Entry.Path);
			Obj->SetStringField(TEXT("name"), Entry.Name);
			Obj->SetStringField(TEXT("type"), Entry.Type);

			TArray<TSharedPtr<FJsonValue>> TagValues;
			for (const FString& Tag : Entry.Tags)
			{
				TagValues.Add(MakeShared<FJsonValueString>(Tag));
			}
			Obj->SetArrayField(TEXT("tags"), TagValues);
			Obj->SetNumberField(TEXT("score"), 0);

			ResultArray.Add(MakeShared<FJsonValueObject>(Obj));
			if (++Count >= Limit)
			{
				break;
			}
		}

		FString Output;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(ResultArray, Writer);
		return Output;
	}

	// Scored mode.
	struct FScoredEntry
	{
		int32 Score;
		int32 Index;
	};
	TArray<FScoredEntry> Scored;
	Scored.Reserve(CachedAssets.Num());

	for (int32 I = 0; I < CachedAssets.Num(); ++I)
	{
		const int32 S = ScoreAsset(CachedAssets[I], Tokens, TypeSet);
		if (S > 0)
		{
			Scored.Add({S, I});
		}
	}

	Scored.Sort([](const FScoredEntry& A, const FScoredEntry& B) { return A.Score > B.Score; });

	const int32 ResultCount = FMath::Min(Scored.Num(), Limit);
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	ResultArray.Reserve(ResultCount);

	for (int32 I = 0; I < ResultCount; ++I)
	{
		const FAssetEntry& Entry = CachedAssets[Scored[I].Index];

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Entry.Path);
		Obj->SetStringField(TEXT("name"), Entry.Name);
		Obj->SetStringField(TEXT("type"), Entry.Type);

		TArray<TSharedPtr<FJsonValue>> TagValues;
		for (const FString& Tag : Entry.Tags)
		{
			TagValues.Add(MakeShared<FJsonValueString>(Tag));
		}
		Obj->SetArrayField(TEXT("tags"), TagValues);
		Obj->SetNumberField(TEXT("score"), Scored[I].Score);

		ResultArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(ResultArray, Writer);
	return Output;
}

FString UArborAssetSearch::ScanProject()
{
	RebuildCache();

	// Build stats.
	TMap<FString, int32> Stats;
	for (const FAssetEntry& Entry : CachedAssets)
	{
		Stats.FindOrAdd(Entry.Type, 0)++;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	TSharedPtr<FJsonObject> StatsObj = MakeShared<FJsonObject>();
	for (const auto& Pair : Stats)
	{
		StatsObj->SetNumberField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("stats"), StatsObj);
	Root->SetNumberField(TEXT("total"), CachedAssets.Num());

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString UArborAssetSearch::GetRegistryStats()
{
	EnsureCache();

	TMap<FString, int32> Stats;
	for (const FAssetEntry& Entry : CachedAssets)
	{
		Stats.FindOrAdd(Entry.Type, 0)++;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const auto& Pair : Stats)
	{
		Root->SetNumberField(Pair.Key, Pair.Value);
	}

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
