"""Arbor gameplay tag helpers.

UE5's stock Python bindings can't construct an FGameplayTag from a string —
``unreal.GameplayTagLibrary.make_literal_gameplay_tag`` requires an already-
valid FGameplayTag, ``GameplayTag.tag_name`` is read-only, and
``UGameplayTagsManager::RequestGameplayTag`` is not exposed via reflection.
This module wraps Arbor's C++ ``UArborTagTools`` UFUNCTIONs so callers can
resolve, validate, and assign tags by string name.

Typical usage::

    import arbor.tags as tags

    # Resolve a single tag (raises if not registered):
    tag = tags.request("Quest.Gym.Main")

    # Set a tag UPROPERTY on a data asset:
    asset = unreal.EditorAssetLibrary.load_asset("/Game/Data/QuestProgression/DA_QP_GymMain")
    tags.set_on_object(asset, "QuestRootTag", "Quest.Gym.Main")

    # Set a tag inside an array element:
    tags.set_on_object(asset, "Branches.0.BranchTag", "Quest.Gym.Branch.Default")

    # Set a tag container:
    tags.set_container_on_object(asset, "Branches.0.GrantsTagsOnComplete",
                                  ["Quest.Gym.Main.Completed"])

    # Discover what's registered:
    print(tags.list(prefix="Quest.Gym"))
"""

import json

import unreal


class TagNotRegisteredError(ValueError):
    """Raised when a tag name doesn't resolve in the gameplay tags manager."""


def _check_module_loaded():
    """Sanity check: arbor.tags requires the C++ UArborTagTools class to be reflected.

    New UFUNCTIONs require a full editor restart after compile (LiveCoding cannot
    register new UFUNCTIONs). If this fails after restarting, the plugin didn't
    rebuild successfully.
    """
    if not hasattr(unreal, "ArborTagTools"):
        raise RuntimeError(
            "unreal.ArborTagTools is missing — rebuild the Arbor plugin and "
            "restart the editor (LiveCoding cannot register new UFUNCTIONs)."
        )


def is_registered(tag_name):
    """Return True iff ``tag_name`` is registered with the gameplay tags manager."""
    _check_module_loaded()
    return bool(unreal.ArborTagTools.is_tag_registered(unreal.Name(tag_name)))


def request(tag_name, error_if_not_found=True):
    """Resolve a registered gameplay tag by name and return the FGameplayTag.

    Args:
        tag_name: Full path string, e.g. ``"Quest.Gym.Main"``.
        error_if_not_found: If True (default), raises :class:`TagNotRegisteredError`
            when the tag isn't registered. If False, returns an empty/invalid tag.

    Returns:
        ``unreal.GameplayTag``. Empty when not registered and ``error_if_not_found``
        is False.
    """
    _check_module_loaded()
    tag = unreal.ArborTagTools.request_gameplay_tag(unreal.Name(tag_name), False)
    # FGameplayTag's "valid" check via the BP library — empty tags compare equal to
    # the default-constructed tag, which has empty tag_name.
    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        if error_if_not_found:
            raise TagNotRegisteredError(
                f"Tag '{tag_name}' is not registered with the gameplay tags manager. "
                f"Add it to DefaultGameplayTags.ini (or via a native module's StartupModule) "
                f"and restart the editor / reload tags."
            )
        return tag
    return tag


def set_on_object(target, property_path, tag_name):
    """Set a single FGameplayTag UPROPERTY on an object by name.

    Supports dotted paths into nested structs and arrays:

    - ``"QuestRootTag"`` — direct field on ``target``
    - ``"Branches.0.BranchTag"`` — array element member
    - ``"Some.Deeply.Nested.Tag"`` — arbitrarily nested

    Args:
        target: ``unreal.Object`` whose property to set.
        property_path: Dotted path string.
        tag_name: Tag to assign. Must be registered.

    Returns:
        Dict from the C++ helper: ``{success, message, resolved_tag}``.

    Raises:
        TagNotRegisteredError: If the tag isn't registered.
        RuntimeError: On property-resolution failure (path wrong, type mismatch, etc.).
    """
    _check_module_loaded()
    if not is_registered(tag_name):
        raise TagNotRegisteredError(f"Tag '{tag_name}' is not registered")

    raw = unreal.ArborTagTools.set_gameplay_tag_on_object(target, property_path, unreal.Name(tag_name))
    result = json.loads(raw)
    if not result.get("success"):
        raise RuntimeError(
            f"set_on_object('{property_path}', '{tag_name}') failed: {result.get('message')}"
        )
    return result


def set_container_on_object(target, property_path, tag_names):
    """Set a FGameplayTagContainer UPROPERTY by replacing its contents with the given tag list.

    Args:
        target: ``unreal.Object`` whose property to set.
        property_path: Dotted path to the FGameplayTagContainer field.
        tag_names: Iterable of tag-name strings.

    Returns:
        Dict: ``{success, message, resolved_tags, unresolved}``.

    Raises:
        TagNotRegisteredError: If any tag in the list isn't registered.
        RuntimeError: On property-resolution failure.
    """
    _check_module_loaded()
    names = [unreal.Name(t) for t in tag_names]
    raw = unreal.ArborTagTools.set_gameplay_tag_container_on_object(target, property_path, names)
    result = json.loads(raw)
    if not result.get("success"):
        unresolved = result.get("unresolved", [])
        if unresolved:
            raise TagNotRegisteredError(
                f"Tags not registered: {unresolved}. "
                f"Add them to DefaultGameplayTags.ini and reload tags."
            )
        raise RuntimeError(
            f"set_container_on_object('{property_path}', {list(tag_names)}) failed: "
            f"{result.get('message')}"
        )
    return result


def list(prefix=""):
    """Return a sorted list of every registered gameplay tag, optionally filtered by prefix.

    Args:
        prefix: Empty (default) returns all tags. Otherwise returns only tags whose
            full path starts with ``prefix`` (e.g. ``"Quest.Gym"``).

    Returns:
        List[str].
    """
    _check_module_loaded()
    raw = unreal.ArborTagTools.list_gameplay_tags(prefix)
    result = json.loads(raw)
    return result.get("tags", [])
