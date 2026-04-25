#include "ArborCodexImagePanel.h"
#include "ArborClaude.h"
#include "ArborConceptArtStudioTab.h"
#include "ArborConceptArtStudioWidget.h"
#include "ArborCodexImageTools.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Misc/MessageDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ArborCodexImagePanel"

void SArborCodexImagePanel::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	ImageCategory = InArgs._ImageCategory;
	GenerateButtonLabel = InArgs._GenerateButtonLabel;
	OnImageChangedDelegate = InArgs._OnImageChanged;

	// Setup primary brush
	PrimaryBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	SetupBrush(InArgs._ConceptArt);

	bool bIsConceptArt = ImageCategory != TEXT("style_images");

	ChildSlot
	[
		SNew(SVerticalBox)

		// Primary concept art with click-to-view and remove overlay
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SBox)
			.HeightOverride(ArborCodexStyle::Spacing::ImagePanelHeight)
			[
				SNew(SOverlay)

				// Clickable image
				+ SOverlay::Slot()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.Cursor(EMouseCursor::Hand)
					.OnClicked_Lambda([this]()
					{
						UTexture2D* Tex = Cast<UTexture2D>(PrimaryBrush.GetResourceObject());
						if (Tex) OpenFullSizeViewer(Tex);
						return FReply::Handled();
					})
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SAssignNew(PrimaryImageWidget, SImage)
							.Image(&PrimaryBrush)
						]
					]
				]

				// X remove button overlay (concept art only)
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				.Padding(4.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.Cursor(EMouseCursor::Default)
					.OnClicked(this, &SArborCodexImagePanel::OnRemovePrimary)
					.Visibility_Lambda([this, bIsConceptArt]()
					{
						return (bIsConceptArt && PrimaryBrush.DrawAs == ESlateBrushDrawType::Image)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
						.Padding(FMargin(4.0f, 2.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("X")))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
							.Font(ArborCodexStyle::Font::Caption())
						]
					]
				]
			]
		]

		// Prompt text
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InArgs._Prompt.IsEmpty()
				? TEXT("")
				: FString::Printf(TEXT("Prompt: %s"), *InArgs._Prompt)))
			.Visibility(InArgs._Prompt.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			.Font(ArborCodexStyle::Font::Caption())
		]

		// Gallery thumbnails
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SAssignNew(GalleryBox, SHorizontalBox)
		]

		// Buttons: Generate + Import
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ArborCodexHelpers::MakeAIButton(
					FText::FromString(GenerateButtonLabel.IsEmpty() ? TEXT("Generate Concept Art") : GenerateButtonLabel),
					FOnClicked::CreateSP(this, &SArborCodexImagePanel::OnGenerateClicked))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.Visibility(bIsConceptArt ? EVisibility::Visible : EVisibility::Collapsed)
				[
					ArborCodexHelpers::MakeSecondaryButton(
						LOCTEXT("ImportImage", "Import Image"),
						FOnClicked::CreateSP(this, &SArborCodexImagePanel::OnImportClicked))
				]
			]
		]

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
			.Padding(0.0f)
			[
				SNew(SBox).HeightOverride(1.0f)
			]
		]
	];

	// Build gallery — pre-allocate to prevent TArray reallocation
	// which would invalidate the raw pointers SImage holds to brush elements
	const TArray<UTexture2D*>& GalleryTextures = InArgs._Gallery;
	GalleryBrushes.SetNum(GalleryTextures.Num());
	GalleryTexturePaths.SetNum(GalleryTextures.Num());
	for (int32 i = 0; i < GalleryTextures.Num(); i++)
	{
		GalleryBrushes[i].DrawAs = ESlateBrushDrawType::NoDrawType;
		GalleryTexturePaths[i] = GalleryTextures[i]
			? FSoftObjectPath(GalleryTextures[i]).ToString()
			: TEXT("");

		if (GalleryTextures[i])
		{
			SetupGalleryBrush(i, GalleryTextures[i]);
		}

		GalleryBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f)
			[
				MakeGalleryThumbnail(i)
			];
	}
}

TSharedRef<SWidget> SArborCodexImagePanel::MakeGalleryThumbnail(int32 Index)
{
	bool bIsConceptArt = ImageCategory != TEXT("style_images");

	return SNew(SOverlay)

		// Clickable thumbnail
		+ SOverlay::Slot()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.Cursor(EMouseCursor::Hand)
			.OnClicked_Lambda([this, Index]()
			{
				if (GalleryBrushes.IsValidIndex(Index))
				{
					UTexture2D* Tex = Cast<UTexture2D>(GalleryBrushes[Index].GetResourceObject());
					if (Tex) OpenFullSizeViewer(Tex);
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::Elevated)
				.Padding(2.0f)
				[
					SNew(SBox)
					.HeightOverride(ArborCodexStyle::Spacing::GalleryThumbSize)
					.WidthOverride(ArborCodexStyle::Spacing::GalleryThumbSize)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage)
							.Image(&GalleryBrushes[Index])
						]
					]
				]
			]
		]

		// X remove button overlay (concept art only)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(2.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.Cursor(EMouseCursor::Default)
			.Visibility(bIsConceptArt ? EVisibility::Visible : EVisibility::Collapsed)
			.OnClicked_Lambda([this, Index]()
			{
				return OnRemoveGalleryImage(Index);
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
				.Padding(FMargin(3.0f, 1.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("X")))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.Font(ArborCodexStyle::Font::Caption())
				]
			]
		];
}

void SArborCodexImagePanel::SetupBrush(UTexture2D* Texture)
{
	if (!Texture)
	{
		PrimaryBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return;
	}

	// Pin texture to prevent GC
	PinnedTextures.AddUnique(Texture);
	Texture->AddToRoot();

	PrimaryBrush.SetResourceObject(Texture);
	PrimaryBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	PrimaryBrush.DrawAs = ESlateBrushDrawType::Image;
}

void SArborCodexImagePanel::SetupGalleryBrush(int32 Index, UTexture2D* Texture)
{
	if (!Texture || !GalleryBrushes.IsValidIndex(Index)) return;

	PinnedTextures.AddUnique(Texture);
	Texture->AddToRoot();

	GalleryBrushes[Index].SetResourceObject(Texture);
	GalleryBrushes[Index].ImageSize = FVector2D(ArborCodexStyle::Spacing::GalleryThumbSize, ArborCodexStyle::Spacing::GalleryThumbSize);
	GalleryBrushes[Index].DrawAs = ESlateBrushDrawType::Image;
}

void SArborCodexImagePanel::Refresh(UTexture2D* InConceptArt, const TArray<UTexture2D*>& InGallery, const FString& InPrompt)
{
	// Unpin old textures
	for (UTexture2D* Tex : PinnedTextures)
	{
		if (Tex && Tex->IsRooted())
		{
			Tex->RemoveFromRoot();
		}
	}
	PinnedTextures.Empty();

	SetupBrush(InConceptArt);

	// Rebuild gallery
	if (GalleryBox.IsValid())
	{
		GalleryBox->ClearChildren();
		GalleryBrushes.Empty();
		GalleryTexturePaths.Empty();
		GalleryBrushes.SetNum(InGallery.Num());
		GalleryTexturePaths.SetNum(InGallery.Num());

		for (int32 i = 0; i < InGallery.Num(); i++)
		{
			GalleryBrushes[i].DrawAs = ESlateBrushDrawType::NoDrawType;
			GalleryTexturePaths[i] = InGallery[i]
				? FSoftObjectPath(InGallery[i]).ToString()
				: TEXT("");

			if (InGallery[i])
			{
				SetupGalleryBrush(i, InGallery[i]);
			}

			GalleryBox->AddSlot()
				.AutoWidth()
				.Padding(2.0f)
				[
					MakeGalleryThumbnail(i)
				];
		}
	}
}

FReply SArborCodexImagePanel::OnGenerateClicked()
{
	// Open the Concept Art Studio tab
	FGlobalTabmanager::Get()->TryInvokeTab(FArborConceptArtStudioTab::TabId);

	// Configure the studio widget with this codex entry
	TSharedPtr<SArborConceptArtStudioWidget> Widget = FArborConceptArtStudioTab::GetWidget();
	if (Widget.IsValid())
	{
		Widget->OpenForAsset(AssetPath);
	}

	return FReply::Handled();
}

FReply SArborCodexImagePanel::OnImportClicked()
{
	FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FOpenAssetDialogConfig Config;
	Config.AssetClassNames.Add(UTexture2D::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;
	Config.DialogTitleOverride = LOCTEXT("SelectImage", "Select Image");

	TArray<FAssetData> SelectedAssets = CBModule.Get().CreateModalOpenAssetDialog(Config);
	if (SelectedAssets.Num() == 0) return FReply::Handled();

	FString TexturePath = SelectedAssets[0].GetObjectPathString();

	bool bHasPrimary = PrimaryBrush.DrawAs == ESlateBrushDrawType::Image;
	if (!bHasPrimary)
	{
		UArborCodexImageTools::SetConceptArt(AssetPath, TexturePath, TEXT(""));
	}
	else
	{
		EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("SetAsPrimaryQ", "Set as primary concept art?\n\nYes = Replace primary image\nNo = Add to gallery"));

		if (Response == EAppReturnType::Yes)
		{
			UArborCodexImageTools::SetConceptArt(AssetPath, TexturePath, TEXT(""));
		}
		else
		{
			UArborCodexImageTools::AddGalleryImage(AssetPath, TexturePath);
		}
	}

	RefreshFromAsset();
	OnImageChangedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SArborCodexImagePanel::OnRemovePrimary()
{
	EAppReturnType::Type Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("RemovePrimaryQ", "Remove primary concept art?"));

	if (Response != EAppReturnType::Yes) return FReply::Handled();

	UArborCodexImageTools::SetConceptArt(AssetPath, TEXT(""), TEXT(""));
	RefreshFromAsset();
	OnImageChangedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

FReply SArborCodexImagePanel::OnRemoveGalleryImage(int32 Index)
{
	if (!GalleryTexturePaths.IsValidIndex(Index) || GalleryTexturePaths[Index].IsEmpty())
		return FReply::Handled();

	UArborCodexImageTools::RemoveGalleryImage(AssetPath, GalleryTexturePaths[Index]);
	RefreshFromAsset();
	OnImageChangedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

void SArborCodexImagePanel::OpenFullSizeViewer(UTexture2D* Texture)
{
	if (!Texture) return;

	PinnedTextures.AddUnique(Texture);
	Texture->AddToRoot();

	FullSizeViewerBrush.SetResourceObject(Texture);
	FullSizeViewerBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	FullSizeViewerBrush.DrawAs = ESlateBrushDrawType::Image;

	TSharedRef<SWindow> ViewerWindow = SNew(SWindow)
		.Title(FText::FromString(Texture->GetName()))
		.ClientSize(FVector2D(800, 600))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage)
				.Image(&FullSizeViewerBrush)
			]
		];

	FSlateApplication::Get().AddWindow(ViewerWindow);
}

void SArborCodexImagePanel::RefreshFromAsset()
{
	UObject* Asset = nullptr;
	{
		FSoftObjectPath SoftPath(AssetPath);
		Asset = SoftPath.TryLoad();
		if (!Asset && !AssetPath.Contains(TEXT(".")))
		{
			const FString ObjName = FPaths::GetBaseFilename(AssetPath);
			FSoftObjectPath FullPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *ObjName));
			Asset = FullPath.TryLoad();
		}
	}
	if (!Asset) return;

	// Get primary concept art via reflection
	UTexture2D* PrimaryTex = nullptr;
	{
		FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArt"));
		FSoftObjectProperty* SoftProp = Prop ? CastField<FSoftObjectProperty>(Prop) : nullptr;
		if (SoftProp)
		{
			const FSoftObjectPtr* SoftPtr = SoftProp->ContainerPtrToValuePtr<FSoftObjectPtr>(Asset);
			PrimaryTex = Cast<UTexture2D>(SoftPtr->LoadSynchronous());
		}
	}

	// Get gallery via reflection
	TArray<UTexture2D*> GalleryTextures;
	{
		FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArtGallery"));
		FArrayProperty* ArrProp = Prop ? CastField<FArrayProperty>(Prop) : nullptr;
		if (ArrProp)
		{
			FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Asset));
			for (int32 i = 0; i < Helper.Num(); i++)
			{
				const FSoftObjectPtr* ElemPtr = reinterpret_cast<const FSoftObjectPtr*>(Helper.GetRawPtr(i));
				UTexture2D* Tex = Cast<UTexture2D>(ElemPtr->LoadSynchronous());
				GalleryTextures.Add(Tex);
			}
		}
	}

	// Get prompt via reflection
	FString Prompt;
	{
		FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArtPrompt"));
		FStrProperty* StrProp = Prop ? CastField<FStrProperty>(Prop) : nullptr;
		if (StrProp)
		{
			Prompt = *StrProp->ContainerPtrToValuePtr<FString>(Asset);
		}
	}

	Refresh(PrimaryTex, GalleryTextures, Prompt);
}

#undef LOCTEXT_NAMESPACE
