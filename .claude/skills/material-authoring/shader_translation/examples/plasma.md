# Worked example: ShaderToy cosine-palette plasma

A minimal, animated ShaderToy shader translated end to end. Good template for the pattern.

## Source (ShaderToy)
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float t = iTime * 0.5;
    vec3 col = 0.5 + 0.5 * cos(t + uv.xyx * 6.0 + vec3(0.0, 2.0, 4.0));
    col *= 0.6 + 0.4 * sin(t + (uv.x + uv.y) * 10.0);
    fragColor = vec4(col, 1.0);
}
```

## Input mapping
- `uv` (`fragCoord/iResolution`) -> `TextureCoordinate` into a `UV` input
- `iTime` -> `Time` into a `Time` input
- no channels/uniforms; the `0.5`, `6.0`, `10.0` stay inline

## Translation (GLSL -> HLSL)
- `vec3` -> `float3`, `cos`/`sin` unchanged, `uv.xyx` -> `float3(UV, UV.x)`
- entry point dropped; the body returns a `float3`

## Build spec (verified: compiles clean, animates in PIE)
```python
import arbor.materials as mat
mat.build_material({
  "path": "/Game/.../M_FX_Plasma",
  "flags": {"shading_model": "MSM_Unlit", "blend_mode": "BLEND_Opaque"},
  "expressions": [
    {"id": "tc",   "class": "MaterialExpressionTextureCoordinate", "properties": {}},
    {"id": "time", "class": "MaterialExpressionTime", "properties": {}},
    {"id": "cust", "class": "MaterialExpressionCustom",
     "properties": {"Description": "cosine-palette plasma", "OutputType": "CMOT_Float3",
       "Code": "float t = Time * 0.5;\nfloat3 col = 0.5 + 0.5 * cos(t + float3(UV, UV.x) * 6.0 + float3(0.0, 2.0, 4.0));\ncol *= 0.6 + 0.4 * sin(t + (UV.x + UV.y) * 10.0);\nreturn col;"},
     "custom_inputs": [{"name": "UV"}, {"name": "Time"}]},
  ],
  "connections": [
    {"from": "tc",   "to": "cust", "to_input": "UV"},
    {"from": "time", "to": "cust", "to_input": "Time"},
  ],
  "outputs": [{"from": "cust", "property": "EmissiveColor"}],
  # auto_layout on by default -> graph opens tidy
})
```

## Validate
- `query_material(path).compile_errors` -> `[]`
- `render_thumbnail` shows the plasma palette (a still frame; it animates only in PIE/Realtime because of `Time`).

## Notes
- `cos`/`sin` over `Time` is the animation; multiply the result by ~2-6 if you want it to bloom.
- Because everything is wired through `UV`/`Time` inputs (not `Parameters.*`), the node is reusable and the inputs are swappable.
