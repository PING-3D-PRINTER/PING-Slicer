# -*- coding: utf-8 -*-
"""PING vendor profiles for PING Slicer V3.5 — v5 (aligned to PING Slicer 3.0 reference).
7 delta models; factory-confirmed Ø/height/nozzles; feed types FP=single(1)/FD=dual(2)/FF=quad(4).
3.0 alignment: dual-material defaults (multi default_filament_profile + extruder_offset),
3.0 filament lineup (PING PLA/SupPLA/PolyABS/SupABS + PETG/ABS/PA-CF), round bed_model, covers.
Slicing values from validated FD300 Orca 底稿; Start/End use newer M6050 multi-feed spec.
"""
import os, json, math, shutil, sys

# ⚠ DEPRECATED(2026-06-10)：機台/製程已改由 embed_params.py 從「F系列參數」正式交付嵌入
#   （25 機型/73 機台/73 製程）。本骨架產生器重跑會【刪除並覆蓋】正式 preset＋PING.json。
#   僅在重建 fdm 基底/骨架時使用：set PING_FORCE_GEN=1 再跑。
if os.environ.get("PING_FORCE_GEN") != "1":
    sys.exit("[DEPRECATED] gen_ping_profiles 會覆蓋 embed_params 的正式 F系列 preset。"
             "確定要跑請先 set PING_FORCE_GEN=1（詳見檔頭註解）")

REPO = r"D:\dev\2026claude\20260604 ORCA客製\PING-Slicer"
PROF = os.path.join(REPO, "resources", "profiles")
FLSUN = os.path.join(REPO, "tools", "ping", "base")  # self-contained base presets (no vendor dependency)
PINGDIR = os.path.join(PROF, "PING")

if os.path.isdir(PINGDIR):
    # keep already-copied binary assets (covers/bed/texture), wipe only json
    for sub in ("machine", "filament", "process"):
        d = os.path.join(PINGDIR, sub)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.endswith(".json"):
                    os.remove(os.path.join(d, f))
    for f in os.listdir(PINGDIR):
        if f == "PING.json":
            pass
for sub in ("machine", "filament", "process"):
    os.makedirs(os.path.join(PINGDIR, sub), exist_ok=True)

# name, Ø, height, nozzles, max_vel, max_accel, feeds, bed(stl,texture)
BED_S = ("PING_FD300_buildplate_model.stl", "ping_buildplate_texture.png")
BED_L = ("PING_FD800_buildplate_model.stl", "ping_buildplate_texture300.png")
MODELS = [
    ("FD300",     300, 270, ["0.25", "0.4", "0.6"], 400, 5000, 2, BED_S),
    ("FP300",     300, 270, ["0.2", "0.4", "0.6"],  400, 5000, 1, BED_S),
    ("FD450 Pro", 450, 600, ["0.4", "0.6", "1.0"],  400, 5000, 2, BED_S),
    ("FD600 Pro", 600, 580, ["0.4", "0.6", "1.0"],  400, 5000, 2, BED_L),
    ("FF600",     600, 580, ["0.4", "0.6", "1.0"],  400, 1500, 4, BED_L),
    ("FD800 Pro", 800, 600, ["0.4", "0.6", "1.0"],  250, 1500, 2, BED_L),
    ("FF800",     800, 600, ["0.4", "0.6", "1.0"],  250, 1500, 4, BED_L),
]
NZ = {"0.2": (0.1, 0.13, 16), "0.25": (0.125, 0.15, 20), "0.4": (0.2, 0.25, 30),
      "0.6": (0.3, 0.35, 45), "1.0": (0.5, 0.55, 70)}
# filament: name, inherits, nozzle°, bed°, fan%, type, is_support, fid
FILS = [
    ("PING PLA",     "fdm_filament_pla", 210, 60, 100, "PLA",  0, "GPINGPLA"),
    ("PING SupPLA",  "fdm_filament_pla", 210, 60, 100, "PETG", 1, "GPINGSPLA"),
    ("PING PolyABS", "fdm_filament_abs", 250, 100, 30, "ABS",  0, "GPINGPABS"),
    ("PING SupABS",  "fdm_filament_abs", 250, 100, 30, "HIPS", 1, "GPINGSABS"),
    ("PING PETG",    "fdm_filament_pet", 235, 75, 50, "PETG", 0, "GPINGPETG"),
    ("PING ABS",     "fdm_filament_abs", 250, 100, 30, "ABS",  0, "GPINGABS"),
    ("PING PA-CF",   "fdm_filament_pa",  255, 70, 30, "PA",   0, "GPINGPACF"),
]
# default filament slots per feed count (model + support pattern)
DEF_FIL = {1: ["PING PLA"],
           2: ["PING PolyABS", "PING SupABS"],
           4: ["PING PLA", "PING PolyABS", "PING SupABS", "PING SupPLA"]}
DEF_COL = {1: ["#000000"], 2: ["#000000", "#FFFFFF"],
           4: ["#000000", "#FF8000", "#FFFFFF", "#00A0A0"]}


def circle(R, n=72):
    return ["%gx%g" % (round(R*math.cos(2*math.pi*i/n), 4), round(R*math.sin(2*math.pi*i/n), 4)) for i in range(n)]


def start_gcode(R, z, E, feeds):
    y = R - 10
    L = ["G28 ;Home", "G90", "M82",
         "M140 S[bed_temperature_initial_layer_single]",
         "M104 S[nozzle_temperature_initial_layer] T0",
         "M190 S[bed_temperature_initial_layer_single]",
         "M109 S[nozzle_temperature_initial_layer] T0"]
    for k in range(feeds):
        yy = -(y - 2*k)
        a, b = ("X-50", "X50") if k % 2 == 0 else ("X50", "X-50")
        if feeds > 1:
            L.append("T%d" % k)
        L += ["G0 F%d %s Y%g Z%g" % (5000 if k == 0 else 800, a, yy, z), "G92 E0",  # 11-27 c-1127-F8K-01
              "G1 F800 %s Y%g E%d" % (b, yy, E), "G92 E0"]
    L += ["G1 Z1 E%d" % (E - 1), "G92 E0"]
    return "\n".join(L)


def end_gcode(R, h, feeds):
    # PING: 退料(抽料)移到 Klipper 韌體開關手動控制 → 結束只做正常收尾，
    #       不再自動退料/清噴頭/回溫。R/h/feeds 保留以維持呼叫相容(各機型結束統一)。
    return "\n".join([
        "G91", "G1 Z10 F3000 ; lift nozzle away from print", "G90",
        "M104 S0 ; hotend off", "M140 S0 ; bed off",
        "G28 X0 Y0 ; home all axes", "M84 ; disable motors",
    ])


def w(path, obj):
    json.dump(obj, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=4)


# base presets
shutil.copyfile(os.path.join(FLSUN, "machine", "fdm_machine_common.json"), os.path.join(PINGDIR, "machine", "fdm_machine_common.json"))
shutil.copyfile(os.path.join(FLSUN, "process", "fdm_process_common.json"), os.path.join(PINGDIR, "process", "fdm_process_common.json"))
FIL_BASE = ["fdm_filament_common.json", "fdm_filament_pla.json", "fdm_filament_abs.json",
            "fdm_filament_pet.json", "fdm_filament_tpu.json", "fdm_filament_pa.json"]
for fn in FIL_BASE:
    s = os.path.join(FLSUN, "filament", fn)
    if os.path.exists(s):
        shutil.copyfile(s, os.path.join(PINGDIR, "filament", fn))

# machine base (dual-material defaults live per-machine; here: klipper + retraction skeleton)
w(os.path.join(PINGDIR, "machine", "fdm_ping_common.json"), {
    "type": "machine", "name": "fdm_ping_common", "from": "system", "instantiation": "false",
    "inherits": "fdm_machine_common", "gcode_flavor": "klipper", "host_type": "octoprint",
    "use_relative_e_distances": "1", "single_extruder_multi_material": "1",
    "machine_pause_gcode": "PAUSE", "nozzle_type": "brass", "printer_structure": "delta",
    "z_hop": ["0.4"], "z_hop_types": ["Normal Lift"],
    "retraction_length": ["1.3"], "retraction_speed": ["20"], "deretraction_speed": ["20"],
    "retract_when_changing_layer": ["1"], "retraction_minimum_travel": ["1"], "wipe": ["0"],
    "retract_before_wipe": ["70%"], "retract_length_toolchange": ["2"], "use_firmware_retraction": "1",
    "extruder_clearance_radius": "65", "extruder_clearance_height_to_rod": "36",
    "extruder_clearance_height_to_lid": "140", "emit_machine_limits_to_gcode": "1",
    "thumbnails": "48x48/PNG,300x300/PNG", "thumbnails_format": "PNG"})

# process skeleton (validated 底稿 values)
w(os.path.join(PINGDIR, "process", "fdm_process_ping_common.json"), {
    "type": "process", "name": "fdm_process_ping_common", "from": "system", "instantiation": "false",
    "inherits": "fdm_process_common",
    "wall_loops": "3", "top_shell_layers": "4", "bottom_shell_layers": "4",
    "top_shell_thickness": "0.8", "bottom_shell_thickness": "0.8",
    "outer_wall_speed": "60", "inner_wall_speed": "60", "sparse_infill_speed": "60",
    "internal_solid_infill_speed": "60", "top_surface_speed": "60", "gap_infill_speed": "60",
    "travel_speed": "250", "initial_layer_speed": "50", "initial_layer_infill_speed": "50", "bridge_speed": "50",
    "outer_wall_acceleration": "2000", "inner_wall_acceleration": "5000", "default_acceleration": "5000",
    "travel_acceleration": "5000", "initial_layer_acceleration": "500", "top_surface_acceleration": "800",
    "sparse_infill_density": "15%", "sparse_infill_pattern": "gyroid", "infill_direction": "45",
    "seam_position": "back", "precise_outer_wall": "1", "wall_sequence": "inner wall/outer wall",
    "detect_overhang_wall": "1", "reduce_crossing_wall": "1", "reduce_infill_retraction": "1", "resolution": "0.012",
    "brim_type": "outer_only", "brim_width": "5",
    "enable_support": "1", "support_type": "tree(auto)", "support_threshold_angle": "30",
    "support_object_xy_distance": "0.3", "support_top_z_distance": "0.2", "support_bottom_z_distance": "0.2",
    "support_speed": "40", "support_interface_speed": "40",
    "support_interface_top_layers": "1", "support_interface_bottom_layers": "1", "support_interface_spacing": "0.2",
    "enable_prime_tower": "1", "prime_tower_width": "30", "prime_volume": "45"})

machine_models, process_list = [], [
    {"name": "fdm_process_common", "sub_path": "process/fdm_process_common.json"},
    {"name": "fdm_process_ping_common", "sub_path": "process/fdm_process_ping_common.json"}]
machine_list = [{"name": "fdm_machine_common", "sub_path": "machine/fdm_machine_common.json"},
                {"name": "fdm_ping_common", "sub_path": "machine/fdm_ping_common.json"}]
# 照片磚機的排除（2026-08-22 甲）——與 verify_profiles.py 的 _PHOTOTILE_COND 同字串，改要一起改
PHOTOTILE_MARK = "PHOTOTILE"
PHOTOTILE_EXCLUDE_COND = "printer_notes!~/.*%s.*/" % PHOTOTILE_MARK
all_machines = []
DEF_MAT = "PING PLA;PING PolyABS;PING SupABS;PING SupPLA;PING PETG;PING ABS;PING PA-CF"
gm = gp = 1

for (model, dia, hgt, nozzles, mv, ma, feeds, bed) in MODELS:
    R = dia/2.0
    mm = model
    bed_stl, bed_tex = bed
    machine_models.append({"name": mm, "sub_path": "machine/%s.json" % mm})
    w(os.path.join(PINGDIR, "machine", "%s.json" % mm), {
        "type": "machine_model", "name": mm, "model_id": "PING_" + model.replace("-", "_").replace(" ", "_"),
        "nozzle_diameter": ";".join(nozzles), "machine_tech": "FFF", "family": "",
        "bed_model": bed_stl, "bed_texture": bed_tex, "hotend_model": "",
        "default_materials": DEF_MAT})
    area = circle(R)
    off = ["0x0"] * feeds
    for nz in nozzles:
        lh, ilh, E = NZ[nz]
        mname = "%s %s nozzle" % (model, nz)
        pname = "%gmm @%s (%s)" % (lh, model, nz)   # 命名規範：去 Standard、材料不進製程名
        all_machines.append(mname)
        machine_list.append({"name": mname, "sub_path": "machine/%s.json" % mname})
        w(os.path.join(PINGDIR, "machine", "%s.json" % mname), {
            "type": "machine", "name": mname, "from": "system", "instantiation": "true",
            "setting_id": "PINGM%03d" % gm, "inherits": "fdm_ping_common",
            "printer_model": mm, "printer_variant": nz, "default_print_profile": pname,
            "alias": mm,   # active 標籤顯示乾淨機型名(無口徑)；口徑走噴嘴 chip

            "nozzle_diameter": [nz] * feeds, "printable_area": area, "printable_height": str(hgt),
            "bed_exclude_area": ["0x0"],
            "default_filament_profile": DEF_FIL[feeds], "extruder_offset": off, "filament_colors": DEF_COL[feeds],
            "retraction_length": ["1.3"] * feeds, "retraction_speed": ["20"] * feeds, "deretraction_speed": ["20"] * feeds,
            "retract_before_wipe": ["70%"] * feeds, "retract_length_toolchange": ["2"] * feeds,
            "retract_when_changing_layer": ["1"] * feeds, "retraction_minimum_travel": ["1"] * feeds,
            "wipe": ["0"] * feeds, "z_hop": ["0.4"] * feeds, "z_hop_types": ["Normal Lift"] * feeds,
            "machine_start_gcode": start_gcode(R, ilh, E, feeds),
            "machine_end_gcode": end_gcode(R, hgt, feeds),
            "machine_max_speed_x": [str(mv), str(mv)], "machine_max_speed_y": [str(mv), str(mv)],
            "machine_max_speed_z": ["20", "20"], "machine_max_speed_e": ["30", "30"],
            "machine_max_acceleration_x": [str(ma), str(ma)], "machine_max_acceleration_y": [str(ma), str(ma)],
            "machine_max_acceleration_z": ["500", "500"], "machine_max_acceleration_e": ["5000", "5000"],
            "machine_max_acceleration_extruding": [str(ma), str(ma)],
            "machine_max_acceleration_travel": [str(ma), str(ma)],
            "machine_max_acceleration_retracting": ["5000", "5000"],
            "machine_max_jerk_x": ["9", "9"], "machine_max_jerk_y": ["9", "9"],
            "machine_max_jerk_z": ["0.2", "0.4"], "machine_max_jerk_e": ["2.5", "2.5"]})
        gm += 1
        process_list.append({"name": pname, "sub_path": "process/%s.json" % pname})
        # 列印加速度規範(2026-06-07)：300機(dia==300)普/內/外=3000、450+=1500；travel兩者皆3000
        pacc = "3000" if dia == 300 else "1500"
        w(os.path.join(PINGDIR, "process", "%s.json" % pname), {
            "type": "process", "name": pname, "from": "system", "instantiation": "true",
            "setting_id": "PINGP%03d" % gp, "inherits": "fdm_process_ping_common",
            "layer_height": str(lh), "initial_layer_print_height": str(ilh),
            "line_width": str(nz), "compatible_printers": [mname],
            "default_acceleration": pacc, "outer_wall_acceleration": pacc,
            "inner_wall_acceleration": pacc, "travel_acceleration": "3000",
            # Scarf 斜接縫(隱形 Z 接縫)：外牆/起始高10%/長度8mm。Orca 2.3.2 無 "scarf slope gap" key。
            "seam_slope_type": "external", "has_scarf_joint_seam": "1",
            "seam_slope_start_height": "10%", "seam_slope_min_length": "8"})
        gp += 1

# 照片磚機不進通用料的相容清單（見下方 compatible_printers 註解）
all_machines_no_phototile = [m for m in all_machines if "照片磚" not in m]

# filaments (3.0-style minimal)
filament_list = [{"name": "fdm_filament_common", "sub_path": "filament/fdm_filament_common.json"}]
for fn in FIL_BASE[1:]:
    filament_list.append({"name": fn.replace(".json", ""), "sub_path": "filament/" + fn})
for (nm, base, nt, bt, fan, ftype, issup, fid) in FILS:
    fp = "filament/%s.json" % nm
    filament_list.append({"name": nm, "sub_path": fp})
    d = {"type": "filament", "name": nm, "from": "system", "instantiation": "true",
         "setting_id": fid, "filament_id": fid, "inherits": base, "filament_vendor": ["PING"],
         "filament_type": [ftype],
         "nozzle_temperature": [str(nt)], "nozzle_temperature_initial_layer": [str(nt)],
         "hot_plate_temp": [str(bt)], "hot_plate_temp_initial_layer": [str(bt)],
         "cool_plate_temp": [str(bt)], "cool_plate_temp_initial_layer": [str(bt)],
         "fan_max_speed": [str(fan)], "fan_min_speed": [str(fan)],
         "filament_retraction_length": ["2"],
         # 🔴 照片磚機排除（Eric 2026-08-22 裁「甲」）：照片磚的零回抽只寫在機器層，任何
         #    filament_retraction_length 非 nil 的料掛上去都會**靜默覆蓋**掉它（0730 已付過一次
         #    學費）。C++ 端 `compatible_printers`（明列）優先於 `compatible_printers_condition`
         #    ⇒ 這裡若照舊寫 all_machines，會把葉檔上的排除條件整個蓋掉、regen 一次就還原成壞的。
         "compatible_printers": all_machines_no_phototile,
         "compatible_printers_condition": PHOTOTILE_EXCLUDE_COND}
    if issup:
        d["filament_is_support"] = ["1"]
    # 換料塔最小清理量(2026-06-10 二修)：倍數=0 停用矩陣，每料此值控制——SupPLA=30、其餘(含SupABS)=15(最佳ABS 15/15 實證)
    d["filament_minimal_purge_on_wipe_tower"] = ["30" if nm == "PING SupPLA" else "15"]
    # PING: 線材預設色(2026-06-08 使用者定)：支撐材料=灰 / 其餘=橘
    _color = "#808080" if issup else "#EA4E16"
    d["filament_colors"] = [_color]
    d["default_filament_colors"] = [_color]
    w(os.path.join(PINGDIR, fp), d)

w(os.path.join(PROF, "PING.json"), {
    "name": "PING", "version": "01.00.00.09", "force_update": "0",
    "description": "PING 3D Printer (LINKIN FACTORY) delta printers",
    "machine_model_list": machine_models, "process_list": process_list,
    "filament_list": filament_list, "machine_list": machine_list})

print("models:", len(machine_models), "machines:", len(all_machines),
      "processes:", len(process_list), "filaments:", len(filament_list))
print("DONE")
