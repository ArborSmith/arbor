---
name: material-authoring
description: Use this skill whenever the user asks for help creating, designing,
  or implementing a UE5 material. This includes requests phrased as "make me a
  material that looks like X", references to surface appearance, shader effects,
  or visual styles. Use this even when the user doesn't say "material"
  explicitly but is clearly describing a surface look ("how would I do wet
  asphalt", "I want a cel-shaded look"). MVP scope: natural-language
  descriptions only, legacy PBR materials. Image input and shader-code input
  fall through to a not-yet-implemented response.
---

# Material authoring workflow

You have a tagged catalog of reference materials at `<project>/MaterialCatalog/entries/*.yaml`. Each entry has a `BuildMaterial` spec, free-form tags, controlled `visual_traits`, and a description. The catalog plus Arbor's `arbor.materials.*` API is the whole pipeline.

Arbor conventions you already know are in `Plugins/Arbor/CLAUDE.md` (read once at session start). This skill covers material-specific knowledge.

## Step 1: identify input mode

- **Natural language description** -> proceed to Step 2
- **Reference image** -> reply: "image input isn't supported yet in v1; please describe what you want and I'll match against the catalog"
- **Shader code / ShaderToy URL** -> reply: "shader translation isn't supported yet in v1; let me know what visual effect you want and I'll find the closest catalog entry"

## Step 2: map the description to tags + traits

Read [<project>/MaterialCatalog/vocabulary.md](<project>/MaterialCatalog/vocabulary.md) so you're using the controlled vocabulary the retrieval script scores against. For the user's description, pick:

- 2-5 free-form `tags` (subject, state, aesthetic)
- 2-5 `visual_traits` from the controlled vocabulary
- A shading model hint when obvious (`DefaultLit`, `Unlit`, `Subsurface`, ...)

Be honest about what you can't infer - if the user says "metal" you know `metallic, low_roughness`; if they say "neon sign" you know `colored_emissive, opaque, stylized_cel` or `pbr_realistic` depending on context.

## Step 3: prefer a Material Instance when possible

Before deciding to author a graph, check whether the request fits the existing parameterized PBR base:

- The base material at `/Game/Materials/M_PBR_Parameterized` exposes parameters: `Albedo` / `Normal` / `Roughness` / `Metallic` / `AO` (textures) and `Tiling` (scalar).
- If the user wants a textured PBR surface with no special effects, a `MaterialInstance` of this base is faster, doesn't recompile, and is easier to iterate.

In that case: skip catalog retrieval entirely and call `arbor.materials.create_material_instance` with the textures the user has or wants imported. Mention in your response that this used the MI path.

If the request needs graph authoring (Custom HLSL, layered effects, unusual outputs), proceed to Step 4.

## Step 4: search the catalog

Run the retrieval script:

```bash
python scripts/search_catalog.py --tags "<tag1> <tag2>" --traits "<trait1> <trait2>" --top 3
```

It prints the top 3 matches with their scores.

The catalog holds two entry types:

- `reference_material` - a whole material with an inline `BuildMaterial` spec (the default; what you adapt and rebuild).
- `pattern` - a reusable **Material Function** referenced by `mf_path`, with `inputs`/`outputs` describing its interface. Compose these into a material instead of inlining them (see Step 5b).

Filter to one type with `--type reference_material` or `--type pattern`. Omit `--type` to see both. A `pattern` result carries `mf_path`, `inputs`, and `outputs` so you can wire it directly.

## Step 5: pick the best match

- One entry clearly matches -> use its `spec` as your starting point
- Multiple entries roughly match -> pick the closest; mention the alternatives in your response
- No entries match well -> tell the user the closest entry; ask whether to adapt it or go a different direction. Don't silently produce something wrong.

## Step 5b: compose patterns (Material Functions)

When the look decomposes into reusable pieces (an SDF shape for a UI fill, FBM noise driving colour variation, a posterize step for cel shading), reference `pattern` entries instead of inlining their subgraphs. To use a pattern in your material spec, add a `MaterialFunctionCall` expression pointing at its `mf_path`, then wire the function's named inputs/outputs:

```python
{"id": "circle", "class": "MaterialExpressionMaterialFunctionCall",
 "properties": {"MaterialFunction": "/Game/Assets/Materials/Functions/Procedural/MF_SDF_Circle"}},
```

Arbor's post-property hook calls `SetMaterialFunction()` so the call's input/output pins resolve by the function's `InputName`/`OutputName` (from the pattern entry's `inputs`/`outputs`). Connect into those pin names like any other node; one `MaterialFunctionCall` replaces a whole subgraph and stays in sync when the function is edited.

The procedural primitives live under `/Game/Assets/Materials/Functions/Procedural/` (SDF shapes, gradients, posterize, noise). Search them with `--type pattern`.

## Step 6: adapt the spec

Modify the chosen `spec` to match the request:

- Swap textures (`Texture` property on TextureSample/TextureSampleParameter2D expressions)
- Adjust colours (`Constant` on Constant3Vector expressions, `DefaultValue` on VectorParameter)
- Change roughness/metallic defaults (`DefaultValue` on ScalarParameter)
- Don't restructure the topology unless the request is fundamentally different - small parameter changes work better

Set the `path` to a sensible target like `/Game/Materials/M_<DescriptiveName>` and keep all sentinel `id` fields from the source so the build is idempotent.

## Step 7: build and report

Call:

```python
import arbor.materials as mat
result = mat.build_material(spec)
```

Then report:

- Where the material was created
- Which catalog entry was the starting point
- What you changed
- Which parameters the user can tweak via a Material Instance later (scalar/vector/texture parameters in the spec)

If the build fails, surface the error and propose a fix - probably a missing texture or an unsupported expression class.

## Authoring reusable primitives (Material Functions)

To add a new composable primitive to the catalog, author a `UMaterialFunction` then register it as a `pattern` entry:

```python
import arbor.materials as mat
mat.build_material_function({
    "path": "/Game/Assets/Materials/Functions/Procedural/MF_<Name>",
    "description": "...",
    "expose_to_library": True,
    "library_categories": ["Procedural"],
    "expressions": [
        {"id": "in_uv", "class": "MaterialExpressionFunctionInput",
         "properties": {"InputName": "UV", "InputType": "FunctionInput_Vector2", "SortPriority": 0}},
        {"id": "out", "class": "MaterialExpressionFunctionOutput",
         "properties": {"OutputName": "Result", "SortPriority": 0}},
        # ... math nodes ...
    ],
    "connections": [ {"from": "...", "to": "out", "to_input": ""} ],  # FunctionOutput input pin is unnamed
})
# then register it as a catalog pattern entry:
from extraction import extract_function
extract_function.extract("/Game/Assets/Materials/Functions/Procedural/MF_<Name>")
```

A function has no material-output pins or flags: its inputs are `MaterialExpressionFunctionInput` nodes and its outputs are `MaterialExpressionFunctionOutput` nodes. Use `mat.query_material_function(path)` to inspect an existing one, and `mat.list_expression_types("...")` / `mat.get_expression_class_params("...")` to discover node classes and pin names rather than guessing.

## Reference files

- [<project>/MaterialCatalog/vocabulary.md](<project>/MaterialCatalog/vocabulary.md) - tag and trait vocabulary
- [<project>/MaterialCatalog/entries/](<project>/MaterialCatalog/entries/) - the entries themselves
- [<project>/MaterialCatalog/README.md](<project>/MaterialCatalog/README.md) - entry shape + how to add new ones
- [references/pbr_quick.md](references/pbr_quick.md) - PBR defaults cheat sheet
- [references/shading_models.md](references/shading_models.md) - shading model selection
- [extraction/](extraction/) - extract_material / extract_function / extract_batch / validate_roundtrip scripts for growing the catalog

## Common mistakes

- **Authoring a graph when an MI would work.** If the user describes a textured PBR surface and nothing exotic, use `create_material_instance` of `M_PBR_Parameterized`. Faster, no recompile, easier to iterate.
- **Editing the source catalog material instead of creating a new one.** The catalog entry's `source` path is what was extracted; always set a new `path` in the spec before calling `build_material`.
- **Removing sentinel `id` fields when adapting a spec.** They make rebuilds idempotent. Keep them; only add new IDs for new expressions.
- **Hand-editing the `Desc` field of an expression in the editor.** That's where Arbor stores its sentinel IDs (`__arbor_id:<id>`). Editing them breaks idempotency.
