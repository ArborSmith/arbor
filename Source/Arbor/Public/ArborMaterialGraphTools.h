// Material graph editing primitives. Lower-level companion to UArborMaterialTools.
//
// UArborMaterialTools (existing) creates whole materials in one shot from typed
// params: CreateMaterial, CreateParameterizedPBRMaterial, etc. Those calls each
// recompile the material once and exit.
//
// UArborMaterialGraphTools (this file) operates on a material's expression graph
// the way UBlueprintBuilder operates on a Blueprint's event graph: add/remove
// individual nodes, set properties, connect pins. Each operation identifies
// expressions by a stable sentinel ID stored in the UMaterialExpression's Desc
// field as "__arbor_id:<id>". The sentinel survives editor save/reload.
//
// Phase 2 (BuildMaterial) batches many of these primitives in one
// FMaterialUpdateContext scope to amortise the recompile.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborMaterialGraphTools.generated.h"

UCLASS()
class ARBOR_API UArborMaterialGraphTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Read-only inspection (no recompile, no save) ----

	/**
	 * Dump a material's expression graph as JSON.
	 *
	 * @param MaterialPath  e.g. "/Game/Materials/M_Brick"
	 * @return JSON: {success, expressions:[{id, class, properties, x, y}],
	 *                connections:[{from_id, from_output, to_id, to_input}],
	 *                outputs:[{from_id, from_output, property}],
	 *                flags:{material_system, shading_model, blend_mode, ...}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString QueryMaterial(const FString& MaterialPath);

	/**
	 * List every UMaterialExpression subclass at runtime.
	 *
	 * @param Filter  Substring filter on class name (case-insensitive). Empty = all.
	 * @return JSON: {success, classes:["MaterialExpressionConstant", ...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString ListMaterialExpressionTypes(const FString& Filter);

	/**
	 * Reflect on an expression class to enumerate its editable properties.
	 *
	 * @param ClassName  e.g. "MaterialExpressionScalarParameter" (U-prefix optional)
	 * @return JSON: {success, class, properties:[{name, type, default?, enum_values?}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString GetMaterialExpressionClassParams(const FString& ClassName);

	// ---- Mutating primitives (mark dirty, deferred recompile) ----

	/**
	 * Add a new expression to a material's graph.
	 *
	 * @param ParamsJson  JSON: {material_path, expression_class, expression_id?,
	 *                          properties?, node_x?, node_y?}
	 *                    `expression_id` is optional - auto-generated if absent.
	 *                    `expression_class` accepts U-prefixed or bare name.
	 * @return JSON: {success, expression_id}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString AddMaterialExpression(const FString& ParamsJson);

	/**
	 * Remove an expression by its sentinel ID.
	 *
	 * @param MaterialPath  Material asset path.
	 * @param ExpressionId  The Arbor ID stamped into the expression's Desc field.
	 * @return JSON: {success}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString RemoveMaterialExpressionById(const FString& MaterialPath, const FString& ExpressionId);

	/**
	 * Set a single property on an existing expression via reflection.
	 *
	 * @param ParamsJson  JSON: {material_path, expression_id, property_name, value}
	 *                    `value` is interpreted per the property's FProperty type.
	 * @return JSON: {success}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString SetMaterialExpressionProperty(const FString& ParamsJson);

	/**
	 * Connect one expression's output to another expression's input pin.
	 *
	 * @param ParamsJson  JSON: {material_path, from_id, to_id, from_output?, to_input?}
	 *                    Empty `to_input` warns if target has multiple inputs.
	 * @return JSON: {success, warning?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString ConnectMaterialNodes(const FString& ParamsJson);

	/**
	 * Wire an expression's output to the material's final output property.
	 *
	 * @param ParamsJson  JSON: {material_path, expression_id, property, from_output?}
	 *                    `property` accepts: "BaseColor", "Normal", "Roughness",
	 *                    "Metallic", "EmissiveColor", "Opacity", "AmbientOcclusion",
	 *                    "WorldPositionOffset".
	 * @return JSON: {success}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString ConnectMaterialOutput(const FString& ParamsJson);

	/**
	 * Explicit terminal recompile + save. After a batch of granular edits,
	 * call this once to commit the changes to the asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString RecompileMaterialAsset(const FString& MaterialPath);

	// ---- Phase 2: BuildMaterial orchestrator ----

	/**
	 * Render a material's thumbnail to a PNG file on disk.
	 *
	 * @param ParamsJson  JSON: {material_path, output_path, width?, height?}
	 *                    width/height default to 256.
	 *                    output_path is absolute; parent dirs auto-created.
	 * @return JSON: {success, output_path, width, height}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString RenderMaterialThumbnail(const FString& ParamsJson);

	/**
	 * Build or update a complete material from a JSON spec. Idempotent by
	 * sentinel ID. Wraps all edits in FMaterialUpdateContext so the recompile
	 * happens once, not per-edit. Orphans (existing expressions in the asset
	 * not referenced by the spec) are removed.
	 *
	 * Spec schema:
	 * {
	 *   "path": "/Game/Materials/M_Foo",
	 *   "parent_class": "Material",            // currently only "Material"
	 *   "flags": {
	 *     "shading_model": "DefaultLit" | "Unlit" | "Subsurface" | ...,
	 *     "blend_mode": "Opaque" | "Masked" | "Translucent" | ...,
	 *     "two_sided": false
	 *   },
	 *   "expressions": [
	 *     {"id": "tc0", "class": "MaterialExpressionTextureCoordinate",
	 *      "properties": {...}, "x": -200, "y": 0},
	 *     ...
	 *   ],
	 *   "connections": [
	 *     {"from": "tc0", "from_output": "", "to": "bc_tex", "to_input": "UVs"},
	 *     ...
	 *   ],
	 *   "outputs": [
	 *     {"from": "bc_tex", "from_output": "RGB", "property": "BaseColor"},
	 *     ...
	 *   ]
	 * }
	 *
	 * @return JSON: {success, material_path, expression_count, connection_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString BuildMaterial(const FString& JsonSpec);

	// ---- Phase 7: Material Function authoring ----

	/**
	 * Dump a Material Function's expression graph as JSON. Mirrors QueryMaterial,
	 * but a function has no material-output pins: its outputs are
	 * FunctionOutput expressions and its inputs are FunctionInput expressions.
	 *
	 * @param FunctionPath  e.g. "/Game/Materials/Functions/MF_SDF_Circle"
	 * @return JSON: {success, function_path, description, expose_to_library,
	 *                library_categories:[...],
	 *                expressions:[{id, class, properties, x, y}],
	 *                connections:[{from_id, from_output, to_id, to_input}],
	 *                inputs:[{name, type, sort}], outputs:[{name, sort}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString QueryMaterialFunction(const FString& FunctionPath);

	/**
	 * Build or update a complete UMaterialFunction from a JSON spec. Idempotent
	 * by sentinel ID, exactly like BuildMaterial. Reuses the same expression /
	 * connection machinery; the differences from BuildMaterial are:
	 *   - the asset is a UMaterialFunction (UMaterialFunctionFactoryNew), not a UMaterial
	 *   - there is NO "outputs"/"flags"/"shading_model" block. A function's
	 *     outputs are MaterialExpressionFunctionOutput nodes and its inputs are
	 *     MaterialExpressionFunctionInput nodes; wire math into a FunctionOutput's
	 *     (unnamed) input pin via the normal "connections" array with empty to_input.
	 *   - finalised with UMaterialEditingLibrary::UpdateMaterialFunction (functions
	 *     don't self-recompile; referencing materials do).
	 *
	 * Spec schema:
	 * {
	 *   "path": "/Game/Materials/Functions/MF_SDF_Circle",
	 *   "description": "Signed distance to a circle; negative inside.",
	 *   "expose_to_library": true,
	 *   "library_categories": ["Procedural", "SDF"],
	 *   "expressions": [
	 *     {"id":"in_uv","class":"MaterialExpressionFunctionInput",
	 *      "properties":{"InputName":"UV","InputType":"FunctionInput_Vector2","SortPriority":0}},
	 *     {"id":"out_d","class":"MaterialExpressionFunctionOutput",
	 *      "properties":{"OutputName":"Distance","SortPriority":0}},
	 *     ...
	 *   ],
	 *   "connections": [
	 *     {"from":"len","from_output":"","to":"out_d","to_input":""}, ...
	 *   ]
	 * }
	 *
	 * @return JSON: {success, function_path, expression_count, connection_count,
	 *                inputs:[names], outputs:[names]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString BuildMaterialFunction(const FString& JsonSpec);

	// ---- Graph layout ----

	/**
	 * Auto-arrange a material's or material function's expression nodes into a
	 * readable left-to-right column layout, using UE's built-in
	 * LayoutMaterialExpressions / LayoutMaterialFunctionExpressions. Detects which
	 * asset type the path is. Layout only moves nodes (editor metadata), so no
	 * recompile happens. build_material / build_material_function also run this
	 * automatically at the end unless the spec sets "auto_layout": false.
	 *
	 * @param JsonParams  {"path": "/Game/.../M_Foo"}  ("material_path" also accepted)
	 * @return JSON: {success, path, type, expression_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|MaterialGraph")
	static FString LayoutMaterial(const FString& JsonParams);
};
