"""Run inside the installed UE Editor. Generates only original Evolve assets locally."""
import unreal

material_path = "/Game/Evolve/Materials/M_DNA"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
material = unreal.load_asset(material_path)
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_DNA", "/Game/Evolve/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create DNA material")
    material.set_editor_property("two_sided", True)
    # Both lit base color and subtle emission preserve readable base colors.
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVertexColor)
    unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionMultiply)
    glow.set_editor_property("const_b", 0.2)
    unreal.MaterialEditingLibrary.connect_material_expressions(color, "", glow, "A")
    unreal.MaterialEditingLibrary.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant)
    roughness.set_editor_property("r", 0.45)
    unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save DNA material")

map_path = "/Game/Evolve/Maps/EvolveDemo"
if not assets.does_asset_exist(map_path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(map_path):
        raise RuntimeError("Could not create demo map")
    if not levels.save_current_level():
        raise RuntimeError("Could not save demo map")
if not assets.does_asset_exist(material_path) or not assets.does_asset_exist(map_path):
    raise RuntimeError("Generated content validation failed")
unreal.log("EVOLVE_ASSETS_READY")
unreal.SystemLibrary.quit_editor()
