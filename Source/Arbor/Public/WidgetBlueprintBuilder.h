#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WidgetBlueprintBuilder.generated.h"

class UWidgetBlueprint;
class UWidget;
class UPanelWidget;
class UWidgetTree;
class FJsonObject;
class FJsonValue;

/**
 * Authoring of UMG Widget Blueprints (the widget tree) from JSON, driven by the
 * ue5-bridge MCP `ue5_widget` tool. Mirrors UBlueprintBuilder: JSON in, JSON
 * string out, update-in-place (never delete-recreate, to preserve references).
 *
 * Event-graph editing is intentionally NOT here - a UWidgetBlueprint IS-A
 * UBlueprint, so node/pin/connect editing goes through UBlueprintBuilder.
 * Animations live in UWidgetAnimationBuilder.
 */
UCLASS()
class ARBOR_API UWidgetBlueprintBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Create a WidgetBlueprint subclass of a UUserWidget-derived class, optionally
	 *  building the whole widget tree in one call.
	 *  JSON: { name, parent_class, content_path?, tree?: [ widget_spec, ... ] }
	 *  Each widget_spec: { name, type, parent?, root?, is_variable?, properties?, slot_properties? }
	 *  @return JSON: { success, asset_path, bind_widgets:[...], error? } */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString BuildWidgetFromJSONString(const FString& JsonString, const FString& AssetPath);

	/** Return the widget tree (names, classes, parent, slot, is_variable), the
	 *  existing animation names, and the parent class's BindWidget/BindWidgetOptional
	 *  properties so the caller knows which exact names to use. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryWidget(const FString& AssetPath);

	/** Add one widget under a named parent panel.
	 *  WidgetJson: { name, type, parent?, root?, is_variable?, properties?, slot_properties? } */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddWidget(const FString& AssetPath, const FString& WidgetJsonString);

	/** Remove a widget (and its children) from the tree by name. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveWidget(const FString& AssetPath, const FString& WidgetName);

	/** Set properties on a widget. PropertyJson is an object of {PropertyName: value}.
	 *  Handles Text, brush image (FSlateBrush.image = texture path), colors, fonts,
	 *  Percent, Visibility, plus generic reflection. Use slot_properties via AddWidget
	 *  for slot fields, or set property "__slot" : {..} here to target the slot. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetWidgetProperty(const FString& AssetPath, const FString& WidgetName, const FString& PropertyJsonString);

	/** Set the tree root to an existing widget (usually a CanvasPanel/Overlay). */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetRootWidget(const FString& AssetPath, const FString& WidgetName);

	/** Runtime discovery of available UWidget classes (mirror list_component_types). */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString ListWidgetTypes(const FString& Filter);

	/** Compile + save the WidgetBlueprint. Surfaces "required widget binding is
	 *  missing" and other compile errors verbatim. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString CompileAndSaveWidget(const FString& AssetPath);

	// ---- Shared helpers (also used by UWidgetAnimationBuilder) ----

	/** Load an existing WidgetBlueprint asset for editing, or nullptr. */
	static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath);

	/** Resolve a UWidget subclass by short name ("TextBlock") or full path. */
	static UClass* ResolveWidgetClass(const FString& TypeName);

	/** Apply a JSON value to an FProperty, recursing into structs and arrays.
	 *  Special-cases FSlateBrush ({image, draw_as, image_size, tint, margin}). */
	static void ApplyJsonToProperty(FProperty* Prop, void* ValuePtr, const TSharedPtr<FJsonValue>& Json);

private:
	static UClass* ResolveParentWidgetClass(const FString& ClassName);

	static UWidgetBlueprint* CreateWidgetBlueprintAsset(const FString& Name, const FString& AssetPath, UClass* ParentClass);

	/** Construct a widget from a spec and parent it. Returns the new widget or nullptr. */
	static UWidget* CreateWidgetFromSpec(UWidgetBlueprint* WBP, const TSharedPtr<FJsonObject>& Spec, FString& OutError);

	static void ApplyProperties(UObject* Target, const TSharedPtr<FJsonObject>& Props);

	/** Report the parent class's meta=(BindWidget)/BindWidgetOptional object properties. */
	static TArray<TSharedPtr<FJsonValue>> CollectBindWidgetProperties(UClass* ParentClass);

	static bool SaveAsset(UObject* Asset);

	static FString MakeError(const FString& Message);
};
