"""Catalog extraction tooling.

These scripts convert in-engine UMaterial assets into YAML catalog entries
that round-trip cleanly through arbor.materials.build_material(spec).

Designed to run inside the UE5 editor's Python interpreter (call them from
ue5_run_python). Pure Python; depends on PyYAML.
"""
