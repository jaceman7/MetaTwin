"""Unreal MCP로 SF 공장 레벨을 만든다. 설비 배치는 Content/Data/plant_layout.json을 따른다.

이미 만든 에셋(/Game/Twin/Factory, /Game/Twin/L_TwinFactory)이 있으면 중단한다.
"""
import json
import math
import os

from mcp_call import Unreal

T = "editor_toolset.toolsets."
SCENE, ASSET, MAT, OBJ = T + "scene.SceneTools", T + "asset.AssetTools", T + "material.MaterialTools", T + "object.ObjectTools"
INST, ACTOR = T + "material_instance.MaterialInstanceTools", T + "actor.ActorTools"
DIR, MAP = "/Game/Twin/Factory", "/Game/Twin/L_TwinFactory"
CUBE, CYL = "/Engine/BasicShapes/Cube", "/Engine/BasicShapes/Cylinder"
HALL_X, HALL_Y, HALL_H = 2700, 2500, 1500  # 홀의 반폭, 반길이, 높이(cm)

u = Unreal()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT, "Content", "Data", "plant_layout.json"), encoding="utf-8") as f:
    MACHINES = json.load(f)["machines"]


def ref(path):
    return {"refPath": path}


def obj(path):
    return ref(f"{path}.{path.rsplit('/', 1)[1]}")


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


def rgb(r, g, b):
    return {"r": r, "g": g, "b": b, "a": 1.0}


def setp(target, **values):
    if u.call(OBJ, "set_properties", instance=target, values=json.dumps(values)) is False:
        raise RuntimeError(f"set_properties failed: {target} {values}")


def component(actor, cls):
    found = u.call(ACTOR, "get_components", actor=actor, component_type=ref(cls))
    return found[0]


def expr(mat, cls, x, y, **props):
    e = u.call(MAT, "add_expression", material_or_function=mat, expression_class=ref("/Script/Engine.MaterialExpression" + cls), x=x, y=y)
    if props:
        setp(e, **props)
    return e


def wire(a, b, pin):
    u.call(MAT, "connect_expressions", from_expression=a, from_output_name="", to_expression=b, to_input_name=pin)


def output(e, prop):
    u.call(MAT, "connect_to_output", expression=e, output_name="", material_property=prop)


def build_materials():
    mat = u.call(MAT, "create_material", folder_path=DIR, asset_name="M_Surface")
    output(expr(mat, "VectorParameter", -400, 0, ParameterName="BaseColor", DefaultValue=rgb(0.04, 0.05, 0.07)), "MP_BaseColor")
    output(expr(mat, "ScalarParameter", -400, 200, ParameterName="Metallic", DefaultValue=0.6), "MP_Metallic")
    output(expr(mat, "ScalarParameter", -400, 300, ParameterName="Roughness", DefaultValue=0.45), "MP_Roughness")
    output(expr(mat, "VectorParameter", -400, 400, ParameterName="Glow", DefaultValue=rgb(0, 0, 0)), "MP_EmissiveColor")
    u.call(MAT, "recompile", material_or_function=mat)

    mat = u.call(MAT, "create_material", folder_path=DIR, asset_name="M_Status")
    setp(mat, bUsedWithInstancedStaticMeshes=True)  # 컨베이어 셀
    color = expr(mat, "VectorParameter", -600, 0, ParameterName="StatusColor", DefaultValue=rgb(0.0, 0.8, 1.0))
    gain = expr(mat, "ScalarParameter", -600, 220, ParameterName="Intensity", DefaultValue=1.5)
    mul = expr(mat, "Multiply", -300, 80)
    wire(color, mul, "A")
    wire(gain, mul, "B")
    output(mul, "MP_EmissiveColor")
    output(expr(mat, "Constant3Vector", -300, -120, Constant=rgb(0.01, 0.01, 0.01)), "MP_BaseColor")
    u.call(MAT, "recompile", material_or_function=mat)

    # 바닥: 월드 좌표 2m 격자선. max(step(frac(x)), step(frac(y)))
    mat = u.call(MAT, "create_material", folder_path=DIR, asset_name="M_Floor")
    output(expr(mat, "Constant3Vector", -300, -300, Constant=rgb(0.018, 0.022, 0.03)), "MP_BaseColor")
    output(expr(mat, "Constant", -300, -200, R=0.28), "MP_Roughness")
    output(expr(mat, "Constant", -300, -120, R=0.2), "MP_Metallic")
    pos = expr(mat, "WorldPosition", -1500, 0)
    scale = expr(mat, "Multiply", -1300, 0, ConstB=1.0 / 200.0)
    wire(pos, scale, "A")
    frac = expr(mat, "Frac", -1150, 0)
    wire(scale, frac, "")
    lines = []
    for i, channel in enumerate("RG"):
        mask = expr(mat, "ComponentMask", -1000, i * 160, **{"R": channel == "R", "G": channel == "G", "B": False, "A": False})
        wire(frac, mask, "")
        sub = expr(mat, "Subtract", -850, i * 160, ConstB=0.5)
        wire(mask, sub, "A")
        dist = expr(mat, "Abs", -720, i * 160)
        wire(sub, dist, "")
        edge = expr(mat, "Subtract", -600, i * 160, ConstB=0.492)
        wire(dist, edge, "A")
        gain = expr(mat, "Multiply", -480, i * 160, ConstB=150.0)
        wire(edge, gain, "A")
        sat = expr(mat, "Saturate", -360, i * 160)
        wire(gain, sat, "")
        lines.append(sat)
    both = expr(mat, "Max", -220, 80)
    wire(lines[0], both, "A")
    wire(lines[1], both, "B")
    tint = expr(mat, "Multiply", -80, 80)
    wire(both, tint, "A")
    wire(expr(mat, "Constant3Vector", -220, 240, Constant=rgb(0.0, 0.10, 0.16)), tint, "B")
    output(tint, "MP_EmissiveColor")
    u.call(MAT, "recompile", material_or_function=mat)

    def instance(name, parent, vectors=None, scalars=None):
        mi = u.call(INST, "create", folder_path=DIR, asset_name=name, parent=obj(f"{DIR}/{parent}"))
        for k, v in (vectors or {}).items():
            u.call(INST, "set_vector_parameter", instance=mi, name=k, value=v)
        for k, v in (scalars or {}).items():
            u.call(INST, "set_scalar_parameter", instance=mi, name=k, value=v)

    instance("MI_Hull", "M_Surface", {"BaseColor": rgb(0.2, 0.23, 0.27)}, {"Metallic": 0.35, "Roughness": 0.5})
    instance("MI_Metal", "M_Surface", {"BaseColor": rgb(0.45, 0.47, 0.5)}, {"Metallic": 1.0, "Roughness": 0.3})
    instance("MI_Wall", "M_Surface", {"BaseColor": rgb(0.035, 0.042, 0.055)}, {"Metallic": 0.1, "Roughness": 0.7})
    instance("MI_Cell", "M_Status", {"StatusColor": rgb(0.15, 0.9, 0.75)}, {"Intensity": 1.2})
    instance("MI_Strip", "M_Status", {"StatusColor": rgb(0.15, 0.55, 1.0)}, {"Intensity": 5.0})


def spawn(cls, name, folder, x, y, z, yaw=0, pitch=0):
    actor = u.call(SCENE, "add_to_scene_from_class", actor_type=ref(cls), name=name,
                   xform={"location": vec(x, y, z), "rotation": {"pitch": pitch, "yaw": yaw, "roll": 0}})
    u.call(SCENE, "set_actor_folder", actor=actor, folder_path=folder)
    return actor


def box(name, folder, center, size, material, mesh=CUBE):
    actor = u.call(SCENE, "add_to_scene_from_asset", asset_path=mesh, name=name,
                   xform={"location": vec(*center), "scale": vec(*(s / 100 for s in size))})
    u.call(SCENE, "set_actor_folder", actor=actor, folder_path=folder)
    setp(component(actor, "/Script/Engine.StaticMeshComponent"), OverrideMaterials=[obj(f"{DIR}/{material}")])
    return actor


def build_hall():
    x, y, h = HALL_X, HALL_Y, HALL_H
    box("Floor", "Hall", (0, 0, -10), (2 * x, 2 * y, 20), "M_Floor")
    box("Ceiling", "Hall", (0, 0, h + 10), (2 * x, 2 * y, 20), "MI_Wall")
    for sign, tag in ((1, "A"), (-1, "B")):
        box(f"WallX_{tag}", "Hall", (sign * x, 0, h / 2), (20, 2 * y, h), "MI_Wall")
        box(f"WallY_{tag}", "Hall", (0, sign * y, h / 2), (2 * x, 20, h), "MI_Wall")
        box(f"StripX_{tag}", "Hall", (sign * (x - 14), 0, 420), (6, 2 * y - 200, 14), "MI_Strip")
        box(f"StripY_{tag}", "Hall", (0, sign * (y - 14), 420), (2 * x - 200, 6, 14), "MI_Strip")
        for i in range(5):
            py = -2000 + i * 1000
            box(f"Column_{tag}{i}", "Hall", (sign * (x - 70), py, h / 2), (110, 110, h), "MI_Hull")
            box(f"ColumnLight_{tag}{i}", "Hall", (sign * (x - 128), py, 620), (6, 18, 700), "MI_Strip")
    for i in range(4):
        by = -1500 + i * 1000
        box(f"Beam_{i}", "Hall", (0, by, h - 60), (2 * x, 60, 80), "MI_Hull")
    for i, py in enumerate((-1500, -500, 500, 1500)):
        for j, px in enumerate((-700, 700)):
            box(f"CeilingStrip_{i}{j}", "Hall", (px, py, h - 112), (700, 60, 12), "MI_Strip")
            light = spawn("/Script/Engine.RectLight", f"Light_{i}{j}", "Lighting", px, py, h - 130, pitch=-90)
            lc = component(light, "/Script/Engine.RectLightComponent")
            setp(lc, Mobility="Movable", IntensityUnits="Candelas", Intensity=420.0, SourceWidth=700.0, SourceHeight=120.0,
                 AttenuationRadius=2600.0, SpecularScale=0.15, LightColor={"r": 190, "g": 220, "b": 255, "a": 255}, VolumetricScatteringIntensity=0.6)
    fog = spawn("/Script/Engine.ExponentialHeightFog", "Fog", "Lighting", 0, 0, 0)
    setp(component(fog, "/Script/Engine.ExponentialHeightFogComponent"), FogDensity=0.012, FogHeightFalloff=0.05,
         bEnableVolumetricFog=True, VolumetricFogScatteringDistribution=0.4)
    spawn("/Script/MetaTwin.TwinEnvironmentActor", "Environment", "Lighting", 0, 0, 0)


def build_line():
    common = {"HullMaterial": obj(f"{DIR}/MI_Hull")}
    for i, m in enumerate(MACHINES):
        actor = spawn("/Script/MetaTwin.TwinMachineActor", m["id"], "Line", m["x"], m["y"], 0)
        setp(actor, MachineId=m["id"], DisplayName=m["name"], Kind=m["kind"], LineOrder=i,
             MetalMaterial=obj(f"{DIR}/MI_Metal"), StatusMaterial=obj(f"{DIR}/M_Status"), **common)
    for a, b in zip(MACHINES, MACHINES[1:]):
        dx, dy = b["x"] - a["x"], b["y"] - a["y"]
        dist = math.hypot(dx, dy)
        # 받침대(300x400) 바깥에서 시작하고 끝난다.
        half = 200 if abs(dy) > abs(dx) else 150
        actor = spawn("/Script/MetaTwin.TwinConveyorActor", f"Conveyor_{a['id']}", "Line",
                      a["x"] + dx / dist * half, a["y"] + dy / dist * half, 0, yaw=math.degrees(math.atan2(dy, dx)))
        setp(actor, SourceId=a["id"], Length=dist - 2 * half, CellMaterial=obj(f"{DIR}/MI_Cell"), **common)


def main():
    if u.call(ASSET, "exists", path=MAP) or u.call(ASSET, "exists", path=DIR):
        raise SystemExit(f"already built: delete {MAP} and {DIR} first")
    build_materials()
    # 조명까지 새로 구성하므로 빈 템플릿에 해당하는 기본 레벨을 복제하고 내용을 비운다.
    u.call(ASSET, "duplicate", path="/Engine/Maps/Templates/Template_Default", new_path=MAP)
    u.call(ASSET, "save_assets", asset_paths=[])
    u.call(SCENE, "load_level", level_path=MAP)
    for name in ("DirectionalLight", "SkyAtmosphere", "SkyLight", "ExponentialHeightFog", "VolumetricCloud", "SM_SkySphere", "Floor"):
        for actor in u.call(SCENE, "find_actors", name=name, tag="", collision_channels=[]):
            u.call(SCENE, "remove_from_scene", actor=actor)
    build_hall()
    build_line()
    start = u.call(SCENE, "find_actors", name="PlayerStart", tag="", collision_channels=[])[0]
    u.call(ACTOR, "set_actor_transform", actor=start, worldspace=True, xform={
        "location": vec(2520, 0, 1180), "rotation": {"pitch": 0, "yaw": 180, "roll": 0}})
    u.call(ASSET, "save_assets", asset_paths=[])
    print("built", MAP, len(u.call(SCENE, "find_actors", name="", tag="", collision_channels=[])), "actors")


if __name__ == "__main__":
    main()
