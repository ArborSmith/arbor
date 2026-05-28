# Shading model selection

Pick based on what the surface physically is. Each shading model has a cost; pick the cheapest that fits.

## DefaultLit

The PBR workhorse. BaseColor / Metallic / Roughness / Normal. Use for:

- Anything opaque + non-organic: stone, metal, plastic, painted surfaces, concrete, brick
- Default fallback when nothing else fits

90% of materials end up here.

## Unlit

Receives no lighting. Just emits a colour. Use for:

- UI surfaces
- Holographic / volumetric-looking effects
- Sky materials
- Anything that needs to ignore world lighting on purpose
- Stylized cel shading (you compute lighting math manually in the graph)

Cheap, since the lighting equation is skipped entirely.

## Subsurface

Light enters the surface, bounces around internally, exits at a different point. Use for:

- Skin (the canonical case)
- Wax candles, jade, marble
- Translucent plastic
- Leaves (sometimes - `SubsurfaceProfile` model is often better for foliage)

The `SubsurfaceColor` input is the colour of the scattered light. Skin: warm red-orange. Jade: green.

## PreintegratedSkin

Cheaper Subsurface variant tuned specifically for faces. Use when shading a character's face if the more expensive Subsurface profile model isn't justified.

## ClearCoat

Two-layer: a clear glossy lacquer on top, your normal PBR underneath. Use for:

- Car paint
- Lacquered wood
- Plastic with a shiny coat over a textured base
- Some types of nail polish

`ClearCoat` (intensity), `ClearCoatRoughness`. The underlying material is configured normally; the clearcoat is added.

## Cloth

Tuned for fabric, with a fuzz term. Use for:

- Velvet, suede, fleece (where the surface has visible fibers)
- Carpet
- Fabrics with a specific "soft" sheen

`FuzzColor` and `Cloth` (intensity) extra inputs.

## Hair

Specific to hair strands rendered as cards or strands. Probably not what you want for a general material.

## Eye

Specifically for character eyes. Not what you want for general materials.

## Subsurface profile

Uses a separately-authored `SubsurfaceProfile` asset for the scatter parameters. Better quality than the basic Subsurface model. Use for:

- Detailed character skin
- High-quality marble / jade / etc.

Costs more.

## Single Layer Water

Volumetric water without the cost of the full Water Plugin. Use when you want a clean water surface that responds to depth.

## Two-sided foliage

Lighting from both sides; useful for leaves and other thin double-sided surfaces. Pair with `blend_mode: Masked` for cut-out shapes.

## Rule of thumb

If the user says "metal", "stone", "plastic", "wood": DefaultLit.
If they say "skin", "wax", "jade": Subsurface (or SubsurfaceProfile).
If they say "anime", "cel", "toon": Unlit (and you'll do the lighting math in-graph).
If they say "neon", "glowing screen", "UI": Unlit with emissive.
If they say "car paint": ClearCoat.
If they say "velvet" or "soft fabric": Cloth.
