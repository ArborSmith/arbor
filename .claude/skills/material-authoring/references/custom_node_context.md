# UE5 Custom Node (MaterialExpressionCustom) - compilation context

The Custom node is UE's escape hatch from the visual graph: raw HLSL embedded into the
generated material shader. Use it for math that is annoying or impossible to express as
discrete nodes (noise, SDFs, raymarches, custom BRDFs).

## Authoring via Arbor

In a `build_material` / `build_material_function` spec, a Custom node is one expression:

```json
{
  "id": "noise",
  "class": "MaterialExpressionCustom",
  "properties": {
    "Description": "FBM noise",
    "Code": "return fbm(UV * Scale);",
    "OutputType": "CMOT_Float1"
  },
  "custom_inputs": [ { "name": "UV" }, { "name": "Scale" } ]
}
```

- `Code`, `OutputType`, `Description` are normal `properties` (set via reflection).
- **`custom_inputs` is a TOP-LEVEL key on the expression**, not under `properties`. Each
  `{ "name": "X" }` becomes an input pin named `X` and an HLSL variable `X` in the body.
  Wire into it with the normal `connections` array (`to_input: "X"`).
- `OutputType`: `CMOT_Float1` / `CMOT_Float2` / `CMOT_Float3` / `CMOT_Float4`, or
  `CMOT_MaterialAttributes` for the multi-output case (advanced).
- The `Code` string is a function *body* - it must `return` a value of the `OutputType`.

## Variable scope inside the Code body

Your code runs inside a generated function with these implicitly available:

- `Parameters` - `FMaterialPixelParameters`:
  - `Parameters.WorldPosition` - float3 world-space position
  - `Parameters.SvPosition` - float4 screen-space position
  - `Parameters.TangentToWorld[2]` - float3 world-space surface normal
  - `Parameters.TexCoords[N]` - float2 UVs at index N
  - `Parameters.VertexColor` - float4 vertex colour
  - `Parameters.CameraVector` - float3 from surface to camera
- `View` - global view state (`View.GameTime`, `View.WorldCameraOrigin`, ...)

Prefer wiring data in as a named `custom_input` (a `TexCoordinate`, `Time`, parameter, etc.)
over reaching into `Parameters` - it keeps the node reusable and the inputs swappable.

## Texture sampling (common failure)

```hlsl
// WRONG (GLSL / D3D9):  tex2D(MyTex, uv)   texture(MyTex, uv)
// RIGHT (UE5 Custom):
Texture2DSample(MyTex, MyTexSampler, uv)
```

Every Texture custom_input automatically gets a `<Name>Sampler` companion.

## GLSL -> HLSL quick map (see shader_translation/references for the full table)

| GLSL | HLSL |
|---|---|
| `vec2/3/4` | `float2/3/4` |
| `mat3/4` | `float3x3/4x4` |
| `mix(a,b,t)` | `lerp(a,b,t)` |
| `fract(x)` | `frac(x)` |
| `mod(a,b)` | `fmod(a,b)` (differs on negatives; use `a - b*floor(a/b)` to match GLSL) |
| `dFdx/dFdy` | `ddx/ddy` |
| `inversesqrt(x)` | `rsqrt(x)` |
| `texture(s,uv)` | `Texture2DSample(s, sSampler, uv)` |

## Limitations

- One output by default. Multiple outputs need `OutputType: CMOT_MaterialAttributes` +
  the `AdditionalOutputs` array (advanced).
- Loops are unrolled at compile time - heavy loops compile slowly and run badly.
- No recursion; non-uniform dynamic branching compiles but performs poorly.

## Validating a Custom node

A material that renders as the default lit-grey sphere FAILED to compile. Read
`query_material(path).compile_errors` for the HLSL error (e.g. an undeclared identifier
means you referenced a `custom_input` name that isn't wired). Recompile first if you just
edited it. Don't guess from screenshots.
