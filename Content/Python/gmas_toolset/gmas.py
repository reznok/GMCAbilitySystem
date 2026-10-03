"""GMAS authoring tools: create GMC abilities and effects, and register
abilities into an ability map data asset."""

import json

import unreal
import toolset_registry

_GAMEPLAY_TAGS_TOOLSET = "GameplayTagsToolset.GameplayTagsToolset"


@unreal.uclass()
class GMASToolset(unreal.ToolsetDefinition):
    """Authors GMC Ability System (GMAS) assets: Blueprint subclasses of
    UGMCAbility and UGMCAbilityEffect, and the ability-to-input mappings stored
    in a UGMCAbilityMapData asset. Use GameplayTagsToolset for tag listing and
    standalone tag management."""

    @toolset_registry.tool_call
    @staticmethod
    def create_gmc_ability(
            name: str,
            folder_path: str,
            ability_tag: unreal.GameplayTag,
            activation_required_tags: list[unreal.GameplayTag],
            activation_blocked_tags: list[unreal.GameplayTag],
            cooldown_time: float,
            cost_effect: unreal.Blueprint | None,
            create_missing_tags: bool) -> unreal.Blueprint:
        """Creates a Blueprint subclass of UGMCAbility, configured on its class defaults.

        Args:
            name: Asset name. A 'GA_' prefix is added if missing.
            folder_path: Destination content folder, e.g. '/Game/Combat/Abilities'.
            ability_tag: The ability's identity tag.
            activation_required_tags: Tags the owner must have for the ability to activate.
            activation_blocked_tags: Tags that, if present on the owner, block activation.
            cooldown_time: Cooldown in seconds. 0 for no cooldown.
            cost_effect: An effect Blueprint whose generated class becomes the ability's
                cost, or None for no cost.
            create_missing_tags: If true, any referenced tag that is not registered in the
                project is added before use; if false, an unregistered tag raises.

        Returns:
            The created ability Blueprint.
        """
        all_tags = [ability_tag] + list(activation_required_tags) + list(activation_blocked_tags)
        _ensure_registered(all_tags, create_missing_tags)

        blueprint = _create_blueprint(_with_prefix(name, "GA_"), folder_path, unreal.GMCAbility)
        cdo = _compiled_cdo(blueprint)

        cdo.set_editor_property("AbilityTag", _clean_tag(ability_tag))
        cdo.set_editor_property("ActivationRequiredTags", _container(activation_required_tags))
        cdo.set_editor_property("ActivationBlockedTags", _container(activation_blocked_tags))
        cdo.set_editor_property("CooldownTime", cooldown_time)
        if cost_effect is not None:
            cdo.set_editor_property("AbilityCost", cost_effect.generated_class())

        _save(blueprint)
        return blueprint

    @toolset_registry.tool_call
    @staticmethod
    def create_gmc_ability_effect(
            name: str,
            folder_path: str,
            effect_tag: unreal.GameplayTag,
            granted_tags: list[unreal.GameplayTag],
            effect_type: unreal.GMASEffectType,
            duration: float,
            periodic_interval: float,
            modifiers: list[unreal.GMCAttributeModifier],
            create_missing_tags: bool) -> unreal.Blueprint:
        """Creates a Blueprint subclass of UGMCAbilityEffect, configured on its
        EffectData class defaults.

        Args:
            name: Asset name. A 'GE_' prefix is added if missing.
            folder_path: Destination content folder, e.g. '/Game/Combat/Effects'.
            effect_tag: The effect's identity tag.
            granted_tags: Tags granted to the target while the effect is active.
            effect_type: Instant, Ticking, Persistent, or Periodic.
            duration: Lifetime in seconds for non-instant effects. 0 for instant/infinite
                per the effect type's semantics.
            periodic_interval: Seconds between applications for Periodic effects.
            modifiers: Attribute modifiers the effect applies. Each carries an attribute
                tag, value type, and value.
            create_missing_tags: If true, any referenced tag that is not registered in the
                project is added before use; if false, an unregistered tag raises.

        Returns:
            The created effect Blueprint.
        """
        all_tags = ([effect_tag] + list(granted_tags)
                    + [m.get_editor_property("attribute_tag") for m in modifiers])
        _ensure_registered(all_tags, create_missing_tags)

        blueprint = _create_blueprint(_with_prefix(name, "GE_"), folder_path, unreal.GMCAbilityEffect)
        cdo = _compiled_cdo(blueprint)

        effect_data = cdo.get_editor_property("EffectData")
        effect_data.set_editor_property("EffectTag", _clean_tag(effect_tag))
        effect_data.set_editor_property("GrantedTags", _container(granted_tags))
        effect_data.set_editor_property("EffectType", effect_type)
        effect_data.set_editor_property("Duration", duration)
        effect_data.set_editor_property("PeriodicInterval", periodic_interval)
        effect_data.set_editor_property("Modifiers", _clean_modifiers(modifiers))
        cdo.set_editor_property("EffectData", effect_data)

        _save(blueprint)
        return blueprint

    @toolset_registry.tool_call
    @staticmethod
    def register_ability(
            map_data: unreal.GMCAbilityMapData,
            input_tag: unreal.GameplayTag,
            abilities: list[unreal.Blueprint],
            granted_by_default: bool,
            mode: str,
            create_missing_tags: bool) -> unreal.GMCAbilityMapData:
        """Adds or updates the entry mapping an input tag to abilities in a
        UGMCAbilityMapData asset, then saves it.

        Args:
            map_data: The ability map data asset to modify.
            input_tag: The input tag the abilities are bound to.
            abilities: Ability Blueprints to bind. Their generated classes are stored.
            granted_by_default: Whether the entry is auto-granted to the owning component.
            mode: 'append' adds the abilities to the existing entry (creating it if absent);
                'merge' is append without duplicates; 'replace' overwrites the entry's
                ability list.
            create_missing_tags: If true and the input tag is not registered, it is added to
                the project before use; if false, an unregistered tag raises.

        Returns:
            The updated ability map data asset.
        """
        if mode not in ("append", "merge", "replace"):
            raise ValueError(f"mode must be 'append', 'merge', or 'replace', got '{mode}'.")
        _ensure_registered([input_tag], create_missing_tags)
        input_tag = _clean_tag(input_tag)

        new_classes = [bp.generated_class() for bp in abilities]
        entries = list(map_data.get_editor_property("AbilityMapData"))
        entry = next(
            (e for e in entries if e.get_editor_property("InputTag") == input_tag), None)
        if entry is None:
            entry = unreal.AbilityMapData()
            entry.set_editor_property("InputTag", input_tag)
            entries.append(entry)

        existing = list(entry.get_editor_property("Abilities"))
        if mode == "replace":
            result = new_classes
        elif mode == "merge":
            result = existing + [c for c in new_classes if c not in existing]
        else:  # append
            result = existing + new_classes

        entry.set_editor_property("Abilities", result)
        entry.set_editor_property("bGrantedByDefault", granted_by_default)
        map_data.set_editor_property("AbilityMapData", entries)

        unreal.EditorAssetLibrary.save_loaded_asset(map_data)
        return map_data


def _with_prefix(name: str, prefix: str) -> str:
    if not name:
        raise ValueError("name must not be empty.")
    return name if name.startswith(prefix) else prefix + name


def _create_blueprint(asset_name: str, folder_path: str, parent_class: type) -> unreal.Blueprint:
    if not folder_path:
        raise ValueError("folder_path must not be empty.")
    asset_path = f"{folder_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise RuntimeError(f"Asset already exists at {asset_path}.")

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, folder_path, unreal.Blueprint.static_class(), factory)
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Failed to create Blueprint at {asset_path}.")
    return blueprint


def _compiled_cdo(blueprint: unreal.Blueprint) -> unreal.Object:
    """Compiles the blueprint to stabilize its generated class, then returns the CDO.
    Properties must be set AFTER this; compiling again would discard CDO edits."""
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    return unreal.get_default_object(blueprint.generated_class())


def _save(blueprint: unreal.Blueprint) -> None:
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)


def _container(tags: list[unreal.GameplayTag]) -> unreal.GameplayTagContainer:
    return unreal.GameplayTagLibrary.make_gameplay_tag_container_from_array(list(tags))


def _clean_tag(tag: unreal.GameplayTag) -> unreal.GameplayTag:
    """Resolves a GameplayTag against the registry. ToolsetRegistry-deserialized tags
    carry the right name but are unresolved and do not survive being assigned to a scalar
    FGameplayTag property; round-tripping through a container yields a resolved tag.
    Returns the tag unchanged if it is empty/unregistered."""
    resolved = list(_container([tag]).get_editor_property("gameplay_tags"))
    return resolved[0] if resolved else tag


def _clean_modifiers(modifiers: list[unreal.GMCAttributeModifier]) -> list[unreal.GMCAttributeModifier]:
    result = []
    for m in modifiers:
        m.set_editor_property("attribute_tag", _clean_tag(m.get_editor_property("attribute_tag")))
        m.set_editor_property("value_as_attribute", _clean_tag(m.get_editor_property("value_as_attribute")))
        result.append(m)
    return result


def _ensure_registered(tags: list[unreal.GameplayTag], create_missing: bool) -> None:
    """Verifies each non-empty tag exists in the project; adds it via GameplayTagsToolset
    when create_missing is set, otherwise raises on the first unregistered tag."""
    known = set(_list_tags())
    for tag in tags:
        name = str(tag.get_editor_property("tag_name"))
        if not name or name == "None":
            continue
        if name in known:
            continue
        if not create_missing:
            raise RuntimeError(
                f"Gameplay tag '{name}' is not registered (set create_missing_tags to add it).")
        _add_tag(name)
        known.add(name)


def _list_tags() -> list[str]:
    result, error = unreal.ToolsetRegistry.execute_tool(
        _GAMEPLAY_TAGS_TOOLSET, "ListTags", json.dumps({"parentTag": ""}))
    if error:
        raise RuntimeError(f"Failed to list gameplay tags: {error}")
    return json.loads(result)["returnValue"]


def _add_tag(tag_name: str) -> None:
    """Adds a tag to the project by delegating to GameplayTagsToolset (no duplication)."""
    _result, error = unreal.ToolsetRegistry.execute_tool(
        _GAMEPLAY_TAGS_TOOLSET, "AddTag",
        json.dumps({"tagName": tag_name, "tagSource": ""}))
    if error:
        raise RuntimeError(f"Failed to add gameplay tag '{tag_name}': {error}")
