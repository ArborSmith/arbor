#include "WidgetAnimationBuilder.h"
#include "WidgetBlueprintBuilder.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieScene2DTransformSection.h"

#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneFloatChannel.h"

#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborWidgetAnim, Log, All);

namespace
{
	FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& JsonString)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	// Standard Sequencer tick resolution and a 60fps display rate.
	const FFrameRate GTickRate(24000, 1);
	const FFrameRate GDisplayRate(60, 1);

	FFrameNumber FrameAt(float Seconds)
	{
		return GTickRate.AsFrameNumber((double)Seconds);
	}
}

FString UWidgetAnimationBuilder::MakeError(const FString& Message)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	UE_LOG(LogArborWidgetAnim, Warning, TEXT("WidgetAnimBuilder error: %s"), *Message);
	return SerializeJson(Obj);
}

// ============================================================================
// Animation + binding helpers
// ============================================================================

UWidgetAnimation* UWidgetAnimationBuilder::CreateOrGetAnimation(UWidgetBlueprint* WBP, FName AnimName)
{
	for (UWidgetAnimation* A : WBP->Animations)
	{
		if (A && A->GetFName() == AnimName) return A;
	}

	UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, AnimName, RF_Transactional);
	Anim->MovieScene = NewObject<UMovieScene>(Anim, AnimName, RF_Transactional);
	Anim->MovieScene->SetTickResolutionDirectly(GTickRate);
	Anim->MovieScene->SetDisplayRate(GDisplayRate);
#if WITH_EDITOR
	Anim->SetDisplayLabel(AnimName.ToString());
#endif
	WBP->Animations.Add(Anim);
	return Anim;
}

FGuid UWidgetAnimationBuilder::BindWidget(UWidgetAnimation* Anim, UWidgetBlueprint* WBP, const FString& WidgetName)
{
	UWidget* W = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!W) return FGuid();

	for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
	{
		if (B.WidgetName == W->GetFName()) return B.AnimationGuid;
	}

	const FGuid Guid = Anim->MovieScene->AddPossessable(W->GetName(), W->GetClass());

	FWidgetAnimationBinding Binding;
	Binding.WidgetName = W->GetFName();
	Binding.AnimationGuid = Guid;
	Binding.bIsRootWidget = (W == WBP->WidgetTree->RootWidget);
	Anim->AnimationBindings.Add(Binding);
	return Guid;
}

void UWidgetAnimationBuilder::AddFloatTrack(UMovieScene* MS, const FGuid& Guid, const FString& PropertyName,
	float StartVal, float EndVal, float Dur)
{
	UMovieSceneFloatTrack* Track = MS->AddTrack<UMovieSceneFloatTrack>(Guid);
	Track->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);

	UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
	Sec->SetRange(TRange<FFrameNumber>(FrameAt(0.f), FrameAt(Dur)));
	Track->AddSection(*Sec);

	FMovieSceneFloatChannel& Ch = Sec->GetChannel();
	Ch.AddCubicKey(FrameAt(0.f), StartVal);
	Ch.AddCubicKey(FrameAt(Dur), EndVal);
}

UMovieScene2DTransformSection* UWidgetAnimationBuilder::AddTransformSection(UMovieScene* MS, const FGuid& Guid,
	uint32 ChannelMask, float Dur)
{
	UMovieScene2DTransformTrack* Track = MS->AddTrack<UMovieScene2DTransformTrack>(Guid);
	Track->SetPropertyNameAndPath(FName(TEXT("RenderTransform")), TEXT("RenderTransform"));

	UMovieScene2DTransformSection* Sec = Cast<UMovieScene2DTransformSection>(Track->CreateNewSection());
	Sec->SetRange(TRange<FFrameNumber>(FrameAt(0.f), FrameAt(Dur)));
	Sec->SetMask(FMovieScene2DTransformMask((EMovieScene2DTransformChannel)ChannelMask));
	Track->AddSection(*Sec);
	return Sec;
}

// ============================================================================
// Strikeoff overlay
// ============================================================================

FString UWidgetAnimationBuilder::CreateStrikeOverlay(UWidgetBlueprint* WBP, const FString& TargetName, FString& OutError)
{
	UWidget* Target = WBP->WidgetTree->FindWidget(FName(*TargetName));
	if (!Target)
	{
		OutError = FString::Printf(TEXT("strikeoff target '%s' not found"), *TargetName);
		return FString();
	}

	int32 ChildIdx = 0;
	UPanelWidget* Parent = UWidgetTree::FindWidgetParent(Target, ChildIdx);
	if (!Parent)
	{
		OutError = FString::Printf(TEXT("strikeoff target '%s' has no parent panel"), *TargetName);
		return FString();
	}

	const FString StrikeName = TargetName + TEXT("_Strike");
	if (WBP->WidgetTree->FindWidget(FName(*StrikeName)))
	{
		return StrikeName; // already created
	}

	UImage* Strike = WBP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*StrikeName));
	Strike->bIsVariable = true;

	// Thin solid bar (RoundedBox renders tint as a filled rect without a texture).
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor::Black);
	Brush.SetImageSize(FVector2D(120.f, 3.f));
	Strike->SetBrush(Brush);

	// Grow from the left: pivot at left-center.
	Strike->SetRenderTransformPivot(FVector2D(0.f, 0.5f));

	UPanelSlot* Slot = Parent->AddChild(Strike);
	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Center);
	}

	WBP->OnVariableAdded(Strike->GetFName());
	return StrikeName;
}

// ============================================================================
// Preset application
// ============================================================================

bool UWidgetAnimationBuilder::ApplyPreset(UWidgetBlueprint* WBP, UWidgetAnimation* Anim,
	const TSharedPtr<FJsonObject>& Track, float& OutMaxDur, FString& OutError)
{
	FString Preset, Target;
	if (!Track->TryGetStringField(TEXT("preset"), Preset) || !Track->TryGetStringField(TEXT("target"), Target))
	{
		OutError = TEXT("track requires 'preset' and 'target'");
		return false;
	}

	double Duration = 0.0;
	const bool bHasDur = Track->TryGetNumberField(TEXT("duration"), Duration);

	UMovieScene* MS = Anim->MovieScene;

	auto Track2 = [&](float D) { OutMaxDur = FMath::Max(OutMaxDur, D); };

	if (Preset.Equals(TEXT("fade_in"), ESearchCase::IgnoreCase))
	{
		const float D = bHasDur ? (float)Duration : 0.35f;
		FGuid Guid = BindWidget(Anim, WBP, Target);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("target '%s' not found"), *Target); return false; }
		AddFloatTrack(MS, Guid, TEXT("RenderOpacity"), 0.f, 1.f, D);
		Track2(D);
		return true;
	}
	if (Preset.Equals(TEXT("fade_out"), ESearchCase::IgnoreCase))
	{
		const float D = bHasDur ? (float)Duration : 0.25f;
		FGuid Guid = BindWidget(Anim, WBP, Target);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("target '%s' not found"), *Target); return false; }
		AddFloatTrack(MS, Guid, TEXT("RenderOpacity"), 1.f, 0.f, D);
		Track2(D);
		return true;
	}
	if (Preset.Equals(TEXT("slide_in"), ESearchCase::IgnoreCase) || Preset.Equals(TEXT("slide_out"), ESearchCase::IgnoreCase))
	{
		const bool bIn = Preset.Equals(TEXT("slide_in"), ESearchCase::IgnoreCase);
		const float D = bHasDur ? (float)Duration : 0.35f;
		double Distance = 120.0;
		Track->TryGetNumberField(TEXT("distance"), Distance);
		FString Dir = TEXT("bottom");
		Track->TryGetStringField(TEXT("direction"), Dir);

		FVector2D Offset(0, 0);
		if (Dir.Equals(TEXT("left"), ESearchCase::IgnoreCase)) Offset = FVector2D(-Distance, 0);
		else if (Dir.Equals(TEXT("right"), ESearchCase::IgnoreCase)) Offset = FVector2D(Distance, 0);
		else if (Dir.Equals(TEXT("top"), ESearchCase::IgnoreCase)) Offset = FVector2D(0, -Distance);
		else Offset = FVector2D(0, Distance); // bottom

		FGuid Guid = BindWidget(Anim, WBP, Target);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("target '%s' not found"), *Target); return false; }

		UMovieScene2DTransformSection* Sec = AddTransformSection(MS, Guid,
			(uint32)EMovieScene2DTransformChannel::Translation, D);

		const FVector2D Start = bIn ? Offset : FVector2D(0, 0);
		const FVector2D End = bIn ? FVector2D(0, 0) : Offset;
		Sec->Translation[0].AddCubicKey(FrameAt(0.f), Start.X);
		Sec->Translation[0].AddCubicKey(FrameAt(D), End.X);
		Sec->Translation[1].AddCubicKey(FrameAt(0.f), Start.Y);
		Sec->Translation[1].AddCubicKey(FrameAt(D), End.Y);
		Track2(D);
		return true;
	}
	if (Preset.Equals(TEXT("pop"), ESearchCase::IgnoreCase) || Preset.Equals(TEXT("scale_in"), ESearchCase::IgnoreCase))
	{
		const float D = bHasDur ? (float)Duration : 0.3f;
		double Overshoot = Preset.Equals(TEXT("pop"), ESearchCase::IgnoreCase) ? 1.1 : 1.0;
		Track->TryGetNumberField(TEXT("overshoot"), Overshoot);

		FGuid Guid = BindWidget(Anim, WBP, Target);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("target '%s' not found"), *Target); return false; }

		UMovieScene2DTransformSection* Sec = AddTransformSection(MS, Guid,
			(uint32)EMovieScene2DTransformChannel::Scale, D);

		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			Sec->Scale[Axis].AddCubicKey(FrameAt(0.f), 0.f);
			if (Overshoot > 1.0)
			{
				Sec->Scale[Axis].AddCubicKey(FrameAt(D * 0.7f), (float)Overshoot);
			}
			Sec->Scale[Axis].AddCubicKey(FrameAt(D), 1.f);
		}
		Track2(D);
		return true;
	}
	if (Preset.Equals(TEXT("pulse"), ESearchCase::IgnoreCase))
	{
		const float D = bHasDur ? (float)Duration : 0.5f;
		double Peak = 1.08;
		Track->TryGetNumberField(TEXT("overshoot"), Peak);

		FGuid Guid = BindWidget(Anim, WBP, Target);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("target '%s' not found"), *Target); return false; }

		UMovieScene2DTransformSection* Sec = AddTransformSection(MS, Guid,
			(uint32)EMovieScene2DTransformChannel::Scale, D);
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			Sec->Scale[Axis].AddCubicKey(FrameAt(0.f), 1.f);
			Sec->Scale[Axis].AddCubicKey(FrameAt(D * 0.5f), (float)Peak);
			Sec->Scale[Axis].AddCubicKey(FrameAt(D), 1.f);
		}
		Track2(D);
		return true;
	}
	if (Preset.Equals(TEXT("strikeoff"), ESearchCase::IgnoreCase))
	{
		const float D = bHasDur ? (float)Duration : 0.4f;
		const FString StrikeName = CreateStrikeOverlay(WBP, Target, OutError);
		if (StrikeName.IsEmpty()) return false;

		FGuid Guid = BindWidget(Anim, WBP, StrikeName);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("strike overlay '%s' binding failed"), *StrikeName); return false; }

		UMovieScene2DTransformSection* Sec = AddTransformSection(MS, Guid,
			(uint32)EMovieScene2DTransformChannel::ScaleX, D);
		Sec->Scale[0].AddCubicKey(FrameAt(0.f), 0.f);
		Sec->Scale[0].AddCubicKey(FrameAt(D), 1.f);
		// keep Y at 1 so the bar retains its thickness
		Sec->Scale[1].AddCubicKey(FrameAt(0.f), 1.f);
		Track2(D);
		return true;
	}

	OutError = FString::Printf(TEXT("unknown preset '%s'"), *Preset);
	return false;
}

void UWidgetAnimationBuilder::RecompileAndSave(UWidgetBlueprint* WBP)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);
	UEditorAssetLibrary::SaveLoadedAsset(WBP, false);
}

// ============================================================================
// Public UFUNCTIONs
// ============================================================================

FString UWidgetAnimationBuilder::AddAnimationFromPreset(const FString& AssetPath, const FString& PresetJsonString)
{
	UWidgetBlueprint* WBP = UWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = ParseJson(PresetJsonString);
	if (!Root.IsValid()) return MakeError(TEXT("invalid JSON"));

	FString Name;
	if (!Root->TryGetStringField(TEXT("name"), Name)) return MakeError(TEXT("'name' required"));

	const TArray<TSharedPtr<FJsonValue>>* Tracks;
	if (!Root->TryGetArrayField(TEXT("tracks"), Tracks) || Tracks->Num() == 0)
		return MakeError(TEXT("'tracks' array required"));

	UWidgetAnimation* Anim = CreateOrGetAnimation(WBP, FName(*Name));

	float MaxDur = 0.f;
	for (const TSharedPtr<FJsonValue>& V : *Tracks)
	{
		const TSharedPtr<FJsonObject>& TrackObj = V->AsObject();
		if (!TrackObj.IsValid()) continue;
		FString Err;
		if (!ApplyPreset(WBP, Anim, TrackObj, MaxDur, Err))
		{
			return MakeError(Err);
		}
	}

	if (MaxDur <= 0.f) MaxDur = 0.35f;
	Anim->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FrameAt(MaxDur)), true);

	RecompileAndSave(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation"), Name);
	Result->SetNumberField(TEXT("duration"), MaxDur);
	return SerializeJson(Result);
}

FString UWidgetAnimationBuilder::QueryAnimations(const FString& AssetPath)
{
	UWidgetBlueprint* WBP = UWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Anims;
	for (UWidgetAnimation* A : WBP->Animations)
	{
		if (!A) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), A->GetName());
		Entry->SetNumberField(TEXT("end_time"), A->GetEndTime());

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FWidgetAnimationBinding& B : A->AnimationBindings)
		{
			TSharedRef<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("widget"), B.WidgetName.ToString());
			BObj->SetStringField(TEXT("guid"), B.AnimationGuid.ToString());
			Bindings.Add(MakeShared<FJsonValueObject>(BObj));
		}
		Entry->SetArrayField(TEXT("bindings"), Bindings);
		Entry->SetNumberField(TEXT("binding_count"), A->AnimationBindings.Num());
		Anims.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("animations"), Anims);
	return SerializeJson(Result);
}

FString UWidgetAnimationBuilder::RemoveAnimation(const FString& AssetPath, const FString& AnimationName)
{
	UWidgetBlueprint* WBP = UWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	int32 RemovedIdx = INDEX_NONE;
	for (int32 i = 0; i < WBP->Animations.Num(); ++i)
	{
		if (WBP->Animations[i] && WBP->Animations[i]->GetName() == AnimationName)
		{
			RemovedIdx = i;
			break;
		}
	}
	if (RemovedIdx == INDEX_NONE) return MakeError(FString::Printf(TEXT("animation '%s' not found"), *AnimationName));

	WBP->Animations.RemoveAt(RemovedIdx);
	RecompileAndSave(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return SerializeJson(Result);
}

FString UWidgetAnimationBuilder::AddAnimationTrack(const FString& AssetPath, const FString& TrackJsonString)
{
	UWidgetBlueprint* WBP = UWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Root = ParseJson(TrackJsonString);
	if (!Root.IsValid()) return MakeError(TEXT("invalid JSON"));

	FString AnimName, Target, TrackType;
	if (!Root->TryGetStringField(TEXT("animation"), AnimName) ||
		!Root->TryGetStringField(TEXT("target"), Target) ||
		!Root->TryGetStringField(TEXT("track_type"), TrackType))
	{
		return MakeError(TEXT("'animation', 'target', 'track_type' required"));
	}

	double Duration = 1.0;
	Root->TryGetNumberField(TEXT("duration"), Duration);

	UWidgetAnimation* Anim = CreateOrGetAnimation(WBP, FName(*AnimName));
	FGuid Guid = BindWidget(Anim, WBP, Target);
	if (!Guid.IsValid()) return MakeError(FString::Printf(TEXT("target '%s' not found"), *Target));

	UMovieScene* MS = Anim->MovieScene;

	const TArray<TSharedPtr<FJsonValue>>* Channels;
	if (!Root->TryGetArrayField(TEXT("channels"), Channels))
		return MakeError(TEXT("'channels' array required"));

	if (TrackType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		FString Property = TEXT("RenderOpacity");
		Root->TryGetStringField(TEXT("property"), Property);
		UMovieSceneFloatTrack* Track = MS->AddTrack<UMovieSceneFloatTrack>(Guid);
		Track->SetPropertyNameAndPath(FName(*Property), Property);
		UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
		Sec->SetRange(TRange<FFrameNumber>(FrameAt(0.f), FrameAt((float)Duration)));
		Track->AddSection(*Sec);
		FMovieSceneFloatChannel& Ch = Sec->GetChannel();
		const TSharedPtr<FJsonObject>& Ch0 = (*Channels)[0]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Keys;
		if (Ch0.IsValid() && Ch0->TryGetArrayField(TEXT("keys"), Keys))
		{
			for (const TSharedPtr<FJsonValue>& K : *Keys)
			{
				const TSharedPtr<FJsonObject>& KO = K->AsObject();
				if (!KO.IsValid()) continue;
				double T = 0, Val = 0;
				KO->TryGetNumberField(TEXT("t"), T);
				KO->TryGetNumberField(TEXT("v"), Val);
				Ch.AddCubicKey(FrameAt((float)T), (float)Val);
			}
		}
	}
	else if (TrackType.Equals(TEXT("transform2d"), ESearchCase::IgnoreCase))
	{
		UMovieScene2DTransformSection* Sec = AddTransformSection(MS, Guid,
			(uint32)EMovieScene2DTransformChannel::AllTransform, (float)Duration);
		for (const TSharedPtr<FJsonValue>& CV : *Channels)
		{
			const TSharedPtr<FJsonObject>& CO = CV->AsObject();
			if (!CO.IsValid()) continue;
			FString Component;
			CO->TryGetStringField(TEXT("component"), Component);
			FMovieSceneFloatChannel* Ch = nullptr;
			if (Component.Equals(TEXT("TranslationX"), ESearchCase::IgnoreCase)) Ch = &Sec->Translation[0];
			else if (Component.Equals(TEXT("TranslationY"), ESearchCase::IgnoreCase)) Ch = &Sec->Translation[1];
			else if (Component.Equals(TEXT("ScaleX"), ESearchCase::IgnoreCase)) Ch = &Sec->Scale[0];
			else if (Component.Equals(TEXT("ScaleY"), ESearchCase::IgnoreCase)) Ch = &Sec->Scale[1];
			else if (Component.Equals(TEXT("Rotation"), ESearchCase::IgnoreCase)) Ch = &Sec->Rotation;
			if (!Ch) continue;

			const TArray<TSharedPtr<FJsonValue>>* Keys;
			if (CO->TryGetArrayField(TEXT("keys"), Keys))
			{
				for (const TSharedPtr<FJsonValue>& K : *Keys)
				{
					const TSharedPtr<FJsonObject>& KO = K->AsObject();
					if (!KO.IsValid()) continue;
					double T = 0, Val = 0;
					KO->TryGetNumberField(TEXT("t"), T);
					KO->TryGetNumberField(TEXT("v"), Val);
					Ch->AddCubicKey(FrameAt((float)T), (float)Val);
				}
			}
		}
	}
	else
	{
		return MakeError(FString::Printf(TEXT("unknown track_type '%s'"), *TrackType));
	}

	Anim->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FrameAt((float)Duration)), true);
	RecompileAndSave(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animation"), AnimName);
	return SerializeJson(Result);
}
