#include "ArborRegistryWidget.h"
#include "ArborRegistryHelper.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "ArborRegistryWidget"

void SArborRegistryWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Toolbar ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SButton)
				.OnClicked(this, &SArborRegistryWidget::OnRefreshClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Refresh", "Refresh"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SButton)
				.OnClicked(this, &SArborRegistryWidget::OnScanClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Scan", "Scan"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				SAssignNew(StatsLabel, STextBlock)
				.Text(LOCTEXT("Loading", "Loading..."))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				SAssignNew(DirtyLabel, STextBlock)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(ScanTimeLabel, STextBlock)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// ---- Filter bar ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("SearchHint", "Search by name, path, or tag..."))
				.OnTextChanged(this, &SArborRegistryWidget::OnSearchTextChanged)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(150.0f)
				[
					SNew(STextComboBox)
					.OptionsSource(&TypeOptions)
					.OnSelectionChanged(this, &SArborRegistryWidget::OnTypeFilterChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(FilteredCountLabel, STextBlock)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// ---- Column headers ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(160.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ColType", "Type"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(200.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ColName", "Name"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ColPath", "Path"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
		]

		// ---- Asset list ----
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(AssetListBox, SVerticalBox)
			]
		]
	];

	// Initial load
	LoadRegistryFromDisk();
	ApplyFilter();
}

void SArborRegistryWidget::LoadRegistryFromDisk()
{
	AllAssets.Empty();
	ScanTime.Empty();

	FString SavedDir = FPaths::ProjectSavedDir();
	FString RegistryPath = FPaths::Combine(SavedDir, TEXT("Arbor"), TEXT("asset_registry.json"));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *RegistryPath))
	{
		StatsLabel->SetText(FText::FromString(TEXT("No registry file found")));
		ScanTimeLabel->SetText(FText::GetEmpty());
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		StatsLabel->SetText(FText::FromString(TEXT("Failed to parse registry JSON")));
		return;
	}

	// Parse scan time
	ScanTime = Root->GetStringField(TEXT("scanned_at"));

	// Parse assets array
	const TArray<TSharedPtr<FJsonValue>>* AssetsArray = nullptr;
	if (Root->TryGetArrayField(TEXT("assets"), AssetsArray))
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : *AssetsArray)
		{
			const TSharedPtr<FJsonObject>& Obj = AssetValue->AsObject();
			if (!Obj.IsValid()) continue;

			FAssetEntry Entry;
			Entry.Path = Obj->GetStringField(TEXT("path"));
			Entry.Name = Obj->GetStringField(TEXT("name"));
			Entry.Type = Obj->GetStringField(TEXT("type"));

			const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
			if (Obj->TryGetArrayField(TEXT("tags"), TagsArray))
			{
				for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
				{
					Entry.Tags.Add(TagValue->AsString());
				}
			}

			AllAssets.Add(MoveTemp(Entry));
		}
	}

	// Update header labels
	StatsLabel->SetText(FText::FromString(
		FString::Printf(TEXT("%d assets indexed"), AllAssets.Num())));
	ScanTimeLabel->SetText(FText::FromString(
		FString::Printf(TEXT("Last scan: %s"), *ScanTime)));

	// Update dirty flag
	bool bDirty = UArborRegistryHelper::IsAssetRegistryDirty();
	DirtyLabel->SetText(FText::FromString(
		bDirty ? TEXT("Dirty: Yes") : TEXT("Dirty: No")));
	DirtyLabel->SetColorAndOpacity(FSlateColor(
		bDirty ? FLinearColor(1.0f, 0.6f, 0.0f) : FLinearColor(0.3f, 0.8f, 0.3f)));

	RebuildTypeOptions();
}

void SArborRegistryWidget::RebuildTypeOptions()
{
	TSet<FString> Types;
	for (const FAssetEntry& Entry : AllAssets)
	{
		Types.Add(Entry.Type);
	}

	TypeOptions.Empty();
	TypeOptions.Add(MakeShared<FString>(TEXT("All")));
	TArray<FString> SortedTypes = Types.Array();
	SortedTypes.Sort();
	for (const FString& Type : SortedTypes)
	{
		TypeOptions.Add(MakeShared<FString>(Type));
	}

	SelectedType = TypeOptions[0];
}

void SArborRegistryWidget::ApplyFilter()
{
	FilteredIndices.Empty();

	FString SearchLower = SearchText.ToLower();
	TArray<FString> SearchTokens;
	if (!SearchLower.IsEmpty())
	{
		SearchLower.ParseIntoArray(SearchTokens, TEXT(" "), true);
	}

	bool bFilterByType = SelectedType.IsValid() && *SelectedType != TEXT("All");

	for (int32 i = 0; i < AllAssets.Num(); i++)
	{
		const FAssetEntry& Entry = AllAssets[i];

		// Type filter
		if (bFilterByType && Entry.Type != *SelectedType)
		{
			continue;
		}

		// Search filter — all tokens must match somewhere
		if (SearchTokens.Num() > 0)
		{
			bool bAllMatch = true;
			for (const FString& Token : SearchTokens)
			{
				bool bTokenMatch = false;
				if (Entry.Name.ToLower().Contains(Token)) bTokenMatch = true;
				else if (Entry.Path.ToLower().Contains(Token)) bTokenMatch = true;
				else
				{
					for (const FString& AssetTag : Entry.Tags)
					{
						if (AssetTag.Contains(Token))
						{
							bTokenMatch = true;
							break;
						}
					}
				}

				if (!bTokenMatch)
				{
					bAllMatch = false;
					break;
				}
			}
			if (!bAllMatch) continue;
		}

		FilteredIndices.Add(i);
	}

	// Rebuild asset list UI
	AssetListBox->ClearChildren();

	// Cap displayed rows
	int32 DisplayLimit = FMath::Min(FilteredIndices.Num(), 500);

	for (int32 j = 0; j < DisplayLimit; j++)
	{
		const FAssetEntry& Entry = AllAssets[FilteredIndices[j]];
		bool bEvenRow = (j % 2 == 0);

		AssetListBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(bEvenRow
				? FLinearColor(0.02f, 0.02f, 0.02f, 0.5f)
				: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
			.Padding(FMargin(4.0f, 2.0f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.MinDesiredWidth(160.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Entry.Type))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.MinDesiredWidth(200.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Entry.Name))
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Path))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]
			]
		];
	}

	// Update filtered count label
	if (FilteredCountLabel.IsValid())
	{
		if (FilteredIndices.Num() > DisplayLimit)
		{
			FilteredCountLabel->SetText(FText::FromString(
				FString::Printf(TEXT("Showing %d of %d matches"), DisplayLimit, FilteredIndices.Num())));
		}
		else
		{
			FilteredCountLabel->SetText(FText::FromString(
				FString::Printf(TEXT("%d matches"), FilteredIndices.Num())));
		}
	}
}

FReply SArborRegistryWidget::OnRefreshClicked()
{
	LoadRegistryFromDisk();
	ApplyFilter();
	return FReply::Handled();
}

FReply SArborRegistryWidget::OnScanClicked()
{
	if (GEngine)
	{
		// NOTE: GEngine->Exec splits on semicolons, so we use __import__
		// to avoid multi-statement Python.
		GEngine->Exec(nullptr,
			TEXT("py __import__('arbor.registry',fromlist=['scan_project']).scan_project()"));
	}
	LoadRegistryFromDisk();
	ApplyFilter();
	return FReply::Handled();
}

void SArborRegistryWidget::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	ApplyFilter();
}

void SArborRegistryWidget::OnTypeFilterChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		SelectedType = NewSelection;
		ApplyFilter();
	}
}

#undef LOCTEXT_NAMESPACE
