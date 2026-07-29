"""
Deploy-FaceParallax-Assets.py

Runs INSIDE the Unreal Editor (Python console, or headless via
UnrealEditor-Cmd.exe -run=pythonscript -script="...") and creates the
binary editor assets that PowerShell/text-file scripts cannot touch:

  1. M_FaceParallax_Master material, with parameters created AND wired
     (lerp between current/prev textures driven by StateBlendAlpha,
     panner-style UV offset driven by ParallaxOffset).
  2. One Material Instance per face layer, parented to the master.
  3. FaceParallaxPreset Data Asset, with ViewAssignments populated as a
     TMap<EFaceAngleState, FFaceViewStateLayerSet> for every state.
  4. BP_FaceParallaxCharacter Blueprint (parent: Character), with a
     FaceParallaxComponent added, HeadBoneName set, and LayerDefinitions
     populated to match the layers created above.

Run this AFTER the C++ source has been copied into your project and
the project has been compiled at least once (the component classes must
exist for the Blueprint step to find them).

USAGE (in-editor Python console):
    exec(open(r"D:\Projects\MyRPG\Deploy-FaceParallax-Assets.py").read())

USAGE (headless, from command line):
    UnrealEditor-Cmd.exe "D:\Projects\MyRPG\MyRPG.uproject" -run=pythonscript -script="Deploy-FaceParallax-Assets.py"

Edit the CONFIG block below before running.
"""

import unreal

# =========================== CONFIG ===========================

CONTENT_ROOT = "/Game/FaceParallax"

# Layers -> (LayerTag, DepthScale, DepthMapIntensity, bInvertParallax)
LAYERS = [
    ("Eyes",  0.5, 1.0, False),
    ("Brows", 0.4, 1.0, False),
    ("Mouth", 0.6, 1.0, False),
    ("Hair",  0.8, 1.0, True),
]

# All view states the preset needs an assignment for.
# Must match EFaceAngleState enum names exactly.
VIEW_STATES = [
    "Front", "ThreeQuarterRight", "RightProfile", "BackRight", "Back",
    "BackLeft", "LeftProfile", "ThreeQuarterLeft", "Top", "Bottom",
]

HEAD_BONE_NAME = "head"
CHARACTER_PARENT_CLASS = unreal.Character  # change to unreal.Pawn/Actor if needed
COMPONENT_CLASS_NAME = "FaceParallaxComponent"      # must exist post C++ compile
PRESET_CLASS_NAME = "FaceParallaxPreset"            # must exist post C++ compile

# ================================================================

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary


def ensure_dir(path):
    if not editor_asset_lib.does_directory_exist(path):
        editor_asset_lib.make_directory(path)


def find_class(name):
    """Resolve a UCLASS by name (works for native C++ classes once compiled)."""
    cls = unreal.load_class(None, f"/Script/{unreal.SystemLibrary.get_project_name()}.{name}")
    if cls:
        return cls
    # fallback: search common module-name guesses
    for module in ["Game", "GameCore"]:
        cls = unreal.load_class(None, f"/Script/{module}.{name}")
        if cls:
            return cls
    raise RuntimeError(
        f"Could not resolve class '{name}'. Make sure the C++ module compiled "
        f"successfully (the project must have been built at least once)."
    )


# --------------------------------------------------------------
# 1. Master Material
# --------------------------------------------------------------
def create_master_material():
    mat_path = f"{CONTENT_ROOT}/Materials"
    ensure_dir(mat_path)

    mat_name = "M_FaceParallax_Master"
    full_path = f"{mat_path}/{mat_name}"

    if editor_asset_lib.does_asset_exist(full_path):
        unreal.log(f"[SKIP] {full_path} already exists")
        return unreal.load_asset(full_path)

    factory = unreal.MaterialFactoryNew()
    material = asset_tools.create_asset(mat_name, mat_path, unreal.Material, factory)

    mel = unreal.MaterialEditingLibrary

    def tex_param(name, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        return node

    def scalar_param(name, x, y, default=0.0):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", default)
        return node

    def vec_param(name, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
        node.set_editor_property("parameter_name", name)
        return node

    def static_switch(name, x, y, default=False):
        node = mel.create_material_expression(material, unreal.MaterialExpressionStaticBoolParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", default)
        return node

    # --- Parameters (mirrors the checklist's parameter list) ---
    albedo_cur   = tex_param("AlbedoTexture",     -600, -300)
    albedo_prev  = tex_param("AlbedoTexturePrev",  -600, -150)
    normal_cur   = tex_param("NormalTexture",      -600,    0)
    normal_prev  = tex_param("NormalTexturePrev",  -600,  150)
    depth_cur    = tex_param("DepthTexture",       -600,  300)
    depth_prev   = tex_param("DepthTexturePrev",   -600,  450)

    blend_alpha  = scalar_param("StateBlendAlpha", -600, 600, 0.0)
    parallax_off = vec_param("ParallaxOffset",     -600, 750)
    art_pos      = vec_param("ArtPosition",         -600, 900)
    art_scale    = vec_param("ArtScale",             -600, 1050)
    art_rot      = scalar_param("ArtRotation",       -600, 1200, 0.0)
    depth_int    = scalar_param("DepthIntensity",    -600, 1350, 1.0)
    debug_depth  = static_switch("DebugDepth",       -600, 1500, False)
    is_topdown   = static_switch("IsTopDown",        -600, 1650, False)
    is_topview   = static_switch("IsTopView",        -600, 1800, False)

    art_pivot    = vec_param("ArtPivot",              -600, 1950)
    nested_frame = scalar_param("NestedAnimFrame",    -600, 2100, 0.0)

    expr_alpha   = scalar_param("ExpressionBlendAlpha", -600, 2250, 0.0)
    expr_alb_prev = tex_param("ExpressionAlbedoPrev",  -600, 2400)
    expr_nrm_prev = tex_param("ExpressionNormalPrev",  -600, 2550)
    expr_dep_prev = tex_param("ExpressionDepthPrev",   -600, 2700)

    # --- UV: pan by ParallaxOffset scaled with ArtPosition/ArtScale ---
    texcoord = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, -300)
    uv_add_pos = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -850, -300)
    mel.connect_material_expressions(texcoord, "", uv_add_pos, "A")
    mel.connect_material_expressions(art_pos, "", uv_add_pos, "B")

    uv_mul_scale = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -750, -300)
    mel.connect_material_expressions(uv_add_pos, "", uv_mul_scale, "A")
    mel.connect_material_expressions(art_scale, "", uv_mul_scale, "B")

    uv_final = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -680, -280)
    mel.connect_material_expressions(uv_mul_scale, "", uv_final, "A")
    mel.connect_material_expressions(parallax_off, "", uv_final, "B")

    for tex_node in (albedo_cur, albedo_prev, normal_cur, normal_prev, depth_cur, depth_prev):
        mel.connect_material_expressions(uv_final, "", tex_node, "UVs")

    # --- Crossfade current/prev per channel via StateBlendAlpha ---
    def lerp(a, b, alpha, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", node, "A")
        mel.connect_material_expressions(b, "", node, "B")
        mel.connect_material_expressions(alpha, "", node, "Alpha")
        return node

    albedo_blend = lerp(albedo_prev, albedo_cur, blend_alpha, -300, -250)
    normal_blend = lerp(normal_prev, normal_cur, blend_alpha, -300, 50)
    depth_blend  = lerp(depth_prev, depth_cur, blend_alpha, -300, 350)

    depth_scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -150, 350)
    mel.connect_material_expressions(depth_blend, "", depth_scaled, "A")
    mel.connect_material_expressions(depth_int, "", depth_scaled, "B")

    # DebugDepth switch: show raw depth in place of albedo when enabled
    debug_switch = mel.create_material_expression(material, unreal.MaterialExpressionStaticSwitch, 50, -100)
    mel.connect_material_expressions(debug_depth, "", debug_switch, "Value")
    mel.connect_material_expressions(depth_scaled, "", debug_switch, "True")
    mel.connect_material_expressions(albedo_blend, "", debug_switch, "False")

    mel.connect_material_property(material, debug_switch, unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(material, normal_blend, unreal.MaterialProperty.MP_NORMAL)

    mel.recompile_material(material)
    editor_asset_lib.save_asset(full_path)
    unreal.log(f"[OK] Created {full_path} with wired parameter graph")
    return material


# --------------------------------------------------------------
# 2. Material Instances per layer
# --------------------------------------------------------------
def create_material_instances(master_material):
    mi_path = f"{CONTENT_ROOT}/Materials/Instances"
    ensure_dir(mi_path)

    instances = {}
    for layer_tag, *_ in LAYERS:
        name = f"MI_FaceParallax_{layer_tag}"
        full_path = f"{mi_path}/{name}"
        if editor_asset_lib.does_asset_exist(full_path):
            unreal.log(f"[SKIP] {full_path} already exists")
            instances[layer_tag] = unreal.load_asset(full_path)
            continue

        factory = unreal.MaterialInstanceConstantFactoryNew()
        mi = asset_tools.create_asset(name, mi_path, unreal.MaterialInstanceConstant, factory)
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master_material)
        editor_asset_lib.save_asset(full_path)
        unreal.log(f"[OK] Created {full_path} (parent: {master_material.get_name()})")
        instances[layer_tag] = mi
    return instances


# --------------------------------------------------------------
# 3. FaceParallaxPreset Data Asset
# --------------------------------------------------------------
def create_preset_asset():
    preset_path = f"{CONTENT_ROOT}/Presets"
    ensure_dir(preset_path)

    name = "DA_FaceParallax_Default"
    full_path = f"{preset_path}/{name}"

    if editor_asset_lib.does_asset_exist(full_path):
        unreal.log(f"[SKIP] {full_path} already exists")
        return unreal.load_asset(full_path)

    preset_class = find_class(PRESET_CLASS_NAME)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", preset_class)
    preset = asset_tools.create_asset(name, preset_path, preset_class, factory)

    # Populate ViewAssignments as a TMap<EFaceAngleState, FFaceViewStateLayerSet>.
    # Each layer set contains a TMap<FName, FFaceArtSlot> for its layers.
    try:
        assignments = {}
        for view_name in VIEW_STATES:
            view_enum = getattr(unreal.EFaceAngleState, view_name)
            layer_set = unreal.FFaceViewStateLayerSet()
            layers = {}
            for layer_tag, *_ in LAYERS:
                slot = unreal.FFaceArtSlot()
                layers[unreal.Name(layer_tag)] = slot
            layer_set.set_editor_property("Layers", layers)
            assignments[view_enum] = layer_set
        preset.set_editor_property("ViewAssignments", assignments)
        unreal.log(f"[OK] Populated ViewAssignments for {len(assignments)} states")
    except Exception as e:
        unreal.log_warning(
            f"[MANUAL] Could not auto-populate ViewAssignments ({e}). "
            f"Open {full_path} in the Editor and populate the map manually."
        )

    editor_asset_lib.save_asset(full_path)
    unreal.log(f"[OK] Created {full_path}")
    return preset


# --------------------------------------------------------------
# 4. Character Blueprint with FaceParallaxComponent wired up
# --------------------------------------------------------------
def create_character_blueprint(preset_asset):
    bp_path = f"{CONTENT_ROOT}/Blueprints"
    ensure_dir(bp_path)

    name = "BP_FaceParallaxCharacter"
    full_path = f"{bp_path}/{name}"

    if editor_asset_lib.does_asset_exist(full_path):
        unreal.log(f"[SKIP] {full_path} already exists")
        return unreal.load_asset(full_path)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", CHARACTER_PARENT_CLASS)
    blueprint = asset_tools.create_asset(name, bp_path, unreal.Blueprint, factory)

    component_class = find_class(COMPONENT_CLASS_NAME)
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handle_root = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)[0]

    add_params = unreal.AddNewSubobjectParams(
        parent_handle=handle_root,
        new_class=component_class,
        blueprint_context=blueprint,
    )
    sub_handle, fail_reason = subsystem.add_new_subobject(add_params)
    if fail_reason:
        unreal.log_warning(f"[MANUAL] Could not add {COMPONENT_CLASS_NAME}: {fail_reason}")
    else:
        subsystem.rename_subobject(sub_handle, unreal.Text("FaceParallax"))
        comp_obj = subsystem.get_data_handle(sub_handle).get_object()

        # Set HeadBoneName and ActivePreset on the CDO/template so every
        # instance starts wired correctly.
        try:
            comp_obj.set_editor_property("HeadBoneName", HEAD_BONE_NAME)
        except Exception as e:
            unreal.log_warning(f"[MANUAL] Couldn't set HeadBoneName ({e}) — set it in the Editor.")

        try:
            comp_obj.set_editor_property("ActivePreset", preset_asset)
        except Exception as e:
            unreal.log_warning(f"[MANUAL] Couldn't set ActivePreset ({e}) — set it in the Editor.")

        try:
            layer_defs = []
            for layer_tag, depth_scale, depth_intensity, invert in LAYERS:
                layer_def = unreal.FFaceLayerDef()
                layer_def.set_editor_property("layer_tag", unreal.Name(layer_tag))
                layer_def.set_editor_property("depth_scale", depth_scale)
                layer_def.set_editor_property("depth_map_intensity", depth_intensity)
                layer_def.set_editor_property("b_invert_parallax", invert)
                layer_defs.append(layer_def)
            comp_obj.set_editor_property("LayerDefinitions", layer_defs)
            unreal.log(f"[OK] Populated {len(layer_defs)} LayerDefinitions entries")
        except Exception as e:
            unreal.log_warning(
                f"[MANUAL] Couldn't set LayerDefinitions ({e}). Property name/shape "
                f"differs from assumption — tell me the exact struct field names "
                f"and I'll correct this section rather than leaving it a checklist item."
            )

    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    unreal.log(f"[OK] Created {full_path} with FaceParallaxComponent attached")
    return blueprint


# --------------------------------------------------------------
# Run
# --------------------------------------------------------------
def main():
    unreal.log("==> Deploying FaceParallax editor assets")
    ensure_dir(CONTENT_ROOT)

    master_mat = create_master_material()
    create_material_instances(master_mat)
    preset = create_preset_asset()
    create_character_blueprint(preset)

    unreal.log("==> Done. Remaining manual work: import your Albedo/Normal/Depth "
               "textures and assign them into each Material Instance; place the "
               "face-layer quad meshes on the skeleton (mesh placement isn't scriptable "
               "without your actual mesh assets present); and verify the ArtPivot, "
               "ExpressionBlendAlpha, and NestedAnimFrame parameter bindings if your "
               "materials use nested art, expression crossfade, or idle animation.")


if __name__ == "__main__":
    main()
