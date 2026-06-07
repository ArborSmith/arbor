# GLSL -> HLSL (UE Custom node) translation table

Apply mechanically when porting GLSL/ShaderToy into a `MaterialExpressionCustom` body.

## Types
| GLSL | HLSL |
|---|---|
| `vec2` `vec3` `vec4` | `float2` `float3` `float4` |
| `ivec*` / `uvec*` | `int*` / `uint*` |
| `mat2` `mat3` `mat4` | `float2x2` `float3x3` `float4x4` |
| `bvec*` | `bool*` |

## Functions
| GLSL | HLSL |
|---|---|
| `mix(a,b,t)` | `lerp(a,b,t)` |
| `fract(x)` | `frac(x)` |
| `mod(a,b)` | `fmod(a,b)` - differs for negatives; use `a - b*floor(a/b)` to match GLSL |
| `inversesqrt(x)` | `rsqrt(x)` |
| `dFdx` `dFdy` | `ddx` `ddy` |
| `atan(y,x)` | `atan2(y,x)` |
| `texture(s,uv)` | `Texture2DSample(s, sSampler, uv)` |
| `textureLod(s,uv,l)` | `Texture2DSampleLevel(s, sSampler, uv, l)` |
| `clamp/min/max/abs/floor/ceil/...` | same names |

## Matrix math
- GLSL is column-major, HLSL row-major. `M * v` (GLSL) usually becomes `mul(v, M)` or `mul(M, v)` - if a rotation looks transposed, swap the `mul` operand order.
- Construct: `mat2(a,b,c,d)` -> `float2x2(a,b,c,d)`.

## Built-ins / globals
| GLSL | HLSL (UE Custom) |
|---|---|
| `gl_FragCoord.xy` | `Parameters.SvPosition.xy` (pixels) |
| `gl_FragColor` | the function's `return` value |
| swizzles (`.xyz`, `.rgb`) | identical |

## Gotchas
- Integer vs float division: `1/2` is `0`. Write `1.0/2.0`.
- HLSL has no implicit `int`->`float` in some ops; prefer float literals.
- Arrays declared `const float k[3] = float[](...)` (GLSL) -> `const float k[3] = {...};` (HLSL).
- No `#version`, no `precision` qualifiers - delete them.
- The Custom node `Code` is a function *body*: end with `return <OutputType-typed value>;`.
