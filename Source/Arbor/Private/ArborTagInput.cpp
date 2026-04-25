#include "ArborTagInput.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"

void SArborTagInput::Construct(const FArguments& InArgs)
{
	Tags = InArgs._InitialTags;
	IsLocked = InArgs._IsLocked;
	OnTagsChanged = InArgs._OnTagsChanged;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Pills
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SAssignNew(PillContainer, SWrapBox)
			.UseAllottedSize(true)
		]

		// Input row (hidden when locked)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(InputRow, SHorizontalBox)
			.Visibility_Lambda([this]()
			{
				return IsLocked.Get(false) ? EVisibility::Collapsed : EVisibility::Visible;
			})

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SAssignNew(InputBox, SEditableTextBox)
				.HintText(InArgs._HintText)
				.Font(ArborCodexStyle::Font::Input())
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter)
					{
						FString Raw = Text.ToString();
						// Support pasting comma-separated values
						TArray<FString> Parts = ArborCodexHelpers::SplitCSV(Raw);
						for (const FString& Part : Parts)
						{
							AddTag(Part);
						}
						if (InputBox.IsValid())
						{
							InputBox->SetText(FText::GetEmpty());
						}
					}
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ArborCodexHelpers::MakeSecondaryButton(
					FText::FromString(TEXT("+ Add")),
					FOnClicked::CreateLambda([this]()
					{
						if (InputBox.IsValid())
						{
							FString Raw = InputBox->GetText().ToString();
							TArray<FString> Parts = ArborCodexHelpers::SplitCSV(Raw);
							for (const FString& Part : Parts)
							{
								AddTag(Part);
							}
							InputBox->SetText(FText::GetEmpty());
						}
						return FReply::Handled();
					}))
			]
		]
	];

	RebuildPills();
}

void SArborTagInput::AddTag(const FString& InTag)
{
	FString Trimmed = InTag.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) return;

	// Case-insensitive duplicate check
	for (const FString& Existing : Tags)
	{
		if (Existing.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return;
		}
	}

	Tags.Add(Trimmed);
	RebuildPills();
	OnTagsChanged.ExecuteIfBound(Tags);
}

void SArborTagInput::RemoveTag(const FString& InTag)
{
	Tags.Remove(InTag);
	RebuildPills();
	OnTagsChanged.ExecuteIfBound(Tags);
}

void SArborTagInput::SetTags(const TArray<FString>& InTags)
{
	Tags = InTags;
	RebuildPills();
}

void SArborTagInput::RebuildPills()
{
	if (!PillContainer.IsValid()) return;
	PillContainer->ClearChildren();

	for (const FString& TagStr : Tags)
	{
		PillContainer->AddSlot()
		.Padding(0.0f, 0.0f, 4.0f, 4.0f)
		[
			CreatePill(TagStr)
		];
	}
}

TSharedRef<SWidget> SArborTagInput::CreatePill(const FString& InTag)
{
	bool bLocked = IsLocked.Get(false);

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InTag))
			.Font(ArborCodexStyle::Font::BodySmall())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::OnAccent))
		];

	if (!bLocked)
	{
		Content->AddSlot()
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
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Accent::PrimaryDim)
		.Padding(FMargin(8.0f, 3.0f, bLocked ? 8.0f : 4.0f, 3.0f))
		[
			Content
		];
}
