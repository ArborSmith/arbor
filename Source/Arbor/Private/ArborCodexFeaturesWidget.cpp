#include "ArborCodexFeaturesWidget.h"
#include "ArborCodexContext.h"
#include "ArborGameContextTypes.h"
#include "ArborClaude.h"
#include "ArborAIPromptDialog.h"
#include "ArborAIFieldIterateDialog.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "ArborCodexSearch.h"
#include "UObject/SavePackage.h"
#include "ArborCodexImagePanel.h"
#include "Framework/Application/SlateApplication.h"
#include "ArborTagInput.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "ArborCodexFeaturesWidget"

bool SArborCodexFeaturesWidget::IsFieldLocked(const FString& FieldKey) const
{
	return LockedFields.Contains(FieldKey);
}

void SArborCodexFeaturesWidget::ToggleFieldLock(const FString& FieldKey)
{
	if (LockedFields.Contains(FieldKey))
		LockedFields.Remove(FieldKey);
	else
		LockedFields.Add(FieldKey);
	SaveLockedFieldsToAsset();
}

void SArborCodexFeaturesWidget::SaveLockedFieldsToAsset()
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num()) return;
	UObject* Obj = UEditorAssetLibrary::LoadAsset(FeatureAssetPaths[SelectedFeatureIndex]);
	UArborFeatureAsset* Asset = Cast<UArborFeatureAsset>(Obj);
	if (!Asset) return;
	Asset->Modify();
	Asset->LockedFields = LockedFields;
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}

void SArborCodexFeaturesWidget::Construct(const FArguments& InArgs)
{
	CodexContext = InArgs._CodexContext;

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

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
						.Text(LOCTEXT("FeaturesLabel", "Features"))
						.Font(ArborCodexStyle::Font::SectionHeader())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("NewFeature", "+ New"),
							FOnClicked::CreateSP(this, &SArborCodexFeaturesWidget::OnNewFeatureClicked))
					]
				]

				// Category filter
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10.0f, 0.0f, 10.0f, 6.0f)
				[
					SAssignNew(CategoryComboBox, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&CategoryOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Selected, ESelectInfo::Type)
					{
						FilterCategory = (Selected.IsValid() && *Selected != TEXT("All")) ? *Selected : TEXT("");
						RebuildFeatureList();
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock)
							.Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")))
							.Font(ArborCodexStyle::Font::Body());
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]() -> FText
						{
							return FText::FromString(FilterCategory.IsEmpty() ? TEXT("All") : FilterCategory);
						})
						.Font(ArborCodexStyle::Font::Body())
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
						SAssignNew(FeatureListBox, SVerticalBox)
					]
				]
			]
		]

		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SAssignNew(FeatureDetailPanel, SScrollBox)
		]
	];

	if (CodexContext.IsValid())
	{
		ContextChangedHandle = CodexContext->OnContextChanged.AddSP(
			this, &SArborCodexFeaturesWidget::OnContextChanged);
		OnContextChanged();
	}
}

void SArborCodexFeaturesWidget::OnContextChanged()
{
	ScanFeatures();
}

void SArborCodexFeaturesWidget::ScanFeatures()
{
	FeatureAssetPaths.Empty();
	FeatureDisplayNames.Empty();
	FeatureCategories.Empty();
	SelectedFeatureIndex = -1;

	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		RebuildFeatureList();
		FeatureDetailPanel->ClearChildren();
		return;
	}

	FString SelectedContextPath = CodexContext->SelectedContextPath;

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UArborFeatureAsset::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Obj = AssetData.GetAsset();
		UArborFeatureAsset* Asset = Cast<UArborFeatureAsset>(Obj);
		if (!Asset)
		{
			continue;
		}

		FString AssetContextPath = Asset->GameContext.ToSoftObjectPath().ToString();
		if (AssetContextPath != SelectedContextPath)
		{
			continue;
		}

		FeatureAssetPaths.Add(AssetData.GetObjectPathString());
		FString DisplayName = Asset->FeatureName.IsEmpty()
			? AssetData.AssetName.ToString()
			: Asset->FeatureName;
		FeatureDisplayNames.Add(DisplayName);
		FeatureCategories.Add(Asset->Category);
	}

	RebuildCategoryOptions();
	RebuildFeatureList();
	FeatureDetailPanel->ClearChildren();
}

void SArborCodexFeaturesWidget::RebuildCategoryOptions()
{
	TSet<FString> Seen;
	CategoryOptions.Empty();
	CategoryOptions.Add(MakeShared<FString>(TEXT("All")));
	for (const FString& Cat : FeatureCategories)
	{
		if (!Cat.IsEmpty() && !Seen.Contains(Cat))
		{
			Seen.Add(Cat);
			CategoryOptions.Add(MakeShared<FString>(Cat));
		}
	}

	// If current filter no longer exists in options, reset to All
	if (!FilterCategory.IsEmpty())
	{
		bool bFound = false;
		for (const TSharedPtr<FString>& Opt : CategoryOptions)
		{
			if (Opt.IsValid() && *Opt == FilterCategory) { bFound = true; break; }
		}
		if (!bFound) FilterCategory = TEXT("");
	}

	if (CategoryComboBox.IsValid())
	{
		CategoryComboBox->RefreshOptions();
	}
}

void SArborCodexFeaturesWidget::RebuildFeatureList()
{
	FeatureListBox->ClearChildren();

	int32 VisualRow = 0;
	for (int32 i = 0; i < FeatureDisplayNames.Num(); i++)
	{
		if (!FilterCategory.IsEmpty() && i < FeatureCategories.Num() && FeatureCategories[i] != FilterCategory)
		{
			continue;
		}

		bool bSelected = (i == SelectedFeatureIndex);
		bool bEvenRow = (VisualRow % 2 == 0);
		VisualRow++;

		FeatureListBox->AddSlot()
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
						SelectedFeatureIndex = i;
						RebuildFeatureList();
						ShowFeatureDetail(i);
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
											if (i >= FeatureAssetPaths.Num()) return nullptr;
											UObject* Obj = UEditorAssetLibrary::LoadAsset(FeatureAssetPaths[i]);
											UArborFeatureAsset* Asset = Cast<UArborFeatureAsset>(Obj);
											if (Asset)
											{
												UTexture2D* Tex = Asset->ConceptArt.Get();
												if (Tex)
												{
													ThumbnailBrushes.FindOrAdd(i).SetResourceObject(Tex);
													ThumbnailBrushes[i].ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
													return &ThumbnailBrushes[i];
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
							.Text(FText::FromString(FeatureDisplayNames[i]))
							.Font(ArborCodexStyle::Font::FieldLabel())
							.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
						]
					]
				]
			]
		];
	}
}

void SArborCodexFeaturesWidget::ShowFeatureDetail(int32 Index)
{
	FeatureDetailPanel->ClearChildren();

	if (Index < 0 || Index >= FeatureAssetPaths.Num())
	{
		return;
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(FeatureAssetPaths[Index]);
	UArborFeatureAsset* Asset = Cast<UArborFeatureAsset>(Obj);
	if (!Asset)
	{
		return;
	}

	LockedFields = Asset->LockedFields;

	// Concept art image panel
	{
		UTexture2D* ConceptArtTex = Asset->ConceptArt.Get();
		TArray<UTexture2D*> GalleryTextures;
		for (const TSoftObjectPtr<UTexture2D>& SoftTex : Asset->ConceptArtGallery)
		{
			UTexture2D* Tex = SoftTex.Get();
			if (Tex)
			{
				GalleryTextures.Add(Tex);
			}
		}

		FeatureDetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SNew(SArborCodexImagePanel)
			.ConceptArt(ConceptArtTex)
			.Gallery(GalleryTextures)
			.Prompt(Asset->ConceptArtPrompt)
			.AssetPath(FeatureAssetPaths[Index])
		];
	}

	// Fields
	FeatureDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("FeatureName", "Name"), FeatureNameInput, Asset->FeatureName,
			LOCTEXT("FeatureNameHint", "Feature name"),
			[this]() { return IsFieldLocked(TEXT("FeatureName")); },
			[this]() { ToggleFieldLock(TEXT("FeatureName")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("FeatureName"), TEXT("Name")); })
	];

	FeatureDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("FeatureCategory", "Category"), FeatureCategoryInput, Asset->Category,
			LOCTEXT("FeatureCategoryHint", "Core Loop/Camera/Controls/Physics/Economy/AI/Combat/Crafting/Progression/Social/Exploration/UI"),
			[this]() { return IsFieldLocked(TEXT("Category")); },
			[this]() { ToggleFieldLock(TEXT("Category")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Category"), TEXT("Category")); })
	];

	FeatureDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeMultilineFieldRow(
			LOCTEXT("FeatureDesc", "Description"), FeatureDescInput, Asset->Description,
			LOCTEXT("FeatureDescHint", "What this feature is and why it exists..."),
			[this]() { return IsFieldLocked(TEXT("Description")); },
			[this]() { ToggleFieldLock(TEXT("Description")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Description"), TEXT("Description")); })
	];

	FeatureDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeTagFieldRow(
			LOCTEXT("FeatureTags", "Tags"), FeatureTagsInput,
			Asset->Tags,
			LOCTEXT("FeatureTagsHint", "e.g. Core Loop, PvP, Economy"),
			[this]() { return IsFieldLocked(TEXT("Tags")); },
			[this]() { ToggleFieldLock(TEXT("Tags")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Tags"), TEXT("Tags")); })
	];

	// Buttons
	FeatureDetailPanel->AddSlot()
	.Padding(12.0f, 12.0f, 12.0f, 8.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakePrimaryButton(
				LOCTEXT("SaveFeature", "Save"),
				FOnClicked::CreateSP(this, &SArborCodexFeaturesWidget::OnSaveFeatureClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("ImproveFeature", "Improve with AI"),
				FOnClicked::CreateSP(this, &SArborCodexFeaturesWidget::OnImproveFeatureClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("HelpImplement", "Help Implement"),
				FOnClicked::CreateSP(this, &SArborCodexFeaturesWidget::OnHelpImplementClicked))
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
				LOCTEXT("DeleteFeature", "Delete"),
				FOnClicked::CreateSP(this, &SArborCodexFeaturesWidget::OnDeleteFeatureClicked))
		]
	];
}

FReply SArborCodexFeaturesWidget::OnSaveFeatureClicked()
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num())
	{
		return FReply::Handled();
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(FeatureAssetPaths[SelectedFeatureIndex]);
	UArborFeatureAsset* Asset = Cast<UArborFeatureAsset>(Obj);
	if (!Asset)
	{
		return FReply::Handled();
	}

	Asset->Modify();
	if (FeatureNameInput.IsValid()) Asset->FeatureName = FeatureNameInput->GetText().ToString().TrimStartAndEnd();
	if (FeatureCategoryInput.IsValid()) Asset->Category = FeatureCategoryInput->GetText().ToString().TrimStartAndEnd();
	if (FeatureDescInput.IsValid()) Asset->Description = FeatureDescInput->GetText().ToString();
	if (FeatureTagsInput.IsValid()) Asset->Tags = FeatureTagsInput->GetTags();

	Asset->LockedFields = LockedFields;

	if (CodexContext.IsValid() && CodexContext->HasContext())
	{
		Asset->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(CodexContext->SelectedContextPath));
	}

	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);

	FeatureDisplayNames[SelectedFeatureIndex] = Asset->FeatureName.IsEmpty()
		? FPackageName::GetShortName(FeatureAssetPaths[SelectedFeatureIndex])
		: Asset->FeatureName;
	if (SelectedFeatureIndex < FeatureCategories.Num())
	{
		FeatureCategories[SelectedFeatureIndex] = Asset->Category;
	}
	RebuildCategoryOptions();
	RebuildFeatureList();

	return FReply::Handled();
}

FReply SArborCodexFeaturesWidget::OnNewFeatureClicked()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetName = FString::Printf(TEXT("Feature_%s"), *Timestamp);
	FString PackagePath = FString::Printf(TEXT("/Game/GameCodex/%s"), *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	UArborFeatureAsset* NewAsset = NewObject<UArborFeatureAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	NewAsset->FeatureName = TEXT("New Feature");
	NewAsset->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(CodexContext->SelectedContextPath));

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);

	ScanFeatures();

	FString NewPath = NewAsset->GetPathName();
	for (int32 i = 0; i < FeatureAssetPaths.Num(); i++)
	{
		if (FeatureAssetPaths[i] == NewPath)
		{
			SelectedFeatureIndex = i;
			RebuildFeatureList();
			ShowFeatureDetail(i);
			break;
		}
	}

	return FReply::Handled();
}

FReply SArborCodexFeaturesWidget::OnImproveFeatureClicked()
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num())
	{
		return FReply::Handled();
	}

	TArray<FString> AllFieldKeys = { TEXT("FeatureName"), TEXT("Category"), TEXT("Description"), TEXT("Tags") };

	TArray<FString> LockedFieldNames;
	for (const FString& Key : AllFieldKeys)
	{
		if (LockedFields.Contains(Key))
		{
			LockedFieldNames.Add(Key);
		}
	}

	if (LockedFieldNames.Num() == AllFieldKeys.Num())
	{
		return FReply::Handled();
	}

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

	FString Name = FeatureNameInput.IsValid() ? FeatureNameInput->GetText().ToString() : TEXT("");
	FString Cat = FeatureCategoryInput.IsValid() ? FeatureCategoryInput->GetText().ToString() : TEXT("");
	FString Desc = FeatureDescInput.IsValid() ? FeatureDescInput->GetText().ToString() : TEXT("");
	FString Tags = FeatureTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(FeatureTagsInput->GetTags()) : TEXT("");

	FString AssetPath = FeatureAssetPaths[SelectedFeatureIndex];

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
			TEXT("\n\nIMPORTANT: The following fields are LOCKED and must NOT be changed: %s. "
				 "Only improve the unlocked fields."),
			*FString::Join(LockedFieldNames, TEXT(", ")));
	}

	FString Prompt = FString::Printf(
		TEXT("I have a game feature that needs fleshing out. The game context is: %s\n\n"
			 "Current feature data:\n"
			 "Name: %s\n"
			 "Category: %s\n"
			 "Description: %s\n"
			 "Tags: %s\n\n"
			 "Please generate 3 DISTINCT variations of improved content:\n"
			 "- Variation A: Conservative refinement (polish what's there)\n"
			 "- Variation B: Bold reimagining (take creative risks)\n"
			 "- Variation C: Balanced middle ground\n\n"
			 "Expand the Description to a detailed paragraph covering what the feature is, why it exists, and how it works. Fill in any empty fields.\n\n"
			 "After generating, present the 3 variations to the user by calling ue5_run_python:\n"
			 "```python\n"
			 "import arbor.variations as var\n"
			 "var.show_text_variations({\n"
			 "    \"variations\": [\n"
			 "        {\"label\": \"Variation A\", \"fields\": {\"FeatureName\": \"...\", \"Category\": \"...\", \"Description\": \"...\", \"Tags\": \"...\"}},\n"
			 "        {\"label\": \"Variation B\", \"fields\": {...}},\n"
			 "        {\"label\": \"Variation C\", \"fields\": {...}}\n"
			 "    ],\n"
			 "    \"category\": \"feature\",\n"
			 "    \"asset_path\": \"%s\",\n"
			 "    \"prompt\": \"improve feature\",\n"
			 "    \"locked_fields\": %s,\n"
			 "    \"field_order\": [\"FeatureName\", \"Category\", \"Description\", \"Tags\"]\n"
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
		*ContextSummary, *Name, *Cat, *Desc, *Tags, *AssetPath, *LockedFieldsJSON, *LockInstruction);

	ArborAIPromptDialog::Show(Prompt, TEXT("Improve Feature with AI"));

	return FReply::Handled();
}

FReply SArborCodexFeaturesWidget::OnHelpImplementClicked()
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num())
	{
		return FReply::Handled();
	}

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

	FString Name = FeatureNameInput.IsValid() ? FeatureNameInput->GetText().ToString() : TEXT("");
	FString Cat = FeatureCategoryInput.IsValid() ? FeatureCategoryInput->GetText().ToString() : TEXT("");
	FString Desc = FeatureDescInput.IsValid() ? FeatureDescInput->GetText().ToString() : TEXT("");
	FString Tags = FeatureTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(FeatureTagsInput->GetTags()) : TEXT("");

	FString Prompt = FString::Printf(
		TEXT("Plan the implementation of this game feature as a C++ system in UE5.\n\n"
			 "## Game Context\n%s\n\n"
			 "## Feature\n"
			 "- **Name:** %s\n"
			 "- **Category:** %s\n"
			 "- **Description:** %s\n"
			 "- **Tags:** %s\n\n"
			 "## Planning Instructions\n\n"
			 "Create a detailed implementation plan following these steps:\n\n"
			 "### 1. Research Phase\n"
			 "- Search Fab (fab_search) for existing plugins, assets, or code packs that could accelerate implementation\n"
			 "- Check if any existing project assets or plugins already provide related functionality (arbor.registry)\n"
			 "- Evaluate what can be reused vs what needs to be built from scratch\n\n"
			 "### 2. Architecture Design\n"
			 "- Design the system in C++ (preferred over Blueprints for core logic)\n"
			 "- Define the classes needed: UObjects, UActorComponents, AActors, USubsystems, etc.\n"
			 "- Identify which UE5 modules and APIs the implementation will depend on\n"
			 "- Specify the header/source file structure\n\n"
			 "### 3. Implementation Plan\n"
			 "- Break down into ordered implementation steps\n"
			 "- For each step: what class/file to create or modify, what it does, and dependencies\n"
			 "- Include any Blueprint exposure (UFUNCTION/UPROPERTY macros) needed for designer-facing tuning\n"
			 "- Note any data assets or config needed\n\n"
			 "### 4. Integration\n"
			 "- How the system hooks into the existing game framework\n"
			 "- Required component setup on actors\n"
			 "- Any editor tooling or debug visualization needed\n\n"
			 "Output a clear, actionable plan — do NOT start implementing yet. Wait for approval."),
		*ContextSummary, *Name, *Cat, *Desc, *Tags);

	ArborAIPromptDialog::Show(Prompt, TEXT("Plan Implementation"), true);

	return FReply::Handled();
}

FString SArborCodexFeaturesWidget::BuildContextSummary() const
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

FReply SArborCodexFeaturesWidget::OnAIIterateField(const FString& FieldKey, const FString& DisplayName)
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString CurrentValue;
	if (FieldKey == TEXT("FeatureName") && FeatureNameInput.IsValid())
		CurrentValue = FeatureNameInput->GetText().ToString();
	else if (FieldKey == TEXT("Category") && FeatureCategoryInput.IsValid())
		CurrentValue = FeatureCategoryInput->GetText().ToString();
	else if (FieldKey == TEXT("Description") && FeatureDescInput.IsValid())
		CurrentValue = FeatureDescInput->GetText().ToString();
	else if (FieldKey == TEXT("Tags") && FeatureTagsInput.IsValid())
		CurrentValue = ArborCodexHelpers::JoinCSV(FeatureTagsInput->GetTags());

	ArborAIFieldIterateDialog::Show(
		DisplayName,
		CurrentValue,
		BuildContextSummary(),
		FeatureAssetPaths[SelectedFeatureIndex],
		FieldKey);

	return FReply::Handled();
}

FReply SArborCodexFeaturesWidget::OnDeleteFeatureClicked()
{
	if (SelectedFeatureIndex < 0 || SelectedFeatureIndex >= FeatureAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString AssetPath = FeatureAssetPaths[SelectedFeatureIndex];
	FString Name = SelectedFeatureIndex < FeatureDisplayNames.Num()
		? FeatureDisplayNames[SelectedFeatureIndex] : FPaths::GetBaseFilename(AssetPath);

	TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
		.Title(LOCTEXT("DeleteFeatureTitle", "Delete Feature"))
		.ClientSize(FVector2D(420, 150))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize);

	TWeakPtr<SWindow> WeakWindow = ConfirmWindow;
	TWeakPtr<SArborCodexFeaturesWidget> WeakSelf = SharedThis(this);

	ConfirmWindow->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(16.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("DeleteFeatureConfirm", "Are you sure you want to delete \"{0}\"?\nThis action cannot be undone."),
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

						if (TSharedPtr<SArborCodexFeaturesWidget> Self = WeakSelf.Pin())
						{
							Self->SelectedFeatureIndex = -1;
							Self->ScanFeatures();
							Self->RebuildFeatureList();
							if (Self->FeatureDetailPanel.IsValid())
							{
								Self->FeatureDetailPanel->ClearChildren();
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
