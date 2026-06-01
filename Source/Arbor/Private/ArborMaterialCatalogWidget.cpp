#include "ArborMaterialCatalogWidget.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ImageUtils.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#include "Brushes/SlateColorBrush.h"
#include "Styling/AppStyle.h"

#include "IPythonScriptPlugin.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "ArborCatalogPreviewViewport.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "ArborMaterialCatalogWidget"


// ============================================================================
// Row widget for the list view
// ============================================================================

class SArborCatalogEntryRow : public STableRow<TSharedPtr<FArborCatalogEntry>>
{
public:
	SLATE_BEGIN_ARGS(SArborCatalogEntryRow) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCatalogEntry>, Entry)
		SLATE_ARGUMENT(TSharedPtr<FSlateDynamicImageBrush>, ThumbnailBrush)
		SLATE_ARGUMENT(FLinearColor, StatusColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Entry = InArgs._Entry;

		const FSlateBrush* ThumbBrush = InArgs._ThumbnailBrush.IsValid()
			? InArgs._ThumbnailBrush.Get()
			: FAppStyle::GetBrush("Icons.Warning");

		STableRow<TSharedPtr<FArborCatalogEntry>>::Construct(
			STableRow<TSharedPtr<FArborCatalogEntry>>::FArguments()
				.Padding(FMargin(4))
				.Content()
				[
					SNew(SHorizontalBox)

					// Thumbnail tile (64x64)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SBox)
						.WidthOverride(64.f)
						.HeightOverride(64.f)
						[
							SNew(SImage).Image(ThumbBrush)
						]
					]

					// ID + status pill stack
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Entry->Id))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 2, 0, 0)
						[
							SNew(SHorizontalBox)

							// Status pill
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("RoundedFilledBorder"))
								.BorderBackgroundColor(InArgs._StatusColor)
								.Padding(FMargin(6, 1))
								[
									SNew(STextBlock)
									.Text(FText::FromString(Entry->Status))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor::Black))
								]
							]

							// Shading model / blend mode hint
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8, 0, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("%s · %s · %d nodes"),
									*Entry->ShadingModel.Replace(TEXT("MSM_"), TEXT("")),
									*Entry->BlendMode.Replace(TEXT("BLEND_"), TEXT("")),
									Entry->ExpressionCount)))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
							]
						]
					]
				],
			OwnerTable);
	}

private:
	TSharedPtr<FArborCatalogEntry> Entry;
};


// ============================================================================
// Main widget
// ============================================================================

void SArborMaterialCatalogWidget::Construct(const FArguments& InArgs)
{
	CatalogRoot = ResolveCatalogRoot();
	LoadTextureSuggestions();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 8, 8, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::FromString(FString::Printf(TEXT("Catalog: %s  (%d entries)"),
						*CatalogRoot, AllEntries.Num()));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
				.MinDesiredWidth(220.f)
				.HintText(LOCTEXT("Search", "Filter by id, tag, or trait..."))
				.OnTextChanged_Lambda([this](const FText& NewText)
				{
					SearchText = NewText.ToString();
					RefreshFilter();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked_Lambda([this]()
				{
					LoadIndex();
					return FReply::Handled();
				})
			]
		]

		// Main split: grid (left) | detail (right)
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
				.Padding(4)
				[
					SAssignNew(ListView, SListView<TSharedPtr<FArborCatalogEntry>>)
					.ListItemsSource(&VisibleEntries)
					.OnGenerateRow(this, &SArborMaterialCatalogWidget::OnGenerateRow)
					.OnSelectionChanged(this, &SArborMaterialCatalogWidget::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
					.ItemHeight(78)
				]
			]

			+ SSplitter::Slot()
			.Value(0.45f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
				.Padding(8)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(DetailContainer, SBox)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NoSelection", "Select an entry to see details."))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
						]
					]
				]
			]
		]
	];

	LoadIndex();
}


FString SArborMaterialCatalogWidget::ResolveCatalogRoot() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("MaterialCatalog"));
}


void SArborMaterialCatalogWidget::LoadIndex()
{
	AllEntries.Reset();
	ThumbnailBrushes.Reset();

	const FString IndexPath = CatalogRoot / TEXT("_index.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *IndexPath))
	{
		RefreshFilter();
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		RefreshFilter();
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
	{
		RefreshFilter();
		return;
	}

	for (const TSharedPtr<FJsonValue>& V : *Entries)
	{
		const TSharedPtr<FJsonObject> Obj = V->AsObject();
		if (!Obj.IsValid()) continue;

		TSharedPtr<FArborCatalogEntry> E = MakeShared<FArborCatalogEntry>();
		E->Id = Obj->GetStringField(TEXT("id"));
		E->YamlPath = Obj->GetStringField(TEXT("yaml_path"));
		Obj->TryGetStringField(TEXT("type"), E->Type);        // missing -> reference_material
		if (E->Type.IsEmpty()) E->Type = TEXT("reference_material");
		E->Source = Obj->GetStringField(TEXT("source"));
		Obj->TryGetStringField(TEXT("mf_path"), E->MFPath);   // pattern entries only
		E->Status = Obj->GetStringField(TEXT("status"));
		Obj->TryGetStringField(TEXT("description"), E->Description);
		Obj->TryGetStringField(TEXT("shading_model"), E->ShadingModel);
		Obj->TryGetStringField(TEXT("blend_mode"), E->BlendMode);
		Obj->TryGetNumberField(TEXT("expression_count"), E->ExpressionCount);
		Obj->TryGetNumberField(TEXT("connection_count"), E->ConnectionCount);
		Obj->TryGetNumberField(TEXT("output_count"), E->OutputCount);

		FString RelThumb;
		if (Obj->TryGetStringField(TEXT("thumbnail_path"), RelThumb) && !RelThumb.IsEmpty())
		{
			E->ThumbnailAbsPath = CatalogRoot / RelThumb;
		}

		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if (Obj->TryGetArrayField(TEXT("tags"), Tags))
			for (const auto& T : *Tags) E->Tags.Add(T->AsString());

		const TArray<TSharedPtr<FJsonValue>>* Traits = nullptr;
		if (Obj->TryGetArrayField(TEXT("visual_traits"), Traits))
			for (const auto& T : *Traits) E->VisualTraits.Add(T->AsString());

		const TArray<TSharedPtr<FJsonValue>>* PTags = nullptr;
		if (Obj->TryGetArrayField(TEXT("proposed_tags"), PTags))
			for (const auto& T : *PTags) E->ProposedTags.Add(T->AsString());

		const TArray<TSharedPtr<FJsonValue>>* PTraits = nullptr;
		if (Obj->TryGetArrayField(TEXT("proposed_visual_traits"), PTraits))
			for (const auto& T : *PTraits) E->ProposedVisualTraits.Add(T->AsString());

		Obj->TryGetStringField(TEXT("proposed_description"), E->ProposedDescription);

		AllEntries.Add(E);
	}

	RefreshFilter();
}


void SArborMaterialCatalogWidget::RefreshFilter()
{
	VisibleEntries.Reset();
	const FString SearchLower = SearchText.ToLower();

	for (const auto& E : AllEntries)
	{
		if (StatusFilter.Num() > 0 && !StatusFilter.Contains(E->Status))
			continue;

		if (!SearchLower.IsEmpty())
		{
			bool bMatches = E->Id.ToLower().Contains(SearchLower)
				|| E->Source.ToLower().Contains(SearchLower)
				|| E->Description.ToLower().Contains(SearchLower);
			if (!bMatches)
			{
				for (const FString& T : E->Tags)
					if (T.ToLower().Contains(SearchLower)) { bMatches = true; break; }
			}
			if (!bMatches)
			{
				for (const FString& T : E->VisualTraits)
					if (T.ToLower().Contains(SearchLower)) { bMatches = true; break; }
			}
			if (!bMatches) continue;
		}

		VisibleEntries.Add(E);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}


TSharedRef<ITableRow> SArborMaterialCatalogWidget::OnGenerateRow(
	TSharedPtr<FArborCatalogEntry> InEntry,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SArborCatalogEntryRow, OwnerTable)
		.Entry(InEntry)
		.ThumbnailBrush(GetThumbnailBrush(InEntry))
		.StatusColor(GetStatusColor(InEntry->Status));
}


void SArborMaterialCatalogWidget::OnSelectionChanged(
	TSharedPtr<FArborCatalogEntry> InEntry, ESelectInfo::Type)
{
	SelectedEntry = InEntry;
	bHasUnsavedEdits = false;
	// Drop any texture-picker MID so the next selection starts from the
	// source material's defaults again.
	PreviewMID.Reset();
	CurrentTextureOverrides.Reset();
	if (InEntry.IsValid())
	{
		PendingTagsCsv = FString::Join(InEntry->Tags, TEXT(", "));
		PendingTraitsCsv = FString::Join(InEntry->VisualTraits, TEXT(", "));
		PendingDescription = InEntry->Description;
	}
	else
	{
		PendingTagsCsv.Reset();
		PendingTraitsCsv.Reset();
		PendingDescription.Reset();
	}
	if (DetailContainer.IsValid())
	{
		DetailContainer->SetContent(BuildDetailPanel());
	}
}


TSharedPtr<FSlateDynamicImageBrush> SArborMaterialCatalogWidget::GetThumbnailBrush(
	const TSharedPtr<FArborCatalogEntry>& Entry)
{
	if (!Entry.IsValid() || Entry->ThumbnailAbsPath.IsEmpty()) return nullptr;

	if (TSharedPtr<FSlateDynamicImageBrush>* Cached = ThumbnailBrushes.Find(Entry->Id))
		return *Cached;

	if (!FPaths::FileExists(Entry->ThumbnailAbsPath)) return nullptr;

	// Use UE's standard PNG loader to grab raw pixels, then wrap in a brush
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Entry->ThumbnailAbsPath))
		return nullptr;

	FImage Loaded;
	if (!FImageUtils::DecompressImage(FileData.GetData(), FileData.Num(), Loaded))
		return nullptr;
	if (Loaded.Format != ERawImageFormat::BGRA8)
	{
		// PNGs should always decode to BGRA8 in our pipeline. If we ever ship a
		// different format here, fail loudly rather than silently mis-render.
		UE_LOG(LogTemp, Warning, TEXT("[ArborCatalog] %s thumbnail has unexpected format %d - skipping"),
			*Entry->Id, (int32)Loaded.Format);
		return nullptr;
	}

	const FName BrushName(FString::Printf(TEXT("ArborCatalogThumb_%s"), *Entry->Id));
	TArray<uint8> RawBGRA;
	RawBGRA.Append(Loaded.RawData.GetData(), Loaded.RawData.Num());

	TSharedPtr<FSlateDynamicImageBrush> Brush = FSlateDynamicImageBrush::CreateWithImageData(
		BrushName, FVector2D(Loaded.SizeX, Loaded.SizeY), RawBGRA);
	if (Brush.IsValid())
	{
		ThumbnailBrushes.Add(Entry->Id, Brush);
	}
	return Brush;
}


TSharedRef<SWidget> SArborMaterialCatalogWidget::BuildDetailPanel()
{
	if (!SelectedEntry.IsValid())
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoSelection", "Select an entry to see details."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
	}

	TSharedPtr<FArborCatalogEntry> E = SelectedEntry;
	const TSharedPtr<FSlateDynamicImageBrush> Brush = GetThumbnailBrush(E);
	const FSlateBrush* ThumbBrush = Brush.IsValid()
		? Brush.Get()
		: FAppStyle::GetBrush("Icons.Warning");

	const bool bHasProposals = (E->ProposedTags.Num() > 0
		|| E->ProposedVisualTraits.Num() > 0
		|| !E->ProposedDescription.IsEmpty());

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	// Lazily create the preview viewport. One instance is reused across
	// selections; we just feed it a new material on each click.
	if (!PreviewViewport.IsValid())
	{
		SAssignNew(PreviewViewport, SArborCatalogPreviewViewport);
	}

	// Load + apply the entry's source material so the sphere reflects current state.
	// Pattern entries reference a Material Function (no standalone material to
	// preview), so we rely on their rendered thumbnail and skip the live load.
	const bool bIsPattern = (E->Type == TEXT("pattern"));
	if (!bIsPattern)
	{
		if (UObject* Asset = UEditorAssetLibrary::LoadAsset(E->Source))
		{
			if (UMaterialInterface* Mat = Cast<UMaterialInterface>(Asset))
			{
				PreviewViewport->SetMaterial(Mat);
			}
		}
	}

	// Header row: live preview + id/source/stats
	Root->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 12, 0)
		[
			SNew(SBox)
			.WidthOverride(440.f).HeightOverride(440.f)
			[
				PreviewViewport.ToSharedRef()
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(E->Id))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bIsPattern
					? FString::Printf(TEXT("%s  (MF pattern)"), *E->MFPath)
					: E->Source))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("status: %s\nshading: %s   blend: %s\nnodes: %d / conns: %d / outs: %d"),
					*E->Status, *E->ShadingModel, *E->BlendMode,
					E->ExpressionCount, E->ConnectionCount, E->OutputCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]
	];

	// Status buttons row (mark ok / needs_review / bad / deprecated)
	auto MakeStatusButton = [this](const FString& Status, const FText& Label, FLinearColor Color)
	{
		return SNew(SButton)
			.Text(Label)
			.ButtonColorAndOpacity(FSlateColor(Color * 0.6f))
			.OnClicked_Lambda([this, Status]() { return this->OnSetStatus(Status); });
	};
	Root->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[ MakeStatusButton(TEXT("ok"), LOCTEXT("MarkOk", "OK"), GetStatusColor(TEXT("ok"))) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[ MakeStatusButton(TEXT("needs_review"), LOCTEXT("MarkReview", "Needs Review"), GetStatusColor(TEXT("needs_review"))) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[ MakeStatusButton(TEXT("bad"), LOCTEXT("MarkBad", "Mark Bad"), GetStatusColor(TEXT("bad"))) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[ MakeStatusButton(TEXT("deprecated"), LOCTEXT("MarkDeprecated", "Deprecate"), GetStatusColor(TEXT("deprecated"))) ]
	];

	// Action buttons row (open in editor, delete)
	Root->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 12)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenInEditor", "Open Material in Editor"))
			.OnClicked(this, &SArborMaterialCatalogWidget::OnOpenInEditor)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Delete", "Delete Entry"))
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.2f, 0.2f)))
			.OnClicked(this, &SArborMaterialCatalogWidget::OnDeleteSelected)
		]
	];

	// Proposed (AI) panel — only shown when proposals exist
	if (bHasProposals)
	{
		const FString PTags = FString::Join(E->ProposedTags, TEXT(", "));
		const FString PTraits = FString::Join(E->ProposedVisualTraits, TEXT(", "));
		Root->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 12)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(8)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AIProposals", "AI proposals (review before accepting)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Tags: %s"), *PTags)))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Traits: %s"), *PTraits)))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(E->ProposedDescription))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("Accept", "Accept Proposals"))
						.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.7f, 0.4f)))
						.OnClicked(this, &SArborMaterialCatalogWidget::OnAcceptProposals)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("Reject", "Reject Proposals"))
						.OnClicked(this, &SArborMaterialCatalogWidget::OnRejectProposals)
					]
				]
			]
		];
	}

	// Editable Tags (CSV)
	Root->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Text(LOCTEXT("Tags", "Tags (comma-separated)"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];
	Root->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[
		SNew(SEditableTextBox)
		.Text(FText::FromString(PendingTagsCsv))
		.OnTextChanged_Lambda([this](const FText& T)
		{
			PendingTagsCsv = T.ToString();
			bHasUnsavedEdits = true;
		})
	];

	// Editable Visual Traits (CSV)
	Root->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Text(LOCTEXT("Traits", "Visual traits (comma-separated)"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];
	Root->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[
		SNew(SEditableTextBox)
		.Text(FText::FromString(PendingTraitsCsv))
		.OnTextChanged_Lambda([this](const FText& T)
		{
			PendingTraitsCsv = T.ToString();
			bHasUnsavedEdits = true;
		})
	];

	// Editable Description (multi-line)
	Root->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Text(LOCTEXT("Description", "Description"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];
	Root->AddSlot().AutoHeight().Padding(0, 2, 0, 8)
	[
		SNew(SBox).HeightOverride(80.f)
		[
			SNew(SMultiLineEditableTextBox)
			.Text(FText::FromString(PendingDescription))
			.AutoWrapText(true)
			.OnTextChanged_Lambda([this](const FText& T)
			{
				PendingDescription = T.ToString();
				bHasUnsavedEdits = true;
			})
		]
	];

	// Save button
	Root->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SButton)
		.Text(LOCTEXT("Save", "Save Changes"))
		.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 0.8f)))
		.OnClicked(this, &SArborMaterialCatalogWidget::OnSaveSelected)
	];

	// Texture parameter picker section (only shown when material has texture params)
	if (UObject* Asset = UEditorAssetLibrary::LoadAsset(E->Source))
	{
		if (UMaterialInterface* Mat = Cast<UMaterialInterface>(Asset))
		{
			Root->AddSlot()
			.AutoHeight()
			.Padding(0, 12, 0, 0)
			[
				BuildTextureParamPicker(Mat)
			];
		}
	}

	return Root;
}


void SArborMaterialCatalogWidget::LoadTextureSuggestions()
{
	TextureSuggestions.Reset();
	TextureSuggestionsScannedCount = 0;

	const FString CachePath = CatalogRoot / TEXT("_texture_suggestions.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *CachePath)) return;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	Root->TryGetNumberField(TEXT("scanned_mics"), TextureSuggestionsScannedCount);

	const TSharedPtr<FJsonObject>* SuggObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("suggestions"), SuggObj) || !SuggObj || !SuggObj->IsValid())
		return;

	for (const auto& Pair : (*SuggObj)->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Pair.Value->TryGetArray(Arr) || !Arr) continue;
		TArray<FString>& Paths = TextureSuggestions.Add(Pair.Key);
		for (const auto& V : *Arr)
		{
			const TSharedPtr<FJsonObject> Item = V->AsObject();
			if (!Item.IsValid()) continue;
			FString Path;
			if (Item->TryGetStringField(TEXT("path"), Path) && !Path.IsEmpty())
			{
				Paths.Add(Path);
			}
		}
	}
}


FReply SArborMaterialCatalogWidget::OnRescanTextures()
{
	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("scan_texture_suggestions"));
	RunCatalogOp(Cmd);
	LoadTextureSuggestions();
	if (DetailContainer.IsValid() && SelectedEntry.IsValid())
	{
		DetailContainer->SetContent(BuildDetailPanel());
	}
	return FReply::Handled();
}


TSharedRef<SWidget> SArborMaterialCatalogWidget::BuildTextureParamPicker(UMaterialInterface* Mat)
{
	if (!Mat) return SNullWidget::NullWidget;

	// Discover texture parameters on the material.
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	Mat->GetAllTextureParameterInfo(Infos, Ids);
	if (Infos.Num() == 0)
	{
		// No texture params - nothing to pick. Show a hint with rescan button anyway.
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(8)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoTextureParams", "Texture parameters: (none exposed on this material)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]
			];
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Header + rescan button
	const int32 ScannedHint = TextureSuggestionsScannedCount;
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TextureParams", "Texture parameters"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("suggestions from %d MICs"), ScannedHint)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("RescanTex", "Rescan"))
			.ToolTipText(LOCTEXT("RescanTexTip", "Re-scan all project MICs to refresh the per-parameter texture suggestions."))
			.OnClicked(this, &SArborMaterialCatalogWidget::OnRescanTextures)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("RetagFromPreview", "Re-tag from preview"))
			.ToolTipText(LOCTEXT("RetagTip", "Re-render this entry's thumbnail using the picked textures, then re-run AI auto-tag so the proposed tags reflect the new visual."))
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.4f, 0.8f)))
			.OnClicked(this, &SArborMaterialCatalogWidget::OnRetagFromPreview)
		]
	];

	// One row per parameter
	for (const FMaterialParameterInfo& Info : Infos)
	{
		const FString ParamName = Info.Name.ToString();

		// Build/cache the combobox options TSharedPtrs (SComboBox needs persistent ptrs)
		TArray<TSharedPtr<FString>>& Options = ComboOptionsCache.FindOrAdd(ParamName);
		Options.Reset();
		if (const TArray<FString>* Sugg = TextureSuggestions.Find(ParamName))
		{
			for (const FString& P : *Sugg)
			{
				Options.Add(MakeShared<FString>(P));
			}
		}

		Box->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(140.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(ParamName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				Options.Num() > 0
				? StaticCastSharedRef<SWidget>(
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&Options)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						const FString Display = Item.IsValid() ? *Item : FString();
						const FString Short = FPaths::GetBaseFilename(Display);
						return SNew(STextBlock)
							.Text(FText::FromString(Short))
							.ToolTipText(FText::FromString(Display));
					})
					.OnSelectionChanged_Lambda(
						[this, ParamName](TSharedPtr<FString> Selected, ESelectInfo::Type)
						{
							this->OnTextureSelected(ParamName, Selected);
						})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PickTex", "Pick a texture..."))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					])
				: StaticCastSharedRef<SWidget>(
					SNew(STextBlock)
					.Text(LOCTEXT("NoSuggestions", "(no suggestions - run Rescan)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f))))
			]
		];
	}

	return Box;
}


void SArborMaterialCatalogWidget::OnTextureSelected(FString ParamName, TSharedPtr<FString> NewPath)
{
	if (!SelectedEntry.IsValid() || !NewPath.IsValid()) return;

	UObject* TexAsset = UEditorAssetLibrary::LoadAsset(*NewPath);
	UTexture* Tex = Cast<UTexture>(TexAsset);
	if (!Tex) return;

	// Ensure we have a transient MID parented to the entry's source. The MID
	// is a per-widget scratchpad - never saved, never registered.
	if (!PreviewMID.IsValid())
	{
		UObject* SrcAsset = UEditorAssetLibrary::LoadAsset(SelectedEntry->Source);
		UMaterialInterface* Src = Cast<UMaterialInterface>(SrcAsset);
		if (!Src) return;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Src, GetTransientPackage());
		if (!MID) return;
		PreviewMID.Reset(MID);
	}

	PreviewMID->SetTextureParameterValue(FName(*ParamName), Tex);
	CurrentTextureOverrides.Add(ParamName, *NewPath);

	if (PreviewViewport.IsValid())
	{
		PreviewViewport->SetMaterial(PreviewMID.Get());
	}
}


FReply SArborMaterialCatalogWidget::OnRetagFromPreview()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();

	// Build the texture_overrides JSON object
	const TSharedRef<FJsonObject> Overrides = MakeShared<FJsonObject>();
	for (const auto& Pair : CurrentTextureOverrides)
	{
		Overrides->SetStringField(Pair.Key, Pair.Value);
	}

	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("retag_from_preview"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	Cmd->SetObjectField(TEXT("texture_overrides"), Overrides);

	RunCatalogOp(Cmd);
	ReloadAfterEdit();
	return FReply::Handled();
}


// ============================================================================
// Operations (route to Python via IPythonScriptPlugin)
// ============================================================================

static FString PathToPyLiteral(const FString& In)
{
	// Embed an absolute path into a Python string literal: escape backslashes.
	FString S = In;
	S.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	S.ReplaceInline(TEXT("'"), TEXT("\\'"));
	return S;
}

TSharedPtr<FJsonObject> SArborMaterialCatalogWidget::RunCatalogOp(const TSharedRef<FJsonObject>& Command)
{
	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	if (!Py || !Py->IsPythonAvailable()) return nullptr;

	// Serialize the command. Python will parse via json.loads.
	FString CmdJson;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&CmdJson);
	FJsonSerializer::Serialize(Command, Writer);

	// Use a raw Python string literal r'''...''' so we don't have to escape JSON.
	// JSON contains double-quotes; triple-single-quote raw strings handle that.
	const FString PyCommand = FString::Printf(
		TEXT("import sys; sys.path.insert(0, r'%s'); "
		     "from extraction import dispatch; dispatch.run(r'''%s''')"),
		*PathToPyLiteral(FPaths::ConvertRelativePathToFull(
			FPaths::ProjectPluginsDir() / TEXT("Arbor/.claude/skills/material-authoring"))),
		*CmdJson);

	FPythonCommandEx PyCmd;
	PyCmd.Command = PyCommand;
	PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
	const bool bOk = Py->ExecPythonCommandEx(PyCmd);
	if (!bOk)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborCatalog] Python op failed: %s"), *PyCmd.CommandResult);
		return nullptr;
	}

	// Read the result Arbor wrote to Saved/Arbor/last_result.json
	const FString ResultPath = FPaths::ProjectSavedDir() / TEXT("Arbor/last_result.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *ResultPath)) return nullptr;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}


void SArborMaterialCatalogWidget::ReloadAfterEdit()
{
	// Remember the selected ID so we can re-select after refresh.
	const FString PrevId = SelectedEntry.IsValid() ? SelectedEntry->Id : FString();
	LoadIndex();
	if (!PrevId.IsEmpty())
	{
		for (const auto& E : VisibleEntries)
		{
			if (E->Id == PrevId)
			{
				ListView->SetSelection(E);
				SelectedEntry = E;
				if (DetailContainer.IsValid())
				{
					DetailContainer->SetContent(BuildDetailPanel());
				}
				break;
			}
		}
	}
}


FReply SArborMaterialCatalogWidget::OnSaveSelected()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();

	// Parse CSVs into JSON arrays
	auto CsvToArray = [](const FString& In) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TArray<FString> Parts;
		In.ParseIntoArray(Parts, TEXT(","), /*CullEmpty=*/true);
		for (const FString& P : Parts)
		{
			const FString T = P.TrimStartAndEnd();
			if (!T.IsEmpty()) Out.Add(MakeShared<FJsonValueString>(T));
		}
		return Out;
	};

	const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetArrayField(TEXT("tags"), CsvToArray(PendingTagsCsv));
	Fields->SetArrayField(TEXT("visual_traits"), CsvToArray(PendingTraitsCsv));
	Fields->SetStringField(TEXT("description"), PendingDescription);

	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("update"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	Cmd->SetObjectField(TEXT("fields"), Fields);

	RunCatalogOp(Cmd);
	bHasUnsavedEdits = false;
	ReloadAfterEdit();
	return FReply::Handled();
}


FReply SArborMaterialCatalogWidget::OnSetStatus(FString NewStatus)
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();
	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("set_status"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	Cmd->SetStringField(TEXT("status"), NewStatus);
	RunCatalogOp(Cmd);
	ReloadAfterEdit();
	return FReply::Handled();
}


FReply SArborMaterialCatalogWidget::OnAcceptProposals()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();
	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("accept_proposals"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	RunCatalogOp(Cmd);
	ReloadAfterEdit();
	return FReply::Handled();
}


FReply SArborMaterialCatalogWidget::OnRejectProposals()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();
	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("reject_proposals"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	RunCatalogOp(Cmd);
	ReloadAfterEdit();
	return FReply::Handled();
}


FReply SArborMaterialCatalogWidget::OnDeleteSelected()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();
	const TSharedRef<FJsonObject> Cmd = MakeShared<FJsonObject>();
	Cmd->SetStringField(TEXT("op"), TEXT("delete"));
	Cmd->SetStringField(TEXT("id"), SelectedEntry->Id);
	RunCatalogOp(Cmd);
	SelectedEntry.Reset();
	LoadIndex();
	if (DetailContainer.IsValid())
	{
		DetailContainer->SetContent(BuildDetailPanel());
	}
	return FReply::Handled();
}


FReply SArborMaterialCatalogWidget::OnOpenInEditor()
{
	if (!SelectedEntry.IsValid()) return FReply::Handled();
	// Pattern entries point at a Material Function; everything else at a Material.
	const FString AssetPath = (SelectedEntry->Type == TEXT("pattern"))
		? SelectedEntry->MFPath : SelectedEntry->Source;
	if (AssetPath.IsEmpty()) return FReply::Handled();
	if (GEditor)
	{
		UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AES)
		{
			AES->OpenEditorForAsset(AssetPath);
		}
	}
	return FReply::Handled();
}


FLinearColor SArborMaterialCatalogWidget::GetStatusColor(const FString& Status) const
{
	if (Status == TEXT("ok")) return FLinearColor(0.36f, 0.78f, 0.44f);          // green
	if (Status == TEXT("needs_review")) return FLinearColor(0.95f, 0.78f, 0.30f); // amber
	if (Status == TEXT("bad")) return FLinearColor(0.85f, 0.35f, 0.30f);         // red
	if (Status == TEXT("deprecated")) return FLinearColor(0.55f, 0.55f, 0.55f);  // gray
	if (Status == TEXT("broken")) return FLinearColor(0.85f, 0.40f, 0.65f);      // magenta
	return FLinearColor(0.7f, 0.7f, 0.7f);
}


#undef LOCTEXT_NAMESPACE
