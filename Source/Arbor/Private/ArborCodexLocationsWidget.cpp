#include "ArborCodexLocationsWidget.h"
#include "ArborCodexContext.h"
#include "ArborGameContextTypes.h"
#include "ArborClaude.h"
#include "ArborAIPromptDialog.h"
#include "ArborAIFieldIterateDialog.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "ArborCodexImagePanel.h"
#include "ArborTagInput.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "ArborCodexSearch.h"
#include "UObject/SavePackage.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "ArborCodexLocationsWidget"

bool SArborCodexLocationsWidget::IsFieldLocked(const FString& FieldKey) const
{
	return LockedFields.Contains(FieldKey);
}

void SArborCodexLocationsWidget::ToggleFieldLock(const FString& FieldKey)
{
	if (LockedFields.Contains(FieldKey))
	{
		LockedFields.Remove(FieldKey);
	}
	else
	{
		LockedFields.Add(FieldKey);
	}
	SaveLockedFieldsToAsset();
}

void SArborCodexLocationsWidget::SaveLockedFieldsToAsset()
{
	if (SelectedLocationIndex < 0 || SelectedLocationIndex >= LocationAssetPaths.Num())
	{
		return;
	}
	UObject* Obj = UEditorAssetLibrary::LoadAsset(LocationAssetPaths[SelectedLocationIndex]);
	UArborLocationAsset* Loc = Cast<UArborLocationAsset>(Obj);
	if (!Loc)
	{
		return;
	}
	Loc->Modify();
	Loc->LockedFields = LockedFields;
	UPackage* Package = Loc->GetOutermost();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Loc, *PackageFilename, SaveArgs);
}

void SArborCodexLocationsWidget::Construct(const FArguments& InArgs)
{
	CodexContext = InArgs._CodexContext;

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Location list (35%)
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Bg::Surface)
			.Padding(0.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10.0f, 8.0f, 10.0f, 6.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LocationsLabel", "Locations"))
						.Font(ArborCodexStyle::Font::SectionHeader())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("NewLocation", "+ New"),
							FOnClicked::CreateSP(this, &SArborCodexLocationsWidget::OnNewLocationClicked))
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

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(LocationListBox, SVerticalBox)
					]
				]
			]
		]

		// Location detail (65%)
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SAssignNew(LocationDetailPanel, SScrollBox)
		]
	];

	if (CodexContext.IsValid())
	{
		ContextChangedHandle = CodexContext->OnContextChanged.AddSP(
			this, &SArborCodexLocationsWidget::OnContextChanged);
		OnContextChanged();
	}
}

void SArborCodexLocationsWidget::OnContextChanged()
{
	ScanLocations();
}

void SArborCodexLocationsWidget::ScanLocations()
{
	LocationAssetPaths.Empty();
	LocationDisplayNames.Empty();
	SelectedLocationIndex = -1;

	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		RebuildLocationList();
		LocationDetailPanel->ClearChildren();
		return;
	}

	FString SelectedContextPath = CodexContext->SelectedContextPath;

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

		LocationAssetPaths.Add(AssetData.GetObjectPathString());
		FString DisplayName = Loc->LocationName.IsEmpty()
			? AssetData.AssetName.ToString()
			: Loc->LocationName;
		LocationDisplayNames.Add(DisplayName);
	}

	RebuildLocationList();
	LocationDetailPanel->ClearChildren();
}

void SArborCodexLocationsWidget::RebuildLocationList()
{
	LocationListBox->ClearChildren();

	for (int32 i = 0; i < LocationDisplayNames.Num(); i++)
	{
		bool bSelected = (i == SelectedLocationIndex);
		bool bEvenRow = (i % 2 == 0);

		LocationListBox->AddSlot()
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
					.OnClicked_Lambda([this, i]()
					{
						SelectedLocationIndex = i;
						RebuildLocationList();
						ShowLocationDetail(i);
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
										.Image_Lambda([this, i]() -> const FSlateBrush*
										{
											if (i >= LocationAssetPaths.Num()) return nullptr;
											UObject* O = UEditorAssetLibrary::LoadAsset(LocationAssetPaths[i]);
											if (UArborLocationAsset* L = Cast<UArborLocationAsset>(O))
											{
												if (UTexture2D* Tex = L->ConceptArt.Get())
												{
													static TMap<FString, FSlateBrush> ThumbCache;
													FSlateBrush& B = ThumbCache.FindOrAdd(LocationAssetPaths[i]);
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
							.Text(FText::FromString(LocationDisplayNames[i]))
							.Font(ArborCodexStyle::Font::FieldLabel())
							.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
						]
					]
				]
			]
		];
	}
}

void SArborCodexLocationsWidget::ShowLocationDetail(int32 Index)
{
	LocationDetailPanel->ClearChildren();

	if (Index < 0 || Index >= LocationAssetPaths.Num())
	{
		return;
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(LocationAssetPaths[Index]);
	UArborLocationAsset* Loc = Cast<UArborLocationAsset>(Obj);
	if (!Loc)
	{
		return;
	}

	LockedFields = Loc->LockedFields;

	// Concept art image panel
	{
		UTexture2D* PrimaryTex = Loc->ConceptArt.LoadSynchronous();
		TArray<UTexture2D*> GalleryTextures;
		for (const auto& SoftRef : Loc->ConceptArtGallery)
		{
			GalleryTextures.Add(SoftRef.LoadSynchronous());
		}

		LocationDetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SNew(SArborCodexImagePanel)
			.ConceptArt(PrimaryTex)
			.Gallery(GalleryTextures)
			.Prompt(Loc->ConceptArtPrompt)
			.AssetPath(LocationAssetPaths[Index])
		];
	}

	// Fields
	LocationDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("LocName", "Name"), LocNameInput, Loc->LocationName,
			LOCTEXT("LocNameHint", "Location name"),
			[this]() { return IsFieldLocked(TEXT("LocationName")); },
			[this]() { ToggleFieldLock(TEXT("LocationName")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("LocationName"), TEXT("Name")); })
	];

	LocationDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("LocRegion", "Region"), LocRegionInput, Loc->Region,
			LOCTEXT("LocRegionHint", "Region or area"),
			[this]() { return IsFieldLocked(TEXT("Region")); },
			[this]() { ToggleFieldLock(TEXT("Region")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Region"), TEXT("Region")); })
	];

	LocationDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("LocAtmosphere", "Atmosphere"), LocAtmosphereInput, Loc->Atmosphere,
			LOCTEXT("LocAtmoHint", "Mood/feel of the location"),
			[this]() { return IsFieldLocked(TEXT("Atmosphere")); },
			[this]() { ToggleFieldLock(TEXT("Atmosphere")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Atmosphere"), TEXT("Atmosphere")); })
	];

	// Description (multiline)
	LocationDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeMultilineFieldRow(
			LOCTEXT("LocDesc", "Description"), LocDescInput, Loc->Description,
			LOCTEXT("LocDescHint", "Detailed description..."),
			[this]() { return IsFieldLocked(TEXT("Description")); },
			[this]() { ToggleFieldLock(TEXT("Description")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Description"), TEXT("Description")); })
	];

	// Tags
	LocationDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeTagFieldRow(
			LOCTEXT("LocTags", "Tags"), LocTagsInput,
			Loc->Tags,
			LOCTEXT("LocTagsHint", "e.g. Dungeon, Safe Zone, Fast Travel"),
			[this]() { return IsFieldLocked(TEXT("Tags")); },
			[this]() { ToggleFieldLock(TEXT("Tags")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Tags"), TEXT("Tags")); })
	];

	// Buttons
	LocationDetailPanel->AddSlot()
	.Padding(12.0f, 12.0f, 12.0f, 8.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakePrimaryButton(
				LOCTEXT("SaveLocation", "Save"),
				FOnClicked::CreateSP(this, &SArborCodexLocationsWidget::OnSaveLocationClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("ImproveLocation", "Improve with AI"),
				FOnClicked::CreateSP(this, &SArborCodexLocationsWidget::OnImproveLocationClicked))
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
				LOCTEXT("DeleteLocation", "Delete"),
				FOnClicked::CreateSP(this, &SArborCodexLocationsWidget::OnDeleteLocationClicked))
		]
	];
}

FReply SArborCodexLocationsWidget::OnSaveLocationClicked()
{
	if (SelectedLocationIndex < 0 || SelectedLocationIndex >= LocationAssetPaths.Num())
	{
		return FReply::Handled();
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(LocationAssetPaths[SelectedLocationIndex]);
	UArborLocationAsset* Loc = Cast<UArborLocationAsset>(Obj);
	if (!Loc)
	{
		return FReply::Handled();
	}

	Loc->Modify();
	if (LocNameInput.IsValid()) Loc->LocationName = LocNameInput->GetText().ToString().TrimStartAndEnd();
	if (LocRegionInput.IsValid()) Loc->Region = LocRegionInput->GetText().ToString().TrimStartAndEnd();
	if (LocAtmosphereInput.IsValid()) Loc->Atmosphere = LocAtmosphereInput->GetText().ToString().TrimStartAndEnd();
	if (LocDescInput.IsValid()) Loc->Description = LocDescInput->GetText().ToString();
	if (LocTagsInput.IsValid()) Loc->Tags = LocTagsInput->GetTags();

	Loc->LockedFields = LockedFields;

	// Set GameContext reference
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		Loc->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(CodexContext->SelectedContextPath));
	}

	UPackage* Package = Loc->GetOutermost();
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Loc, *PackageFilename, SaveArgs);

	// Update display name in list
	LocationDisplayNames[SelectedLocationIndex] = Loc->LocationName.IsEmpty()
		? FPackageName::GetShortName(LocationAssetPaths[SelectedLocationIndex])
		: Loc->LocationName;
	RebuildLocationList();

	return FReply::Handled();
}

FReply SArborCodexLocationsWidget::OnNewLocationClicked()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetName = FString::Printf(TEXT("Loc_%s"), *Timestamp);
	FString PackagePath = FString::Printf(TEXT("/Game/GameCodex/%s"), *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	UArborLocationAsset* NewLoc = NewObject<UArborLocationAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	NewLoc->LocationName = TEXT("New Location");
	NewLoc->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(CodexContext->SelectedContextPath));

	FAssetRegistryModule::AssetCreated(NewLoc);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewLoc, *PackageFilename, SaveArgs);

	ScanLocations();

	// Select the new location
	FString NewPath = NewLoc->GetPathName();
	for (int32 i = 0; i < LocationAssetPaths.Num(); i++)
	{
		if (LocationAssetPaths[i] == NewPath)
		{
			SelectedLocationIndex = i;
			RebuildLocationList();
			ShowLocationDetail(i);
			break;
		}
	}

	return FReply::Handled();
}

FReply SArborCodexLocationsWidget::OnImproveLocationClicked()
{
	if (SelectedLocationIndex < 0 || SelectedLocationIndex >= LocationAssetPaths.Num())
	{
		return FReply::Handled();
	}

	// Check if all fields are locked
	static const TArray<FString> AllFieldKeys = {
		TEXT("LocationName"), TEXT("Region"), TEXT("Atmosphere"),
		TEXT("Description"), TEXT("Tags")
	};

	TArray<FString> LockedFieldNames;
	for (const FString& Key : AllFieldKeys)
	{
		if (IsFieldLocked(Key))
		{
			LockedFieldNames.Add(Key);
		}
	}

	if (LockedFieldNames.Num() == AllFieldKeys.Num())
	{
		return FReply::Handled();
	}

	// Build context summary
	FString ContextSummary;
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		UObject* CtxObj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(CtxObj);
		if (Ctx)
		{
			ContextSummary = FString::Printf(
				TEXT("%s — %s. %s. Tone: %s"),
				*Ctx->GameTitle, *Ctx->Genre, *Ctx->Setting, *Ctx->Tone);
		}
	}

	FString Name = LocNameInput.IsValid() ? LocNameInput->GetText().ToString() : TEXT("");
	FString Region = LocRegionInput.IsValid() ? LocRegionInput->GetText().ToString() : TEXT("");
	FString Atmosphere = LocAtmosphereInput.IsValid() ? LocAtmosphereInput->GetText().ToString() : TEXT("");
	FString Description = LocDescInput.IsValid() ? LocDescInput->GetText().ToString() : TEXT("");
	FString Tags = LocTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(LocTagsInput->GetTags()) : TEXT("");

	FString AssetPath = LocationAssetPaths[SelectedLocationIndex];

	FString LockedFieldsJSON;
	if (LockedFieldNames.Num() > 0)
	{
		TArray<FString> Quoted;
		for (const FString& F : LockedFieldNames) { Quoted.Add(FString::Printf(TEXT("\"%s\""), *F)); }
		LockedFieldsJSON = FString::Printf(TEXT("[%s]"), *FString::Join(Quoted, TEXT(", ")));
	}
	else
	{
		LockedFieldsJSON = TEXT("[]");
	}

	FString LockInstruction;
	if (LockedFieldNames.Num() > 0)
	{
		LockInstruction = FString::Printf(
			TEXT("\n\nIMPORTANT: The following fields are LOCKED and must NOT be modified. "
				 "Keep their values exactly as shown above: %s"),
			*FString::Join(LockedFieldNames, TEXT(", ")));
	}

	FString Prompt = FString::Printf(
		TEXT("I have a game location that needs fleshing out. The game context is: %s\n\n"
			 "Current location data:\n"
			 "Name: %s\n"
			 "Region: %s\n"
			 "Atmosphere: %s\n"
			 "Description: %s\n"
			 "Tags: %s\n\n"
			 "Please generate 3 DISTINCT variations of improved content:\n"
			 "- Variation A: Conservative refinement (polish what's there)\n"
			 "- Variation B: Bold reimagining (take creative risks)\n"
			 "- Variation C: Balanced middle ground\n\n"
			 "Expand the Description to 2-3 vivid paragraphs and refine the Atmosphere description.\n\n"
			 "After generating, present the 3 variations to the user by calling ue5_run_python:\n"
			 "```python\n"
			 "import arbor.variations as var\n"
			 "var.show_text_variations({\n"
			 "    \"variations\": [\n"
			 "        {\"label\": \"Variation A\", \"fields\": {\"LocationName\": \"...\", \"Region\": \"...\", \"Atmosphere\": \"...\", \"Description\": \"...\", \"Tags\": \"...\"}},\n"
			 "        {\"label\": \"Variation B\", \"fields\": {...}},\n"
			 "        {\"label\": \"Variation C\", \"fields\": {...}}\n"
			 "    ],\n"
			 "    \"category\": \"location\",\n"
			 "    \"asset_path\": \"%s\",\n"
			 "    \"prompt\": \"improve location\",\n"
			 "    \"locked_fields\": %s,\n"
			 "    \"field_order\": [\"LocationName\", \"Region\", \"Atmosphere\", \"Description\", \"Tags\"]\n"
			 "})\n"
			 "```\n\n"
			 "Then poll for the user's choice by calling ue5_run_python:\n"
			 "```python\n"
			 "import arbor.variations as var\n"
			 "var.get_text_variation_result()\n"
			 "```\n\n"
			 "When status is \"select\", apply the selected variation's fields to the asset using the codex update MCP tool.\n"
			 "When status is \"regenerate\", generate 3 new variations incorporating the user's comments.\n"
			 "When status is \"cancel\", stop.%s"),
		*ContextSummary, *Name, *Region, *Atmosphere, *Description, *Tags, *AssetPath, *LockedFieldsJSON, *LockInstruction);

	ArborAIPromptDialog::Show(Prompt, TEXT("Improve Location with AI"));

	return FReply::Handled();
}

FString SArborCodexLocationsWidget::BuildContextSummary() const
{
	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		UObject* CtxObj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(CtxObj);
		if (Ctx)
		{
			return FString::Printf(
				TEXT("%s — %s. %s. Tone: %s"),
				*Ctx->GameTitle, *Ctx->Genre, *Ctx->Setting, *Ctx->Tone);
		}
	}
	return TEXT("");
}

FReply SArborCodexLocationsWidget::OnAIIterateField(const FString& FieldKey, const FString& DisplayName)
{
	if (SelectedLocationIndex < 0 || SelectedLocationIndex >= LocationAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString CurrentValue;
	if (FieldKey == TEXT("LocationName") && LocNameInput.IsValid())
		CurrentValue = LocNameInput->GetText().ToString();
	else if (FieldKey == TEXT("Region") && LocRegionInput.IsValid())
		CurrentValue = LocRegionInput->GetText().ToString();
	else if (FieldKey == TEXT("Atmosphere") && LocAtmosphereInput.IsValid())
		CurrentValue = LocAtmosphereInput->GetText().ToString();
	else if (FieldKey == TEXT("Description") && LocDescInput.IsValid())
		CurrentValue = LocDescInput->GetText().ToString();
	else if (FieldKey == TEXT("Tags") && LocTagsInput.IsValid())
		CurrentValue = ArborCodexHelpers::JoinCSV(LocTagsInput->GetTags());

	ArborAIFieldIterateDialog::Show(
		DisplayName,
		CurrentValue,
		BuildContextSummary(),
		LocationAssetPaths[SelectedLocationIndex],
		FieldKey);

	return FReply::Handled();
}

FReply SArborCodexLocationsWidget::OnDeleteLocationClicked()
{
	if (SelectedLocationIndex < 0 || SelectedLocationIndex >= LocationAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString AssetPath = LocationAssetPaths[SelectedLocationIndex];
	FString Name = SelectedLocationIndex < LocationDisplayNames.Num()
		? LocationDisplayNames[SelectedLocationIndex] : FPaths::GetBaseFilename(AssetPath);

	TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
		.Title(LOCTEXT("DeleteLocationTitle", "Delete Location"))
		.ClientSize(FVector2D(420, 150))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize);

	TWeakPtr<SWindow> WeakWindow = ConfirmWindow;
	TWeakPtr<SArborCodexLocationsWidget> WeakSelf = SharedThis(this);

	ConfirmWindow->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(16.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("DeleteLocationConfirm", "Are you sure you want to delete \"{0}\"?\nThis action cannot be undone."),
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

						if (TSharedPtr<SArborCodexLocationsWidget> Self = WeakSelf.Pin())
						{
							Self->SelectedLocationIndex = -1;
							Self->ScanLocations();
							Self->RebuildLocationList();
							if (Self->LocationDetailPanel.IsValid())
							{
								Self->LocationDetailPanel->ClearChildren();
							}
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

#undef LOCTEXT_NAMESPACE
