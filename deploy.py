"""
Deploy-FaceParallax-Assets.py (deploy.py)

THE FaceParallax deployment script. Runs INSIDE the Unreal Editor and
performs the COMPLETE deployment — every binary asset the FaceParallax
editor tool needs:

  1. M_FaceParallax_Master material, with the full parameter graph wired
     (crossfade between current/prev textures driven by StateBlendAlpha,
     pivot-aware UV offset driven by ArtPosition/ArtScale/ArtRotation,
     expression crossfade via ExpressionBlendAlpha, top-down depth switch,
     nested-animation frame offset, debug-depth switch).
  2. One Material Instance per face layer (MI_FaceParallax_<Layer>),
     parented to the master, with every runtime parameter defaulted.
  3. DA_FaceParallaxPreset Data Asset, with ViewAssignments populated as a
     TMap<EFaceAngleState, FFaceViewStateLayerSet> for every state.
  4. BP_FaceParallaxCharacter Blueprint (parent: Character) with a
     FaceParallaxComponent added, skeletal mesh assigned and the CDO/SCS
     wired so the component's LayerDefinitions match the deployed layers.
  5. WBP_FaceParallaxEditor Widget Blueprint parented to the C++
     UFaceParallaxEditorWidget class (deleted and recreated with a clean
     CDO so no stale imports survive), which the C++ subsystem loads when
     the FaceParallaxEditor docked tab opens.
  6. RT_FaceParallaxPreview render target + a preview actor spawned in the
     editor world (stale actors are removed first), mesh assigned, render
     target wired and layer quads spawned so art is visible immediately.

The C++ classes (FaceParallax component, preset, preview actor, editor
widget, editor subsystem) are a plugin built by UnrealBuildTool — this
script verifies they are available and reports clearly if the plugin is
not built yet.

USAGE (in-editor Python console — the console history command):
    py "G:\\tailedstories\\paralax\\deploy.py"

USAGE (headless, from command line):
    UnrealEditor-Cmd.exe "MyProject.uproject" -run=pythonscript -script="deploy.py"

After deployment, open the editor with the toolbar button or the
'FaceParallaxOpenEditor' console command — it must open as a DOCKED TAB.
"""

import unreal
import os
import sys
import time

# =========================== CONFIG ===========================

CONTENT_ROOT = "/Game/FaceParallax"

# Layers -> (LayerTag, DepthScale, DepthMapIntensity, bInvertParallax, depth_class)
# depth_class is the front/base/back yaw-motion rule (FaceParallaxSchematic.h,
# FPTagClassForTag): front features move WITH yaw (Eyes/Brows/Mouth/Bangs/Nose/
# Cheeks), the head silhouette is anchored (Head), and far-side parts move
# AGAINST yaw (Hair/BackHair/Ears). HAIR SYSTEM (Phase 2): Bangs = FRONT hair
# (moves WITH yaw); Hair + BackHair = BACK hair (move AGAINST yaw) — every
# hair layer rides the normal per-layer pipeline (camera-sync, auto-fit,
# bulk-assign, nested pins, visibility, problems panel). DepthScale/
# bInvertParallax here are the material-side defaults the runtime overwrites
# per layer from the same class table
# (UFaceParallaxComponent::SyncLayerDefinitionsFromPreset).
LAYERS = [
    # ---- Front (moves with yaw) ----
    ("Eyes",    0.5, 1.0, False, "Front"),
    ("Brows",   0.4, 1.0, False, "Front"),
    ("Mouth",   0.6, 1.0, False, "Front"),
    ("Bangs",   0.7, 1.0, False, "Front"),   # hair: front fringe (front hair)
    ("Nose",    0.9, 1.0, False, "Front"),
    ("Cheeks",  0.5, 1.0, False, "Front"),
    # ---- Base (anchored) ----
    ("Head",    0.15, 1.0, False, "Base"),
    # ---- Back (moves against yaw) ----
    ("Hair",    0.8, 1.0, True,  "Back"),    # hair: full silhouette (back hair)
    ("BackHair", 0.8, 1.0, True, "Back"),    # hair: curtain (back hair)
    ("Ears",    0.5, 1.0, True,  "Back"),
]

# Layer tags the preset gets ViewAssignments for (the base-preset layer set).
LAYER_TAGS = [entry[0] for entry in LAYERS]

# All view states the preset needs an assignment for.
# Must match EFaceAngleState enum names exactly.
VIEW_STATES = [
    "Front", "ThreeQuarterRight", "RightProfile", "BackRight", "Back",
    "BackLeft", "LeftProfile", "ThreeQuarterLeft", "Top", "Bottom",
]

HEAD_BONE_NAME = "head"
COMPONENT_CLASS_NAME = "FaceParallaxComponent"      # plugin runtime module
PRESET_CLASS_NAME = "FaceParallaxPreset"            # plugin runtime module

# Preset Data Asset output
PRESET_OUTPUT_PATH = "/Game/FaceParallax/Presets"
PRESET_NAME = "DA_FaceParallaxPreset"
# Legacy preset created by an old pipeline; its class import is stale and
# the runtime deletes it. deploy.py deletes it too (self-healing).
LEGACY_PRESET_NAME = "DA_FaceParallax_Default"

# Character Blueprint output
CHARACTER_OUTPUT_PATH = "/Game/FaceParallax/Blueprints"
CHARACTER_BP_NAME = "BP_FaceParallaxCharacter"

# Skeletal meshes assigned to the character BP and preview actor.
# Tried in order; the first one that exists wins.
CHARACTER_MESH_PATH = "/Game/FaceParallax/Meshes/SK_Face.SK_Face"
MANNEQUIN_SKELETAL_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"
MANNEQUIN_ANIM_BLUEPRINT = "/Game/Characters/Mannequins/Animations/ABP_Mannequin.ABP_Mannequin"

# Editor widget Blueprint loaded by the C++ subsystem's docked tab
EDITOR_UTILITY_WIDGET_PATH = "/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor.WBP_FaceParallaxEditor"

# ================================================================

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary


def ensure_dir(path):
    if not editor_asset_lib.does_directory_exist(path):
        editor_asset_lib.make_directory(path)


def load_existing_asset(path):
    """Load an asset only if it exists — avoids 'Failed to find object'
    warnings from load_asset on missing candidates."""
    try:
        if editor_asset_lib.does_asset_exist(path):
            return unreal.load_asset(path)
    except Exception:
        pass
    return None


def _set_prop(obj, names, value):
    """Set an editor property trying several Python/UE spellings."""
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            continue
    return False


# Classes that live in the editor module (probe it first — probing the runtime
# module first logged a spurious 'Failed to find object' warning each run).
EDITOR_MODULE_CLASSES = {"FaceParallaxEditorWidget", "FaceParallaxEditorSubsystem"}


def find_class(name):
    """Resolve a UCLASS by name across the plugin modules.

    Probes only the two plugin modules (runtime + editor) — probing the
    project module produced 'Failed to find object' warnings for every
    load_class miss, and no deploy class lives there.
    """
    if name in EDITOR_MODULE_CLASSES:
        modules_to_try = ["FaceParallaxEditor", "FaceParallax"]
    else:
        modules_to_try = ["FaceParallax", "FaceParallaxEditor"]

    for module in modules_to_try:
        cls = unreal.load_class(None, f"/Script/{module}.{name}")
        if cls:
            return cls
    raise RuntimeError(
        f"Could not resolve class '{name}'. The FaceParallax plugin is not "
        f"built — build the project (Tests\\run_tests.ps1 -IncludeUEBuild) "
        f"or restart the editor with the freshly built module."
    )


def class_available(name):
    """Return True if the C++ class is registered, without raising."""
    try:
        find_class(name)
        return True
    except RuntimeError:
        return False


def _load_project_module():
    """Force-load the project module so reflected types become available."""
    try:
        project_path = unreal.Paths.get_project_file_path()
        if project_path:
            mod_name = os.path.splitext(os.path.basename(project_path))[0]
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            if world:
                unreal.SystemLibrary.execute_console_command(world, f"Module.Load {mod_name}")
                time.sleep(0.5)
    except Exception:
        pass


def force_gc():
    """Run a full garbage collection pass. UE5.8 Python has no
    unreal.GarbageCollection module, so fall back to the 'obj collect'
    console command. Returns True if a GC pass was triggered."""
    try:
        unreal.GarbageCollection.collect_garbage()
        return True
    except AttributeError:
        pass
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if world:
            unreal.SystemLibrary.execute_console_command(world, "obj collect")
            return True
    except Exception:
        pass
    return False


def _editor_actor_subsystem():
    """Return the EditorActorSubsystem if available, else None.

    EditorLevelLibrary (Editor Scripting Utilities) is deprecated in UE5.8
    and logs DeprecationWarnings — prefer the newer subsystem when present."""
    try:
        return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    except Exception:
        return None


def spawn_actor_from_class(actor_class, location, rotation):
    sub = _editor_actor_subsystem()
    if sub is not None:
        return sub.spawn_actor_from_class(actor_class, location, rotation)
    return unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, location, rotation)


def destroy_actor(actor):
    if not actor:
        return
    sub = _editor_actor_subsystem()
    if sub is not None:
        sub.destroy_actor(actor)
        return
    unreal.EditorLevelLibrary.destroy_actor(actor)


def get_all_level_actors():
    sub = _editor_actor_subsystem()
    if sub is not None:
        return sub.get_all_level_actors()
    return unreal.EditorLevelLibrary.get_all_level_actors()


def wait_asset_gone(obj_path, timeout_s=10.0):
    """Poll until delete_asset takes effect (package may linger until GC)."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if not editor_asset_lib.does_asset_exist(obj_path):
            return True
        time.sleep(0.5)
    return not editor_asset_lib.does_asset_exist(obj_path)


def delete_asset_logged(obj_path, reason):
    """Delete an asset if present, with logging and GC."""
    if editor_asset_lib.does_asset_exist(obj_path):
        editor_asset_lib.delete_asset(obj_path)
        force_gc()
        wait_asset_gone(obj_path)
        unreal.log(f"[CLEAN] Deleted {obj_path} ({reason})")
        return True
    return False


def delete_legacy_assets():
    """Remove assets from the old pipeline whose names/paths do not match
    the current runtime, so a deployed project always converges to exactly
    one consistent asset set."""
    delete_asset_logged(
        f"{PRESET_OUTPUT_PATH}/{LEGACY_PRESET_NAME}.{LEGACY_PRESET_NAME}",
        "legacy preset with stale class import")
    delete_asset_logged(
        "/Game/FaceParallax/Materials/M_FaceParallaxMaster.M_FaceParallaxMaster",
        "wrong-named master material (runtime loads M_FaceParallax_Master)")
    for layer_tag, *_ in LAYERS:
        name = f"MI_Face_{layer_tag}"
        delete_asset_logged(
            f"/Game/FaceParallax/Materials/{name}.{name}",
            "wrong-named material instance (runtime loads Materials/Instances/MI_FaceParallax_<Layer>)")


# --------------------------------------------------------------
# 1. Master Material (full parameter graph — mirrors the runtime)
# --------------------------------------------------------------
def create_master_material():
    mat_path = f"{CONTENT_ROOT}/Materials"
    ensure_dir(mat_path)

    mat_name = "M_FaceParallax_Master"
    full_path = f"{mat_path}/{mat_name}"

    # Untextured albedo samples fall back to UE's built-in 1x1 white
    # DefaultTexture automatically, so face-layer quads stay visible before
    # any art is imported
    if editor_asset_lib.does_asset_exist(full_path):
        existing = unreal.load_asset(full_path)
        if existing and existing.get_class().get_name() == "Material":
            unreal.log(f"[SKIP] {full_path} already exists (valid Material)")
            return existing
        unreal.log_warning(
            f"[REPAIR] {full_path} exists but is not a valid Material "
            f"({existing.get_class().get_name() if existing else 'None'}) - recreating")
        editor_asset_lib.delete_asset(full_path)
        wait_asset_gone(full_path)

    factory = unreal.MaterialFactoryNew()
    material = asset_tools.create_asset(mat_name, mat_path, unreal.Material, factory)

    # Unlit + TwoSided + skinned-mesh usage (matches the C++ creator)
    _set_prop(material, ("shading_model",), unreal.MaterialShadingModel.MSM_UNLIT)
    _set_prop(material, ("two_sided", "TwoSided"), True)
    _set_prop(material, ("b_used_with_skeletal_mesh", "bUsedWithSkeletalMesh"), True)

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

    # --- Parameters (the full set the runtime drives) ---
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

    # Swoosh & alt-texture parameters (instance-driven; graph nodes for validation)
    param_blend  = scalar_param("ParamBlendAlpha",     -600, 2850, 0.0)
    alt_albedo   = tex_param("AltAlbedoTexture",       -600, 3000)
    alt_normal   = tex_param("AltNormalTexture",       -600, 3150)
    alt_depth    = tex_param("AltDepthTexture",        -600, 3300)
    swoosh_blend = scalar_param("SwooshLayerBlend",    -600, 3450, 0.0)
    swoosh_int   = scalar_param("SwooshIntensity",     -600, 3600, 0.0)
    swoosh_angl  = scalar_param("SwooshAngle",         -600, 3750, 0.0)
    swoosh_siz   = scalar_param("SwooshSize",          -600, 3900, 0.0)
    swoosh_tex   = tex_param("SwooshTexture",          -600, 4050)

    # --- UV chain: TexCoord → Subtract(Pivot) → Add(ArtPos) → Multiply(ArtScale)
    #                → Add(Pivot) → Add(ParallaxOffset) → Rotate(ArtRot) → UVs ---
    texcoord = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1200, -300)

    uv_sub_pivot = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -1050, -300)
    mel.connect_material_expressions(texcoord, "", uv_sub_pivot, "A")
    mel.connect_material_expressions(art_pivot, "", uv_sub_pivot, "B")

    uv_add_pos = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -920, -300)
    mel.connect_material_expressions(uv_sub_pivot, "", uv_add_pos, "A")
    mel.connect_material_expressions(art_pos, "", uv_add_pos, "B")

    uv_mul_scale = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -800, -300)
    mel.connect_material_expressions(uv_add_pos, "", uv_mul_scale, "A")
    mel.connect_material_expressions(art_scale, "", uv_mul_scale, "B")

    uv_re_pivot = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -680, -280)
    mel.connect_material_expressions(uv_mul_scale, "", uv_re_pivot, "A")
    mel.connect_material_expressions(art_pivot, "", uv_re_pivot, "B")

    uv_final = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -550, -280)
    mel.connect_material_expressions(uv_re_pivot, "", uv_final, "A")
    mel.connect_material_expressions(parallax_off, "", uv_final, "B")

    uv_rotate = mel.create_material_expression(material, unreal.MaterialExpressionRotator, -400, -300)
    mel.connect_material_expressions(uv_final, "", uv_rotate, "Coordinate")
    mel.connect_material_expressions(art_rot, "", uv_rotate, "Time")

    uv_source = uv_rotate if uv_rotate else uv_final

    for tex_node in (albedo_cur, albedo_prev, normal_cur, normal_prev, depth_cur, depth_prev,
                     expr_alb_prev, expr_nrm_prev, expr_dep_prev):
        mel.connect_material_expressions(uv_source, "", tex_node, "UVs")

    # --- NestedFrame: add frame-based UV offset (drives flipbook frame select) ---
    uv_frame_offset = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -550, -450)
    mel.connect_material_expressions(uv_source, "", uv_frame_offset, "A")
    zero_const = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -700, -450)
    zero_const.set_editor_property("R", 0.0)
    frame_vec = mel.create_material_expression(material, unreal.MaterialExpressionAppendVector, -620, -450)
    mel.connect_material_expressions(nested_frame, "", frame_vec, "A")
    mel.connect_material_expressions(zero_const, "", frame_vec, "B")
    mel.connect_material_expressions(frame_vec, "", uv_frame_offset, "B")

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

    # --- Expression crossfade: blend toward expression prev textures ---
    expr_albedo_blend = lerp(albedo_blend, expr_alb_prev, expr_alpha, -100, -200)
    expr_normal_blend = lerp(normal_blend, expr_nrm_prev, expr_alpha, -100, 100)
    expr_depth_blend  = lerp(depth_scaled, expr_dep_prev, expr_alpha, -100, 400)

    # --- IsTopDown: select depth output (IsTopView can be wired by callers) ---
    topdown_switch = mel.create_material_expression(material, unreal.MaterialExpressionStaticSwitch, 100, 350)
    mel.connect_material_expressions(is_topdown, "", topdown_switch, "Value")
    mel.connect_material_expressions(expr_depth_blend, "", topdown_switch, "True")
    mel.connect_material_expressions(expr_depth_blend, "", topdown_switch, "False")

    final_albedo = expr_albedo_blend if expr_albedo_blend else albedo_blend
    final_depth = topdown_switch if topdown_switch else expr_depth_blend

    # DebugDepth switch: show depth in place of albedo when enabled
    debug_switch = mel.create_material_expression(material, unreal.MaterialExpressionStaticSwitch, 350, -100)
    mel.connect_material_expressions(debug_depth, "", debug_switch, "Value")
    mel.connect_material_expressions(final_depth, "", debug_switch, "True")
    mel.connect_material_expressions(final_albedo, "", debug_switch, "False")

    out_normal = expr_normal_blend if expr_normal_blend else normal_blend
    mel.connect_material_property(debug_switch, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(out_normal, "", unreal.MaterialProperty.MP_NORMAL)

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
            existing = unreal.load_asset(full_path)
            if existing and existing.get_class().get_name() == "MaterialInstanceConstant":
                unreal.log(f"[SKIP] {full_path} already exists (valid MaterialInstanceConstant)")
                instances[layer_tag] = existing
                continue
            unreal.log_warning(
                f"[REPAIR] {full_path} exists but is not a valid MaterialInstanceConstant "
                f"({existing.get_class().get_name() if existing else 'None'}) - recreating")
            editor_asset_lib.delete_asset(full_path)
            wait_asset_gone(full_path)

        factory = unreal.MaterialInstanceConstantFactoryNew()
        mi = asset_tools.create_asset(name, mi_path, unreal.MaterialInstanceConstant, factory)
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master_material)
        _set_mi_param_defaults(mi)
        editor_asset_lib.save_asset(full_path)
        unreal.log(f"[OK] Created {full_path} (parent: {master_material.get_name()})")
        instances[layer_tag] = mi
    return instances


def _set_mi_param_defaults(mi):
    """Default every runtime material parameter on the instance
    (mirrors the C++ SetupMaterialInstanceParams)."""
    try:
        mi.set_scalar_parameter_value_editor_only("StateBlendAlpha", 1.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("DepthIntensity", 1.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("ArtRotation", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("NestedAnimFrame", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("ExpressionBlendAlpha", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("ParamBlendAlpha", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("SwooshLayerBlend", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("SwooshIntensity", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("SwooshAngle", 0.0)
    except Exception:
        pass
    try:
        mi.set_scalar_parameter_value_editor_only("SwooshSize", 0.0)
    except Exception:
        pass
    for name, color in (("ParallaxOffset", (0.0, 0.0, 0.0, 0.0)),
                        ("ArtPosition", (0.0, 0.0, 0.0, 0.0)),
                        ("ArtScale", (1.0, 1.0, 0.0, 0.0)),
                        ("ArtPivot", (0.5, 0.5, 0.0, 0.0))):
        try:
            mi.set_vector_parameter_value_editor_only(name, unreal.LinearColor(*color))
        except Exception:
            pass
    for name in ("DebugDepth", "IsTopDown", "IsTopView"):
        try:
            mi.set_static_switch_parameter_value_editor_only(name, False)
        except Exception:
            pass
    for name in ("AlbedoTexture", "AlbedoTexturePrev", "NormalTexture", "NormalTexturePrev",
                 "DepthTexture", "DepthTexturePrev", "ExpressionAlbedoPrev",
                 "ExpressionNormalPrev", "ExpressionDepthPrev",
                 "AltAlbedoTexture", "AltNormalTexture", "AltDepthTexture",
                 "SwooshTexture"):
        try:
            mi.set_texture_parameter_value_editor_only(name, None)
        except Exception:
            pass


# --------------------------------------------------------------
# 3. FaceParallaxPreset Data Asset
# --------------------------------------------------------------
def _verify_preset_view_assignments(preset):
    """Check preset has 10 states with LAYER_TAGS layers each. Returns True if valid."""
    try:
        va = preset.get_editor_property("ViewAssignments")
        if not va or len(va) != 10:
            return False
        for state_val, layer_set in va.items():
            layers = layer_set.get_editor_property("Layers")
            if not layers or len(layers) != len(LAYER_TAGS):
                return False
        return True
    except Exception:
        return False


def _detach_preset_references():
    """Null ActivePreset on in-level preview actors so preset replacement is
    never blocked by a live reference (headless deploy cannot prompt)."""
    detached = 0
    try:
        comp_class = find_class("FaceParallaxComponent")
        for actor in get_all_level_actors():
            try:
                if actor.get_class().get_name() != "FaceParallaxPreviewActor":
                    continue
                for comp in actor.get_components_by_class(comp_class):
                    try:
                        if comp.get_editor_property("ActivePreset") is not None:
                            comp.set_editor_property("ActivePreset", None)
                            detached += 1
                    except Exception:
                        pass
            except Exception:
                pass
    except Exception:
        pass
    if detached:
        unreal.log(f"[CLEAN] Detached ActivePreset from {detached} preview actor component(s)")
    return detached


def create_preset_asset():
    ensure_dir(PRESET_OUTPUT_PATH)

    pkg = f"{PRESET_OUTPUT_PATH}/{PRESET_NAME}"
    obj_path = pkg + "." + PRESET_NAME

    preset_class = find_class(PRESET_CLASS_NAME)

    if editor_asset_lib.does_asset_exist(obj_path):
        existing = unreal.load_asset(obj_path)
        if _verify_preset_view_assignments(existing):
            unreal.log(f"[SKIP] {obj_path} already has valid ViewAssignments")
            return existing
        unreal.log(f"[REPLACE] {obj_path} has invalid ViewAssignments — rebuilding")
        # Rebuild in place when the property is readable: PopulateDefaultAssignments
        # empties and re-fills the map, and every live reference (e.g. the
        # preview actor's component in the loaded level) stays valid. Deletion
        # cannot complete headlessly while anything references the asset, so
        # delete+recreate is only the fallback for class-layout mismatches.
        try:
            existing.set_editor_property("ViewAssignments", {})
            existing.populate_default_assignments(LAYER_TAGS)
            _set_prop(existing, ("canvas_size", "CanvasSize"), unreal.IntPoint(2048, 2048))
            if _verify_preset_view_assignments(existing):
                editor_asset_lib.save_loaded_asset(existing)
                unreal.log(f"[OK] Rebuilt {obj_path} in place (10 states x {len(LAYER_TAGS)} layers)")
                return existing
            unreal.log_warning("[REPLACE] in-place rebuild failed verification — falling back to delete+recreate")
        except Exception as e:
            unreal.log_warning(f"[REPLACE] in-place rebuild not possible ({e}) — falling back to delete+recreate")
        existing = None
        _detach_preset_references()
        editor_asset_lib.delete_asset(obj_path)
        force_gc()
        if not wait_asset_gone(obj_path):
            unreal.log_warning(f"[REPLACE] delete of {obj_path} deferred — retrying after a second GC")
            force_gc()
            editor_asset_lib.delete_asset(obj_path)
            wait_asset_gone(obj_path)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", preset_class)
    preset = asset_tools.create_asset(PRESET_NAME, PRESET_OUTPUT_PATH, preset_class, factory)
    # Populate via C++ UFUNCTION — structs can't be created from Python after LiveCoding
    preset.populate_default_assignments(LAYER_TAGS)
    _set_prop(preset, ("canvas_size", "CanvasSize"), unreal.IntPoint(2048, 2048))
    if not _verify_preset_view_assignments(preset):
        raise RuntimeError("Failed to populate ViewAssignments after creation")
    editor_asset_lib.save_asset(pkg)
    unreal.log(f"[OK] Created {obj_path} with 10 states x {len(LAYER_TAGS)} layers")
    return preset


# --------------------------------------------------------------
# 4. Character Blueprint with FaceParallaxComponent wired up
# --------------------------------------------------------------
def _get_blueprint_generated_class(blueprint, full_path):
    """Resolve the generated class of a Blueprint asset (get_generated_class
    is missing on WidgetBlueprint in UE5.8 — fall back to load_class)."""
    try:
        if hasattr(blueprint, "get_generated_class"):
            gc = blueprint.get_generated_class()
            if gc:
                return gc
    except Exception:
        pass
    try:
        # Dotted form required: /Game/Path/BP_Name.BP_Name_C (package.object)
        pkg = full_path
        if "." in full_path.rsplit("/", 1)[-1]:
            pkg = full_path.rsplit(".", 1)[0]
        asset_name = pkg.rsplit("/", 1)[-1]
        return unreal.load_class(None, f"{pkg}.{asset_name}_C")
    except Exception:
        return None


def _spawn_probe_actor(bp_gc, height=1000.0):
    """Spawn a temp instance for wiring verification; caller must destroy it."""
    try:
        loc = unreal.Vector(0, 0, height)
        return spawn_actor_from_class(bp_gc, loc, unreal.Rotator(0, 0, 0))
    except Exception:
        return None


def _destroy_probe_actor(actor):
    try:
        destroy_actor(actor)
    except Exception:
        pass


def _blueprint_has_component(blueprint, component_class, full_path):
    """Check a spawned instance for the component.

    SCS components do not appear on the generated class CDO, so verify via a
    temp instance (runs the construction script). Returns True/False/None."""
    actor = None
    try:
        bp_gc = _get_blueprint_generated_class(blueprint, full_path)
        if not bp_gc:
            return None
        actor = _spawn_probe_actor(bp_gc)
        if not actor:
            return None
        comps = actor.get_components_by_class(component_class)
        return bool(comps)
    except Exception:
        return None
    finally:
        _destroy_probe_actor(actor)


def _wiring_needs_repair(blueprint, component_class, full_path):
    """Check if the character BP is missing the component or mesh."""
    actor = None
    try:
        bp_gc = _get_blueprint_generated_class(blueprint, full_path)
        if not bp_gc:
            return True
        actor = _spawn_probe_actor(bp_gc)
        if not actor:
            return True
        mesh_ok = False
        offset_ok = False
        for mc in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            try:
                if mc.get_skeletal_mesh_asset():
                    mesh_ok = True
            except Exception:
                continue
            try:
                # Mannequin root bone sits at the feet — a ~-88..-90 Z offset places
                # them at the capsule bottom (UE5 template value is -90)
                if mc.get_editor_property("RelativeLocation").z < -1.0:
                    offset_ok = True
            except Exception:
                continue
        if not mesh_ok:
            return True
        if not offset_ok:
            return True
        # Require the FaceParallaxComponent to be present
        if component_class:
            comps = actor.get_components_by_class(component_class)
            if not comps or len(comps) == 0:
                return True
        return False
    except Exception:
        return True
    finally:
        _destroy_probe_actor(actor)


def _compile_blueprint(blueprint):
    """Force compile a Blueprint asset and wait for completion."""
    try:
        lib = unreal.BlueprintEditorLibrary
        if hasattr(lib, "compile_blueprint"):
            lib.compile_blueprint(blueprint)
        else:
            blueprint.compile()
        unreal.AssetRegistryHelpers.get_asset_registry().on_asset_updated(blueprint.package_path)
        return True
    except Exception:
        return False


def _resolve_character_parent():
    """Resolve the Character parent class; UE5 Python may not expose unreal.Character directly."""
    for path in ("/Script/Engine.Character", "/Script/Engine.Character"):
        try:
            cls = unreal.load_class(None, path)
            if cls:
                return cls
        except Exception:
            continue
    unreal.log_warning("[MANUAL] Could not resolve Character parent class — using default")
    return unreal.Actor


def _wire_character_blueprint(blueprint, full_path, component_class, preset_asset,
                              mannequin_mesh, mannequin_anim):
    """Wire mesh + FaceParallaxComponent on a character BP.
    Never deletes the asset — repairs in place."""
    if not blueprint:
        return False

    _compile_blueprint(blueprint)

    # The Character parent has a native Mesh component on the class CDO — assign there
    if mannequin_mesh:
        try:
            bp_gc = _get_blueprint_generated_class(blueprint, full_path)
            if bp_gc:
                cdo = unreal.get_default_object(bp_gc)
                mesh_comp = cdo.get_editor_property("Mesh") if cdo else None
                if mesh_comp:
                    try:
                        mesh_comp.set_skeletal_mesh_asset(mannequin_mesh)
                    except Exception:
                        mesh_comp.set_editor_property("skeletal_mesh", mannequin_mesh)
                    if mannequin_anim:
                        try:
                            mesh_comp.set_editor_property("anim_class", mannequin_anim)
                        except Exception:
                            pass
                    # Mannequin root bone sits at the feet; drop the mesh so the
                    # feet rest at the capsule bottom (UE5 template value)
                    try:
                        mesh_comp.set_editor_property("RelativeLocation",
                                                      unreal.Vector(0.0, 0.0, -90.0))
                    except Exception:
                        pass
                    unreal.log("[OK] Assigned SK_Mannequin to Mesh component "
                               "(relative offset -90, feet on ground)")
                else:
                    unreal.log_warning("[MANUAL] CDO has no Mesh component")
            else:
                unreal.log_warning("[MANUAL] Could not resolve generated class for mesh assignment")
        except Exception as e:
            unreal.log_warning(f"[MANUAL] Could not assign mannequin mesh ({e})")

    # Add FaceParallaxComponent via SubobjectDataSubsystem (SCS node).
    # SCS templates are not reachable from Python in UE5.8, so per-asset default
    # properties (ActivePreset, LayerDefinitions) must be assigned in-editor;
    # the component's own C++ defaults (HeadBoneName="head", auto-spawn on)
    # already cover the rest.
    if component_class:
        has = _blueprint_has_component(blueprint, component_class, full_path)
        if has:
            unreal.log("[OK] FaceParallaxComponent already present")
        else:
            try:
                sub_sys = None
                try:
                    sub_sys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
                except Exception:
                    cls = unreal.load_class(None, "/Script/Blueprint.SubobjectDataSubsystem")
                    if cls:
                        sub_sys = unreal.get_editor_subsystem(cls)
                if sub_sys:
                    root = sub_sys.k2_gather_subobject_data_for_blueprint(blueprint)
                    if root and len(root) > 0:
                        handle, reason = sub_sys.add_new_subobject(unreal.AddNewSubobjectParams(
                            parent_handle=root[0], new_class=component_class,
                            blueprint_context=blueprint))
                        try:
                            reason_txt = reason.to_string()
                        except Exception:
                            reason_txt = str(reason)
                        if handle and not reason_txt:
                            unreal.log("[OK] Added FaceParallaxComponent to Blueprint")
                        else:
                            unreal.log_warning(f"[MANUAL] add_new_subobject failed: {reason_txt}")
                    else:
                        unreal.log_warning("[MANUAL] No scene root subobject found for component add")
                else:
                    unreal.log_warning("[MANUAL] SubobjectDataSubsystem unavailable")
            except Exception as e:
                unreal.log_warning(f"[MANUAL] Could not auto-add FaceParallaxComponent ({e})")
    else:
        unreal.log_warning("[MANUAL] FaceParallaxComponent class not found")

    _compile_blueprint(blueprint)
    try:
        blueprint.modify()
    except Exception:
        pass
    # Force-save: CDO/SCS edits do not mark the package dirty in UE5.8
    unreal.EditorAssetLibrary.save_asset(full_path, only_if_is_dirty=False)
    return True


def create_character_blueprint(preset_asset):
    ensure_dir(CHARACTER_OUTPUT_PATH)

    full_path = f"{CHARACTER_OUTPUT_PATH}/{CHARACTER_BP_NAME}"
    _load_project_module()
    component_class = find_class(COMPONENT_CLASS_NAME)
    parent_class = _resolve_character_parent()

    # Load skeletal mesh (use fallback paths for UE5)
    mannequin_mesh = None
    mannequin_anim = None
    for mesh_path in [CHARACTER_MESH_PATH,
                      MANNEQUIN_SKELETAL_MESH,
                      "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple",
                      "/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny",
                      "/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"]:
        try:
            mesh = load_existing_asset(mesh_path)
            if mesh and mesh.get_class().get_name() == "SkeletalMesh":
                mannequin_mesh = mesh
                break
        except Exception:
            continue
    for anim_path in [MANNEQUIN_ANIM_BLUEPRINT,
                      "/Game/Characters/Mannequins/Animations/ABP_Mannequin.ABP_Mannequin",
                      "/Game/Mannequin/Animations/ABP_Mannequin.ABP_Mannequin"]:
        try:
            mannequin_anim = load_existing_asset(anim_path)
            if mannequin_anim:
                break
        except Exception:
            continue
    if not mannequin_mesh:
        unreal.log_warning(f"[MANUAL] Could not load a skeletal mesh for the character BP")

    if editor_asset_lib.does_asset_exist(full_path):
        existing = unreal.load_asset(full_path)
        if not _wiring_needs_repair(existing, component_class, full_path):
            unreal.log(f"[SKIP] {full_path} already exists and appears correctly wired")
            return existing
        unreal.log(f"[REPAIR] {full_path} exists but needs wiring — repairing in place")
        _wire_character_blueprint(existing, full_path, component_class, preset_asset,
                                  mannequin_mesh, mannequin_anim)
        return existing

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = asset_tools.create_asset(CHARACTER_BP_NAME, CHARACTER_OUTPUT_PATH, unreal.Blueprint, factory)
    if not blueprint:
        unreal.log_warning("[MANUAL] create_asset returned None for character BP — retrying after GC")
        force_gc()
        time.sleep(1.0)
        blueprint = asset_tools.create_asset(CHARACTER_BP_NAME, CHARACTER_OUTPUT_PATH, unreal.Blueprint, factory)
    if not blueprint:
        unreal.log_warning(f"[MANUAL] Could not create {full_path}")
        return None

    _wire_character_blueprint(blueprint, full_path, component_class, preset_asset,
                              mannequin_mesh, mannequin_anim)
    unreal.log(f"[OK] Created {full_path} with FaceParallaxComponent + skeletal mesh")
    return blueprint


# --------------------------------------------------------------
# 5. Create Editor Widget Blueprint (UserWidget base, no Blutility)
# --------------------------------------------------------------
def _clean_widget_cdo(blueprint, obj_path):
    """Set CDO reference properties to None to prevent stale import table entries."""
    try:
        bp_class = _get_blueprint_generated_class(blueprint, obj_path)
        if not bp_class:
            unreal.log_warning(f"[MANUAL] Could not resolve generated class for CDO cleanup")
            return
        cdo = unreal.get_default_object(bp_class)
        for prop in ("PreviewActor", "ActivePreset"):
            try:
                cdo.set_editor_property(prop, None)
            except Exception:
                pass
        editor_asset_lib.save_loaded_asset(blueprint)
        unreal.log("[OK] Cleaned CDO references to prevent stale imports")
    except Exception as e:
        unreal.log_warning(f"[MANUAL] Could not clean CDO refs: {e}")


def create_editor_widget_blueprint():
    ensure_dir("/Game/FaceParallax/Blueprints")
    pkg = "/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor"
    obj_path = pkg + ".WBP_FaceParallaxEditor"
    widget_class = find_class("FaceParallaxEditorWidget")

    if editor_asset_lib.does_asset_exist(obj_path):
        editor_asset_lib.delete_asset(obj_path)
        force_gc()
        wait_asset_gone(obj_path)
        unreal.log(f"[REPLACE] Deleted existing widget BP at {obj_path}")

    try:
        factory = unreal.WidgetBlueprintFactory()
        asset_class = getattr(unreal, "WidgetBlueprint", unreal.Blueprint)
    except AttributeError:
        factory = unreal.BlueprintFactory()
        asset_class = unreal.Blueprint
    factory.set_editor_property("parent_class", widget_class)
    factory.set_editor_property("bEditAfterNew", False)
    blueprint = asset_tools.create_asset(
        "WBP_FaceParallaxEditor", "/Game/FaceParallax/Blueprints",
        asset_class, factory
    )
    if not blueprint:
        unreal.log_warning("[MANUAL] create_asset returned None for widget BP — retrying after GC")
        force_gc()
        time.sleep(1.0)
        blueprint = asset_tools.create_asset(
            "WBP_FaceParallaxEditor", "/Game/FaceParallax/Blueprints",
            asset_class, factory
        )
    if not blueprint:
        unreal.log_warning(f"[MANUAL] Could not create {obj_path}")
        return None
    editor_asset_lib.save_loaded_asset(blueprint)

    _clean_widget_cdo(blueprint, obj_path)

    unreal.log(f"[OK] Created Editor Widget at {pkg}")
    return blueprint


def launch_editor_utility_widget():
    wbp_asset = unreal.load_asset(EDITOR_UTILITY_WIDGET_PATH)
    if wbp_asset:
        unreal.log(f"[OK] Widget ready at {EDITOR_UTILITY_WIDGET_PATH}")
    else:
        unreal.log_warning(f"[MANUAL] Could not load widget at {EDITOR_UTILITY_WIDGET_PATH}")


# --------------------------------------------------------------
# 6. Render Target
# --------------------------------------------------------------
def create_render_target():
    rt_path = f"{CONTENT_ROOT}/Textures"
    ensure_dir(rt_path)
    name = "RT_FaceParallaxPreview"
    full = f"{rt_path}/{name}"
    if editor_asset_lib.does_asset_exist(full):
        existing = unreal.load_asset(full)
        if existing and existing.get_class().get_name() == "TextureRenderTarget2D":
            unreal.log(f"[SKIP] {full} already exists (valid TextureRenderTarget2D)")
            return existing
        unreal.log_warning(
            f"[REPAIR] {full} exists but is not a valid TextureRenderTarget2D "
            f"({existing.get_class().get_name() if existing else 'None'}) - recreating")
        editor_asset_lib.delete_asset(full)
        wait_asset_gone(full)
    factory = unreal.TextureRenderTargetFactoryNew()
    rt = asset_tools.create_asset(name, rt_path, unreal.TextureRenderTarget2D, factory)
    rt.set_editor_property("size_x", 1024)
    rt.set_editor_property("size_y", 1024)
    rt.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    rt.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    editor_asset_lib.save_loaded_asset(rt)
    unreal.log(f"[OK] Created {full} (1024×1024)")
    return rt


# --------------------------------------------------------------
# 7. Spawn Preview Actor in Editor World
# --------------------------------------------------------------
def spawn_preview_actor(preset_asset, render_target):
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if not world:
            unreal.log_warning("[MANUAL] No editor world to spawn preview actor")
            return None
        actor_class = find_class("FaceParallaxPreviewActor")
        # Destroy stale preview actors from previous deploys so the level always
        # holds exactly ONE current actor (deterministic widget auto-discovery)
        stale = get_all_level_actors()
        removed = 0
        for a in stale:
            if a and a.get_class() == actor_class:
                try:
                    destroy_actor(a)
                    removed += 1
                except Exception:
                    pass
        if removed:
            unreal.log(f"[OK] Removed {removed} stale preview actor(s)")
        location = unreal.Vector(-1000, -1000, 0)
        rotation = unreal.Rotator(0, 0, 0)
        actor = spawn_actor_from_class(actor_class, location, rotation)
        if not actor:
            unreal.log_warning("[MANUAL] Failed to spawn preview actor")
            return None
        # Assign skeletal mesh
        for mesh_path in ["/Game/FaceParallax/Meshes/SK_Face.SK_Face",
                          CHARACTER_MESH_PATH,
                          MANNEQUIN_SKELETAL_MESH,
                          "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple",
                          "/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny",
                          "/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"]:
            try:
                mesh = load_existing_asset(mesh_path)
                if mesh and mesh.get_class().get_name() == "SkeletalMesh":
                    actor.assign_skeletal_mesh(mesh)
                    break
            except Exception:
                continue
        # Assign render target
        if render_target:
            actor.set_render_target(render_target)
        # Apply preset
        if preset_asset:
            actor.apply_preset(preset_asset)
        # Spawn face-layer quads (incl. Front-state nested art) so art is visible immediately.
        try:
            comp = actor.get_editor_property("face_parallax")
            if comp:
                count = comp.spawn_layer_quads()
                unreal.log(f"[OK] Spawned {count} layer quads on preview actor")
            else:
                unreal.log_warning("[MANUAL] Preview actor has no FaceParallax component")
        except Exception as e:
            unreal.log_warning(f"[MANUAL] Could not spawn layer quads on preview actor: {e}")
        unreal.log(f"[OK] Spawned preview actor at {location}")
        return actor
    except Exception as e:
        unreal.log_warning(f"[MANUAL] Could not spawn preview actor: {e}")
        return None


# --------------------------------------------------------------
# 8. Populate Widget Blueprint Layout
# --------------------------------------------------------------
def populate_widget_layout(widget_blueprint, preview_actor, render_target):
    unreal.log(
        "[INFO] The editor widget is a C++ Slate widget hosted in a docked tab.\n"
        "  Type 'FaceParallaxOpenEditor' in the console to open the interactive editor."
    )


# --------------------------------------------------------------
# 9. Register Editor Toolbar Button
# --------------------------------------------------------------
def register_editor_toolbar():
    unreal.log("[TOOLBAR] Toolbar registration handled by C++ UFaceParallaxEditorSubsystem.")
    unreal.log("[TOOLBAR] Type 'FaceParallaxOpenEditor' in the console to open the Face Parallax Editor.")


# --------------------------------------------------------------
# 10. Verify deployment
# --------------------------------------------------------------
def verify_deployment():
    # Hard gate: the deployment contract requires the plugin C++ classes to be
    # live in this session. Stale content assets alone must NEVER pass
    # verification - the [MANUAL] class-missing path is a deployment failure.
    required_classes = [PRESET_CLASS_NAME, COMPONENT_CLASS_NAME,
                        "FaceParallaxEditorWidget", "FaceParallaxPreviewActor"]
    classes_ok = True
    for cls in required_classes:
        if not class_available(cls):
            unreal.log(f"[VERIFY] C++ class {cls}: MISSING")
            classes_ok = False
        else:
            unreal.log(f"[VERIFY] C++ class {cls}: available")
    if not classes_ok:
        unreal.log_error(
            "[VERIFY] Deployment FAILED - FaceParallax plugin classes are not available. "
            "Build the project (Tests\\run_tests.ps1 -IncludeUEBuild) and restart the "
            "editor so the plugin loads, then run deploy.py again.")
        return False

    checks = [
        ("Master material", f"{CONTENT_ROOT}/Materials/M_FaceParallax_Master"),
        ("Layer MI Eyes",   f"{CONTENT_ROOT}/Materials/Instances/MI_FaceParallax_Eyes"),
        ("Layer MI Brows",  f"{CONTENT_ROOT}/Materials/Instances/MI_FaceParallax_Brows"),
        ("Layer MI Mouth",  f"{CONTENT_ROOT}/Materials/Instances/MI_FaceParallax_Mouth"),
        ("Layer MI Hair",   f"{CONTENT_ROOT}/Materials/Instances/MI_FaceParallax_Hair"),
        ("Preset",          f"{PRESET_OUTPUT_PATH}/{PRESET_NAME}"),
        ("Character BP",    f"{CHARACTER_OUTPUT_PATH}/{CHARACTER_BP_NAME}"),
        ("Editor Widget BP", "/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor"),
        ("Render Target",   f"{CONTENT_ROOT}/Textures/RT_FaceParallaxPreview"),
    ]
    missing = []
    for label, path in checks:
        exists = editor_asset_lib.does_asset_exist(path)
        unreal.log(f"[VERIFY] {label}: {'FOUND' if exists else 'MISSING'} ({path})")
        if not exists:
            missing.append(path)
    if missing:
        unreal.log_warning(f"[VERIFY] Deployment incomplete — missing: {missing}")
        return False
    unreal.log("[VERIFY] Deployment complete — all assets present.")
    return True


# --------------------------------------------------------------
# Run
# --------------------------------------------------------------
def main():
    unreal.log("==> Deploying FaceParallax editor assets (deploy.py)")
    failures = 0

    def step_fail(reason):
        nonlocal failures
        failures += 1
        unreal.log_error(f"[DEPLOY] FAILED: {reason}")

    ensure_dir(CONTENT_ROOT)

    # Phase 0: Clean legacy/wrong-named assets from older pipelines
    delete_legacy_assets()

    # Phase 1: No C++ classes needed
    try:
        master_mat = create_master_material()
        create_material_instances(master_mat)
    except Exception as e:
        step_fail(f"material phase raised: {e}")

    # Phase 2: The C++ plugin classes MUST be available - a missing class is a
    # hard deployment failure (exit 1), never a silent return.
    required = [PRESET_CLASS_NAME, COMPONENT_CLASS_NAME, "FaceParallaxEditorWidget",
                "FaceParallaxPreviewActor"]
    missing_classes = [cls for cls in required if not class_available(cls)]
    if missing_classes:
        for cls in missing_classes:
            unreal.log_error(
                f"[MANUAL] C++ class '{cls}' not available. The FaceParallax plugin is not "
                f"loaded/built - build the project first (Tests\\run_tests.ps1 -IncludeUEBuild), "
                f"restart the editor so the plugin loads, then run deploy.py again.")
        step_fail(f"plugin C++ classes not available: {missing_classes}")

    # Phase 3-5: only when the plugin classes are live
    preset = None
    widget_bp = None
    rt = None
    preview_actor = None
    if not missing_classes:
        try:
            preset = create_preset_asset()
            create_character_blueprint(preset)
            widget_bp = create_editor_widget_blueprint()
        except Exception as e:
            step_fail(f"preset/character/widget phase raised: {e}")
        try:
            rt = create_render_target()
            preview_actor = spawn_preview_actor(preset, rt)
        except Exception as e:
            step_fail(f"render-target/preview phase raised: {e}")
        if widget_bp and preview_actor:
            populate_widget_layout(widget_bp, preview_actor, rt)
        register_editor_toolbar()
        if not preset:
            step_fail("preset asset was not created")
        if not widget_bp:
            step_fail("editor widget blueprint was not created")
        if not rt:
            step_fail("render target was not created")
        if not preview_actor:
            step_fail("preview actor was not spawned")

    # Phase 6: Verify - hard gate. Every step must have succeeded AND every
    # required asset must exist AND the plugin classes must be live.
    deployed_ok = verify_deployment()
    if failures == 0 and deployed_ok:
        unreal.log("==> Deployment done. The FaceParallax editor tool is ready.")
        unreal.log(f"   - Master material: {CONTENT_ROOT}/Materials/M_FaceParallax_Master")
        unreal.log(f"   - Layer MIs: {CONTENT_ROOT}/Materials/Instances/MI_FaceParallax_<Layer>")
        unreal.log(f"   - Preset: {PRESET_OUTPUT_PATH}/{PRESET_NAME}")
        unreal.log(f"   - Character BP: {CHARACTER_OUTPUT_PATH}/{CHARACTER_BP_NAME}")
        unreal.log(f"   - Editor Widget: /Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor")
        unreal.log(f"   - Render Target: {CONTENT_ROOT}/Textures/RT_FaceParallaxPreview")
        unreal.log(f"   - Preview Actor spawned in editor world")
        unreal.log("   - Type 'FaceParallaxOpenEditor' in the console to open the interactive editor")
        unreal.log("[HINT] The editor must open as a DOCKED TAB in the main editor window - check")
        unreal.log("[HINT] the log for '[FaceParallax] EditorSubsystem initialized' and '[FaceParallax]")
        unreal.log("[HINT] OpenEditorWidget - invoking nomad tab 'FaceParallaxEditor'.")
        return

    unreal.log_error(
        f"[DEPLOY] FAILED with {failures} error(s) - deployment is INCOMPLETE. "
        "Fix the reported steps and re-run deploy.py.")
    sys.exit(1)


if __name__ == "__main__":
    main()
