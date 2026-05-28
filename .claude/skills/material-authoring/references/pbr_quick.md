# PBR cheat sheet

Pick sensible defaults when adapting a catalog spec. The four core PBR inputs and what to put in them.

## Metallic

Almost always exactly 0 or exactly 1.

- **0** for non-metal (dielectric): wood, plastic, skin, fabric, dirt, paint, stone, concrete, brick
- **1** for metal: bare metal, chrome, gold, copper, brushed aluminum

Values in between are physically rare and look unsettled. The only common in-between case is a partially-painted metal surface, which is usually better modeled as two materials masked together.

## Roughness

- **0.0 - 0.1** - mirrors, polished chrome, water surface (calm)
- **0.1 - 0.3** - glossy plastics, wet surfaces, lacquered wood, polished metal
- **0.3 - 0.6** - satin finishes, slightly weathered metal, ceramic, eggshell paint
- **0.6 - 0.9** - matte plastics, fabric, dry stone, untreated wood, concrete
- **0.9 - 1.0** - chalk, completely diffuse - rarely the right answer; 0.85 usually reads better

When the user says "wet": drop roughness 0.3-0.5 below the dry value.
When they say "weathered": raise roughness toward 0.8+.

## BaseColor

Real-world surfaces rarely sit at the colour extremes:

- Skin: ~0.7 brightness, slight red tint, with subsurface scattering
- Coal: ~0.05 (dark but not black)
- Snow: ~0.95 (bright but not white)
- Charcoal: ~0.04
- Fresh white paint: ~0.85

For **metals**, BaseColor *is the tint of the reflection*, not a "diffuse colour":

- Gold: ~(1.0, 0.85, 0.5)
- Copper: ~(0.95, 0.65, 0.55)
- Silver: ~(0.95, 0.93, 0.88)
- Iron: ~(0.55, 0.55, 0.55)

If a user asks for "shiny black metal", the answer is dark BaseColor with metallic=1 and low roughness - NOT BaseColor 0 (which becomes a pure mirror with no tint).

## Normal

Tangent-space, RGB encoding XYZ. Default is `(0.5, 0.5, 1.0)` in 0-1 space (i.e. `(0, 0, 1)` in -1..1 space) meaning "pointing out of the surface."

Almost always sourced from a normal map texture, not hand-authored. When the catalog entry's spec has a `MaterialExpressionTextureSampleParameter2D` with `ParameterName: "Normal"` and `SamplerType: SAMPLERTYPE_Normal`, that's the normal map slot - the user can swap textures via a Material Instance later.

## Emissive

Watch the units. EmissiveColor inputs are HDR (can exceed 1.0). Common multipliers:

- A glow that's "barely there": multiplier 0.5 - 1.0
- Distinct emissive (LED, lit screen): 2 - 5
- "Looks bright like a real light": 10 - 100 (with bloom)
- The sun: 10000+

When adding emissive to an existing PBR material, the cleanest pattern is `BaseColor * EmissiveStrength` plumbed through a Multiply node into `EmissiveColor`, with `EmissiveStrength` exposed as a `ScalarParameter`.

## Opacity / Translucency

`Opacity` only does anything when the material's `blend_mode` is something other than `Opaque`. Choices:

- `Translucent` - sorted, no lighting on the translucent surface by default
- `Masked` - hard edges via the `OpacityMask` input + the material's `OpacityMaskClipValue`
- `Additive` - no depth interaction, brightens what's behind

Translucent materials are expensive and behave badly with shadows / SSR. Prefer `Masked` for hard edges (foliage, fences) and `Opaque` whenever possible.

## When in doubt

Pick the defaults from the catalog entry that came closest to the description. The vocabulary's `mid_roughness` is 0.5, `low_roughness` is 0.2, `high_roughness` is 0.75 - these are good fallbacks when the user is vague.
