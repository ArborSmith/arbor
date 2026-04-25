"""Arbor — Python utilities for Unreal Engine 5 level building.

Part of the Arbor plugin.  Import submodules directly::

    import arbor.layout
    arbor.layout.make_room((0, 0, 0), 500, 500)

Or use the top-level convenience re-exports::

    from arbor import make_room, spawn_actor, setup_outdoor_scene
"""

__version__ = "1.0.0"

# ---------------------------------------------------------------------------
# Re-export public API for convenience
# ---------------------------------------------------------------------------

from arbor.utils import (
    find_actor_by_name,
    find_actors_by_class,
    get_all_actors,
    load_asset,
    save_asset,
    write_result,
    safe_run,
    spawn_actor,
    delete_actor,
    batch_delete,
    make_rotator,
)

from arbor.actors import (
    get_actor_info,
    set_actor_transform,
    rename_actor,
    duplicate_actor,
    set_actor_mobility,
    group_actors,
    get_actor_bounds,
    select_actors,
    get_selected_actors,
    snap_to_ground,
    snap_all_to_ground,
    snap_selected_to_ground,
)

from arbor.layout import (
    spawn_primitive,
    make_wall,
    make_floor,
    make_room,
    make_ramp,
    make_stairs,
)

from arbor.materials import (
    create_material,
    create_material_from_textures,
    create_material_instance,
    create_parameterized_pbr_material,
    create_world_aligned_material,
    ensure_pbr_base_material,
    assign_material,
    assign_material_by_name,
)

from arbor.lighting import (
    add_point_light,
    add_spot_light,
    add_directional_light,
    add_rect_light,
    add_sky_atmosphere,
    add_sky_light,
    add_exponential_height_fog,
    add_volumetric_cloud,
    add_post_process_volume,
    setup_outdoor_scene,
    setup_indoor_scene,
)

from arbor.terrain import (
    create_landscape,
    import_heightmap,
    create_flat_landscape,
    set_landscape_material,
    get_landscape_size,
    apply_heightmap,
    get_heightmap_data,
    create_rolling_hills,
    carve_river_valley,
    add_water_body_river,
    add_water_body_lake,
    setup_terrain_with_river,
    create_layer_info,
    add_layer_to_landscape,
    get_landscape_layers,
    set_layer_weights,
    get_layer_weights,
    setup_landscape_layers,
    paint_layer_circle,
    auto_paint_layers,
)

from arbor.scatter import (
    scatter_meshes,
    scatter_on_landscape,
    add_foliage_type,
    paint_foliage,
)

from arbor.foliage import (
    create_foliage_type,
    paint_foliage_in_bounds,
    paint_foliage_on_landscape,
    remove_foliage,
    list_foliage_types,
    scatter_grass,
    scatter_bushes,
    scatter_flowers,
    scatter_ground_cover,
)

from arbor.nav import (
    add_navmesh_volume,
    setup_ai_controller,
    add_eqs_context,
    create_eqs,
    create_eqs_find_patrol_point,
    create_eqs_find_cover,
    create_eqs_find_flank_position,
    create_eqs_find_nearest_player,
    create_eqs_find_attack_position,
    query_eqs,
    add_eqs_generator,
    remove_eqs_generator,
    set_eqs_generator_params,
    add_eqs_test,
    remove_eqs_test,
    set_eqs_test_params,
)

from arbor.blueprints import (
    create_character_bp,
    create_ai_controller_bp,
    create_game_mode_bp,
)

from arbor.mesh import (
    fix_mesh_pivot,
    fix_mesh_scale,
    fix_collision,
    disable_collision,
)

from arbor.vfx import (
    add_local_fog_volume,
    list_niagara_systems,
    spawn_niagara_system,
    list_decal_materials,
    spawn_decal,
    scatter_decals,
)

from arbor.structure import (
    build_from_plan,
    make_house,
    make_tower,
    make_castle,
    make_wall_segment,
    make_archway,
    delete_structure,
)

from arbor.capture import (
    take_screenshot,
    take_screenshot_from,
    take_screenshot_top_down,
    take_screenshot_orbit,
    show_image,
    show_last_screenshot,
)

from arbor.inspect import (
    inspect_actor,
    inspect_blueprint,
    inspect_asset,
    inspect_component,
    list_properties,
    find_property,
)

from arbor.textures import (
    show_texture_review,
    get_texture_review_result,
    import_texture,
    create_pbr_material,
    import_and_create_material,
    import_and_create_material_instance,
)

from arbor.concept_art import (
    import_concept_art,
    import_gallery_image,
)

from arbor.preview import (
    preview_textures,
    preview_asset,
    preview_material,
    preview_mesh,
    remove_preview_sphere,
)

from arbor.playtest import (
    start_pie,
    stop_pie,
    is_pie_running,
    teleport_player,
    look_at,
    screenshot_from_player,
    get_player_location,
    get_framerate,
    check_player_can_reach,
    walk_path,
    run_playtest,
)

from arbor.anchors import (
    analyze_mesh,
    get_anchor_metadata,
    set_anchor_metadata,
)

from arbor.environment import (
    validate_graph,
    resolve_graph,
    build_environment,
    clear_environment,
)

from arbor.input import (
    press_key,
    release_key,
    release_all_keys,
    tap_key,
    hold_key,
    look_direction,
    smooth_look,
    move_direction,
    jump,
    interact,
    get_held_keys,
)
