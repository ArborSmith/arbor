#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WidgetAnimationBuilder.generated.h"

class UWidgetBlueprint;
class UWidgetAnimation;
class UMovieScene;
class FJsonObject;

/**
 * Authoring of UMG widget animations (UWidgetAnimation) from designer-friendly
 * preset recipes, driven by the ue5-bridge `ue5_widget_animation` tool.
 *
 * Primary API is presets (fade/slide/pop/pulse/strikeoff). A thin low-level
 * AddAnimationTrack escape hatch exposes raw channel keyframes.
 *
 * Animations are stored on UWidgetBlueprint::Animations; after authoring the
 * blueprint is recompiled so each animation surfaces as a UWidgetAnimation*
 * variable referenceable from the event graph (PlayAnimation).
 */
UCLASS()
class ARBOR_API UWidgetAnimationBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Add (or extend) an animation from a preset spec.
	 *  JSON: { name, tracks: [ { preset, target, duration?, direction?, distance?, overshoot? }, ... ] }
	 *  Presets: fade_in, fade_out, slide_in, slide_out, pop, scale_in, pulse, strikeoff. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddAnimationFromPreset(const FString& AssetPath, const FString& PresetJsonString);

	/** List animations on a WidgetBlueprint with durations and bindings. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryAnimations(const FString& AssetPath);

	/** Remove an animation by name. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveAnimation(const FString& AssetPath, const FString& AnimationName);

	/** Low-level escape hatch: author a track with explicit channel keyframes.
	 *  JSON: { animation, target, track_type:"float"|"transform2d", property?, duration?,
	 *          channels:[ { component, keys:[ {t, v}, ... ] } ] }
	 *  component for transform2d: TranslationX|TranslationY|ScaleX|ScaleY|Rotation */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddAnimationTrack(const FString& AssetPath, const FString& TrackJsonString);

private:
	static UWidgetAnimation* CreateOrGetAnimation(UWidgetBlueprint* WBP, FName AnimName);

	/** Bind a widget by name; returns the possessable GUID (reuses an existing binding). */
	static FGuid BindWidget(UWidgetAnimation* Anim, UWidgetBlueprint* WBP, const FString& WidgetName);

	static void AddFloatTrack(UMovieScene* MS, const FGuid& Guid, const FString& PropertyName,
		float StartVal, float EndVal, float Dur);

	/** Key a single channel index (0..1) of a fresh 2D transform section. Channels:
	 *  0=TranslationX 1=TranslationY 2=Rotation 3=ScaleX 4=ScaleY (see impl). */
	static class UMovieScene2DTransformSection* AddTransformSection(UMovieScene* MS, const FGuid& Guid,
		uint32 ChannelMask, float Dur);

	static bool ApplyPreset(UWidgetBlueprint* WBP, UWidgetAnimation* Anim,
		const TSharedPtr<FJsonObject>& Track, float& OutMaxDur, FString& OutError);

	/** Create a thin strike-through Image overlay over a target text widget. */
	static FString CreateStrikeOverlay(UWidgetBlueprint* WBP, const FString& TargetName, FString& OutError);

	static void RecompileAndSave(UWidgetBlueprint* WBP);

	static FString MakeError(const FString& Message);
};
