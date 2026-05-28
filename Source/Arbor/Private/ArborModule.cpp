#include "ArborModule.h"
#include "ArborSettings.h"
#include "ArborRegistryHelper.h"
#include "ArborChatTab.h"
#include "ArborRegistryTab.h"
#include "ArborMaterialCatalogTab.h"
#include "ArborGameCodexTab.h"
#include "ArborTextureReviewTab.h"
#include "ArborTextureReviewWidget.h"
#include "ArborConceptArtStudioTab.h"
#include "ArborConceptArtStudioWidget.h"
#include "ArborTextVariationTab.h"
#include "ArborTextVariationWidget.h"
#include "ArborScreenshotTab.h"
#include "ArborImageViewerTab.h"
#include "ArborImageViewerWidget.h"
#include "BehaviorTreeBuilder.h"
#include "BlueprintBuilder.h"
#include "EQSBuilder.h"
#include "AnimBlueprintBuilder.h"
#include "LandscapeBuilder.h"
#include "PCGBuilder.h"
#include "PCGGraph.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Engine/Blueprint.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Framework/Docking/TabManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ILiveCodingModule.h"
#include "Containers/Ticker.h"
#include "ToolMenus.h"
#include "ISettingsModule.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "FArborModule"

// ---------------------------------------------------------------------------
// Shared utility: detect base64-encoded JSON vs file path
// ---------------------------------------------------------------------------
static bool TryDecodeBase64JSON(const FString& Input, FString& OutJsonString)
{
	// If input contains path-like characters it's a file path, not base64
	if (Input.Contains(TEXT("/")) || Input.Contains(TEXT("\\")) || Input.Contains(TEXT(".")))
	{
		return false;
	}

	TArray<uint8> DecodedBytes;
	if (!FBase64::Decode(Input, DecodedBytes))
	{
		return false;
	}
	DecodedBytes.Add(0); // Null-terminate
	OutJsonString = UTF8_TO_TCHAR(reinterpret_cast<const char*>(DecodedBytes.GetData()));
	return true;
}

void FArborModule::RegisterMenus()
{
	// All feature gates computed once, null-safe by construction — short-circuit
	// guarantees the Settings dereference never runs when Settings is null.
	const UArborSettings* Settings = GetDefault<UArborSettings>();
	const bool bExperimental         = Settings && Settings->bEnableExperimentalFeatures;
	const bool bEnableCodex          = bExperimental && Settings->bEnableCodex;
	const bool bEnableConceptArtStudio = bExperimental && Settings->bEnableConceptArtStudio;

	UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
	MainMenu->AddSubMenu(
		"MainMenu",
		NAME_None,
		"Arbor",
		LOCTEXT("ArborMenu", "Arbor"),
		LOCTEXT("ArborMenuTooltip", "Arbor AI Toolkit")
	);

	UToolMenu* ArborMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Arbor");
	FToolMenuSection& Section = ArborMenu->FindOrAddSection("Panels");

	Section.AddMenuEntry(
		"ArborChat",
		LOCTEXT("ArborChat", "Chat"),
		LOCTEXT("ArborChatTip", "Open the Arbor Chat panel"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborChatTab::TabId);
		}))
	);

	Section.AddMenuEntry(
		"ArborRegistry",
		LOCTEXT("ArborRegistry", "Registry"),
		LOCTEXT("ArborRegistryTip", "Open the Arbor asset registry viewer"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborRegistryTab::TabId);
		}))
	);

	Section.AddMenuEntry(
		"ArborMaterialCatalog",
		LOCTEXT("ArborMaterialCatalog", "Material Catalog"),
		LOCTEXT("ArborMaterialCatalogTip", "Browse and curate the project's tagged material catalog"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborMaterialCatalogTab::TabId);
		}))
	);

	if (bEnableCodex)
	{
		Section.AddMenuEntry(
			"ArborGameCodex",
			LOCTEXT("ArborGameCodex", "Game Codex"),
			LOCTEXT("ArborGameCodexTip", "Game design bible — contexts, locations, characters, features"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FArborGameCodexTab::TabId);
			}))
		);
	}

	Section.AddMenuEntry(
		"ArborTextureReview",
		LOCTEXT("ArborTextureReview", "Texture Review"),
		LOCTEXT("ArborTextureReviewTip", "Open the Arbor texture review window"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborTextureReviewTab::TabId);
		}))
	);

	if (bEnableConceptArtStudio)
	{
		Section.AddMenuEntry(
			"ArborConceptArtStudio",
			LOCTEXT("ArborConceptArtStudio", "Concept Art Studio"),
			LOCTEXT("ArborConceptArtStudioTip", "Unified concept art generation pipeline"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FArborConceptArtStudioTab::TabId);
			}))
		);

		Section.AddMenuEntry(
			"ArborTextVariation",
			LOCTEXT("ArborTextVariation", "Text Variation Review"),
			LOCTEXT("ArborTextVariationTip", "Open the Arbor text variation review window"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FArborTextVariationTab::TabId);
			}))
		);
	}

	Section.AddMenuEntry(
		"ArborScreenshots",
		LOCTEXT("ArborScreenshots", "Screenshots"),
		LOCTEXT("ArborScreenshotsTip", "Open the Arbor screenshot gallery"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborScreenshotTab::TabId);
		}))
	);

	Section.AddSeparator("ArborSeparator");

	Section.AddMenuEntry(
		"ArborSettings",
		LOCTEXT("ArborSettings", "Settings..."),
		LOCTEXT("ArborSettingsTip", "Open Arbor plugin settings"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings")
				.ShowViewer("Project", "Plugins", "Arbor");
		}))
	);
}

void FArborModule::StartupModule()
{
	// CoreRedirects for UArborSystemAsset → UArborFeatureAsset rename
	TArray<FCoreRedirect> Redirects;
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Arbor.ArborSystemAsset"),
		TEXT("/Script/Arbor.ArborFeatureAsset"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Property,
		TEXT("/Script/Arbor.ArborFeatureAsset.SystemName"),
		TEXT("/Script/Arbor.ArborFeatureAsset.FeatureName"));
	// Class redirects for removed types → FeatureAsset
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Arbor.ArborItemAsset"),
		TEXT("/Script/Arbor.ArborFeatureAsset"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Arbor.ArborFactionAsset"),
		TEXT("/Script/Arbor.ArborFeatureAsset"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Arbor.ArborLoreAsset"),
		TEXT("/Script/Arbor.ArborFeatureAsset"));
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Arbor.ArborAbilityAsset"),
		TEXT("/Script/Arbor.ArborFeatureAsset"));
	FCoreRedirects::AddRedirectList(Redirects, TEXT("Arbor"));

	// All feature gates computed once, null-safe by construction — short-circuit
	// guarantees the Settings dereference never runs when Settings is null.
	const UArborSettings* Settings = GetDefault<UArborSettings>();
	const bool bExperimental         = Settings && Settings->bEnableExperimentalFeatures;
	const bool bEnableCodex          = bExperimental && Settings->bEnableCodex;
	const bool bEnableConceptArtStudio = bExperimental && Settings->bEnableConceptArtStudio;

	FArborChatTab::Register();
	FArborRegistryTab::Register();
	FArborMaterialCatalogTab::Register();
	FArborTextureReviewTab::Register();
	FArborScreenshotTab::Register();
	FArborImageViewerTab::Register();

	if (bEnableCodex)
	{
		FArborGameCodexTab::Register();
	}
	if (bEnableConceptArtStudio)
	{
		FArborTextVariationTab::Register();
		FArborConceptArtStudioTab::Register();
	}

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FArborModule::RegisterMenus));

	// ========================================================================
	// Console commands — auto-detect base64-encoded JSON vs file path.
	// Usage: Arbor.BuildBT <base64_or_file_path> <asset_path>
	// ========================================================================

	BuildBTCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.BuildBT"),
		TEXT("Build a BehaviorTree from JSON. First arg can be a file path or base64-encoded JSON. Usage: Arbor.BuildBT <json_or_base64> <asset_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.BuildBT requires 2 arguments: <json_or_base64> <asset_path>"));
				return;
			}

			const FString& AssetPath = Args[1];
			FString JsonString;
			UBehaviorTree* Result;

			if (TryDecodeBase64JSON(Args[0], JsonString))
			{
				Result = UBehaviorTreeBuilder::BuildBehaviorTreeFromJSONString(JsonString, AssetPath);
			}
			else
			{
				Result = UBehaviorTreeBuilder::BuildBehaviorTreeFromJSON(Args[0], AssetPath);
			}

			if (Result)
			{
				UE_LOG(LogTemp, Log, TEXT("Arbor: Successfully built behavior tree at %s"), *AssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor: Failed to build behavior tree from %s"), *Args[0]);
			}
		}),
		ECVF_Default
	);

	BuildBPCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.BuildBP"),
		TEXT("Build a Blueprint from JSON. First arg can be a file path or base64-encoded JSON. Usage: Arbor.BuildBP <json_or_base64> <asset_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.BuildBP requires 2 arguments: <json_or_base64> <asset_path>"));
				return;
			}

			const FString& AssetPath = Args[1];
			FString JsonString;
			UBlueprint* Result;

			if (TryDecodeBase64JSON(Args[0], JsonString))
			{
				Result = UBlueprintBuilder::BuildBlueprintFromJSONString(JsonString, AssetPath);
			}
			else
			{
				Result = UBlueprintBuilder::BuildBlueprintFromJSON(Args[0], AssetPath);
			}

			if (Result)
			{
				UE_LOG(LogTemp, Log, TEXT("Arbor: Successfully built Blueprint at %s"), *AssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor: Failed to build Blueprint from %s"), *Args[0]);
			}
		}),
		ECVF_Default
	);

	BuildEQSCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.BuildEQS"),
		TEXT("Build an EQS query from JSON. First arg can be a file path or base64-encoded JSON. Usage: Arbor.BuildEQS <json_or_base64> <asset_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.BuildEQS requires 2 arguments: <json_or_base64> <asset_path>"));
				return;
			}

			const FString& AssetPath = Args[1];
			FString JsonString;
			UEnvQuery* Result;

			if (TryDecodeBase64JSON(Args[0], JsonString))
			{
				Result = UEQSBuilder::BuildEQSFromJSONString(JsonString, AssetPath);
			}
			else
			{
				Result = UEQSBuilder::BuildEQSFromJSON(Args[0], AssetPath);
			}

			if (Result)
			{
				UE_LOG(LogTemp, Log, TEXT("Arbor: Successfully built EQS query at %s"), *AssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor: Failed to build EQS query from %s"), *Args[0]);
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// Texture Review — open the texture review window with generated images.
	// Usage: Arbor.TextureReview <base64_json>
	// ========================================================================

	TextureReviewCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.TextureReview"),
		TEXT("Open the texture review window with AI-generated images. Arg is base64-encoded JSON or file path. Usage: Arbor.TextureReview <json_or_base64>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.TextureReview requires 1 argument: <json_or_base64>"));
				return;
			}

			FString JsonString;
			if (!TryDecodeBase64JSON(Args[0], JsonString))
			{
				// Treat as file path
				if (!FFileHelper::LoadFileToString(JsonString, *Args[0]))
				{
					UE_LOG(LogTemp, Error, TEXT("Arbor.TextureReview: Failed to read file: %s"), *Args[0]);
					return;
				}
			}

			// Open / focus the tab
			FGlobalTabmanager::Get()->TryInvokeTab(FArborTextureReviewTab::TabId);

			// Load images into the widget
			TSharedPtr<SArborTextureReviewWidget> Widget = FArborTextureReviewTab::GetWidget();
			if (Widget.IsValid())
			{
				Widget->LoadImages(JsonString);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.TextureReview: Widget not available"));
			}
		}),
		ECVF_Default
	);


	// ========================================================================
	// Concept Art Studio — open the unified concept art pipeline window.
	// Usage: Arbor.ConceptArtStudio [base64_json]
	// ========================================================================

	ConceptArtStudioCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.ConceptArtStudio"),
		TEXT("Open the Concept Art Studio. Optional arg is base64-encoded JSON or file path. Usage: Arbor.ConceptArtStudio [json_or_base64]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// Open / focus the tab
			FGlobalTabmanager::Get()->TryInvokeTab(FArborConceptArtStudioTab::TabId);

			if (Args.Num() < 1)
			{
				// No args — just open the tab
				return;
			}

			FString JsonString;
			if (!TryDecodeBase64JSON(Args[0], JsonString))
			{
				// Treat as file path
				if (!FFileHelper::LoadFileToString(JsonString, *Args[0]))
				{
					UE_LOG(LogTemp, Error, TEXT("Arbor.ConceptArtStudio: Failed to read file: %s"), *Args[0]);
					return;
				}
			}

			// Update the widget with state
			TSharedPtr<SArborConceptArtStudioWidget> Widget = FArborConceptArtStudioTab::GetWidget();
			if (Widget.IsValid())
			{
				Widget->UpdateFromState(JsonString);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.ConceptArtStudio: Widget not available"));
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// Text Variation — open the text variation review window.
	// Usage: Arbor.TextVariation <base64_json>
	// ========================================================================

	TextVariationCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.TextVariation"),
		TEXT("Open the text variation review window with AI-generated variations. Arg is base64-encoded JSON or file path. Usage: Arbor.TextVariation <json_or_base64>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.TextVariation requires 1 argument: <json_or_base64>"));
				return;
			}

			FString JsonString;
			if (!TryDecodeBase64JSON(Args[0], JsonString))
			{
				// Treat as file path
				if (!FFileHelper::LoadFileToString(JsonString, *Args[0]))
				{
					UE_LOG(LogTemp, Error, TEXT("Arbor.TextVariation: Failed to read file: %s"), *Args[0]);
					return;
				}
			}

			// Open / focus the tab
			FGlobalTabmanager::Get()->TryInvokeTab(FArborTextVariationTab::TabId);

			// Load variations into the widget
			TSharedPtr<SArborTextVariationWidget> Widget = FArborTextVariationTab::GetWidget();
			if (Widget.IsValid())
			{
				Widget->LoadVariations(JsonString);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.TextVariation: Widget not available"));
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// AnimGraph — set up locomotion AnimGraph in an AnimBlueprint.
	// Usage: Arbor.BuildAnimGraph <base64_json> <asset_path>
	// ========================================================================

	BuildAnimGraphCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.BuildAnimGraph"),
		TEXT("Set up AnimGraph in an AnimBlueprint. Args: <params_json_or_base64> <asset_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.BuildAnimGraph requires 2 arguments: <params_json_or_base64> <asset_path>"));
				return;
			}

			const FString& AssetPath = Args[1];
			FString JsonString;

			if (TryDecodeBase64JSON(Args[0], JsonString))
			{
				// Base64 decoded
			}
			else
			{
				// Treat as file path
				if (!FFileHelper::LoadFileToString(JsonString, *Args[0]))
				{
					UE_LOG(LogTemp, Error, TEXT("Arbor.BuildAnimGraph: Failed to read file: %s"), *Args[0]);
					return;
				}
			}

			FString Result = UAnimBlueprintBuilder::SetupLocomotionGraph(AssetPath, JsonString);
			UE_LOG(LogTemp, Log, TEXT("Arbor.BuildAnimGraph result: %s"), *Result);
		}),
		ECVF_Default
	);

	// ========================================================================
	// Preview — open assets in their editors for visual inspection.
	// Usage: Arbor.Preview <asset_path1> <asset_path2> ...
	// ========================================================================

	PreviewCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.Preview"),
		TEXT("Open one or more assets in UE5 editor tabs for side-by-side preview. Usage: Arbor.Preview <asset_path1> <asset_path2> ..."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.Preview requires at least 1 asset path"));
				return;
			}

			TArray<UObject*> Assets;
			for (const FString& Path : Args)
			{
				UObject* Asset = LoadObject<UObject>(nullptr, *Path);
				if (Asset)
				{
					Assets.Add(Asset);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Arbor.Preview: Asset not found: %s"), *Path);
				}
			}

			if (Assets.Num() > 0)
			{
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem)
				{
					AssetEditorSubsystem->OpenEditorForAssets(Assets);
					UE_LOG(LogTemp, Log, TEXT("Arbor.Preview: Opened %d asset(s) for preview"), Assets.Num());
				}
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// Registry debug window — open the asset registry viewer.
	// Usage: Arbor.Registry
	// ========================================================================

	RegistryCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.Registry"),
		TEXT("Open the Arbor asset registry debug window."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborRegistryTab::TabId);
		}),
		ECVF_Default
	);

	// ========================================================================
	// Screenshots — open the screenshot gallery.
	// Usage: Arbor.Screenshots
	// ========================================================================

	ScreenshotsCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.Screenshots"),
		TEXT("Open the Arbor screenshot gallery."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArborScreenshotTab::TabId);
		}),
		ECVF_Default
	);

	// ========================================================================
	// Live Coding — trigger C++ hot reload from Python / MCP.
	// Usage: Arbor.LiveCompile
	// ========================================================================

	LiveCompileCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.LiveCompile"),
		TEXT("Trigger a Live Coding compile (C++ hot reload). No arguments."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
			if (!LiveCoding)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.LiveCompile: Live Coding module not loaded"));
				return;
			}

			if (!LiveCoding->IsEnabledForSession())
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.LiveCompile: Live Coding is not enabled for this session"));
				return;
			}

			if (LiveCoding->IsCompiling())
			{
				UE_LOG(LogTemp, Warning, TEXT("Arbor.LiveCompile: A compile is already in progress"));
				return;
			}

			LiveCoding->Compile();
			UE_LOG(LogTemp, Log, TEXT("Arbor.LiveCompile: Compile triggered"));
		}),
		ECVF_Default
	);

	// ========================================================================
	// ShowImage — open a single image in the in-editor image viewer tab.
	// Usage: Arbor.ShowImage <absolute_path>
	// ========================================================================

	ShowImageCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.ShowImage"),
		TEXT("Open an image file in the in-editor viewer tab. Usage: Arbor.ShowImage <absolute_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.ShowImage requires 1 argument: <image_path>"));
				return;
			}

			// Args may be split on spaces — rejoin for paths with spaces
			FString ImagePath = FString::Join(Args, TEXT(" "));

			FGlobalTabmanager::Get()->TryInvokeTab(FArborImageViewerTab::TabId);

			TSharedPtr<SArborImageViewerWidget> Widget = FArborImageViewerTab::GetWidget();
			if (Widget.IsValid())
			{
				Widget->LoadImage(ImagePath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.ShowImage: Widget not available"));
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// PCG — Build a PCG Graph from JSON.
	// Usage: Arbor.BuildPCG <base64_or_file_path> <asset_path>
	// ========================================================================

	BuildPCGCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Arbor.BuildPCG"),
		TEXT("Build a PCG Graph from JSON. First arg can be a file path or base64-encoded JSON. Usage: Arbor.BuildPCG <json_or_base64> <asset_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor.BuildPCG requires 2 arguments: <json_or_base64> <asset_path>"));
				return;
			}

			const FString& AssetPath = Args[1];
			FString JsonString;
			UPCGGraph* Result;

			if (TryDecodeBase64JSON(Args[0], JsonString))
			{
				Result = UPCGBuilder::BuildPCGGraphFromJSONString(JsonString, AssetPath);
			}
			else
			{
				Result = UPCGBuilder::BuildPCGGraphFromJSON(Args[0], AssetPath);
			}

			if (Result)
			{
				UE_LOG(LogTemp, Log, TEXT("Arbor: Successfully built PCG graph at %s"), *AssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Arbor: Failed to build PCG graph from %s"), *Args[0]);
			}
		}),
		ECVF_Default
	);

	// ========================================================================
	// Asset registry dirty flag — fires on any asset add/remove so Python
	// can auto-rescan the registry before the next search.
	// ========================================================================
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddStatic(&UArborRegistryHelper::OnAssetChanged);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddStatic(&UArborRegistryHelper::OnAssetChanged);

	// Auto-scan: once all assets are discovered, trigger a Python registry rescan
	// so the disk cache is fresh before the user or MCP queries anything.
	AssetRegistry.OnFilesLoaded().AddLambda([]()
	{
		UArborRegistryHelper::MarkAssetRegistryDirty();
		UE_LOG(LogTemp, Log, TEXT("Arbor: Asset registry loaded — triggering auto-scan"));

		// Defer by one frame to ensure Python Script Plugin is fully ready.
		// NOTE: GEngine->Exec splits on semicolons, so we use __import__
		// to avoid multi-statement Python.
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([](float) -> bool
			{
				if (GEngine)
				{
					GEngine->Exec(nullptr,
						TEXT("py __import__('arbor.registry',fromlist=['scan_project']).scan_project()"));
				}
				return false; // one-shot
			}), 0.0f);
	});
}

void FArborModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FArborChatTab::Unregister();
	FArborRegistryTab::Unregister();
	FArborMaterialCatalogTab::Unregister();
	FArborGameCodexTab::Unregister();
	FArborTextureReviewTab::Unregister();
	FArborTextVariationTab::Unregister();
	FArborScreenshotTab::Unregister();
	FArborImageViewerTab::Unregister();
	FArborConceptArtStudioTab::Unregister();

	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
		AssetRegistry.OnAssetAdded().Remove(AssetAddedHandle);
		AssetRegistry.OnAssetRemoved().Remove(AssetRemovedHandle);
	}

	if (BuildBTCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BuildBTCommand);
		BuildBTCommand = nullptr;
	}

	if (BuildBPCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BuildBPCommand);
		BuildBPCommand = nullptr;
	}

	if (BuildEQSCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BuildEQSCommand);
		BuildEQSCommand = nullptr;
	}

	if (TextureReviewCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(TextureReviewCommand);
		TextureReviewCommand = nullptr;
	}

	if (TextVariationCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(TextVariationCommand);
		TextVariationCommand = nullptr;
	}

	if (PreviewCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PreviewCommand);
		PreviewCommand = nullptr;
	}

	if (BuildAnimGraphCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BuildAnimGraphCommand);
		BuildAnimGraphCommand = nullptr;
	}

	if (RegistryCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(RegistryCommand);
		RegistryCommand = nullptr;
	}

	if (LiveCompileCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LiveCompileCommand);
		LiveCompileCommand = nullptr;
	}

	if (ScreenshotsCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ScreenshotsCommand);
		ScreenshotsCommand = nullptr;
	}

	if (ShowImageCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ShowImageCommand);
		ShowImageCommand = nullptr;
	}

	if (BuildPCGCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BuildPCGCommand);
		BuildPCGCommand = nullptr;
	}

	if (ConceptArtStudioCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConceptArtStudioCommand);
		ConceptArtStudioCommand = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArborModule, Arbor)
