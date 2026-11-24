"""Unreal MCP로 M1 레벨과 상태 머티리얼을 만든다. 이미 있으면 중단한다."""
import json
from mcp_call import Unreal

T = "editor_toolset.toolsets."
SCENE, ASSET, MAT, OBJ = T + "scene.SceneTools", T + "asset.AssetTools", T + "material.MaterialTools", T + "object.ObjectTools"
MAP = "/Game/Twin/L_TwinM1"
MATERIAL = "/Game/Twin/M_TwinStatus"
u = Unreal()


def ref(path):
    return {"refPath": path}


def setp(obj, **values):
    if u.call(OBJ, "set_properties", instance=obj, values=json.dumps(values)) is False:
        raise RuntimeError(f"set_properties failed: {obj} {values}")


if u.call(ASSET, "exists", path=MAP) or u.call(ASSET, "exists", path=MATERIAL):
    raise SystemExit("already built: delete /Game/Twin assets first")

mat = u.call(MAT, "create_material", folder_path="/Game/Twin", asset_name="M_TwinStatus")
color = u.call(MAT, "add_expression", material_or_function=mat,
               expression_class=ref("/Script/Engine.MaterialExpressionVectorParameter"), x=-600, y=0)
setp(color, ParameterName="StatusColor", DefaultValue={"r": 0.0, "g": 0.8, "b": 1.0, "a": 1.0})
gain = u.call(MAT, "add_expression", material_or_function=mat,
              expression_class=ref("/Script/Engine.MaterialExpressionScalarParameter"), x=-600, y=220)
setp(gain, ParameterName="Intensity", DefaultValue=1.0)
mul = u.call(MAT, "add_expression", material_or_function=mat,
             expression_class=ref("/Script/Engine.MaterialExpressionMultiply"), x=-300, y=80)
u.call(MAT, "connect_expressions", from_expression=color, from_output_name="", to_expression=mul, to_input_name="A")
u.call(MAT, "connect_expressions", from_expression=gain, from_output_name="", to_expression=mul, to_input_name="B")
u.call(MAT, "connect_to_output", expression=mul, output_name="", material_property="MP_EmissiveColor")
u.call(MAT, "recompile", material_or_function=mat)

u.call(ASSET, "duplicate", path="/Engine/Maps/Templates/Template_Default", new_path=MAP)
u.call(ASSET, "save_assets", asset_paths=[MATERIAL, MAP])
u.call(SCENE, "load_level", level_path=MAP)
for i, mid in enumerate(("COATER-01", "PRESS-01", "FORMATION-01")):
    actor = u.call(SCENE, "add_to_scene_from_class", actor_type=ref("/Script/MetaTwin.TwinMachineActor"),
                   name=mid, xform={"location": {"x": 0, "y": (i - 1) * 420, "z": 150},
                                    "scale": {"x": 2, "y": 3, "z": 3}})
    setp(actor, MachineId=mid, StatusMaterial=ref(MATERIAL + ".M_TwinStatus"))
start = u.call(SCENE, "find_actors", name="PlayerStart", tag="", collision_channels=[])[0]
u.call(T + "actor.ActorTools", "set_actor_transform", actor=start, worldspace=True, xform={
    "location": {"x": 1150, "y": 0, "z": 260}, "rotation": {"pitch": 0, "yaw": 180, "roll": 0}})
u.call(ASSET, "save_assets", asset_paths=[MATERIAL, MAP])
print("built", MAP)
