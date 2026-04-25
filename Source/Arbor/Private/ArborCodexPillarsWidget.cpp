#include "ArborCodexPillarsWidget.h"
#include "ArborCodexContext.h"
#include "ArborGameContextTypes.h"
#include "ArborClaude.h"
#include "ArborAIPromptDialog.h"
#include "ArborAIFieldIterateDialog.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "ArborCodexSearch.h"
#include "UObject/SavePackage.h"
#include "ArborCodexImagePanel.h"
#include "ArborTagInput.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "ArborCodexPillarsWidget"

bool SArborCodexPillarsWidget::IsFieldLocked(const FString& FieldKey) const
{
	return LockedFields.Contains(FieldKey);
}

void SArborCodexPillarsWidget::ToggleFieldLock(const FString& FieldKey)
{
	if (LockedFields.Contains(FieldKey))
		LockedFields.Remove(FieldKey);
	else
		LockedFields.Add(FieldKey);
	SaveLockedFieldsToAsset();
}

void SArborCodexPillarsWidget::SaveLockedFieldsToAsset()
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num()) return;
	UObject* Obj = UEditorAssetLibrary::LoadAsset(PillarAssetPaths[SelectedPillarIndex]);
	UArborPillarAsset* Asset = Cast<UArborPillarAsset>(Obj);
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

void SArborCodexPillarsWidget::Construct(const FArguments& InArgs)
{
	CodexContext = InArgs._CodexContext;

	// Initialize PillarType dropdown options
	PillarTypeOptions.Add(MakeShareable(new FString(TEXT("Theme"))));
	PillarTypeOptions.Add(MakeShareable(new FString(TEXT("Pillar"))));

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
						.Text(LOCTEXT("PillarsLabel", "Pillars"))
						.Font(ArborCodexStyle::Font::SectionHeader())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("NewPillar", "+ New"),
							FOnClicked::CreateSP(this, &SArborCodexPillarsWidget::OnNewPillarClicked))
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
						SAssignNew(PillarListBox, SVerticalBox)
					]
				]
			]
		]

		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SAssignNew(PillarDetailPanel, SScrollBox)
		]
	];

	if (CodexContext.IsValid())
	{
		ContextChangedHandle = CodexContext->OnContextChanged.AddSP(
			this, &SArborCodexPillarsWidget::OnContextChanged);
		OnContextChanged();
	}
}

void SArborCodexPillarsWidget::OnContextChanged()
{
	ScanPillars();
}

void SArborCodexPillarsWidget::ScanPillars()
{
	PillarAssetPaths.Empty();
	PillarDisplayNames.Empty();
	SelectedPillarIndex = -1;

	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		RebuildPillarList();
		PillarDetailPanel->ClearChildren();
		return;
	}

	FString SelectedContextPath = CodexContext->SelectedContextPath;

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UArborPillarAsset::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Obj = AssetData.GetAsset();
		UArborPillarAsset* Asset = Cast<UArborPillarAsset>(Obj);
		if (!Asset)
		{
			continue;
		}

		FString AssetContextPath = Asset->GameContext.ToSoftObjectPath().ToString();
		if (AssetContextPath != SelectedContextPath)
		{
			continue;
		}

		PillarAssetPaths.Add(AssetData.GetObjectPathString());

		// Display name format: "[Theme] Name" or "[Pillar] Name"
		FString TypeLabel = (Asset->PillarType == EArborPillarType::Theme) ? TEXT("[Theme]") : TEXT("[Pillar]");
		FString Name = Asset->PillarName.IsEmpty()
			? AssetData.AssetName.ToString()
			: Asset->PillarName;
		FString DisplayName = FString::Printf(TEXT("%s %s"), *TypeLabel, *Name);
		PillarDisplayNames.Add(DisplayName);
	}

	RebuildPillarList();
	PillarDetailPanel->ClearChildren();
}

void SArborCodexPillarsWidget::RebuildPillarList()
{
	PillarListBox->ClearChildren();

	for (int32 i = 0; i < PillarDisplayNames.Num(); i++)
	{
		bool bSelected = (i == SelectedPillarIndex);
		bool bEvenRow = (i % 2 == 0);

		PillarListBox->AddSlot()
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
						SelectedPillarIndex = i;
						RebuildPillarList();
						ShowPillarDetail(i);
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
											if (i >= PillarAssetPaths.Num()) return nullptr;
											UObject* Obj = UEditorAssetLibrary::LoadAsset(PillarAssetPaths[i]);
											UArborPillarAsset* Asset = Cast<UArborPillarAsset>(Obj);
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
							.Text(FText::FromString(PillarDisplayNames[i]))
							.Font(ArborCodexStyle::Font::FieldLabel())
							.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
						]
					]
				]
			]
		];
	}
}

void SArborCodexPillarsWidget::ShowPillarDetail(int32 Index)
{
	PillarDetailPanel->ClearChildren();

	if (Index < 0 || Index >= PillarAssetPaths.Num())
	{
		return;
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(PillarAssetPaths[Index]);
	UArborPillarAsset* Asset = Cast<UArborPillarAsset>(Obj);
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

		PillarDetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SNew(SArborCodexImagePanel)
			.ConceptArt(ConceptArtTex)
			.Gallery(GalleryTextures)
			.Prompt(Asset->ConceptArtPrompt)
			.AssetPath(PillarAssetPaths[Index])
		];
	}

	// PillarName field
	PillarDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("PillarName", "Name"), PillarNameInput, Asset->PillarName,
			LOCTEXT("PillarNameHint", "Pillar or theme name"),
			[this]() { return IsFieldLocked(TEXT("PillarName")); },
			[this]() { ToggleFieldLock(TEXT("PillarName")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("PillarName"), TEXT("Name")); })
	];

	// PillarType dropdown
	{
		int32 CurrentTypeIndex = (Asset->PillarType == EArborPillarType::Theme) ? 0 : 1;

		PillarDetailPanel->AddSlot()
		.Padding(ArborCodexStyle::Spacing::FieldOuter)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(120.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PillarType", "Type"))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(PillarTypeCombo, STextComboBox)
				.OptionsSource(&PillarTypeOptions)
				.InitiallySelectedItem(PillarTypeOptions[CurrentTypeIndex])
				.Font(ArborCodexStyle::Font::Input())
			]
		];
	}

	// Description field (multiline)
	PillarDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeMultilineFieldRow(
			LOCTEXT("PillarDesc", "Description"), PillarDescInput, Asset->Description,
			LOCTEXT("PillarDescHint", "Describe this pillar or theme in detail..."),
			[this]() { return IsFieldLocked(TEXT("Description")); },
			[this]() { ToggleFieldLock(TEXT("Description")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Description"), TEXT("Description")); })
	];

	// Tags field
	PillarDetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeTagFieldRow(
			LOCTEXT("PillarTags", "Tags"), PillarTagsInput,
			Asset->Tags,
			LOCTEXT("PillarTagsHint", "e.g. Exploration, Survival, Narrative"),
			[this]() { return IsFieldLocked(TEXT("Tags")); },
			[this]() { ToggleFieldLock(TEXT("Tags")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Tags"), TEXT("Tags")); })
	];

	// Buttons
	PillarDetailPanel->AddSlot()
	.Padding(12.0f, 12.0f, 12.0f, 8.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakePrimaryButton(
				LOCTEXT("SavePillar", "Save"),
				FOnClicked::CreateSP(this, &SArborCodexPillarsWidget::OnSavePillarClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("ImprovePillar", "Improve with AI"),
				FOnClicked::CreateSP(this, &SArborCodexPillarsWidget::OnImprovePillarClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("HelpImplement", "Help Implement"),
				FOnClicked::CreateSP(this, &SArborCodexPillarsWidget::OnHelpImplementClicked))
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
				LOCTEXT("DeletePillar", "Delete"),
				FOnClicked::CreateSP(this, &SArborCodexPillarsWidget::OnDeletePillarClicked))
		]
	];
}

FReply SArborCodexPillarsWidget::OnSavePillarClicked()
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num())
	{
		return FReply::Handled();
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(PillarAssetPaths[SelectedPillarIndex]);
	UArborPillarAsset* Asset = Cast<UArborPillarAsset>(Obj);
	if (!Asset)
	{
		return FReply::Handled();
	}

	Asset->Modify();
	if (PillarNameInput.IsValid()) Asset->PillarName = PillarNameInput->GetText().ToString().TrimStartAndEnd();
	if (PillarDescInput.IsValid()) Asset->Description = PillarDescInput->GetText().ToString();
	if (PillarTagsInput.IsValid()) Asset->Tags = PillarTagsInput->GetTags();

	// Save PillarType from combo
	if (PillarTypeCombo.IsValid())
	{
		TSharedPtr<FString> Selected = PillarTypeCombo->GetSelectedItem();
		if (Selected.IsValid())
		{
			if (*Selected == TEXT("Theme"))
				Asset->PillarType = EArborPillarType::Theme;
			else
				Asset->PillarType = EArborPillarType::Pillar;
		}
	}

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

	// Update display name with type prefix
	FString TypeLabel = (Asset->PillarType == EArborPillarType::Theme) ? TEXT("[Theme]") : TEXT("[Pillar]");
	FString Name = Asset->PillarName.IsEmpty()
		? FPackageName::GetShortName(PillarAssetPaths[SelectedPillarIndex])
		: Asset->PillarName;
	PillarDisplayNames[SelectedPillarIndex] = FString::Printf(TEXT("%s %s"), *TypeLabel, *Name);
	RebuildPillarList();

	return FReply::Handled();
}

FReply SArborCodexPillarsWidget::OnNewPillarClicked()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetName = FString::Printf(TEXT("Pillar_%s"), *Timestamp);
	FString PackagePath = FString::Printf(TEXT("/Game/GameCodex/%s"), *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	UArborPillarAsset* NewAsset = NewObject<UArborPillarAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	NewAsset->PillarName = TEXT("New Pillar");
	NewAsset->PillarType = EArborPillarType::Pillar;
	NewAsset->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(CodexContext->SelectedContextPath));

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);

	ScanPillars();

	FString NewPath = NewAsset->GetPathName();
	for (int32 i = 0; i < PillarAssetPaths.Num(); i++)
	{
		if (PillarAssetPaths[i] == NewPath)
		{
			SelectedPillarIndex = i;
			RebuildPillarList();
			ShowPillarDetail(i);
			break;
		}
	}

	return FReply::Handled();
}

FReply SArborCodexPillarsWidget::OnImprovePillarClicked()
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num())
	{
		return FReply::Handled();
	}

	TArray<FString> AllFieldKeys = { TEXT("PillarName"), TEXT("PillarType"), TEXT("Description"), TEXT("Tags") };

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

	FString Name = PillarNameInput.IsValid() ? PillarNameInput->GetText().ToString() : TEXT("");
	FString Type = TEXT("Pillar");
	if (PillarTypeCombo.IsValid())
	{
		TSharedPtr<FString> Selected = PillarTypeCombo->GetSelectedItem();
		if (Selected.IsValid()) Type = *Selected;
	}
	FString Desc = PillarDescInput.IsValid() ? PillarDescInput->GetText().ToString() : TEXT("");
	FString Tags = PillarTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(PillarTagsInput->GetTags()) : TEXT("");

	FString AssetPath = PillarAssetPaths[SelectedPillarIndex];

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
		TEXT("I have a game design pillar/theme that needs fleshing out. The game context is: %s\n\n"
			 "Current pillar data:\n"
			 "Name: %s\n"
			 "Type: %s\n"
			 "Description: %s\n"
			 "Tags: %s\n\n"
			 "Please generate 3 DISTINCT variations of improved content:\n"
			 "- Variation A: Conservative refinement (polish what's there)\n"
			 "- Variation B: Bold reimagining (take creative risks)\n"
			 "- Variation C: Balanced middle ground\n\n"
			 "Expand the Description to a detailed paragraph explaining how this %s "
			 "influences design decisions, fill in any empty fields, "
			 "and suggest Tags that connect to other game elements.\n\n"
			 "After generating, present the 3 variations to the user by calling ue5_run_python:\n"
			 "```python\n"
			 "import arbor.variations as var\n"
			 "var.show_text_variations({\n"
			 "    \"variations\": [\n"
			 "        {\"label\": \"Variation A\", \"fields\": {\"PillarName\": \"...\", \"PillarType\": \"...\", \"Description\": \"...\", \"Tags\": \"...\"}},\n"
			 "        {\"label\": \"Variation B\", \"fields\": {...}},\n"
			 "        {\"label\": \"Variation C\", \"fields\": {...}}\n"
			 "    ],\n"
			 "    \"category\": \"pillar\",\n"
			 "    \"asset_path\": \"%s\",\n"
			 "    \"prompt\": \"improve pillar\",\n"
			 "    \"locked_fields\": %s,\n"
			 "    \"field_order\": [\"PillarName\", \"PillarType\", \"Description\", \"Tags\"]\n"
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
		*ContextSummary, *Name, *Type, *Desc, *Tags, *Type.ToLower(), *AssetPath, *LockedFieldsJSON, *LockInstruction);

	ArborAIPromptDialog::Show(Prompt, TEXT("Improve Pillar with AI"));

	return FReply::Handled();
}

FReply SArborCodexPillarsWidget::OnHelpImplementClicked()
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num())
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

	FString Name = PillarNameInput.IsValid() ? PillarNameInput->GetText().ToString() : TEXT("");
	FString Type = TEXT("Pillar");
	if (PillarTypeCombo.IsValid())
	{
		TSharedPtr<FString> Selected = PillarTypeCombo->GetSelectedItem();
		if (Selected.IsValid()) Type = *Selected;
	}
	FString Desc = PillarDescInput.IsValid() ? PillarDescInput->GetText().ToString() : TEXT("");
	FString Tags = PillarTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(PillarTagsInput->GetTags()) : TEXT("");

	FString Prompt = FString::Printf(
		TEXT("I have a game design %s that needs implementing in UE5. The game context is: %s\n\n"
			 "%s: %s\n"
			 "Type: %s\n"
			 "Description: %s\n"
			 "Tags: %s\n\n"
			 "Please help implement this %s by:\n"
			 "1. Creating features and systems that embody this %s\n"
			 "2. Setting up required Blueprints using ue5_blueprint\n"
			 "3. Creating data assets or data tables if needed\n"
			 "4. Wiring up the game logic\n\n"
			 "Use the arbor tools (ue5_blueprint, ue5_actors, ue5_ai, ue5_run_python) to build\n"
			 "the implementation directly in the editor."),
		*Type.ToLower(), *ContextSummary, *Type, *Name, *Type, *Desc, *Tags, *Type.ToLower(), *Type.ToLower());

	ArborAIPromptDialog::Show(Prompt, TEXT("Help Implement with Claude"), true);

	return FReply::Handled();
}

FString SArborCodexPillarsWidget::BuildContextSummary() const
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

FReply SArborCodexPillarsWidget::OnAIIterateField(const FString& FieldKey, const FString& DisplayName)
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString CurrentValue;
	if (FieldKey == TEXT("PillarName") && PillarNameInput.IsValid())
		CurrentValue = PillarNameInput->GetText().ToString();
	else if (FieldKey == TEXT("Description") && PillarDescInput.IsValid())
		CurrentValue = PillarDescInput->GetText().ToString();
	else if (FieldKey == TEXT("Tags") && PillarTagsInput.IsValid())
		CurrentValue = ArborCodexHelpers::JoinCSV(PillarTagsInput->GetTags());

	ArborAIFieldIterateDialog::Show(
		DisplayName,
		CurrentValue,
		BuildContextSummary(),
		PillarAssetPaths[SelectedPillarIndex],
		FieldKey);

	return FReply::Handled();
}

FReply SArborCodexPillarsWidget::OnDeletePillarClicked()
{
	if (SelectedPillarIndex < 0 || SelectedPillarIndex >= PillarAssetPaths.Num())
	{
		return FReply::Handled();
	}

	FString AssetPath = PillarAssetPaths[SelectedPillarIndex];
	FString Name = SelectedPillarIndex < PillarDisplayNames.Num()
		? PillarDisplayNames[SelectedPillarIndex] : FPaths::GetBaseFilename(AssetPath);

	TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
		.Title(LOCTEXT("DeletePillarTitle", "Delete Pillar"))
		.ClientSize(FVector2D(420, 150))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize);

	TWeakPtr<SWindow> WeakWindow = ConfirmWindow;
	TWeakPtr<SArborCodexPillarsWidget> WeakSelf = SharedThis(this);

	ConfirmWindow->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(16.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("DeletePillarConfirm", "Are you sure you want to delete \"{0}\"?\nThis action cannot be undone."),
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

						if (TSharedPtr<SArborCodexPillarsWidget> Self = WeakSelf.Pin())
						{
							Self->SelectedPillarIndex = -1;
							Self->ScanPillars();
							Self->RebuildPillarList();
							if (Self->PillarDetailPanel.IsValid())
							{
								Self->PillarDetailPanel->ClearChildren();
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
