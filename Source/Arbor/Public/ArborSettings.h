#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ArborGameContextTypes.h"
#include "ArborSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Arbor"))
class ARBOR_API UArborSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UArborSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Arbor"); }

	UPROPERTY(EditAnywhere, Config, Category="Fab Marketplace",
		meta=(DisplayName="Show thumbnails in Fab search results",
			  ToolTip="Fetches preview images so you can see assets before choosing. Disable for faster text-only results."))
	bool bFabSearchThumbnails = true;

	/** Draw debug spheres and arrows at anchor points when analyzing meshes
	 *  or building environments. Useful for verifying anchor placement. */
	UPROPERTY(EditAnywhere, Config, Category="Anchors",
		meta=(DisplayName="Show anchor debug visualization",
			  ToolTip="Draw debug spheres and direction arrows at anchor points. Persists until flushed or editor restart."))
	bool bShowAnchorDebug = false;

	/** Duration in seconds for anchor debug draws. 0 = single frame, -1 = persistent. */
	UPROPERTY(EditAnywhere, Config, Category="Anchors",
		meta=(DisplayName="Anchor debug duration (seconds)",
			  ClampMin="-1", ClampMax="300",
			  ToolTip="How long anchor debug markers persist. -1 = until manually cleared. 0 = one frame."))
	float AnchorDebugDuration = -1.0f;

	/** Font size for chat message text. Increase for recording or streaming
	 *  so text is readable on smaller screens. Reopen the Chat tab after changing. */
	UPROPERTY(EditAnywhere, Config, Category="Chat",
		meta=(DisplayName="Chat font size",
			  ClampMin="8", ClampMax="32",
			  ToolTip="Font size for chat messages. Increase for recording/streaming. Reopen the Chat tab to apply."))
	int32 ChatFontSize = 11;

	/** Maximum context window size in tokens for the Claude model.
	 *  All current Claude models (Haiku, Sonnet, Opus) use 200k.
	 *  The CLI does not expose this value, so it must be set manually
	 *  if Anthropic releases a model with a different context size. */
	UPROPERTY(EditAnywhere, Config, Category="Chat",
		meta=(DisplayName="Max context tokens",
			  ClampMin="1000"))
	int32 MaxContextTokens = 200000;

	UPROPERTY(EditAnywhere, Config, Category="Character Generation",
		meta=(DisplayName="Default Game Context"))
	TSoftObjectPtr<UArborGameContextAsset> DefaultGameContext;

	/** Master switch for experimental Arbor features. When off, experimental MCP
	 *  tools are hidden from Claude and experimental UI panels are absent from
	 *  the Arbor menu. Turn on at your own risk — experimental features may
	 *  change or break without notice. */
	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Enable Experimental Features",
			  ToolTip="Master switch. When off, experimental tools and UI panels are hidden. Experimental features are unsupported and may change without notice."))
	bool bEnableExperimentalFeatures = false;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Game Codex",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="Design-bible system for game contexts, locations, characters, and features."))
	bool bEnableCodex = true;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Concept Art Studio",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="Unified concept art generation pipeline."))
	bool bEnableConceptArtStudio = true;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Environment",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="Anchor-based environment building workflow."))
	bool bEnableEnvironment = true;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Anchors",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="Anchor metadata and debug visualization on static meshes."))
	bool bEnableAnchors = true;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="PCG",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="Procedural Content Generation graph builders + landscape scattering."))
	bool bEnablePCG = true;

	UPROPERTY(EditAnywhere, Config, Category="Experimental Features",
		meta=(DisplayName="Widget (UMG)",
			  EditCondition="bEnableExperimentalFeatures",
			  ToolTip="UMG Widget Blueprint authoring + preset-driven UI animations (ue5_widget / ue5_widget_animation)."))
	bool bEnableWidget = true;

	/** Returns a JSON blob describing which feature flags are enabled. Called by
	 *  the ue5-bridge MCP server at boot via Remote Control API to decide which
	 *  tool categories to register. */
	UFUNCTION(BlueprintCallable, Category="Arbor|Features")
	FString GetEnabledFeaturesJson() const;
};
