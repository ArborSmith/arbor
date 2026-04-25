#include "ArborCharacterSheetWidget.h"
#include "ArborCharacterTypes.h"
#include "ArborCharacterBuilder.h"
#include "ArborGameContextTypes.h"
#include "ArborCodexContext.h"
#include "ArborPathUtils.h"
#include "ArborClaude.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "ArborCodexSearch.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "ArborCodexImagePanel.h"
#include "ArborGenerate3DMeshDialog.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "ArborCharacterSheetWidget"

// ─── Construction ────────────────────────────────────────────────────────────

void SArborCharacterSheetWidget::Construct(const FArguments& InArgs)
{
	CodexContext = InArgs._CodexContext;
	FolderPath = TEXT("/Game/Characters");

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// ---- Left panel: header + controls + list ----
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Bg::Surface)
			.Padding(0.0f)
			[
				SNew(SVerticalBox)

				// Header: title + New button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(12.0f, 10.0f, 12.0f, 6.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CharactersHeader", "Characters"))
						.Font(ArborCodexStyle::Font::SectionHeader())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("NewCharacter", "+ New"),
							FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnNewCharacterClicked))
					]
				]

				// Separator
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
					.Padding(0.0f)
					[ SNew(SBox).HeightOverride(1.0f) ]
				]

				// Role filter
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(12.0f, 6.0f, 12.0f, 2.0f)
				[
					SAssignNew(RoleFilterCombo, STextComboBox)
					.OptionsSource(&RoleFilterOptions)
					.OnSelectionChanged(this, &SArborCharacterSheetWidget::OnRoleFilterChanged)
				]

				// Search
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(12.0f, 4.0f, 12.0f, 6.0f)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("SearchHint", "Search by name..."))
					.Font(ArborCodexStyle::Font::Input())
					.OnTextChanged(this, &SArborCharacterSheetWidget::OnSearchTextChanged)
				]

				// Separator
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
					.Padding(0.0f)
					[ SNew(SBox).HeightOverride(1.0f) ]
				]

				// Character list
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(CharacterListBox, SVerticalBox)
					]
				]
			]
		]

		// ---- Right panel: detail ----
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SAssignNew(DetailPanel, SScrollBox)
		]
	];

	// Initialize role filter options
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("All")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Player")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("NPC")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Enemy")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Boss")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Companion")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Vehicle")));
	RoleFilterOptions.Add(MakeShared<FString>(TEXT("Other")));
	if (RoleFilterCombo.IsValid())
	{
		RoleFilterCombo->SetSelectedItem(RoleFilterOptions[0]);
	}

	if (CodexContext.IsValid())
	{
		ContextChangedHandle = CodexContext->OnContextChanged.AddSP(
			this, &SArborCharacterSheetWidget::OnCodexContextChanged);
	}

	ScanFolder();

	if (!CodexContext.IsValid())
	{
		ScanGameContextAssets();
	}
	else
	{
		ScanLocationAssets();
		LoadKeywordsFromContext();
	}

	// Build initial empty detail panel
	RebuildDetailPanel();
}

void SArborCharacterSheetWidget::OnCodexContextChanged()
{
	ScanFolder();
	ScanLocationAssets();
	LoadKeywordsFromContext();
	if (SelectedIndex >= 0 && SelectedIndex < AllCharacters.Num())
	{
		PopulateFromAsset(SelectedIndex);
	}
	else
	{
		RebuildDetailPanel();
	}
}

// ─── Data loading / filtering ────────────────────────────────────────────────

void SArborCharacterSheetWidget::ScanFolder()
{
	AllCharacters.Empty();
	FilteredIndices.Empty();
	SelectedIndex = -1;

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByPath(FName(*FolderPath), AssetList, /*bRecursive=*/true);

	// Also scan the GameContext-derived Characters subfolder if a context is selected
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		FString ContextDerivedPath = DerivePathFromGameContext(CodexContext->SelectedContextPath, TEXT("Characters"));
		if (!ContextDerivedPath.IsEmpty() && ContextDerivedPath != FolderPath)
		{
			AssetRegistry.GetAssetsByPath(FName(*ContextDerivedPath), AssetList, /*bRecursive=*/true);
		}
	}

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetClassPath != UCharacterDataAsset::StaticClass()->GetClassPathName())
		{
			continue;
		}

		UObject* LoadedObj = AssetData.GetAsset();
		UCharacterDataAsset* CharAsset = Cast<UCharacterDataAsset>(LoadedObj);
		if (!CharAsset)
		{
			continue;
		}

		// Context filtering when CodexContext is active
		if (CodexContext.IsValid() && CodexContext->HasContext())
		{
			FString CharContextPath = CharAsset->GameContext.ToSoftObjectPath().ToString();
			if (CharContextPath != CodexContext->SelectedContextPath)
			{
				continue;
			}
		}
		else if (CodexContext.IsValid() && !CodexContext->HasContext())
		{
			// "(None)" selected - show all assets
		}

		FCharacterListEntry Entry;
		Entry.AssetPath = AssetData.GetObjectPathString();
		Entry.Name = CharAsset->CharacterName;
		AllCharacters.Add(MoveTemp(Entry));
	}

	ApplyFilter();
}

void SArborCharacterSheetWidget::ApplyFilter()
{
	FilteredIndices.Empty();

	TArray<FString> SearchTokens;
	if (!SearchText.IsEmpty())
	{
		SearchText.ToLower().ParseIntoArray(SearchTokens, TEXT(" "), true);
	}

	for (int32 i = 0; i < AllCharacters.Num(); i++)
	{
		const FCharacterListEntry& Entry = AllCharacters[i];

		// Role filter
		if (RoleFilterIndex > 0)
		{
			UObject* Obj = UEditorAssetLibrary::LoadAsset(Entry.AssetPath);
			UCharacterDataAsset* CharAsset = Cast<UCharacterDataAsset>(Obj);
			if (CharAsset)
			{
				int32 RoleVal = static_cast<int32>(CharAsset->Role);
				// RoleFilterIndex 1=Player(0), 2=NPC(1), 3=Enemy(2), 4=Boss(3), 5=Companion(4), 6=Vehicle(5), 7=Other(6)
				if (RoleVal != (RoleFilterIndex - 1)) continue;
			}
		}

		if (SearchTokens.Num() > 0)
		{
			bool bAllMatch = true;
			for (const FString& Token : SearchTokens)
			{
				bool bTokenMatch = false;
				if (Entry.Name.ToLower().Contains(Token)) bTokenMatch = true;

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

	RebuildList();
}

void SArborCharacterSheetWidget::OnRoleFilterChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid()) return;
	for (int32 i = 0; i < RoleFilterOptions.Num(); i++)
	{
		if (*RoleFilterOptions[i] == *NewValue)
		{
			RoleFilterIndex = i;
			ApplyFilter();
			return;
		}
	}
}

void SArborCharacterSheetWidget::RebuildList()
{
	CharacterListBox->ClearChildren();

	for (int32 j = 0; j < FilteredIndices.Num(); j++)
	{
		int32 DataIndex = FilteredIndices[j];
		const FCharacterListEntry& Entry = AllCharacters[DataIndex];
		bool bEvenRow = (j % 2 == 0);
		bool bSelected = (DataIndex == SelectedIndex);

		CharacterListBox->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Left accent stripe
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(bSelected ? ArborCodexStyle::Accent::Primary : FLinearColor::Transparent)
				.Padding(0.0f)
				[
					SNew(SBox).WidthOverride(3.0f)
				]
			]

			// Content
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(bSelected
					? ArborCodexStyle::State::Selected
					: (bEvenRow ? ArborCodexStyle::Bg::SurfaceAlt : ArborCodexStyle::Bg::Surface))
				.Padding(ArborCodexStyle::Spacing::ListItemPadding)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.OnClicked_Lambda([this, DataIndex]()
					{
						SelectedIndex = DataIndex;
						PopulateFromAsset(DataIndex);
						RebuildList();
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, ArborCodexStyle::Spacing::ListThumbGap, 0.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(ArborCodexStyle::Bg::Elevated)
							.Padding(2.0f)
							[
								SNew(SBox)
								.WidthOverride(ArborCodexStyle::Spacing::ListThumbSize)
								.HeightOverride(ArborCodexStyle::Spacing::ListThumbSize)
								[
									SNew(SScaleBox)
									.Stretch(EStretch::ScaleToFit)
									[
										SNew(SImage)
										.Image_Lambda([this, DataIndex]() -> const FSlateBrush*
										{
											if (DataIndex >= AllCharacters.Num()) return nullptr;
											UObject* O = UEditorAssetLibrary::LoadAsset(AllCharacters[DataIndex].AssetPath);
											if (UCharacterDataAsset* C = Cast<UCharacterDataAsset>(O))
											{
												if (UTexture2D* Tex = C->ConceptArt.Get())
												{
													static TMap<FString, FSlateBrush> ThumbCache;
													FSlateBrush& B = ThumbCache.FindOrAdd(AllCharacters[DataIndex].AssetPath);
													B.SetResourceObject(Tex);
													B.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
													B.DrawAs = ESlateBrushDrawType::Image;
													return &B;
												}
											}
											return nullptr;
										})
									]
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Entry.Name))
							.Font(ArborCodexStyle::Font::FieldLabel())
							.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
						]
					]
				]
			]
		];
	}
}

// ─── Detail panel ────────────────────────────────────────────────────────────

void SArborCharacterSheetWidget::PopulateFromAsset(int32 Index)
{
	if (Index < 0 || Index >= AllCharacters.Num())
	{
		return;
	}

	const FCharacterListEntry& Entry = AllCharacters[Index];

	UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(Entry.AssetPath);
	UCharacterDataAsset* CharAsset = Cast<UCharacterDataAsset>(LoadedObj);
	if (!CharAsset)
	{
		return;
	}

	CurrentAssetPath = Entry.AssetPath;
	CurrentName = CharAsset->CharacterName;
	CurrentDescription = CharAsset->Description;
	CurrentTags = CharAsset->Tags;

	RebuildDetailPanel();
}

void SArborCharacterSheetWidget::ClearAllFields()
{
	CurrentAssetPath.Empty();
	CurrentName.Empty();
	CurrentDescription.Empty();
	CurrentTags.Empty();
	SelectedKeywords.Empty();
	LockedSections.Empty();
}

FReply SArborCharacterSheetWidget::OnNewCharacterClicked()
{
	SelectedIndex = -1;
	ClearAllFields();
	RebuildDetailPanel();
	RebuildList();
	return FReply::Handled();
}

void SArborCharacterSheetWidget::RebuildDetailPanel()
{
	DetailPanel->ClearChildren();

	// Reset widget pointers (they'll be recreated below)
	NameInput.Reset();
	DescriptionInput.Reset();
	AddTagInput.Reset();
	TagsPillBox.Reset();
	LocationComboBox.Reset();
	KeywordSearchBox.Reset();
	AvailableKeywordsWrapBox.Reset();
	SelectedKeywordsWrapBox.Reset();
	GameContextComboBox.Reset();
	StatusLabel.Reset();

	// Concept art image panel
	{
		UTexture2D* PrimaryTex = nullptr;
		TArray<UTexture2D*> GalleryTextures;
		FString PromptText;

		if (UCharacterDataAsset* CharAsset = Cast<UCharacterDataAsset>(UEditorAssetLibrary::LoadAsset(CurrentAssetPath)))
		{
			PrimaryTex = CharAsset->ConceptArt.LoadSynchronous();
			for (const auto& SoftRef : CharAsset->ConceptArtGallery)
			{
				GalleryTextures.Add(SoftRef.LoadSynchronous());
			}
			PromptText = CharAsset->ConceptArtPrompt;
		}

		DetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SNew(SArborCodexImagePanel)
			.ConceptArt(PrimaryTex)
			.Gallery(GalleryTextures)
			.Prompt(PromptText)
			.AssetPath(CurrentAssetPath)
		];
	}

	// ---- Name ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 8.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("NameLabel", "Name *"))
		.Font(ArborCodexStyle::Font::FieldLabel())
		.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SAssignNew(NameInput, SEditableTextBox)
		.Text(FText::FromString(CurrentName))
		.HintText(LOCTEXT("NameHint", "Character name"))
	];

	// ---- Role ----
	{
		// Determine current role for display
		EArborCharacterRole CurrentRole = EArborCharacterRole::NPC;
		if (!CurrentAssetPath.IsEmpty())
		{
			UObject* RoleObj = UEditorAssetLibrary::LoadAsset(CurrentAssetPath);
			UCharacterDataAsset* RoleAsset = Cast<UCharacterDataAsset>(RoleObj);
			if (RoleAsset) CurrentRole = RoleAsset->Role;
		}

		static const TArray<FString> RoleNames = {
			TEXT("Player"), TEXT("NPC"), TEXT("Enemy"), TEXT("Boss"),
			TEXT("Companion"), TEXT("Vehicle"), TEXT("Other")
		};

		TSharedPtr<STextComboBox> RoleCombo;
		TArray<TSharedPtr<FString>>* RoleOpts = new TArray<TSharedPtr<FString>>();
		for (const FString& R : RoleNames)
		{
			RoleOpts->Add(MakeShared<FString>(R));
		}

		DetailPanel->AddSlot()
		.Padding(8.0f, 0.0f, 8.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RoleLabel", "Role"))
			.Font(ArborCodexStyle::Font::FieldLabel())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
		];

		DetailPanel->AddSlot()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SAssignNew(RoleCombo, STextComboBox)
			.OptionsSource(RoleOpts)
			.OnSelectionChanged_Lambda([](TSharedPtr<FString>, ESelectInfo::Type) {})
		];

		int32 RoleIdx = static_cast<int32>(CurrentRole);
		if (RoleIdx >= 0 && RoleIdx < RoleOpts->Num())
		{
			RoleCombo->SetSelectedItem((*RoleOpts)[RoleIdx]);
		}
	}

	// ---- Description section ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 8.0f, 8.0f, 4.0f)
	[
		CreateSectionHeader(LOCTEXT("DescriptionHeader", "Description"), FName("description"))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SNew(SBox)
		.MinDesiredHeight(80.0f)
		.MaxDesiredHeight(200.0f)
		[
			SAssignNew(DescriptionInput, SMultiLineEditableTextBox)
			.Text(FText::FromString(CurrentDescription))
			.HintText(LOCTEXT("DescriptionHint", "Character description, backstory, play style..."))
			.AutoWrapText(true)
		]
	];

	// ---- Tags section ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 8.0f, 8.0f, 4.0f)
	[
		CreateSectionHeader(LOCTEXT("TagsHeader", "Tags"), FName("tags"))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 4.0f)
	[
		SAssignNew(TagsPillBox, SWrapBox)
		.UseAllottedSize(true)
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SAssignNew(AddTagInput, SEditableTextBox)
			.HintText(LOCTEXT("AddTagHint", "New tag..."))
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter)
				{
					FString NewTag = Text.ToString().TrimStartAndEnd();
					if (!NewTag.IsEmpty())
					{
						AddTag(NewTag);
					}
				}
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ArborCodexHelpers::MakeSecondaryButton(
				LOCTEXT("AddTag", "+ Add"),
				FOnClicked::CreateLambda([this]()
				{
					if (AddTagInput.IsValid())
					{
						FString NewTag = AddTagInput->GetText().ToString().TrimStartAndEnd();
						if (!NewTag.IsEmpty())
						{
							AddTag(NewTag);
						}
					}
					return FReply::Handled();
				}))
		]
	];

	RebuildTagsPills();

	// ---- Separator ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 4.0f, 8.0f, 4.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
		.Padding(0.0f)
		[ SNew(SBox).HeightOverride(1.0f) ]
	];

	// ---- Game Context selector (standalone mode only) ----
	if (!CodexContext.IsValid())
	{
		DetailPanel->AddSlot()
		.Padding(8.0f, 4.0f, 8.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GameContextLabel", "Game Context"))
			.Font(ArborCodexStyle::Font::FieldLabel())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
		];

		DetailPanel->AddSlot()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SAssignNew(GameContextComboBox, STextComboBox)
			.OptionsSource(&GameContextDisplayNames)
			.OnSelectionChanged(this, &SArborCharacterSheetWidget::OnGameContextSelected)
		];

		if (GameContextComboBox.IsValid() && GameContextDisplayNames.Num() > 0)
		{
			int32 SelectIdx = FMath::Max(0, SelectedGameContextIndex);
			if (SelectIdx < GameContextDisplayNames.Num())
			{
				GameContextComboBox->SetSelectedItem(GameContextDisplayNames[SelectIdx]);
			}
		}
	}

	// ---- Location selector ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 4.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LocationLabel", "Location"))
		.Font(ArborCodexStyle::Font::FieldLabel())
		.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SAssignNew(LocationComboBox, STextComboBox)
		.OptionsSource(&LocationDisplayNames)
		.OnSelectionChanged(this, &SArborCharacterSheetWidget::OnLocationSelected)
	];

	if (LocationComboBox.IsValid() && LocationDisplayNames.Num() > 0)
	{
		int32 SelectIdx = FMath::Max(0, SelectedLocationIndex);
		if (SelectIdx < LocationDisplayNames.Num())
		{
			LocationComboBox->SetSelectedItem(LocationDisplayNames[SelectIdx]);
		}
	}

	// ---- Keywords section ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 4.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("KeywordsLabel", "Keywords"))
		.Font(ArborCodexStyle::Font::FieldLabel())
		.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 4.0f)
	[
		SAssignNew(KeywordSearchBox, SEditableTextBox)
		.HintText(LOCTEXT("KeywordSearchHint", "Filter keywords..."))
		.OnTextChanged(this, &SArborCharacterSheetWidget::OnKeywordSearchChanged)
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SAssignNew(AvailableKeywordsWrapBox, SWrapBox)
		.UseAllottedSize(true)
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SelectedKeywordsLabel", "Selected:"))
		.Font(ArborCodexStyle::Font::BodySmall())
		.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
	];

	DetailPanel->AddSlot()
	.Padding(8.0f, 0.0f, 8.0f, 8.0f)
	[
		SAssignNew(SelectedKeywordsWrapBox, SWrapBox)
		.UseAllottedSize(true)
	];

	RebuildAvailableKeywords();
	RebuildSelectedKeywords();

	// ---- Separator ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 4.0f, 8.0f, 4.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
		.Padding(0.0f)
		[ SNew(SBox).HeightOverride(1.0f) ]
	];

	// ---- Action buttons ----
	DetailPanel->AddSlot()
	.Padding(FMargin(12.0f, 12.0f, 12.0f, 8.0f))
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakePrimaryButton(
				LOCTEXT("Save", "Save"),
				FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnSaveClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("GenerateAll", "Generate All"),
				FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnGenerateAllClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("Generate3DMesh", "Generate 3D Mesh"),
				FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnGenerate3DMeshClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeSecondaryButton(
				LOCTEXT("OpenInEditor", "Open in UE5"),
				FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnOpenInEditorClicked))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ArborCodexHelpers::MakeDangerButton(
				LOCTEXT("DeleteCharacter", "Delete"),
				FOnClicked::CreateSP(this, &SArborCharacterSheetWidget::OnDeleteCharacterClicked))
		]
	];

	// ---- Status label ----
	DetailPanel->AddSlot()
	.Padding(8.0f, 4.0f, 8.0f, 8.0f)
	[
		SAssignNew(StatusLabel, STextBlock)
		.AutoWrapText(true)
	];
}

// ─── Section header with Lock + AI buttons ───────────────────────────────────

TSharedRef<SWidget> SArborCharacterSheetWidget::CreateSectionHeader(const FText& Title, FName SectionId)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
				.Padding(0.0f)
				[ SNew(SBox).HeightOverride(1.0f) ]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
				.Padding(0.0f)
				[ SNew(SBox).HeightOverride(1.0f) ]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, SectionId]()
			{
				return OnToggleLock(SectionId);
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, SectionId]() -> FText
				{
					return IsSectionLocked(SectionId)
						? LOCTEXT("Locked", "Locked")
						: LOCTEXT("Lock", "Lock");
				})
				.ColorAndOpacity_Lambda([this, SectionId]() -> FSlateColor
				{
					return IsSectionLocked(SectionId)
						? FSlateColor(ArborCodexStyle::State::Locked)
						: FSlateColor(ArborCodexStyle::State::Unlocked);
				})
				.Font(ArborCodexStyle::Font::LockLabel())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.IsEnabled_Lambda([this, SectionId]() { return !IsSectionLocked(SectionId); })
			.OnClicked_Lambda([this, SectionId]()
			{
				return OnSectionAIClicked(SectionId);
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AI", "AI"))
				.Font(ArborCodexStyle::Font::ButtonText())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::AI))
			]
		];
}

FReply SArborCharacterSheetWidget::OnToggleLock(FName SectionId)
{
	if (LockedSections.Contains(SectionId))
	{
		LockedSections.Remove(SectionId);
	}
	else
	{
		LockedSections.Add(SectionId);
	}
	return FReply::Handled();
}

bool SArborCharacterSheetWidget::IsSectionLocked(FName SectionId) const
{
	return LockedSections.Contains(SectionId);
}

// ─── Tag editing ─────────────────────────────────────────────────────────────

void SArborCharacterSheetWidget::AddTag(const FString& InTag)
{
	if (!CurrentTags.Contains(InTag))
	{
		CurrentTags.Add(InTag);
	}
	if (AddTagInput.IsValid())
	{
		AddTagInput->SetText(FText::GetEmpty());
	}
	RebuildTagsPills();
}

void SArborCharacterSheetWidget::RemoveTag(const FString& InTag)
{
	CurrentTags.Remove(InTag);
	RebuildTagsPills();
}

void SArborCharacterSheetWidget::RebuildTagsPills()
{
	if (!TagsPillBox.IsValid())
	{
		return;
	}

	TagsPillBox->ClearChildren();

	for (const FString& TagStr : CurrentTags)
	{
		TagsPillBox->AddSlot()
		.Padding(2.0f)
		[
			CreateRemovableTagPill(TagStr)
		];
	}
}

TSharedRef<SWidget> SArborCharacterSheetWidget::CreateRemovableTagPill(const FString& InTag)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Accent::PrimaryDim)
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InTag))
				.Font(ArborCodexStyle::Font::BodySmall())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::OnAccent))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, InTag]()
				{
					RemoveTag(InTag);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u00D7")))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::State::Danger))
				]
			]
		];
}

// ─── Save ────────────────────────────────────────────────────────────────────

FString SArborCharacterSheetWidget::CollectFormAsJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : CurrentName;
	FString Description = DescriptionInput.IsValid() ? DescriptionInput->GetText().ToString().TrimStartAndEnd() : CurrentDescription;

	Root->SetStringField(TEXT("name"), Name);

	// When editing an existing asset, use its original folder so CreateCharacterAsset
	// resolves to the same package path and updates in place instead of creating a duplicate.
	if (!CurrentAssetPath.IsEmpty())
	{
		FString OriginalFolder = FPackageName::GetLongPackagePath(CurrentAssetPath);
		Root->SetStringField(TEXT("content_path"), OriginalFolder);
	}
	else
	{
		Root->SetStringField(TEXT("content_path"), FolderPath);
	}

	if (!Description.IsEmpty())
	{
		Root->SetStringField(TEXT("description"), Description);
	}

	// Tags
	if (CurrentTags.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TagsArr;
		for (const FString& TagStr : CurrentTags)
		{
			TagsArr.Add(MakeShared<FJsonValueString>(TagStr));
		}
		Root->SetArrayField(TEXT("tags"), TagsArr);
	}

	// Game context
	FString ContextAssetPath;
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		ContextAssetPath = CodexContext->SelectedContextPath;
	}
	else if (!CodexContext.IsValid() && SelectedGameContextIndex > 0 && SelectedGameContextIndex < GameContextAssetPaths.Num())
	{
		ContextAssetPath = GameContextAssetPaths[SelectedGameContextIndex];
	}

	if (!ContextAssetPath.IsEmpty())
	{
		Root->SetStringField(TEXT("game_context"), ContextAssetPath);
	}

	// Keywords
	if (SelectedKeywords.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> KeywordsArr;
		for (const FString& Kw : SelectedKeywords)
		{
			KeywordsArr.Add(MakeShared<FJsonValueString>(Kw));
		}
		Root->SetArrayField(TEXT("keywords"), KeywordsArr);
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FReply SArborCharacterSheetWidget::OnSaveClicked()
{
	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
	if (Name.IsEmpty())
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NameRequired", "Name is required."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	FString Json = CollectFormAsJson();
	FString Result = UArborCharacterBuilder::CreateCharacterAsset(Json);

	TSharedPtr<FJsonObject> ResultObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
	if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
	{
		bool bSuccess = false;
		ResultObj->TryGetBoolField(TEXT("success"), bSuccess);

		if (bSuccess)
		{
			FString AssetPath;
			ResultObj->TryGetStringField(TEXT("asset_path"), AssetPath);

			ScanFolder();

			// Find and select the saved asset
			for (int32 i = 0; i < AllCharacters.Num(); i++)
			{
				if (AllCharacters[i].AssetPath.Contains(AssetPath))
				{
					SelectedIndex = i;
					PopulateFromAsset(i);
					RebuildList();
					break;
				}
			}

			if (StatusLabel.IsValid())
			{
				StatusLabel->SetText(LOCTEXT("Saved", "Character saved successfully."));
				StatusLabel->SetColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::Primary));
			}
		}
		else
		{
			FString Error;
			ResultObj->TryGetStringField(TEXT("error"), Error);
			if (StatusLabel.IsValid())
			{
				StatusLabel->SetText(FText::FromString(FString::Printf(TEXT("Error: %s"), *Error)));
				StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
			}
		}
	}

	return FReply::Handled();
}

// ─── Open in Editor ──────────────────────────────────────────────────────────

FReply SArborCharacterSheetWidget::OnOpenInEditorClicked()
{
	if (!CurrentAssetPath.IsEmpty())
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(CurrentAssetPath);
		if (Asset)
		{
			UAssetEditorSubsystem* EditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (EditorSub)
			{
				EditorSub->OpenEditorForAsset(Asset);
			}
		}
	}
	return FReply::Handled();
}

// ─── Per-section AI ──────────────────────────────────────────────────────────

FString SArborCharacterSheetWidget::BuildSectionPrompt(FName SectionId) const
{
	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : CurrentName;

	FString Prompt;

	// Game context info
	FString ContextAssetPath;
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		ContextAssetPath = CodexContext->SelectedContextPath;
	}
	else if (!CodexContext.IsValid() && SelectedGameContextIndex > 0 && SelectedGameContextIndex < GameContextAssetPaths.Num())
	{
		ContextAssetPath = GameContextAssetPaths[SelectedGameContextIndex];
	}

	if (!ContextAssetPath.IsEmpty())
	{
		UObject* CtxObj = UEditorAssetLibrary::LoadAsset(ContextAssetPath);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(CtxObj);
		if (Ctx)
		{
			Prompt += TEXT("## Game Context\n");
			Prompt += FString::Printf(TEXT("Title: %s\n"), *Ctx->GameTitle);
			Prompt += FString::Printf(TEXT("Genre: %s\n"), *Ctx->Genre);
			Prompt += FString::Printf(TEXT("Setting: %s\n"), *Ctx->Setting);
			Prompt += FString::Printf(TEXT("Tone: %s\n\n"), *Ctx->Tone);
		}
	}

	// Character context for coherence
	Prompt += TEXT("## Character Context\n");
	Prompt += FString::Printf(TEXT("Name: %s\n"), *Name);
	if (CurrentTags.Num() > 0)
	{
		Prompt += FString::Printf(TEXT("Tags: %s\n"), *FString::Join(CurrentTags, TEXT(", ")));
	}
	Prompt += TEXT("\n");

	// Section-specific instruction
	FString SectionStr = SectionId.ToString();

	if (SectionStr == TEXT("description"))
	{
		Prompt += TEXT("Regenerate ONLY the description for this character.\n");
		Prompt += TEXT("Write a 2-3 paragraph description covering backstory, play style, and personality.\n\n");
	}
	else if (SectionStr == TEXT("tags"))
	{
		Prompt += TEXT("Regenerate ONLY the tags for this character.\n");
		Prompt += TEXT("Generate 3-8 short tag strings that define this character.\n\n");
	}

	// How to save
	if (!CurrentAssetPath.IsEmpty())
	{
		Prompt += FString::Printf(
			TEXT("Save using ue5_codex(action: \"character_update_section\", asset_path: \"%s\", section: \"%s\", data: ...).\n"),
			*CurrentAssetPath, *SectionStr);
	}
	else
	{
		Prompt += FString::Printf(
			TEXT("Save using ue5_codex(action: \"character_create\") with only the %s section filled, along with the name. Save to folder: %s\n"),
			*SectionStr, *FolderPath);
	}

	Prompt += TEXT("Do not ask for confirmation, just do it.");

	return Prompt;
}

FReply SArborCharacterSheetWidget::OnSectionAIClicked(FName SectionId)
{
	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : CurrentName;
	if (Name.IsEmpty())
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NameRequiredAI", "Name is required before AI generation."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	FString Prompt = BuildSectionPrompt(SectionId);

	if (!FArborClaude::SendMessage(Prompt))
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NoChatWidget", "Could not start Claude. Try opening the Chat tab first."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::Format(
			LOCTEXT("SentSectionAI", "Generating {0}... Check the Chat tab, then Scan when done."),
			FText::FromName(SectionId)));
		StatusLabel->SetColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::Primary));
	}

	return FReply::Handled();
}

// ─── Generate All (lock-aware) ───────────────────────────────────────────────

FString SArborCharacterSheetWidget::BuildGenerateAllPrompt() const
{
	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : CurrentName;

	FString Prompt;

	// Game context
	FString ContextAssetPath;
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		ContextAssetPath = CodexContext->SelectedContextPath;
	}
	else if (!CodexContext.IsValid() && SelectedGameContextIndex > 0 && SelectedGameContextIndex < GameContextAssetPaths.Num())
	{
		ContextAssetPath = GameContextAssetPaths[SelectedGameContextIndex];
	}

	if (!ContextAssetPath.IsEmpty())
	{
		UObject* CtxObj = UEditorAssetLibrary::LoadAsset(ContextAssetPath);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(CtxObj);
		if (Ctx)
		{
			Prompt += TEXT("## Game Context\n");
			Prompt += FString::Printf(TEXT("Title: %s\n"), *Ctx->GameTitle);
			Prompt += FString::Printf(TEXT("Genre: %s\n"), *Ctx->Genre);
			Prompt += FString::Printf(TEXT("Setting: %s\n"), *Ctx->Setting);
			Prompt += FString::Printf(TEXT("Tone: %s\n"), *Ctx->Tone);
			if (!Ctx->WorldDescription.IsEmpty())
			{
				Prompt += FString::Printf(TEXT("World: %s\n"), *Ctx->WorldDescription);
			}
			Prompt += TEXT("\n");
		}
	}

	// Location
	if (SelectedLocationIndex > 0 && SelectedLocationIndex < LocationAssetPaths.Num())
	{
		UObject* LocObj = UEditorAssetLibrary::LoadAsset(LocationAssetPaths[SelectedLocationIndex]);
		UArborLocationAsset* Loc = Cast<UArborLocationAsset>(LocObj);
		if (Loc)
		{
			Prompt += TEXT("## Location\n");
			Prompt += FString::Printf(TEXT("Name: %s\n"), *Loc->LocationName);
			if (!Loc->Region.IsEmpty()) Prompt += FString::Printf(TEXT("Region: %s\n"), *Loc->Region);
			if (!Loc->Atmosphere.IsEmpty()) Prompt += FString::Printf(TEXT("Atmosphere: %s\n"), *Loc->Atmosphere);
			if (!Loc->Description.IsEmpty()) Prompt += FString::Printf(TEXT("Description: %s\n"), *Loc->Description);
			Prompt += TEXT("\n");
		}
	}

	// Character
	Prompt += FString::Printf(
		TEXT("## Character\n"
			 "Create a complete character data asset using the ue5_codex MCP tool (action: \"character_create\").\n\n"
			 "Character Name: %s\n"),
		*Name);

	if (SelectedKeywords.Num() > 0)
	{
		Prompt += FString::Printf(TEXT("Keywords: %s\n"), *FString::Join(SelectedKeywords, TEXT(", ")));
	}

	if (!ContextAssetPath.IsEmpty())
	{
		Prompt += FString::Printf(TEXT("game_context: %s\n"), *ContextAssetPath);
	}

	Prompt += FString::Printf(TEXT("Save to folder: %s\n\n"), *FolderPath);

	// Determine locked vs unlocked sections
	static const TArray<TTuple<FName, FString>> AllSections = {
		{FName("description"), TEXT("description: 2-3 paragraphs covering backstory, play style, and personality")},
		{FName("tags"), TEXT("tags: 3-8 short tag strings that define this character")}
	};

	TArray<FString> UnlockedDescriptions;
	TArray<FString> LockedDescriptions;

	for (const auto& Section : AllSections)
	{
		if (IsSectionLocked(Section.Get<0>()))
		{
			LockedDescriptions.Add(Section.Get<1>());
		}
		else
		{
			UnlockedDescriptions.Add(Section.Get<1>());
		}
	}

	if (UnlockedDescriptions.Num() > 0)
	{
		Prompt += TEXT("Generate the following sections:\n");
		for (const FString& Desc : UnlockedDescriptions)
		{
			Prompt += FString::Printf(TEXT("- %s\n"), *Desc);
		}
	}

	if (LockedDescriptions.Num() > 0)
	{
		Prompt += TEXT("\nKeep the following sections EXACTLY as they are (do not regenerate):\n");
		for (const FString& Desc : LockedDescriptions)
		{
			Prompt += FString::Printf(TEXT("- %s\n"), *Desc);
		}

		// Include locked section values as context
		if (IsSectionLocked(FName("description")))
		{
			FString D = DescriptionInput.IsValid() ? DescriptionInput->GetText().ToString() : CurrentDescription;
			if (!D.IsEmpty()) Prompt += FString::Printf(TEXT("\nCurrent description: %s\n"), *D);
		}
		if (IsSectionLocked(FName("tags")) && CurrentTags.Num() > 0)
		{
			Prompt += FString::Printf(TEXT("\nCurrent tags: %s\n"), *FString::Join(CurrentTags, TEXT(", ")));
		}
	}

	Prompt += TEXT("\nCall the MCP tool to save the character. Do not ask for confirmation, just create it.");

	return Prompt;
}

FReply SArborCharacterSheetWidget::OnGenerateAllClicked()
{
	FString Name = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : TEXT("");
	if (Name.IsEmpty())
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NameRequiredGen", "Name is required."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	// Check if all sections are locked
	static const TArray<FName> SectionIds = {
		FName("description"), FName("tags")
	};

	bool bAllLocked = true;
	for (const FName& Id : SectionIds)
	{
		if (!IsSectionLocked(Id))
		{
			bAllLocked = false;
			break;
		}
	}

	if (bAllLocked)
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("AllLocked", "All sections are locked. Unlock at least one section to generate."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.2f)));
		}
		return FReply::Handled();
	}

	if (!FArborClaude::SendMessage(BuildGenerateAllPrompt()))
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NoChatWidgetGen", "Could not start Claude. Try opening the Chat tab first."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(LOCTEXT("SentToChat", "Sent to Chat. Switch to the Chat tab to see progress, then Scan when done."));
		StatusLabel->SetColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::Primary));
	}

	return FReply::Handled();
}

// ─── Browse mode event handlers ──────────────────────────────────────────────

FReply SArborCharacterSheetWidget::OnScanClicked()
{
	ScanFolder();
	if (SelectedIndex >= 0 && SelectedIndex < AllCharacters.Num())
	{
		PopulateFromAsset(SelectedIndex);
	}
	else
	{
		RebuildDetailPanel();
	}
	return FReply::Handled();
}

void SArborCharacterSheetWidget::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	ApplyFilter();
}

void SArborCharacterSheetWidget::OnFolderPathCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	FolderPath = NewText.ToString();
}

// ─── Game Context / Location / Keywords ──────────────────────────────────────

void SArborCharacterSheetWidget::ScanGameContextAssets()
{
	GameContextDisplayNames.Empty();
	GameContextAssetPaths.Empty();
	SelectedGameContextIndex = -1;

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UArborGameContextAsset::StaticClass()->GetClassPathName(), AssetList);

	GameContextDisplayNames.Add(MakeShared<FString>(TEXT("(None)")));
	GameContextAssetPaths.Add(TEXT(""));

	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Obj = AssetData.GetAsset();
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
		if (!Ctx)
		{
			continue;
		}

		FString DisplayName = Ctx->GameTitle.IsEmpty()
			? AssetData.AssetName.ToString()
			: Ctx->GameTitle;
		GameContextDisplayNames.Add(MakeShared<FString>(DisplayName));
		GameContextAssetPaths.Add(AssetData.GetObjectPathString());
	}

	ScanLocationAssets();
	AllKeywords.Empty();
	SelectedKeywords.Empty();
}

void SArborCharacterSheetWidget::ScanLocationAssets()
{
	LocationDisplayNames.Empty();
	LocationAssetPaths.Empty();
	SelectedLocationIndex = -1;

	LocationDisplayNames.Add(MakeShared<FString>(TEXT("(None)")));
	LocationAssetPaths.Add(TEXT(""));

	FString SelectedContextPath;
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		SelectedContextPath = CodexContext->SelectedContextPath;
	}
	else if (!CodexContext.IsValid() && SelectedGameContextIndex > 0 && SelectedGameContextIndex < GameContextAssetPaths.Num())
	{
		SelectedContextPath = GameContextAssetPaths[SelectedGameContextIndex];
	}

	if (!SelectedContextPath.IsEmpty())
	{
		IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssetsByClass(UArborLocationAsset::StaticClass()->GetClassPathName(), AssetList);

		for (const FAssetData& AssetData : AssetList)
		{
			UObject* Obj = AssetData.GetAsset();
			UArborLocationAsset* Loc = Cast<UArborLocationAsset>(Obj);
			if (!Loc)
			{
				continue;
			}

			FString LocContextPath = Loc->GameContext.ToSoftObjectPath().ToString();
			if (LocContextPath != SelectedContextPath)
			{
				continue;
			}

			FString DisplayName = Loc->LocationName.IsEmpty()
				? AssetData.AssetName.ToString()
				: Loc->LocationName;
			LocationDisplayNames.Add(MakeShared<FString>(DisplayName));
			LocationAssetPaths.Add(AssetData.GetObjectPathString());
		}
	}

	if (LocationComboBox.IsValid())
	{
		LocationComboBox->RefreshOptions();
		if (LocationDisplayNames.Num() > 0)
		{
			LocationComboBox->SetSelectedItem(LocationDisplayNames[0]);
		}
	}
}

void SArborCharacterSheetWidget::OnGameContextSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	SelectedGameContextIndex = -1;
	if (NewValue.IsValid())
	{
		for (int32 i = 0; i < GameContextDisplayNames.Num(); i++)
		{
			if (*GameContextDisplayNames[i] == *NewValue)
			{
				SelectedGameContextIndex = i;
				break;
			}
		}
	}

	ScanLocationAssets();
	LoadKeywordsFromContext();
}

void SArborCharacterSheetWidget::OnLocationSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	SelectedLocationIndex = -1;
	if (NewValue.IsValid())
	{
		for (int32 i = 0; i < LocationDisplayNames.Num(); i++)
		{
			if (*LocationDisplayNames[i] == *NewValue)
			{
				SelectedLocationIndex = i;
				break;
			}
		}
	}
}

void SArborCharacterSheetWidget::LoadKeywordsFromContext()
{
	AllKeywords.Empty();
	SelectedKeywords.Empty();
	KeywordFilterText.Empty();
	if (KeywordSearchBox.IsValid())
	{
		KeywordSearchBox->SetText(FText::GetEmpty());
	}

	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		UObject* Obj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
		if (Ctx)
		{
			AllKeywords = Ctx->Tags;
		}
	}
	else if (!CodexContext.IsValid() && SelectedGameContextIndex > 0 && SelectedGameContextIndex < GameContextAssetPaths.Num())
	{
		UObject* Obj = UEditorAssetLibrary::LoadAsset(GameContextAssetPaths[SelectedGameContextIndex]);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
		if (Ctx)
		{
			AllKeywords = Ctx->Tags;
		}
	}

	RebuildAvailableKeywords();
	RebuildSelectedKeywords();
}

void SArborCharacterSheetWidget::RebuildAvailableKeywords()
{
	if (!AvailableKeywordsWrapBox.IsValid())
	{
		return;
	}

	AvailableKeywordsWrapBox->ClearChildren();

	for (const FString& Keyword : AllKeywords)
	{
		if (SelectedKeywords.Contains(Keyword))
		{
			continue;
		}

		if (!KeywordFilterText.IsEmpty() && !Keyword.Contains(KeywordFilterText))
		{
			continue;
		}

		AvailableKeywordsWrapBox->AddSlot()
		.Padding(2.0f)
		[
			CreateClickableKeywordPill(Keyword)
		];
	}
}

void SArborCharacterSheetWidget::RebuildSelectedKeywords()
{
	if (!SelectedKeywordsWrapBox.IsValid())
	{
		return;
	}

	SelectedKeywordsWrapBox->ClearChildren();

	for (const FString& Keyword : SelectedKeywords)
	{
		SelectedKeywordsWrapBox->AddSlot()
		.Padding(2.0f)
		[
			CreateRemovableKeywordPill(Keyword)
		];
	}
}

void SArborCharacterSheetWidget::OnKeywordSearchChanged(const FText& NewText)
{
	KeywordFilterText = NewText.ToString();
	RebuildAvailableKeywords();
}

void SArborCharacterSheetWidget::AddKeyword(const FString& Keyword)
{
	if (!SelectedKeywords.Contains(Keyword))
	{
		SelectedKeywords.Add(Keyword);
	}
	RebuildAvailableKeywords();
	RebuildSelectedKeywords();
}

void SArborCharacterSheetWidget::RemoveKeyword(const FString& Keyword)
{
	SelectedKeywords.Remove(Keyword);
	RebuildAvailableKeywords();
	RebuildSelectedKeywords();
}

TSharedRef<SWidget> SArborCharacterSheetWidget::CreateClickableKeywordPill(const FString& Keyword)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.OnClicked_Lambda([this, Keyword]()
		{
			AddKeyword(Keyword);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Bg::Elevated)
			.Padding(FMargin(8.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Keyword))
				.Font(ArborCodexStyle::Font::BodySmall())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]
		];
}

TSharedRef<SWidget> SArborCharacterSheetWidget::CreateRemovableKeywordPill(const FString& Keyword)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Accent::PrimaryDim)
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Keyword))
				.Font(ArborCodexStyle::Font::BodySmall())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::OnAccent))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, Keyword]()
				{
					RemoveKeyword(Keyword);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u00D7")))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::State::Danger))
				]
			]
		];
}

FReply SArborCharacterSheetWidget::OnDeleteCharacterClicked()
{
	if (CurrentAssetPath.IsEmpty())
	{
		return FReply::Handled();
	}

	FString Name = CurrentName.IsEmpty() ? FPaths::GetBaseFilename(CurrentAssetPath) : CurrentName;

	TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
		.Title(LOCTEXT("DeleteCharacterTitle", "Delete Character"))
		.ClientSize(FVector2D(420, 150))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize);

	TWeakPtr<SWindow> WeakWindow = ConfirmWindow;
	TWeakPtr<SArborCharacterSheetWidget> WeakSelf = SharedThis(this);
	FString AssetPath = CurrentAssetPath;

	ConfirmWindow->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(16.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("DeleteCharacterConfirm", "Are you sure you want to delete \"{0}\"?\nThis action cannot be undone."),
				FText::FromString(Name)))
			.AutoWrapText(true)
			.Font(ArborCodexStyle::Font::Body())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(16.0f, 0.0f, 16.0f, 16.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ArborCodexHelpers::MakeSecondaryButton(
					LOCTEXT("CancelDelete", "Cancel"),
					FOnClicked::CreateLambda([WeakWindow]()
					{
						if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						{
							Win->RequestDestroyWindow();
						}
						return FReply::Handled();
					}))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ArborCodexHelpers::MakeDangerButton(
					LOCTEXT("ConfirmDelete", "Delete"),
					FOnClicked::CreateLambda([WeakWindow, WeakSelf, AssetPath]()
					{
						UArborCodexSearch::DeleteCodexEntry(AssetPath);

						if (TSharedPtr<SArborCharacterSheetWidget> Self = WeakSelf.Pin())
						{
							Self->SelectedIndex = -1;
							Self->CurrentAssetPath.Empty();
							Self->ScanFolder();
							Self->ApplyFilter();
							Self->RebuildList();
							Self->ClearAllFields();
						}

						if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						{
							Win->RequestDestroyWindow();
						}
						return FReply::Handled();
					}))
			]
		]
	);

	FSlateApplication::Get().AddWindow(ConfirmWindow);

	return FReply::Handled();
}

// ─── Generate 3D Mesh ────────────────────────────────────────────────────────

FReply SArborCharacterSheetWidget::OnGenerate3DMeshClicked()
{
	ArborGenerate3DMeshDialog::FMeshGenOptions Options;
	Options.CharacterName = NameInput.IsValid() ? NameInput->GetText().ToString().TrimStartAndEnd() : CurrentName;
	Options.AssetPath = CurrentAssetPath;

	if (Options.CharacterName.IsEmpty())
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NameRequired3D", "Name is required before generating a 3D mesh."));
			StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f)));
		}
		return FReply::Handled();
	}

	// Gather concept art from the character asset
	if (!CurrentAssetPath.IsEmpty())
	{
		UObject* Obj = UEditorAssetLibrary::LoadAsset(CurrentAssetPath);
		UCharacterDataAsset* CharAsset = Cast<UCharacterDataAsset>(Obj);
		if (CharAsset)
		{
			Options.PrimaryConceptArt = CharAsset->ConceptArt.LoadSynchronous();

			for (const auto& SoftRef : CharAsset->ConceptArtGallery)
			{
				UTexture2D* Tex = SoftRef.LoadSynchronous();
				if (Tex)
				{
					Options.GalleryTextures.Add(Tex);
				}
			}
		}
	}

	ArborGenerate3DMeshDialog::Show(Options);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
