# ShaderToy uniforms -> UE Custom-node inputs

ShaderToy's entry point is `void mainImage(out vec4 fragColor, in vec2 fragCoord)`. Translate
its implicit uniforms to graph nodes wired into named `custom_inputs` (don't hardcode them).

| ShaderToy | UE node -> custom_input | Notes |
|---|---|---|
| `fragCoord.xy / iResolution.xy` (normalized UV) | `MaterialExpressionTextureCoordinate` -> `UV` (float2) | The usual `uv` in ShaderToy. Most shaders start with `vec2 uv = fragCoord/iResolution.xy;` - just use the `UV` input directly. |
| `iResolution` (pixel size) | `VectorParameter` -> `Resolution`, or skip | Only needed if the shader uses absolute pixels. For aspect-correct UV: `UV * float2(Aspect, 1)`. |
| `iTime` | `MaterialExpressionTime` -> `Time` (float) | Seconds. Animates only in PIE/Realtime. |
| `iTimeDelta` | not available | Use a small constant if needed. |
| `iFrame` | not available | Approximate with `floor(Time * fps)`. |
| `iChannel0..3` (sampler2D) | `MaterialExpressionTextureObjectParameter` -> `Tex0..3` | Sample with `Texture2DSample(Tex0, Tex0Sampler, uv)`. Each texture input auto-gets a `<Name>Sampler`. |
| `iChannelResolution[n]` | `VectorParameter` if needed | Rarely used. |
| `iMouse` | `VectorParameter` -> `Mouse` (for testing) | No live mouse in a material. |
| `iDate`, `iSampleRate` | not available | Drop. |

## Standard skeleton

ShaderToy:
```glsl
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord/iResolution.xy;
    vec3 col = 0.5 + 0.5*cos(iTime + uv.xyx + vec3(0,2,4));
    fragColor = vec4(col, 1.0);
}
```

Becomes a Custom node (`OutputType: CMOT_Float3`, `custom_inputs: [UV, Time]`):
```hlsl
float3 col = 0.5 + 0.5 * cos(Time + float3(UV, UV.x) + float3(0,2,4));
return col;
```
plus `TextureCoordinate -> UV`, `Time -> Time`, output -> `EmissiveColor`, Unlit.

## Output
ShaderToy writes RGBA but materials are RGB here: drop alpha, route RGB to `EmissiveColor`
on an **Unlit** material. Multiply by ~2-8 for bloom on bright/neon shaders.
