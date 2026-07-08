#include "ArborTextVariationWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "ArborClaude.h"

#define LOCTEXT_NAMESPACE "ArborTextVariation"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void SArborTextVariationWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Header: prompt + category
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(HeaderLabel, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.Text(LOCTEXT("NoVariations", "No variations loaded. Use Arbor.TextVariation to load."))
				.AutoWrapText(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(CategoryLabel, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 11))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		]

		// Cards area (scrollable)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SAssignNew(CardsContainer, SBox)
			]
		]

		// Status label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SAssignNew(StatusLabel, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		]

		// Comments input
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.Text(LOCTEXT("CommentsLabel", "Feedback for regeneration:"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MinDesiredHeight(48.0f)
				.MaxDesiredHeight(100.0f)
				[
					SAssignNew(CommentsBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("CommentsHint", "Enter comments to refine the variations..."))
				]
			]
		]

		// Action buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(UseSelectedButton, SButton)
				.IsEnabled(false)
				.OnClicked_Lambda([this]()
				{
					OnUseSelected();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UseSelected", "Use Selected"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(RegenerateButton, SButton)
				.IsEnabled(false)
				.OnClicked_Lambda([this]()
				{
					OnRegenerate();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Regenerate", "Regenerate with Comments"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked_Lambda([this]()
				{
					OnCancel();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Cancel", "Cancel"))
				]
			]
		]
	];
}

// ---------------------------------------------------------------------------
// Load variations from JSON manifest
// ---------------------------------------------------------------------------

void SArborTextVariationWidget::LoadVariations(const FString& JsonManifest)
{
	ClearVariations();

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonManifest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Arbor TextVariation: Failed to parse JSON manifest"));
		return;
	}

	OriginalPrompt = Root->GetStringField(TEXT("prompt"));
	Category = Root->GetStringField(TEXT("category"));
	Root->TryGetStringField(TEXT("asset_path"), AssetPath);

	// Locked fields
	LockedFieldNames.Empty();
	const TArray<TSharedPtr<FJsonValue>>* LockedArray;
	if (Root->TryGetArrayField(TEXT("locked_fields"), LockedArray))
	{
		for (const auto& Val : *LockedArray)
		{
			FString S;
			if (Val->TryGetString(S))
			{
				LockedFieldNames.Add(S);
			}
		}
	}

	// Field order
	FieldOrder.Empty();
	const TArray<TSharedPtr<FJsonValue>>* OrderArray;
	if (Root->TryGetArrayField(TEXT("field_order"), OrderArray))
	{
		for (const auto& Val : *OrderArray)
		{
			FString S;
			if (Val->TryGetString(S))
			{
				FieldOrder.Add(S);
			}
		}
	}

	// Variations
	const TArray<TSharedPtr<FJsonValue>>* VarArray;
	if (!Root->TryGetArrayField(TEXT("variations"), VarArray))
	{
		UE_LOG(LogTemp, Error, TEXT("Arbor TextVariation: No 'variations' array in manifest"));
		return;
	}

	for (const auto& VarValue : *VarArray)
	{
		const TSharedPtr<FJsonObject>& VarObj = VarValue->AsObject();
		if (!VarObj.IsValid()) continue;

		FTextVariationEntry Entry;
		Entry.Label = VarObj->GetStringField(TEXT("label"));

		const TSharedPtr<FJsonObject>* FieldsObj;
		if (VarObj->TryGetObjectField(TEXT("fields"), FieldsObj))
		{
			for (const auto& Pair : (*FieldsObj)->Values)
			{
				FString Val;
				if (Pair.Value->TryGetString(Val))
				{
					Entry.Fields.Add(FString(*Pair.Key), Val);
				}
			}
		}

		VariationEntries.Add(MoveTemp(Entry));
	}

	// Update UI
	if (HeaderLabel.IsValid())
	{
		HeaderLabel->SetText(FText::FromString(
			FString::Printf(TEXT("Select a variation — %d options"), VariationEntries.Num())));
	}
	if (CategoryLabel.IsValid())
	{
		CategoryLabel->SetText(FText::FromString(
			FString::Printf(TEXT("[%s]"), *Category.ToUpper())));
	}
	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%d variations loaded. Click a card to select."), VariationEntries.Num())));
	}

	// Enable regenerate button
	if (RegenerateButton.IsValid())
	{
		RegenerateButton->SetEnabled(true);
	}

	RebuildCards();

	UE_LOG(LogTemp, Log, TEXT("Arbor TextVariation: Loaded %d variations for category '%s'"),
		VariationEntries.Num(), *Category);
}

// ---------------------------------------------------------------------------
// Card grid
// ---------------------------------------------------------------------------

void SArborTextVariationWidget::RebuildCards()
{
	CardBorders.Empty();

	if (!CardsContainer.IsValid()) return;

	TSharedPtr<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (int32 i = 0; i < VariationEntries.Num(); i++)
	{
		const FTextVariationEntry& Entry = VariationEntries[i];

		// Build field rows inside a scroll box
		TSharedPtr<SVerticalBox> FieldsBox = SNew(SVerticalBox);

		// Use field_order if available, otherwise iterate map
		TArray<FString> Keys;
		if (FieldOrder.Num() > 0)
		{
			Keys = FieldOrder;
		}
		else
		{
			Entry.Fields.GetKeys(Keys);
			Keys.Sort();
		}

		for (const FString& Key : Keys)
		{
			const FString* Value = Entry.Fields.Find(Key);
			if (!Value) continue;

			bool bLocked = LockedFieldNames.Contains(Key);

			FieldsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SVerticalBox)

				// Field name
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Key))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(bLocked
						? FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f))
						: FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
				]

				// Field value
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(*Value))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.AutoWrapText(true)
					.ColorAndOpacity(bLocked
						? FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f))
						: FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
				]
			];
		}

		TSharedPtr<SBorder> Border;

		Row->AddSlot()
		.FillWidth(1.0f)
		.Padding(4.0f)
		[
			SAssignNew(Border, SBorder)
			.BorderImage(FAppStyle::GetBrush("Border"))
			.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
			.Padding(12.0f)
			.Cursor(EMouseCursor::Hand)
			.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
			{
				if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
				{
					OnCardClicked(i);
					return FReply::Handled();
				}
				return FReply::Unhandled();
			})
			[
				SNew(SBox)
				.MinDesiredWidth(320.0f)
				.MaxDesiredWidth(400.0f)
				[
					SNew(SVerticalBox)

					// Variation label
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Entry.Label))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.7f, 1.0f)))
					]

					// Fields (scrollable)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							FieldsBox.ToSharedRef()
						]
					]
				]
			]
		];

		CardBorders.Add(Border);
	}

	CardsContainer->SetContent(Row.ToSharedRef());
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void SArborTextVariationWidget::OnCardClicked(int32 Index)
{
	SelectedIndex = Index;
	UpdateSelectionHighlight();

	if (UseSelectedButton.IsValid())
	{
		UseSelectedButton->SetEnabled(true);
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected: %s"), *VariationEntries[Index].Label)));
	}
}

void SArborTextVariationWidget::UpdateSelectionHighlight()
{
	for (int32 i = 0; i < CardBorders.Num(); i++)
	{
		if (CardBorders[i].IsValid())
		{
			FLinearColor Color = (i == SelectedIndex)
				? FLinearColor(0.2f, 0.6f, 1.0f, 1.0f)   // Blue highlight
				: FLinearColor(0.15f, 0.15f, 0.15f, 1.0f); // Default dark
			CardBorders[i]->SetBorderBackgroundColor(Color);
		}
	}
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void SArborTextVariationWidget::OnUseSelected()
{
	if (SelectedIndex < 0 || SelectedIndex >= VariationEntries.Num()) return;

	WriteResult(TEXT("select"));

	const FTextVariationEntry& Entry = VariationEntries[SelectedIndex];

	if (!AssetPath.IsEmpty())
	{
		// Build a JSON representation of the selected fields for the message
		FString FieldsSummary;
		for (const auto& Pair : Entry.Fields)
		{
			if (!LockedFieldNames.Contains(Pair.Key))
			{
				FieldsSummary += FString::Printf(TEXT("  %s: %s\n"), *Pair.Key, *Pair.Value);
			}
		}

		FString Message = FString::Printf(
			TEXT("The user selected text variation \"%s\" from the variation review window.\n")
			TEXT("Category: %s\n")
			TEXT("Asset path: %s\n\n")
			TEXT("Selected field values:\n%s\n")
			TEXT("Please apply these values to the codex entry using the codex update MCP tool."),
			*Entry.Label, *Category, *AssetPath, *FieldsSummary);
		FArborClaude::SendMessage(Message);
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected %s. Applying to asset..."), *Entry.Label)));
	}
}

void SArborTextVariationWidget::OnRegenerate()
{
	WriteResult(TEXT("regenerate"));

	if (StatusLabel.IsValid())
	{
		FString Comments = CommentsBox.IsValid() ? CommentsBox->GetText().ToString() : TEXT("");
		StatusLabel->SetText(FText::FromString(
			Comments.IsEmpty()
				? TEXT("Regeneration requested. Waiting for new variations...")
				: TEXT("Regeneration requested with feedback. Waiting...")));
	}
}

void SArborTextVariationWidget::OnCancel()
{
	WriteResult(TEXT("cancel"));

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(LOCTEXT("Cancelled", "Cancelled."));
	}
}

void SArborTextVariationWidget::WriteResult(const FString& Action)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetStringField(TEXT("action"), Action);
	Result->SetNumberField(TEXT("selected_index"), SelectedIndex);
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("prompt"), OriginalPrompt);
	Result->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

	// Comments
	FString Comments = CommentsBox.IsValid() ? CommentsBox->GetText().ToString() : TEXT("");
	Result->SetStringField(TEXT("comments"), Comments);

	// Selected fields
	if (SelectedIndex >= 0 && SelectedIndex < VariationEntries.Num())
	{
		const FTextVariationEntry& Entry = VariationEntries[SelectedIndex];
		Result->SetStringField(TEXT("selected_label"), Entry.Label);

		TSharedPtr<FJsonObject> FieldsObj = MakeShareable(new FJsonObject());
		for (const auto& Pair : Entry.Fields)
		{
			FieldsObj->SetStringField(Pair.Key, Pair.Value);
		}
		Result->SetObjectField(TEXT("selected_fields"), FieldsObj);
	}

	// Serialize
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

	// Write to Saved/Arbor/text_variation_result.json
	FString OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Arbor"), TEXT("text_variation_result.json"));
	FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogTemp, Log, TEXT("Arbor TextVariation: Wrote result to %s (action=%s)"), *OutputPath, *Action);
}

void SArborTextVariationWidget::ClearVariations()
{
	VariationEntries.Empty();
	CardBorders.Empty();
	SelectedIndex = -1;
	OriginalPrompt.Empty();
	Category.Empty();
	AssetPath.Empty();
	LockedFieldNames.Empty();
	FieldOrder.Empty();

	if (CardsContainer.IsValid())
	{
		CardsContainer->SetContent(SNullWidget::NullWidget);
	}
	if (UseSelectedButton.IsValid())
	{
		UseSelectedButton->SetEnabled(false);
	}
	if (RegenerateButton.IsValid())
	{
		RegenerateButton->SetEnabled(false);
	}
}

#undef LOCTEXT_NAMESPACE
