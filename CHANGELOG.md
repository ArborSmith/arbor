# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] — Initial public release

First public release. Early stage — APIs unstable, not production ready.

- 11 stable MCP categories: actors, blueprint, AI, terrain, materials,
  lighting, foliage, mesh, assets, capture, playtest
- 5 experimental categories (gated behind `bEnableExperimentalFeatures`):
  codex, environment, anchors, concept_art_studio, pcg
- Editor plugin (C++) + Python utility library (`arbor.*`) + TypeScript
  MCP bridge server (`bridge/`)
- See `README.md` for setup, `CLAUDE.md` for the plugin reference, and
  `bridge/CLAUDE.md` for bridge architecture details.
