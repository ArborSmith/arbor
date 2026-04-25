#include "ArborCodexOverviewWidget.h"
#include "ArborCodexContext.h"
#include "ArborGameContextTypes.h"
#include "ArborClaude.h"
#include "ArborAIPromptDialog.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "ArborCodexImagePanel.h"
#include "ArborTagInput.h"
#include "ArborAIFieldIterateDialog.h"
#include "Engine/Texture2D.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"

#define LOCTEXT_NAMESPACE "ArborCodexOverviewWidget"

bool SArborCodexOverviewWidget::IsFieldLocked(const FString& FieldKey) const
{
	return LockedFields.Contains(FieldKey);
}

void SArborCodexOverviewWidget::ToggleFieldLock(const FString& FieldKey)
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

void SArborCodexOverviewWidget::SaveLockedFieldsToAsset()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext() || CodexContext->SelectedContextPath.IsEmpty())
	{
		return;
	}
	UObject* Obj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
	UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
	if (!Ctx)
	{
		return;
	}
	Ctx->Modify();
	Ctx->LockedFields = LockedFields;
	UPackage* Package = Ctx->GetOutermost();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Ctx, *PackageFilename, SaveArgs);
}

void SArborCodexOverviewWidget::Construct(const FArguments& InArgs)
{
	CodexContext = InArgs._CodexContext;

	ChildSlot
	[
		SAssignNew(DetailPanel, SScrollBox)
	];

	if (CodexContext.IsValid())
	{
		ContextChangedHandle = CodexContext->OnContextChanged.AddSP(
			this, &SArborCodexOverviewWidget::OnContextChanged);
		OnContextChanged();
	}
}

void SArborCodexOverviewWidget::OnContextChanged()
{
	ShowContextDetail();
}

void SArborCodexOverviewWidget::ShowContextDetail()
{
	DetailPanel->ClearChildren();

	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		DetailPanel->AddSlot()
		.Padding(16.0f, 32.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoContext", "Select or create a Game Context to edit its details."))
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
		];
		return;
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
	UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
	if (!Ctx)
	{
		return;
	}

	LockedFields = Ctx->LockedFields;

	// Concept art image panel
	{
		UTexture2D* PrimaryTex = Ctx->ConceptArt.LoadSynchronous();
		TArray<UTexture2D*> GalleryTextures;
		for (const auto& SoftRef : Ctx->ConceptArtGallery)
		{
			GalleryTextures.Add(SoftRef.LoadSynchronous());
		}

		DetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SNew(SArborCodexImagePanel)
			.ConceptArt(PrimaryTex)
			.Gallery(GalleryTextures)
			.Prompt(Ctx->ConceptArtPrompt)
			.AssetPath(CodexContext->SelectedContextPath)
		];
	}

	// Header
	DetailPanel->AddSlot()
	.Padding(8.0f, 8.0f, 8.0f, 4.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("OverviewHeader", "Game Context Overview"))
		.Font(ArborCodexStyle::Font::PageHeader())
		.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
	];

	DetailPanel->AddSlot()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
		.Padding(0.0f)
		[
			SNew(SBox).HeightOverride(1.0f)
		]
	];

	// Title
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("CtxTitle", "Title"),
			CtxTitleInput,
			Ctx->GameTitle,
			LOCTEXT("CtxTitleHint", "Game title"),
			[this]() { return IsFieldLocked(TEXT("GameTitle")); },
			[this]() { ToggleFieldLock(TEXT("GameTitle")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("GameTitle"), TEXT("Title")); })
	];

	// Genre
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("CtxGenre", "Genre"),
			CtxGenreInput,
			Ctx->Genre,
			LOCTEXT("CtxGenreHint", "e.g. Dark Fantasy RPG"),
			[this]() { return IsFieldLocked(TEXT("Genre")); },
			[this]() { ToggleFieldLock(TEXT("Genre")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Genre"), TEXT("Genre")); })
	];

	// Setting
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("CtxSetting", "Setting (Where/What)"),
			CtxSettingInput,
			Ctx->Setting,
			LOCTEXT("CtxSettingHint", "Brief setting line"),
			[this]() { return IsFieldLocked(TEXT("Setting")); },
			[this]() { ToggleFieldLock(TEXT("Setting")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Setting"), TEXT("Setting")); })
	];

	// Tone
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeFieldRow(
			LOCTEXT("CtxTone", "Tone (How It Feels)"),
			CtxToneInput,
			Ctx->Tone,
			LOCTEXT("CtxToneHint", "e.g. Gritty and morally gray"),
			[this]() { return IsFieldLocked(TEXT("Tone")); },
			[this]() { ToggleFieldLock(TEXT("Tone")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Tone"), TEXT("Tone")); })
	];

	// World Description (multiline)
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeMultilineFieldRow(
			LOCTEXT("CtxWorld", "World Description"),
			CtxWorldInput,
			Ctx->WorldDescription,
			LOCTEXT("CtxWorldHint", "Longer world lore..."),
			[this]() { return IsFieldLocked(TEXT("WorldDescription")); },
			[this]() { ToggleFieldLock(TEXT("WorldDescription")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("WorldDescription"), TEXT("World Description")); })
	];

	// Tags
	DetailPanel->AddSlot()
	.Padding(ArborCodexStyle::Spacing::FieldOuter)
	[
		ArborCodexHelpers::MakeTagFieldRow(
			LOCTEXT("CtxTags", "Tags"), CtxTagsInput,
			Ctx->Tags,
			LOCTEXT("CtxTagsHint", "e.g. Open World, Multiplayer, Roguelike"),
			[this]() { return IsFieldLocked(TEXT("Tags")); },
			[this]() { ToggleFieldLock(TEXT("Tags")); return FReply::Handled(); },
			[this]() { return OnAIIterateField(TEXT("Tags"), TEXT("Tags")); })
	];

	// Style Images section
	{
		DetailPanel->AddSlot()
		.Padding(8.0f, 12.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StyleImagesHeader", "Style Images"))
			.Font(ArborCodexStyle::Font::SectionHeader())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
		];

		DetailPanel->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
			.Padding(0.0f)
			[
				SNew(SBox).HeightOverride(1.0f)
			]
		];

		UTexture2D* StylePrimaryTex = nullptr;
		TArray<UTexture2D*> StyleGalleryTextures;
		for (const auto& SoftRef : Ctx->StyleImages)
		{
			UTexture2D* Tex = SoftRef.LoadSynchronous();
			if (!StylePrimaryTex && Tex)
			{
				StylePrimaryTex = Tex;
			}
			StyleGalleryTextures.Add(Tex);
		}

		DetailPanel->AddSlot()
		.Padding(0.0f)
		[
			SAssignNew(StyleImagePanel, SArborCodexImagePanel)
			.ConceptArt(StylePrimaryTex)
			.Gallery(StyleGalleryTextures)
			.Prompt(Ctx->StyleImagePrompt)
			.AssetPath(CodexContext->SelectedContextPath)
			.ImageCategory(TEXT("style_images"))
			.GenerateButtonLabel(TEXT("Generate Style Images"))
		];
	}

	// Buttons
	DetailPanel->AddSlot()
	.Padding(12.0f, 12.0f, 12.0f, 8.0f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			ArborCodexHelpers::MakePrimaryButton(
				LOCTEXT("SaveContext", "Save"),
				FOnClicked::CreateSP(this, &SArborCodexOverviewWidget::OnSaveContextClicked))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ArborCodexHelpers::MakeAIButton(
				LOCTEXT("ImproveContext", "Improve with AI"),
				FOnClicked::CreateSP(this, &SArborCodexOverviewWidget::OnImproveContextClicked))
		]
	];
}

FReply SArborCodexOverviewWidget::OnSaveContextClicked()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	UObject* Obj = UEditorAssetLibrary::LoadAsset(CodexContext->SelectedContextPath);
	UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
	if (!Ctx)
	{
		return FReply::Handled();
	}

	Ctx->Modify();
	if (CtxTitleInput.IsValid()) Ctx->GameTitle = CtxTitleInput->GetText().ToString().TrimStartAndEnd();
	if (CtxGenreInput.IsValid()) Ctx->Genre = CtxGenreInput->GetText().ToString().TrimStartAndEnd();
	if (CtxSettingInput.IsValid()) Ctx->Setting = CtxSettingInput->GetText().ToString().TrimStartAndEnd();
	if (CtxToneInput.IsValid()) Ctx->Tone = CtxToneInput->GetText().ToString().TrimStartAndEnd();
	if (CtxWorldInput.IsValid()) Ctx->WorldDescription = CtxWorldInput->GetText().ToString();
	// Themes and GamePillars are struct arrays — edited via Claude/MCP, not the widget
	if (CtxTagsInput.IsValid()) Ctx->Tags = CtxTagsInput->GetTags();
	Ctx->LockedFields = LockedFields;

	UPackage* Package = Ctx->GetOutermost();
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Ctx, *PackageFilename, SaveArgs);

	return FReply::Handled();
}

FReply SArborCodexOverviewWidget::OnImproveContextClicked()
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	// Check if all fields are locked
	static const TArray<FString> AllFieldKeys = {
		TEXT("GameTitle"), TEXT("Genre"), TEXT("Setting"), TEXT("Tone"),
		TEXT("WorldDescription"), TEXT("Tags")
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

	FString Title = CtxTitleInput.IsValid() ? CtxTitleInput->GetText().ToString() : TEXT("");
	FString Genre = CtxGenreInput.IsValid() ? CtxGenreInput->GetText().ToString() : TEXT("");
	FString Setting = CtxSettingInput.IsValid() ? CtxSettingInput->GetText().ToString() : TEXT("");
	FString Tone = CtxToneInput.IsValid() ? CtxToneInput->GetText().ToString() : TEXT("");
	FString World = CtxWorldInput.IsValid() ? CtxWorldInput->GetText().ToString() : TEXT("");
	FString Tags = CtxTagsInput.IsValid() ? ArborCodexHelpers::JoinCSV(CtxTagsInput->GetTags()) : TEXT("");

	FString AssetPath = CodexContext->SelectedContextPath;

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
			TEXT("\n\nIMPORTANT: The following fields are LOCKED and must NOT be modified — "
				 "keep their values exactly as shown above: %s"),
			*FString::Join(LockedFieldNames, TEXT(", ")));
	}

	FString Prompt = FString::Printf(
		TEXT("I have a game context that needs fleshing out. Please generate 3 DISTINCT variations "
			 "of improved content, each taking a different creative direction:\n"
			 "- Variation A: Conservative refinement (polish what's there)\n"
			 "- Variation B: Bold reimagining (take creative risks)\n"
			 "- Variation C: Balanced middle ground\n\n"
			 "Current data:\n"
			 "Title: %s\n"
			 "Genre: %s\n"
			 "Setting: %s\n"
			 "Tone: %s\n"
			 "World Description: %s\n"
			 "Tags: %s\n\n"
			 "Expand the World Description to 2-3 rich paragraphs, refine the Setting and Tone descriptions.\n\n"
			 "After generating, present the 3 variations to the user by calling ue5_run_python:\n"
			 "```python\n"
			 "import arbor.variations as var\n"
			 "var.show_text_variations({\n"
			 "    \"variations\": [\n"
			 "        {\"label\": \"Variation A\", \"fields\": {\"GameTitle\": \"...\", \"Genre\": \"...\", \"Setting\": \"...\", \"Tone\": \"...\", \"WorldDescription\": \"...\", \"Tags\": \"...\"}},\n"
			 "        {\"label\": \"Variation B\", \"fields\": {...}},\n"
			 "        {\"label\": \"Variation C\", \"fields\": {...}}\n"
			 "    ],\n"
			 "    \"category\": \"context\",\n"
			 "    \"asset_path\": \"%s\",\n"
			 "    \"prompt\": \"improve context\",\n"
			 "    \"locked_fields\": %s,\n"
			 "    \"field_order\": [\"GameTitle\", \"Genre\", \"Setting\", \"Tone\", \"WorldDescription\", \"Tags\"]\n"
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
		*Title, *Genre, *Setting, *Tone, *World, *Tags, *AssetPath, *LockedFieldsJSON, *LockInstruction);

	ArborAIPromptDialog::Show(Prompt, TEXT("Improve Context with AI"));

	return FReply::Handled();
}

FString SArborCodexOverviewWidget::BuildContextSummary() const
{
	FString Title = CtxTitleInput.IsValid() ? CtxTitleInput->GetText().ToString() : TEXT("");
	FString Genre = CtxGenreInput.IsValid() ? CtxGenreInput->GetText().ToString() : TEXT("");
	FString Setting = CtxSettingInput.IsValid() ? CtxSettingInput->GetText().ToString() : TEXT("");
	FString Tone = CtxToneInput.IsValid() ? CtxToneInput->GetText().ToString() : TEXT("");
	return FString::Printf(TEXT("%s — %s. %s. Tone: %s"), *Title, *Genre, *Setting, *Tone);
}

FReply SArborCodexOverviewWidget::OnAIIterateField(const FString& FieldKey, const FString& DisplayName)
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext())
	{
		return FReply::Handled();
	}

	FString CurrentValue;
	if (FieldKey == TEXT("GameTitle") && CtxTitleInput.IsValid())
		CurrentValue = CtxTitleInput->GetText().ToString();
	else if (FieldKey == TEXT("Genre") && CtxGenreInput.IsValid())
		CurrentValue = CtxGenreInput->GetText().ToString();
	else if (FieldKey == TEXT("Setting") && CtxSettingInput.IsValid())
		CurrentValue = CtxSettingInput->GetText().ToString();
	else if (FieldKey == TEXT("Tone") && CtxToneInput.IsValid())
		CurrentValue = CtxToneInput->GetText().ToString();
	else if (FieldKey == TEXT("WorldDescription") && CtxWorldInput.IsValid())
		CurrentValue = CtxWorldInput->GetText().ToString();
	else if (FieldKey == TEXT("Tags") && CtxTagsInput.IsValid())
		CurrentValue = ArborCodexHelpers::JoinCSV(CtxTagsInput->GetTags());

	ArborAIFieldIterateDialog::Show(
		DisplayName,
		CurrentValue,
		BuildContextSummary(),
		CodexContext->SelectedContextPath,
		FieldKey);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
