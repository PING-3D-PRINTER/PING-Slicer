#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PING 參數嵌入器 v2 — 完整 F 系列（11 機型家族、136 config）
吃參數端交付：D:\\dev\\2026claude\\20260603 切片參數\\PING Slicer V3.5\\F系列參數\\{機型}\\*.config
→ 依 OrcaSlicer 權威分類拆 machine / process → 寫 resources/profiles/PING/ → 重建 PING.json

用法（repo 根目錄）：
    python tools/ping/embed_params.py            # 用預設交付路徑
    python tools/ping/embed_params.py "<F系列參數資料夾>"

結構（2026-06-10 定案）：
- 7 個 FD 雙料家族（FD300 / FD300 E / FD300 Pro / FD300 E Pro / FD450/600/800 Pro）
  × 3 模式（雙料=家族名 / 單料頭 / 同進，各自獨立 printer_model）× 3 口徑。
  雙料 4 組合（PLA+SUP/PLA+PLA/ABS+SUP/ABS+ABS）共用機台/製程，只差線材選擇 →
  機台/製程一律取 PLA+SUP 為母檔。
- FP300 / FP300 E：單料機（口徑 0.2/0.4/0.6——單料機最小 0.2、雙料機最小 0.25）。
- FF600 / FF800（交付夾名帶 Pro、preset 名不帶）：四進一出四色，口徑 0.6/1.0；
  線材 0.6/1.0 數值不同 → 口徑別子 preset（@FF 0.6 / @FF 1.0，alias 顯示母名），
  FF600/FF800 同口徑共用。

軟體端 override（源檔尚未套用的裁定值；參數端源檔修正後可移除）：
- 加速度規範(2026-06-07)：300級=3000 / 450+級=1500 / travel=3000（FF 維持實機值，未裁定）
- Scarf 接縫(§8)：seam_slope_type=external / start 10% / min_length 8
  （注意 2.3.2 無 has_scarf_joint_seam key，external 即啟用）
- 單料頭/同進/FP 製程速度(2026-06-10 裁定)：travel 250 / 填充 60 / support 40
"""
import re, json, os, sys, io, shutil, glob, math

# Windows 主控台預設 cp950；輸出一被轉向到檔案／管線，Python 就退回 cp950，
# 最後那行印 ✅ 的 print 會 UnicodeEncodeError ⇒ **葉檔早就寫完了，exit code 卻是 1**
# （SOP_參數入版紀律 §T 的源頭修；在這之前自動化一律要外掛 PYTHONIOENCODING=utf-8）。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PINGDIR = os.path.join(REPO, "resources", "profiles", "PING")
PROF = os.path.join(REPO, "resources", "profiles")
PRESET_CPP = os.path.join(REPO, "src", "libslic3r", "Preset.cpp")
DEFAULT_SRC = r"D:\dev\2026claude\20260603 切片參數\PING Slicer V3.5\F系列參數"
# FF 同進/3in1 範本（無源 config、屬衍生模式）：已驗證手工檔收進 repo 當範本，重產時複製併入。
# 內含 machine（含 machine_model 底檔）／process／filament（3in1 專用 T4/T3 料）／cover。
# 2026-07-01：3in1 收 2 槽（T4/T3 走 filament_start_gcode）+ 同進(T5) 編進產生器（範本複製法）。
FF_EXTRA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "base", "ff_extra")
# ★ 照片磚範本（2026-07-08 收編）：垂直混色照片磚專用機（衍生自同進、Eric 實印驗證）。
# 無源 config——%APPDATA% 手工建置檔收進 repo 當範本（建置腳本見當時 scratchpad
# build_phototile_machine.py／extend_phototile_nozzles.py／build_fd300_phototile.py）。
# 範本已含：SEMM=1（槽位才會吃 64 槽 default_filament_profile）＋64 槽開滿＋
# Eric 四項製程預設（填充0%／上下殼0層／牆2圈／接縫背部）＋零回抽（retraction 全 0
# ＋use_firmware_retraction=0，防繞道 Klipper G10）＋seam_gap 0%/wipe_on_loops 0
#（2026-07-08 轉角 C 缺口定案）。機名含「同進」＝gcode 後處理閘門零改碼認得。
# 順序寫死＝重現 %APPDATA% 建置時的 setting_id（PINGM066-070／PINGP111-115）。
PHOTOTILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "base", "phototile")
# FF600 同進照片磚（Eric 2026-08-05 令「四料沒有機器可以試，請幫我建一下」）＝
# 照 SOP_加機型 §2 由 FF800 同進照片磚派生，FF600 專屬值（圓床多邊形／可印高度 580／
# 預擠線 Y／床模型 FD600）全部取自 FF600 同進的實檔，產法見 tools/ping/build_ff600_phototile.py。
PHOTOTILE_MACHINES = ["FF800 同進照片磚 0.6 nozzle", "FF800 同進照片磚 0.4 nozzle",
                      "FF800 同進照片磚 1.0 nozzle", "FD300 同進照片磚 0.4 nozzle",
                      "FD300 同進照片磚 0.6 nozzle",
                      "FF600 同進照片磚 0.4 nozzle", "FF600 同進照片磚 0.6 nozzle",
                      "FF600 同進照片磚 1.0 nozzle",
                      # 🆕 2026-09-07 Eric 裁「丙・補機型」（回報中心 #99）：FD 同進的其餘四個家族。
                      #   母版產法＝tools/ping/build_fd_pro_phototile.py（內建陽性對照）。
                      #   2026-09-12 隨開發線照片磚整區移植進出貨線（Eric 裁「進、標未實印」，牌 c-0912-PTI-01）。
                      "FD300 Pro 同進照片磚 0.4 nozzle", "FD300 Pro 同進照片磚 0.6 nozzle",
                      "FD450 Pro 同進照片磚 0.4 nozzle", "FD450 Pro 同進照片磚 0.6 nozzle",
                      "FD450 Pro 同進照片磚 1.0 nozzle",
                      "FD600 Pro 同進照片磚 0.4 nozzle", "FD600 Pro 同進照片磚 0.6 nozzle",
                      "FD600 Pro 同進照片磚 1.0 nozzle",
                      "FD800 Pro 同進照片磚 0.4 nozzle", "FD800 Pro 同進照片磚 0.6 nozzle",
                      "FD800 Pro 同進照片磚 1.0 nozzle"]
PHOTOTILE_PROCS = ["0.35mm @FF800 同進照片磚 (0.6)", "0.25mm @FF800 同進照片磚 (0.4)",
                   "0.45mm @FF800 同進照片磚 (1.0)", "0.2mm @FD300 同進照片磚 (0.4)",
                   "0.3mm @FD300 同進照片磚 (0.6)",
                   "0.25mm @FF600 同進照片磚 (0.4)", "0.35mm @FF600 同進照片磚 (0.6)",
                   "0.45mm @FF600 同進照片磚 (1.0)",
                   # 🆕 2026-09-07 同上。層高沿用 FD 家族既有規則（0.4→0.2／0.6→0.3／1.0→0.5）。
                   "0.2mm @FD300 Pro 同進照片磚 (0.4)", "0.3mm @FD300 Pro 同進照片磚 (0.6)",
                   "0.2mm @FD450 Pro 同進照片磚 (0.4)", "0.3mm @FD450 Pro 同進照片磚 (0.6)",
                   "0.5mm @FD450 Pro 同進照片磚 (1.0)",
                   "0.2mm @FD600 Pro 同進照片磚 (0.4)", "0.3mm @FD600 Pro 同進照片磚 (0.6)",
                   "0.5mm @FD600 Pro 同進照片磚 (1.0)",
                   "0.2mm @FD800 Pro 同進照片磚 (0.4)", "0.3mm @FD800 Pro 同進照片磚 (0.6)",
                   "0.5mm @FD800 Pro 同進照片磚 (1.0)"]
# ★ 側欄縮圖的「各機專屬圖」（Eric 2026-09-01 令「小圖示跟 FP300 也一起換」，牌 c-0901-IMG-01；源頭修 c-0925-CAR-01）：
#   這 7 台（13 個 model_id，變體共用本體那張）的 printer_preview 是型錄實拍去背後手工排版的 240x240
#  （底對齊 y=226、置中 x=120、框 110x208），不是家族封面縮圖 ⇒ §4c-2 對它們逐位元組複製範本、不重畫。
#   沒有這張表時，09-11 7c196099d3 的一次 regen 把 44a054b6dd 那 11 支靜默蓋回家族共用圖
#  （Classic 四台變成 FP300 的照片、DUAL 300 變成 FD300 的），一路帶到 T064 與客戶版 V3.6.2。
#   換圖＝換範本檔再 regen；新機型要專屬圖＝範本放進 base/printer_preview/ 並在這裡加一列。
#   FD300 Pro 同進照片磚（09-07）與 FP300 關門（09-08）是 0901 之後才加的變體，照「變體跟本體同一張」
#   跟本體（Eric 2026-09-25 照建議，決策 Q3）。
PREVIEW_OWN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "base", "printer_preview")
PREVIEW_OWN_ICON = {   # model_id → 範本檔名
    "PING_EDU_200": "PING_EDU_200.png", "PING_PING_200": "PING_PING_200.png",
    "PING_PING_270": "PING_PING_270.png", "PING_PING_300Plus": "PING_PING_300Plus.png",
    "PING_FP300": "PING_FP300.png", "PING_FP300_關門": "PING_FP300.png",
    "PING_FD300_Pro": "PING_FD300_Pro.png", "PING_FD300_Pro_同進": "PING_FD300_Pro.png",
    "PING_FD300_Pro_單料頭": "PING_FD300_Pro.png", "PING_FD300_Pro_同進照片磚": "PING_FD300_Pro.png",
    "PING_DUAL_300": "PING_DUAL_300.png", "PING_DUAL_300_同進": "PING_DUAL_300.png",
    "PING_DUAL_300_單料頭": "PING_DUAL_300.png",
}

# ---------- 1. OrcaSlicer 權威 key 分類 ----------
_src = open(PRESET_CPP, encoding="utf-8", errors="ignore").read()
def _ex(v):
    m = re.search(r"s_Preset_%s_options\s*\{(.*?)\n\}" % v, _src, re.S)
    return set(re.findall(r'"([a-zA-Z_][a-zA-Z0-9_]*)"', m.group(1))) if m else set()
PROC, FILA, MACH = _ex("print"), _ex("filament"), _ex("printer")
MACH |= _ex("machine_limits")   # printer_options() 會 append machine_max_*

# 專案層/身分 key——不進任何 preset（含 2.3.2 載入時會報 incorrect keys 的項目）
SKIP = {"from","name","version","inherits_group","different_settings_to_system",
        "print_settings_id","printer_settings_id","filament_settings_id",
        "print_compatible_printers","is_custom_defined",
        # machine 層會被剝除的專案 key（debug log: incorrect keys）
        "filament_ids","flush_multiplier","flush_volumes_matrix","flush_volumes_vector",
        "start_end_points",
        # process 層會被剝除的專案/盤面 key
        "bbl_calib_mark_logo","curr_bed_type","first_layer_print_sequence",
        "other_layers_print_sequence","other_layers_print_sequence_nums",
        "wipe_tower_x","wipe_tower_y","has_scarf_joint_seam"}
# 📌 2026-08-27 拆除紀錄（牌 c-0827-SIT-04）：rotate_solid_infill_direction 曾加進上面的 SKIP
#    當暫時護欄（牌 c-0827-SIT-03），來源清乾淨後依約拆除，恢復原行為（走 FORCE_P → process）。
#    它是上游 PR #9924（2025-06-22）淘汰的舊鍵，改用 solid_infill_rotate_template；現行
#    PING Slicer 匯出不會產生它，只剩 PrintConfig.cpp 的 handle_legacy 會轉換 ⇒ 若來源檔又
#    出現它，代表那批是舊版 slicer 匯出的，回頭查工程端 slicer 版本。
#    **不要再往 SKIP 加一次**——那是靜默的。改由下面第 3 節的 DEPRECATED 閘門有聲擋下
#    （Eric 2026-08-27 裁「A 案」，牌 c-0827-SIT-05）。
# 未列權威清單、需手動歸 machine 者（per-extruder/槽位類）
FORCE_M = {"deretraction_speed","extruder_colour","extruder_offset","max_layer_height",
    "min_layer_height","nozzle_diameter","retract_before_wipe","retract_length_toolchange",
    "retract_lift_above","retract_lift_below","retract_restart_extra","retract_restart_extra_toolchange",
    "retract_when_changing_layer","retraction_length","retraction_minimum_travel","retraction_speed",
    "wipe","wipe_distance","z_hop","default_filament_profile",
    "filament_colors","default_filament_colors"}
FORCE_P = {"Each_layer_prime_tower","overhang_speed_classic",
    "rotate_solid_infill_direction","tree_support_adaptive_layer_height",
    "tree_support_branch_diameter_double_wall",
    "layer_height","slice_closing_radius","filename_format"}

def cat(k):
    if k in SKIP: return None
    if k in FORCE_M: return "M"
    if k in FORCE_P: return "P"
    if k in MACH and k not in PROC: return "M"
    if k in FILA and k not in PROC: return "F"
    if k in PROC: return "P"
    return "P"

def split(cfg):
    b = {"M":{}, "P":{}, "F":{}}
    for k,v in cfg.items():
        c = cat(k)
        if c: b[c][k] = v
    return b

def fil_at(cfg_F, idx, feeds):
    """從扁平 filament 區取第 idx 槽 → 單槽 filament 預設值"""
    out = {}
    for k,v in cfg_F.items():
        if isinstance(v, list) and len(v) > idx:
            out[k] = [v[idx]]
        else:
            out[k] = v
    return out

def jdump(path, obj):
    json.dump(obj, io.open(path,"w",encoding="utf-8"), ensure_ascii=False, indent=4)

# ---------- 2. 機型家族定義 ----------
# (交付夾名, preset 機型名, kind)；kind: dual=雙料機3模式 / single=單料機 / ff=四進一出
# 順序＝精靈顯示順序（2026-06-10 使用者定）：單料 → 雙料 → 四料；同類依列印範圍小→大
FAMS = [
    ("FP300",       "FP300",       "single"),
    # FP300 關門（Eric 2026-09-08「FP300 應該也會有關門的模式，只是它是單料的，請幫我複製過去」；
    #   2026-09-11 補進出貨線，牌 c-0911-MCH-01）：同一台實體機、吃 FP300 交付 config，
    #   kind=single＝只出單料本體（比照 FD300 關門 的 dual1 只出雙料本體）。
    #   幾何走 BED_OVERRIDE，**比照出貨線的 FD300 關門**（見該處註解，不是開發線那版）。
    ("FP300",       "FP300 關門",  "single"),
    ("FD300",       "FD300",       "dual"),
    # 關門模式（Eric 2026-07-26）：門關著印（ABS 保艙溫）＝列印範圍剩直徑 200、高度不變；
    # 同一台實體機、吃 FD300 交付 config，只縮床＋預擠內移（BED_OVERRIDE）。
    # kind=dual1＝只出雙料本體（Eric 裁「關門版只需要 FD300」，不出 關門 同進/單料頭）。
    ("FD300",       "FD300 關門",  "dual1"),
    ("FD300 Pro",   "FD300 Pro",   "dual"),
    ("FD450 Pro",   "FD450 Pro",   "dual"),
    ("FD600 Pro",   "FD600 Pro",   "dual"),
    ("FD800 Pro",   "FD800 Pro",   "dual"),
    ("FF600 Pro",   "FF600",       "ff"),
    ("FF800 Pro",   "FF800",       "ff"),
]
# PING(2026-06-12)：P200+（過渡版）是「客戶專屬」機型，不進通用版 FAMS。
# 客戶精簡交付：環境變數 PING_ONLY=P200+ → FAMS 換成只剩 P200+ 這一台（其餘全砍、
# FF 線材也跳過）→ 產出「只有這台機器」的客戶版 build（讀 FP300 config + 床 override）。
PING_ONLY = os.environ.get("PING_ONLY", "").strip()
if PING_ONLY == "P200+":
    FAMS = [("FP300", "P200+", "single")]
elif PING_ONLY:
    raise SystemExit("不支援的 PING_ONLY=%r（目前僅 P200+）" % PING_ONLY)
# 資安版 E 機型（FD300 E / FD300 E Pro / FP300 E）不上 slicer 精靈——2026-06-10 使用者定
# （畫面太滿；參數端交付 config 保留，要上架時加回 FAMS 即可）
DEF_FIL_DUAL   = ["PING PLA - 220", "PING SupPLA"]
DEF_FIL_SINGLE = ["PING PLA - 220"]
# ★ 基礎支改名（Eric 2026-07-28 裁「把 PLA 改 PLA_210。它是給 FP300 使用的噴頭」・規格檔
#   _切片規則同步_來自pingslicer_TPE回抽速度與PLA210_20260728.md）：
#   「PING PLA」→「PING PLA - 210」（噴溫本就 210、值不動；名稱帶溫度尾碼與「- 220」一致）。
#   renamed_from ⚠ 字串（T004 鐵則）；filament_id/setting_id GPINGPLA 不動（同一支材料身份）；
#   alias 給獨立值防併組（3in1 教訓）。
# ★ 0728 v2 連動定案（Eric 回「連動」）：**單一出料機一律預設 210**＝FP300×3＋FD300 系
#   單料頭×6＋同進×6＋同進照片磚×2（機）＋其 model 檔；**雙料機維持 220**＝FD300/FD300 Pro
#   標準雙料×6＋關門×3（v1 誤把關門列單一出料、v2 更正＝關門是 FD300 雙料變體）。
#   P200+（客戶版）不在範圍＝維持 220 待裁。Classic 變體預設 Classic 220 是否改＝另案待裁。
# ★ 保留號段（2026-09-11 T039 打包棒移植進出貨線，牌 c-0911-NZ-01）——為什麼要有它：
#   出貨線交付夾在 2026-09-09 被 NZ 棒加了 0.25／0.2 口徑 config，而 0.25 在字串排序上排在 0.4 前面
#   ⇒ 無保留號段時 regen 會把新口徑插在既有項之前，既有 setting_id 被整段推走。
#   2026-09-11 隔離工作樹實測基線：既有項位移 321 筆（machine 90／process 231）。
#   ⚠ 出貨線與開發線的差別（照抄開發線必漏，SOP_參數入版紀律 §S-3）：
#     ① 出貨線有三條 emit 路徑（main／emit_classic／emit_ff_extra），開發線只有 main。
#        emit_classic 內又分兩段共四個 emit 點：_emit_one 閉包（Classic 底機）＋變體迴圈（同進／單料頭）。
#     ② gm／gp 在 emit_classic 是傳參，拿不到 main 的區域計數器 ⇒ 保留號段計數器必須是模組層級。
#   兩段互不相挪（任一段用滿再議）：
#     950–959＝中段機型（新機型插在既有機型中間，如日後的 FP300 關門）＝ EXT_RESERVED_MODELS
#     960–999＝既有機型後加口徑                                        ＝ EXT_RESERVED_NOZZLES
#   ⚠ 下面 15 組是 2026-09-11 由實測推導（＝磁碟有葉檔但不在 PING.json 的孤兒機器檔），
#     不是照抄開發線那 9 組——開發線沒有 Classic DUAL，會漏 DUAL 450/600/800 的 同進(0.25)／單料頭(0.2) 共 6 組。
#   🆕 950 段（中段機型）已於 2026-09-11 接線（牌 c-0911-MCH-01，為了把 FP300 關門 補進出貨線）：
#      用途＝「新機型整台插在 FAMS 中段」，不走保留號段的話會把後面所有 setting_id 整批推走。
#      與 960 段的分工：950＝整台新機型（_ext_*）、960/1100＝既有機型後加口徑（_nz_*）。
#      🔴 兩段刻意不共用計數器：中段機型的棧板雙生在主迴圈之後才 emit，若後加口徑也吃 950 段，
#      主迴圈裡新口徑的製程會先把號拿走，把中段機型的雙生推出該段（開發線 NZ 棒實測過這個坑）。
EXT_RESERVED_MODELS = ("FP300 關門",)
EXT_MODEL_START = 950
EXT_RESERVED_NOZZLES = {
    "FD450 Pro": ("0.25",), "FD450 Pro 同進": ("0.25",), "FD450 Pro 單料頭": ("0.2",),
    "FD600 Pro": ("0.25",), "FD600 Pro 同進": ("0.25",), "FD600 Pro 單料頭": ("0.2",),
    "FD800 Pro": ("0.25",), "FD800 Pro 同進": ("0.25",), "FD800 Pro 單料頭": ("0.2",),
    "DUAL 450 同進": ("0.25",), "DUAL 450 單料頭": ("0.2",),
    "DUAL 600 同進": ("0.25",), "DUAL 600 單料頭": ("0.2",),
    "DUAL 800 同進": ("0.25",), "DUAL 800 單料頭": ("0.2",),
    # 🆕 2026-09-11 Eric 令「DUAL 450/600/800 本體也加 0.25」。本體這三台走 4a-8（extras 尾段）emit，
    #    但 4a-8 是「逐機型」跑的：DUAL 450 的新 0.25 仍排在 DUAL 600／800 既有 0.4／1.0 之前
    #    ⇒ 實測照樣推走 8 支既有項。把清單裡的 0.25 挪到最後只降到 8、降不到 0，
    #    因為「最後」在那個迴圈裡是「該機型的最後」，不是「整段的最後」。
    #    ⇒ 正解就是本表：後加口徑一律走 960 段、不碰主計數器，位置怎麼排都不影響既有 id。
    "DUAL 450": ("0.25",), "DUAL 600": ("0.25",), "DUAL 800": ("0.25",),
}
EXT_NOZZLE_START      = 960    # 後加口徑的 machine（實用 15 格）
_ext_m = _ext_p = EXT_MODEL_START   # 950 段：整台新機型
EXT_NOZZLE_PROC_START = 1100   # 後加口徑的 process：出貨線一次就要 45 格，塞不進 960–999 的 40 格。
#   ⚠ 不讓它從 960 一路溢出到 PINGP1000——那會在段中途跨過位數邊界，
#     與本專案「繞開數位邊界而非踩過」的慣例相反（同 SOP_內部測試版發布 §7 bundle 99→100）。
#     另起 1100 段：整段四位數、不跨邊界、離既有最大 PINGP~330 夠遠，餘裕 900 格。
_nz_m = EXT_NOZZLE_START           # 模組層級＝三條 emit 路徑共用（本次修正的重點）
_nz_p = EXT_NOZZLE_PROC_START

def _nz_reset():
    global _nz_m, _nz_p, _ext_m, _ext_p
    _nz_m = EXT_NOZZLE_START
    _nz_p = EXT_NOZZLE_PROC_START
    _ext_m = _ext_p = EXT_MODEL_START

def _ext_take_m():
    global _ext_m
    v = "PINGM%03d" % _ext_m; _ext_m += 1; return v

def _ext_take_p():
    global _ext_p
    v = "PINGP%03d" % _ext_p; _ext_p += 1; return v

def _ext_proc(nm):
    """製程／雙生製程名是否屬 950 段機型（判準＝名字含 "@<機型> (" 子字串）"""
    return any(("@" + r + " (") in nm for r in EXT_RESERVED_MODELS)

def _nz_res(model, nz):
    """該 (機型, 口徑) 是否為既有機型的後加口徑（走 960 段）"""
    return nz in EXT_RESERVED_NOZZLES.get(model, ())

def _nz_take_m():
    global _nz_m
    v = "PINGM%03d" % _nz_m; _nz_m += 1; return v

def _nz_take_p():
    global _nz_p
    v = "PINGP%03d" % _nz_p; _nz_p += 1; return v

def _nz_proc(nm):
    """製程名是否屬後加口徑（判準＝名字含 "@<機型> (<口徑>)" 子字串）"""
    return any(("@%s (%s)" % (m, z)) in nm for m, zs in EXT_RESERVED_NOZZLES.items() for z in zs)


BASE_PLA_OLD, BASE_PLA_NEW = "PING PLA", "PING PLA - 210"
# ★ 高流量噴頭專用線材（2026-07-12 Eric 裁定：高流量＝噴頭屬性非機型屬性，回抽值落材料層；
# 規格 _切片規則同步_來自pingslicer_高流量噴頭線材_20260712.md）。不分口徑、不限機型
#（無 compatible_printers）。覆蓋值＝Eric %APPDATA% 權威樣本 4 鍵（規格表另列的擦拭/空駛
# 欄樣本未覆蓋＝吃機器層，照「權威＝樣本」抄 4 鍵，偏差已回報參數端）。
HFN_PLA = "PING PLA - 高流量噴頭"
HFN_SUP = "PING SupPLA - 高流量噴頭"
HFN_PETG = "PING PETG - 高流量噴頭"   # 2026-07-18 Eric：PETG 也開高流量支
# 2026-07-12 Eric 二裁：擦拭/空駛 4 欄也入覆蓋（8 鍵＝完整定義；取代同日稍早「4 鍵免加」）
# 2026-07-12 統一四值④：PA 0.2 兩支都套；噴溫 210/210 只 PLA 版（SupPLA 版溫度待 Eric 裁）
# 2026-07-30 Eric 三裁：高流量家族回抽長度 2→3（「雙料高流量與四料高流量都一致 3/30/30」）
HFN_OVERRIDES = {"filament_retraction_length": ["3"], "filament_retraction_speed": ["30"],
                 "filament_deretraction_speed": ["30"], "filament_retract_restart_extra": ["0.6"],
                 "filament_retraction_minimum_travel": ["3"], "filament_wipe": ["1"],
                 "filament_wipe_distance": ["5"], "filament_retract_before_wipe": ["100%"],
                 "enable_pressure_advance": ["1"], "pressure_advance": ["0.2"]}
# 2026-07-12 Eric 補裁：SupPLA 版噴溫同 210（高流量噴頭組整組統一 210/210，不套 SUP=220 慣例）
HFN_EXTRA = {HFN_PLA: {"nozzle_temperature_initial_layer": ["210"], "nozzle_temperature": ["210"]},
             HFN_SUP: {"nozzle_temperature_initial_layer": ["210"], "nozzle_temperature": ["210"]},
             # PETG 高流量（2026-07-19 Eric 二裁）：噴溫 230（原 235）；床 75 承 PING PETG 基底、
             # PA 0.2 由 HFN_OVERRIDES 帶。基底由 PETG-235 改 PING PETG（兩支值全等；235 支同日刪除）
             HFN_PETG: {"nozzle_temperature_initial_layer": ["230"], "nozzle_temperature": ["230"]}}
# FD450/600/800 Pro 出廠＝高流量噴頭 → 預設線材改高流量支（FD300 系/FP300 不動）
def def_fil_dual_for(base):
    return [HFN_PLA, HFN_SUP] if tier_of(base) == "450" else DEF_FIL_DUAL
def def_fil_single_for(base):
    if tier_of(base) == "450":
        return [HFN_PLA]
    if base == "P200+":
        return DEF_FIL_SINGLE   # 客戶版不在 0728 連動範圍＝維持 220（待裁）
    return [BASE_PLA_NEW]   # 0728 v2 連動：FP300＋FD300 系單一出料（單料頭/同進）＝210
# FF 四料線材改名（同裁定：綁機型＝錯）：「高流量 @FF」→「四料高流量噴頭」系、解除機型綁定。
# setting_id/filament_id 不變（同一支材料身份）；3in1 專用支不動。
# ★ 2026-08-16 Eric 二次改名：「四料高流量噴頭」→「**四料同進噴頭**」。
#   理由（Eric 原話）：**高流量指的是加熱本體長度比一般流量長**；四料噴頭與雙料高流量噴頭
#   加熱長度一樣、同屬高流量，四料只是多了兩個通道 ⇒ 舊名在暗示「流量比高流量更高」＝錯誤資訊。
#   這支真正的專屬條件是**四進一出（同進）**，所以改用模式命名。
#   ⚠ setting_id/filament_id 仍不變（同一支材料身份）；舊名走 renamed_from（字串，T004 鐵則）。
FF_FIL_ALIAS = {"PLA": "PING PLA - 四料同進噴頭", "SupPLA": "PING SupPLA - 四料同進噴頭"}
FF_FIL_OLD   = {"PLA": "PING PLA - 四料高流量噴頭", "SupPLA": "PING SupPLA - 四料高流量噴頭"}
# ★ 2026-08-16 Eric 裁「甲」：照片磚豁免——照片磚機 64 槽維持舊值（流量 30、清料 120），
#   故另開專用支綁死照片磚機；等 Eric 自己實印後再決定要不要跟著吃 50。
PT_FIL_PLA = "PING PLA(照片磚)"
# 🆕 2026-08-19 Eric 裁「給它專用支」：FD300 同進照片磚＝**雙料一般流量**硬體，與 FF 同進照片磚
#   （四進一出高流量）不是同一種噴頭 ⇒ 不能共用 PT_FIL_PLA（那支 PA 0.4／清料 120 是四料值）。
#   真因：照片磚機的**零回抽寫在機器層**（`retraction_length 0,0`＋`use_firmware_retraction 0`），
#   但**線材覆蓋會贏**。FD300 照片磚 64 槽原本吃共用的 `PING PLA - 210`，0819 一般流量改回抽 2
#   之後就會蓋掉零回抽 ⇒ 從 PLA-210 派生一支綁死 FD300 照片磚機的專用支，
#   噴溫／PA 0.08／流量／色全部原封不動，只把回抽長度留 `nil`（吃機器層 0）。
#   ⚠ 名字刻意是「(照片磚 FD300)」而非「(照片磚)」——後者是 is_hf 的判定字串，
#     這支是一般流量硬體，**不可**落進高流量家族（0816 改名連坐教訓的反向應用）。
PT_FIL_PLA_FD = "PING PLA(照片磚 FD300)"
# 🆕 2026-09-07（Eric 裁「丙・補機型」，回報中心 #99；2026-09-12 移植進出貨線）：**雙料高流量**的照片磚專用支。
#   為什麼還要第三支：實查各家族同進機的 default_filament_profile ——
#     FD300／FD300 Pro 同進 ＝ `PING PLA - 210`（一般流量）
#     FD450／600／800 Pro 同進 ＝ `PING PLA - 高流量噴頭`（**高流量**）
#   ⇒ 新補的三個高流量家族不能吃 PT_FIL_PLA_FD（那支從 PLA-210 派生、是一般流量值），
#     也不能吃 PT_FIL_PLA（那支是**四料**高流量，PA 0.4／清料 120）。
#   ⚠ 名字含「高流量」⇒ 天然落進 is_hf（回抽 3/30/30、額外回填 0.6），與四料照片磚支同待遇；
#     回抽長度仍由 is_pt 釘成 nil（吃機器層）。改名前先回頭看那兩行。
PT_FIL_PLA_FDHF = "PING PLA(照片磚 FD高流量)"

# ★ 照片磚專用線材 × 機型：**單一對照表**（2026-09-07 立）。
#   在此之前這件事散在兩處：4b-1e 的產生迴圈，與 4d-2 的「照片磚 default_materials」——
#   後者寫死成 `FD300 開頭用 FD 支、其餘用四料支`，補機型時 FD450/600/800 Pro
#   就被指到**四料**那支（PA 0.4／清料 120 是四進一出的值）。判準散兩處＝必然漂移。
#   ⇒ 兩處都改吃這張表。要加家族＝加一列。
#   ⓘ 出貨線寫成函式而非模組層常數：BASE_PLA_NEW／HFN_PLA 在本檔的定義位置晚於此處，呼叫時才取值。
def pt_fil_specs():
    return [
        # (專用支名, 基底 PLA, setting_id/filament_id, 吃它的 machine_model)
        (PT_FIL_PLA_FD,   BASE_PLA_NEW, "PINGFILPTPLAFD",
         ["FD300 同進照片磚", "FD300 Pro 同進照片磚"]),                                  # 雙料一般流量
        (PT_FIL_PLA_FDHF, HFN_PLA,      "PINGFILPTPLAFDHF",
         ["FD450 Pro 同進照片磚", "FD600 Pro 同進照片磚", "FD800 Pro 同進照片磚"]),        # 雙料高流量
    ]


def pt_filament_for_model(model):
    """這台照片磚機該吃哪支專用線材。表上沒有＝FF 家族（四進一出高流量）。"""
    for _nm, _base, _id, _models in pt_fil_specs():
        if model in _models:
            return _nm
    return PT_FIL_PLA
# 🆕 2026-09-07 Eric 四裁（回報中心 #153・reporter 劉勝賢・base_release T035）：
#   3in1 兩支**改名**而非另開新支——回報者原本要自己在機上建「PING PLA(3in1) - 高流量噴頭」
#   自訂支才有對應噴頭的料（#153 原文：「不需要自行額外建立或手動調整」）。
#   ⚠ 這兩支本來就落在 is_hf 高流量家族（回抽 3/30/30/0.6、清料 120，Eric 0723 確認
#     「3in1 走流量律」），改名是**把既有事實寫進名字**，不是改變家族歸屬。
#   ⚠ 名字用**帶空格**的 " - "（與回報者自建支同字串、也與全庫 "PING PLA - 高流量噴頭" 一致）。
#   setting_id／filament_id **不變**（PINGFIL3PLA06／PINGFIL3SUP06＝同一支材料身份，T004 慣例）；
#   舊名走 renamed_from（**字串**，T004 鐵則）——實際落在 ff_extra 範本檔內。
#   同裁②：PLA 支流量 20→**30**（SupPLA 支 Eric 未點名＝維持 12，同 0816「未點名不順手改」）。
TI_FIL_OLD = {"PLA": "PING PLA(3in1)",  "SupPLA": "PING SupPLA(3in1)"}
TI_FIL_NEW = {"PLA": "PING PLA(3in1) - 高流量噴頭",
              "SupPLA": "PING SupPLA(3in1) - 高流量噴頭"}
FF_FIL_RENAME = {}   # 舊名→新名（4b 填入，供 ff_extra/照片磚範本 default 引用改名）
for _k in ("PLA", "SupPLA"):
    FF_FIL_RENAME[FF_FIL_OLD[_k]] = FF_FIL_ALIAS[_k]          # 0816 本代改名
for _nz in ("0.4", "0.6", "1.0"):
    # 2026-07-18 口徑合一：舊「@FF 口徑」與「四料高流量噴頭 口徑」一律指到合併支（無口徑尾碼）
    FF_FIL_RENAME["PING PLA - 高流量 @FF %s" % _nz] = FF_FIL_ALIAS["PLA"]
    FF_FIL_RENAME["PING SupPLA - 高流量 @FF %s" % _nz] = FF_FIL_ALIAS["SupPLA"]
    FF_FIL_RENAME["%s %s" % (FF_FIL_OLD["PLA"], _nz)] = FF_FIL_ALIAS["PLA"]      # 前代帶口徑
    FF_FIL_RENAME["%s %s" % (FF_FIL_OLD["SupPLA"], _nz)] = FF_FIL_ALIAS["SupPLA"]
    FF_FIL_RENAME["%s %s" % (FF_FIL_ALIAS["PLA"], _nz)] = FF_FIL_ALIAS["PLA"]
    FF_FIL_RENAME["%s %s" % (FF_FIL_ALIAS["SupPLA"], _nz)] = FF_FIL_ALIAS["SupPLA"]
# 🆕 0907 #153 改名進同一張表＝一次拿到三件事：①3in1 機器檔的 default_filament_profile／
#    default_materials 自動改指新名（rename_ff_filament_refs）②4b 尾端清掉舊名殘檔
#    ③4d 過濾掉 PING.json 的舊名條目（防斷鏈）。⚠ quad/pt 分支只覆寫 PLA 系的四料/照片磚映射，
#    3in1 機兩者皆不成立（名字含 "3in1"）⇒ 這兩條在任何分支下都保持 3in1 舊名→3in1 新名。
for _k in ("PLA", "SupPLA"):
    FF_FIL_RENAME[TI_FIL_OLD[_k]] = TI_FIL_NEW[_k]
def _dedup_semilist(s):
    """分號清單去重（保序）。口徑合一（2026-07-18）後多口徑引用同映到合併支會重複——
    default_materials 重複無意義，去重＝921921c8 手工清 2 項的 regen-durable 版。"""
    seen, out = set(), []
    for x in s.split(";"):
        if x and x not in seen:
            seen.add(x); out.append(x)
    return ";".join(out)
def rename_ff_filament_refs(d, pt=False, quad=False):
    """機器/機型檔內的高流量 @FF 引用改新名（default_filament_profile／default_materials）。
    🆕 pt=True（照片磚機）：再把同進支改指**照片磚專用支**——Eric 2026-08-16 裁「甲」＝
       照片磚維持改名前的舊值（流量 30），不跟著吃 50，等他自己實印再決定。
       ⚠ FD300 同進照片磚吃的是「PING PLA - 210」、不在映射表內 ⇒ 不受影響（實查過）。
    🆕 quad=True（**四料本體機**，走 ff_extra 範本的 FF800 0.4 等）：改指「高流量噴頭」支
       ——`def_fil_ff()` 只管本函式外自產的那 5 支，範本機不經它，必須在這裡一起接住。"""
    m = FF_FIL_RENAME
    if pt or quad:
        tgt = PT_FIL_PLA if pt else HFN_PLA
        m = dict(FF_FIL_RENAME)
        m[FF_FIL_OLD["PLA"]] = tgt
        m[FF_FIL_ALIAS["PLA"]] = tgt
        for _n in ("0.4", "0.6", "1.0"):
            m["%s %s" % (FF_FIL_OLD["PLA"], _n)] = tgt
            m["%s %s" % (FF_FIL_ALIAS["PLA"], _n)] = tgt
            m["PING PLA - 高流量 @FF %s" % _n] = tgt
    v = d.get("default_filament_profile")
    if isinstance(v, list):
        d["default_filament_profile"] = [m.get(x, x) for x in v]
    dm = d.get("default_materials")
    if isinstance(dm, str):
        d["default_materials"] = _dedup_semilist(";".join(m.get(x, x) for x in dm.split(";")))
# PING(2026-07-02)：範本收編的口徑變體——machine 檔在 ff_extra（無交付 config，Eric 已實機驗收），
# 這裡只把口徑補進 machine_model 的 nozzle_diameter（精靈勾選）；default_materials 維持交付口徑不動。
EXTRA_MODEL_NOZZLES = {"FF800": ["0.4"]}
def def_fil_ff(nz):
    """FF **本體四料機**的機台槽位預設（default_filament_profile）。
    口徑合一（2026-07-18）：四槽預設＝合併支，不再帶口徑尾碼。
    🆕 **Eric 2026-08-13 裁：本體四料機四個槽全部是 PLA**——四色是各噴頭「各自獨立印」，
       四個槽都是本體材料，不該預裝支撐材；第四噴頭放支撐材的是 **3in1**
       （前三同動印 body＋第四印 SUP；軟體裡收成 2 槽，第 2 槽即是），同進則是單槽。
    ⚠ 本裁定**推翻**我方原本「FF800 0.4 是漏改」的推定——它才是唯一照規則做的那支
       （它走 ff_extra 範本、不經本函式），要改的是本函式產的 FF600×3＋FF800 0.6/1.0 共 5 支。
       ⛔ **參數不一致時不要用「多數決」判對錯**——教訓見 `SOP_preset連動與下拉過濾.md` §10。"""
    # 🆕 **Eric 2026-08-16 裁：四料本體機四槽改指「高流量噴頭」支**——四料分開進＝各噴頭
    #    各自獨立出料、不共用噴頭 ⇒ 不需要同進支那組「清料 120／PA 0.4」；用一般高流量噴頭支
    #    才自洽（加熱段長度相同＝同屬高流量，差別只在有沒有四進一出）。
    return [HFN_PLA]*4
def _backfill_filament_attrs(_tag):
    """★ 配料屬性顯式化（Eric 2026-08-13 裁・「選材料→製程自動收斂」批1）

    為什麼：家族軸判定讀 `filament_is_support`／`filament_soluble`（有支撐材⇒易拆；水溶⇒易拆水溶）。
      施工前 28 支 instantiated 中 **12 支缺 is_support、19 支缺 soluble**，靠 C++ 預設 false 與
      `fdm_filament_common` 繼承在運作 ⇒ **引擎行為是對的，但護欄與跨層檢查驗不到**（缺鍵型靜默）。
      寫成顯式值，verify 的 G1／G2 才有東西可驗，C++ 端也不必靠繼承鏈猜。

    做法：缺鍵者補 ["0"]；**已有顯式值一律不動**（PVA 的 1/1、Sup* 系的 is_support=1 全數保留）。
    ⚠ **不排除 Classic**：家族軸全機種適用，且這兩鍵是切片語意、不是 Klipper 指令
      （不觸 Marlin 隔離護欄——與 4b-2／4b-2b／4b-2c 排除 Classic 的理由不同，別照抄那兩行）。
    ⚠ 一律用 `fd[key] = [...]` 直接賦值，**不要重建 dict**——重排鍵序會產生全庫無關 diff。
    ⓘ 基底檔（`fdm_filament_*`，instantiation=false）天然不在 PING*.json glob 內，另加閘門雙保險。
    ⓘ 本函式**在 main() 內被呼叫兩次**（4a-0b 衍生前／4b-2g 衍生後），理由見 4a-0b 註解。
    """
    tot = n_is = n_sol = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if fd.get("instantiation") != "true":
            continue
        tot += 1
        touched = False
        if fd.get("filament_is_support") is None:
            fd["filament_is_support"] = ["0"]; n_is += 1; touched = True
        if fd.get("filament_soluble") is None:
            fd["filament_soluble"] = ["0"]; n_sol += 1; touched = True
        if touched:
            jdump(fp_path, fd)
    # 無條件印（同 4b-7）：每次 regen 都留下對帳數字，G1 的 28 支基準看 log 就知道
    print("  配料屬性顯式化 %s：instantiated %d 支｜補 is_support %d 支／補 soluble %d 支"
          % (_tag, tot, n_is, n_sol))
def seed_materials_ff(nz):
    """FF 機型 `default_materials`（可勾線材清單）的**種子值**——與上面的「槽位預設」是兩件事。
    刻意保留 SupPLA：使用者仍要選得到支撐料，且維持它排在 PLA 之後的既有顯示順序。
    ⚠ 0813 裁定只動「槽位預設」、沒動可勾清單 ⇒ 不順手改 UI 顯示順序（否則支撐料會依
      filament_list 序被推到第 9 位——`apply_default_materials()` 的 keep 保序邏輯所致）。"""
    return [FF_FIL_ALIAS["PLA"], FF_FIL_ALIAS["SupPLA"]]
# ⓘ 2026-08-07 起本常數只是「種子值」——最終 default_materials 由 4d-2 的
#   apply_default_materials() post-pass 全族重算（Eric 0807 裁）。死名 PING ABS - 250／
#   PING PolyABS（0725 ABS 整併已移除）在此一併清掉，post-pass 也會再擋一次。
DEFAULT_MATERIALS_FD = ("PING PLA - 220;PING SupPLA;PING PLA - 210;"
                        "PING SupABS;PING PETG;PING ABS;PING ABS(玻璃);PING PA-CF;"
                        # 高流量噴頭支入精靈預設清單（FD450+ 預設線材要看得見；任何 FD 換噴頭可選）
                        "PING PLA - 高流量噴頭;PING SupPLA - 高流量噴頭;PING PETG - 高流量噴頭")
# 床模型依機台直徑（300mm 原盤 XY 等比縮放產生；2026-06-10 修 FF600 黑色床板不滿版）
# 🆕 2026-09-07（回報中心 #112・Eric 裁「甲」）：床貼圖改 **SVG**。
#   起因＝「拉大看模型時底圖 LOGO 邊緣不銳利」。查下來不是解析度不夠（原 PNG 3192×3191、
#   墨跡帶 2297×489 放大三倍仍銳利），而是點陣圖在 3D 視角拉近時本來就會被放大。
#   引擎確實支援 SVG（`3DBed.cpp:501` 走 load_from_svg_file，載入時以 GPU max_tex_size 點陣化，
#   且有 `svg_source` uniform）⇒ 換成向量後縮放不再受解析度限制。
#   內容＝`resources/images/OrcaSlicer.svg` 原封搬入（純向量、7 個 path、零點陣、零 base64），
#   只外包一層 transform 縮放置中——**沒有重畫任何一筆**（CIS 核心原則 1）。
#   ⚠ 兩點視覺變更，Eric 2026-09-07 已裁「甲」：
#     ①字距：舊圖是 CIS 的「寬字距版」、新圖是「緊湊版」（2026 起的主商標）。
#     ②顏色：舊三份資產各用各的橘（床 #E2532E／SVG 原檔 #d24d21），**沒有一個是 CIS 正色**
#       ⇒ 一律校正到 #EA4E16／#202221（CIS §1 正色、§10 反「手打近似值」）。
#   產生器＝tools/ping/make_bed_texture_svg.py（同時產關門版＝同一份只改垂直位移）。
BED_TEXTURE = "ping_buildplate_texture.svg"

def apply_bed_texture(mm):
    """把床貼圖對齊 BED_TEXTURE（machine_model 專用）。

    🔴 為什麼需要這一支（2026-09-07 實錯後補，回報中心 #112 收尾）：
    FF 同進/3in1 與照片磚兩批機型走的是**範本複製法**（emit_ff_extra／emit_phototile
    直接把 tools/ping/base/ 下的 JSON 原樣搬出來、只重編 setting_id），
    **BED_TEXTURE 這個常數碰不到它們** ⇒ 0907 把床貼圖 PNG→SVG 時，
    42 個機型只換到 35 個，剩下 7 個（FF600/FF800 的 同進・3in1・同進照片磚 ＋ FD300 同進照片磚）
    還是舊的鋸齒 PNG，**而且不會有任何錯誤訊息**。
    改範本檔可以治這一次，但下次再換貼圖還會漏同樣 7 個 ⇒ 照「源頭優先」把對齊放進產生器。
    BED_OVERRIDE 仍然贏（P200+ 專屬貼圖、FD300 關門的關門版貼圖）。"""
    mm["bed_texture"] = BED_OVERRIDE.get(mm.get("name", ""), {}).get("bed_texture", BED_TEXTURE)
    return mm

BED_STL = {"FD300":"PING_FD300_buildplate_model.stl","FP300":"PING_FD300_buildplate_model.stl",
           "P200+":"P200+_buildplate_model.stl",   # 250 床盤（FD300 300 盤 ×0.833 置中）切齊網格
           "FD450":"PING_FD450_buildplate_model.stl",
           "FD600":"PING_FD600_buildplate_model.stl","FF600":"PING_FD600_buildplate_model.stl",
           "FD800":"PING_FD800_buildplate_model.stl","FF800":"PING_FD800_buildplate_model.stl"}
def bed_for(model):
    key = max((k for k in BED_STL if model.startswith(k)), key=len)
    return BED_STL[key]

# PING(2026-06-12)：衍生機型——吃別人的 G槽 config，只改「列印範圍」（床/高/預擠位置）。
# P200+（過渡版，名稱沿用客戶端既有名）：套 FP300 全套參數。
#   - printable_area＝直徑250（門打開的最大列印範圍）、printable_height＝200
#   - ⚠預擠線位置用「門關」常態直徑200（半徑100）計算：FP300 Y-140/-138（距前緣10mm）
#     → 平移 50mm 至 -90/-88（落在半徑100 內，門關著也不撞）。X±50 在範圍內不動。
# 床 STL 沿用 FD300（視覺略大、過渡機可接受）。
BED_OVERRIDE = {
    # bed_texture：P200+ 專屬床貼圖——在門開列印範圍 250 內多畫一圈「門關 200」橘色標示
    #（P200+_buildplate_texture.png＝原圖＋直徑200橘圈，0.8×網格半徑；使用者 2026-06-15 要求）
    "P200+": {"area_diameter": 250.0, "height": "200", "prime_y_shift": 50,
              "bed_texture": "P200+_buildplate_texture.png",
              "nozzles": ["0.4", "0.6"]},   # 去掉 0.2、只留 0.4/0.6（使用者 2026-06-15）
    # FD300 關門（Eric 2026-07-26 建立；2026-08-11 改床形）：高度不變（不帶 height 鍵＝沿用 FD300）；
    # 預擠直線 Y-140/-138 → +50 內移＝P200+ 門關 200 實機驗過的同款幾何。
    # ⚠ 預擠弧白名單（apply_fd300_prime_arc）全名比對不含本機型＝不會誤套 R144 弧（超出範圍）。
    # 🆕 **床形由「Ø200 圓」改為圓角三角形**（Eric 2026-08-11 給幾何條件＋看圖確認「圖面 OK」）：
    #    圓角貼合 Ø300、內切圓 Ø200 ⇒ 見 rounded_triangle_area()。**可印面積 +51%**。
    #    預擠線落點不受影響：Y-90/-88 在 x≈±50 處，該處三角形下緣為 y=-100 的直邊 ⇒ 仍在範圍內
    #    （原本靠「半徑 100 圓內」保證，現在靠「距床心 100 的下直邊」保證，餘裕相同）。
    #    🆕 **bed_model_center**（Eric 2026-08-11 夜裁「走乙案」）＝眼驗①處置。
    #       引擎預設把床身 3D 模型擺在 printable_area 的「外框中心」，那只有在床形對稱時才等於
    #       盤心；圓角三角形外框 Y[-100,+150] ⇒ 中心 (0,+25) ⇒ 圓盤被往後畫 25mm（純渲染，
    #       切片判定走 m_build_volume 真實多邊形、不受影響）。本鍵把真正的盤心明講出來，
    #       產生機台 preset 的 `bed_model_offset`（見 apply_bed_override）；C++ 端＝
    #       3DBed.cpp update_model_offset()。PING 圓盤機的盤心恆為床原點 ⇒ "0x0"。
    #       ⚠ 空值＝維持引擎原行為，所以其他機型一律零影響。
    #    🆕 **bed_texture 改用關門專屬貼圖**（Eric 2026-08-11 夜裁「logo 下移」）：床貼圖是
    #       「拉滿床形外框再用床形裁切」（3DBed.cpp:49-67 init_model_from_poly），原圖 logo
    #       垂直置中 ⇒ 上緣兩側被三角形斜邊切掉（實測溢出 5.03mm）。專屬圖＝原檔往床前緣
    #       平移 17mm（餘裕 +3.47mm），**只平移不重畫**（CIS 鐵則）。
    #       🆕 2026-09-07 隨 #112 一起改 SVG：平移仍是 17mm、做法從「PIL 搬像素」變成
    #       「同一份 SVG 只改 transform 的 ty」——更精確也更簡單。產生器改為
    #       tools/ping/make_bed_texture_svg.py（舊的 make_closeddoor_texture.py 已標示退役）；
    #       閘門仍吃 bed_texture_ink_extents.json。
    "FD300 關門": {"area_polygon": "rounded_triangle", "prime_y_shift": 50,
                   "bed_model_center": "0x0",
                   "bed_texture": "ping_buildplate_texture_closeddoor.svg"},
    # FP300 關門（2026-09-11 補進出貨線，牌 c-0911-MCH-01）：與上面 FD300 關門 **完全同幾何**。
    #   依據＝FP300 與 FD300 的床實查相同（Ø300／72 點／X,Y ±150／高 300），同一道門 ⇒ 同一塊可印區。
    # 🔴 **刻意不照抄開發線**：開發線那顆（724b5fc571）寫的是 area_diameter 200.0 的圓，
    #   因為開發線的 FD300 關門 至今仍停在 2026-07-26 的圓形版——它整支檔裡沒有 rounded_triangle、
    #   也沒有 closeddoor 貼圖。出貨線的關門床形是 Eric 2026-08-11 看圖裁的圓角三角（尖端朝 +Y、
    #   平邊朝門側）且已出貨。照抄來源會讓兩台關門機在同一套軟體裡長得不一樣，而且不會有任何錯誤訊息。
    #   （同 SOP_參數入版紀律 §S：跨線搬東西要先查目標線的現況，不是照抄來源。）
    "FP300 關門": {"area_polygon": "rounded_triangle", "prime_y_shift": 50,
                   "bed_model_center": "0x0",
                   "bed_texture": "ping_buildplate_texture_closeddoor.svg"},
}

# ---------- Classic 前代機（V3.6） ----------
# 名稱、尺寸與預設口徑以 PingSlicer V2.1 為權威；速度／回抽沿用同版品質檔。
# 這批皆為 Marlin 非 Klipper 機：不送 machine limits、不產生 M204、不用韌體回抽／PA。
# EDU 料管約 40cm，保留專屬 4/30；PING 270 約 70cm，使用 6/60。
CLASSIC_SPECS = [
    {"name":"EDU 200",  "src_model":"FP300",       "src_nozzle":"0.6", "src_diameter":300,
     "diameter":200, "height":"200", "nozzle":"0.6", "layer":"0.3",  "initial":"0.35",
     "retract":"4", "retract_speed":"30", "dual":False, "heated_bed":False, "speed_class":"single"},
    {"name":"PING 200", "src_model":"FP300",       "src_nozzle":"0.4", "src_diameter":300,
     "diameter":200, "height":"200", "nozzle":"0.4", "layer":"0.2",  "initial":"0.25",
     "retract":"2", "retract_speed":"20", "dual":False, "heated_bed":True,  "speed_class":"single"},
    {"name":"PING 270", "src_model":"FP300",       "src_nozzle":"0.4", "src_diameter":300,
     "diameter":270, "height":"300", "nozzle":"0.4", "layer":"0.2",  "initial":"0.25",
     "retract":"6", "retract_speed":"60", "dual":False, "heated_bed":True,  "speed_class":"single"},
    {"name":"PING 300+","src_model":"FP300",       "src_nozzle":"0.4", "src_diameter":300,
     "diameter":300, "height":"270", "nozzle":"0.4", "layer":"0.2",  "initial":"0.25",
     "retract":"2", "retract_speed":"20", "dual":False, "heated_bed":True,  "speed_class":"single"},
    {"name":"DUAL 300", "src_model":"FD300",       "src_nozzle":"0.4", "src_diameter":300,
     "diameter":300, "height":"270", "nozzle":"0.4", "layer":"0.2",  "initial":"0.25",
     "retract":"2", "retract_speed":"20", "dual":True,  "heated_bed":True,  "speed_class":"dual04"},
    {"name":"DUAL 450", "src_model":"FD450 Pro",   "src_nozzle":"0.6", "src_diameter":450,
     "diameter":450, "height":"600", "nozzle":"0.6", "layer":"0.25", "initial":"0.3",
     "retract":"3", "retract_speed":"30", "dual":True,  "heated_bed":True,  "speed_class":"dual06"},
    {"name":"DUAL 600", "src_model":"FD600 Pro",   "src_nozzle":"0.6", "src_diameter":600,
     "diameter":600, "height":"580", "nozzle":"0.6", "layer":"0.25", "initial":"0.3",
     "retract":"3", "retract_speed":"30", "dual":True,  "heated_bed":True,  "speed_class":"dual06"},
    {"name":"DUAL 800", "src_model":"FD800 Pro",   "src_nozzle":"0.6", "src_diameter":800,
     "diameter":800, "height":"580", "nozzle":"0.6", "layer":"0.25", "initial":"0.3",
     "retract":"3", "retract_speed":"30", "dual":True,  "heated_bed":True,  "speed_class":"dual06"},
]
CLASSIC_MODELS = [s["name"] for s in CLASSIC_SPECS]

# 口徑 → **FD 母檔**的層高／首層高（實查 FD300／FD450 Pro／FD600 Pro／FD800 Pro 的預設製程，2026-09-01）。
# Eric 2026-09-01 裁 Q1＝丙「直接跟 FD 來源的 0.4／1.0 走」⇒ 新補的口徑用這張表，不自己發明數字。
# ⚠ 既有口徑**不套這張表**：DUAL 450／600／800 的 0.6 現況是 layer 0.25／initial 0.3（Classic 專屬值，
#   與 FD 的 0.3／0.35 不同），那是已出貨的值，本棒不動它（要不要對齊另裁）。
SRC_LAYER_BY_NOZZLE = {"0.25": "0.125", "0.4": "0.2", "0.6": "0.3", "1.0": "0.5"}
SRC_INITIAL_BY_NOZZLE = {"0.25": "0.15", "0.4": "0.25", "0.6": "0.35", "1.0": "0.55"}


def _nozzles_of_spec(spec):
    """該台 Classic 的所有口徑（既有 + 補的），依數值排序。"""
    nzs = [spec["nozzle"]] + [e["nozzle"] for e in spec.get("extra_nozzles", [])]
    return sorted(dict.fromkeys(nzs), key=float)


# ★ Eric 2026-09-01 裁 Q2＝「四台一起」：四台 DUAL base 目前各只有一個口徑
#   （300＝0.4；450／600／800＝0.6），但它們的同進／單料頭變體都有三個 ⇒ 照 FD 來源補齊。
#   口徑取自對應 FD 母機實有者。
#   🆕 2026-09-11（Eric 令「DUAL 450/600/800 本體也加 0.25」，牌 `c-0911-NZ-01`）：三台大 DUAL 補 0.25。
#      起因＝NZ 棒 0909 替 FD450/600/800 Pro 加了 0.25 之後，本表的來源註記就過期了
#      （原寫「FD450/600/800 Pro＝0.4/0.6/1.0」）。既然丙案是「跟 FD 來源走」，來源多了 0.25，這裡就該跟上；
#      而先前只補了它們的 同進／單料頭 變體、沒補本體，是不對稱且沒有寫下理由的狀態。
#   🔴 更正（實測後）：原本以為「4a-8 在全庫最尾 ⇒ 零位移、不必進保留號段」——**錯的**。
#      4a-8 是逐機型跑的：DUAL 450 的新 0.25 排在 DUAL 600／800 既有 0.4／1.0 之前，實測推走 8 支。
#      這三台已一併列入 EXT_RESERVED_NOZZLES（見檔頭），走 960 段、不碰主計數器。
_CLASSIC_EXTRA_NOZZLES = {
    "DUAL 300": ["0.25", "0.6"],
    # 0.25 排在清單最後＝維持本檔「新增排最後」慣例。⚠ 但**光靠排序救不了**：實測
    #    插在前面位移 12 支、挪到各機型最後仍位移 8 支（跨機型仍插隊）⇒ 真正解法是保留號段。
    "DUAL 450": ["0.4", "1.0", "0.25"],
    "DUAL 600": ["0.4", "1.0", "0.25"],
    "DUAL 800": ["0.4", "1.0", "0.25"],
}
for _s in CLASSIC_SPECS:
    _extra = _CLASSIC_EXTRA_NOZZLES.get(_s["name"])
    if not _extra:
        continue
    _s["extra_nozzles"] = [{"nozzle": _n, "src_nozzle": _n,
                            "layer": SRC_LAYER_BY_NOZZLE[_n],
                            "initial": SRC_INITIAL_BY_NOZZLE[_n]} for _n in _extra]
del _s, _extra

def scale_circle_area(area_pts, target_diameter):
    """圓床 printable_area 是以床心(0,0)為原點的 72 點；FP300 半徑150 → 等比縮放至目標直徑"""
    s = (target_diameter / 2.0) / 150.0
    out = []
    for p in area_pts:
        x, y = p.split("x")
        out.append("%gx%g" % (round(float(x) * s, 4), round(float(y) * s, 4)))
    return out

def scale_circle_area_from(area_pts, source_diameter, target_diameter):
    """以來源機的實際直徑等比縮放圓床，供 Classic 從相近 Fast 機型複製。"""
    s = float(target_diameter) / float(source_diameter)
    out = []
    for p in area_pts:
        x, y = p.split("x")
        out.append("%gx%g" % (round(float(x) * s, 4), round(float(y) * s, 4)))
    return out

# ★ 圓角三角形床（Eric 2026-08-11 裁・FD300 關門真實可印範圍）
#   起因：關門機型原本用「直徑 200 的圓」近似，但門關著的實際可達範圍不是圓——
#   Eric 給的幾何條件有兩條，兩條就把形狀鎖死、無自由度：
#     ①三個圓角「貼合 Ø300」⇒ 圓角半徑 R_OUT = 150（＝原 FD300 滿床圓）
#     ②三角形「內切圓 Ø200」⇒ 三條直邊各距床心 R_IN = 100
#   ⇒ 形狀 ＝「內切圓 r=100 的正三角形」∩「R=150 的圓」＝三直邊＋三圓弧。
#   實得：弦長 223.61×3、外框 285.0(X)×250.0(Y)、面積 47,452mm²（比 Ø200 圓多 51%）。
#   ⚠ **必須是凸多邊形**：引擎 BuildVolume 對凹形（Type::Custom）的碰撞判定會**退回用凸包**
#     （BuildVolume.cpp:78-79 由 m_convex_hull 做分解）＝凹口不會被擋；本形狀為凸 ⇒ 走
#     Type::Convex 精準路徑（BuildVolume.cpp:400/543）。日後要改形狀，**凸性是硬條件**。
#   ⚠ 床身 STL 仍是圓盤（BED_S）＝刻意不換：深色圓盤是模型、可印區格線才是 printable_area，
#     兩者疊起來就是「圓盤上一塊三角亮區」＝Eric 2026-08-11 看圖確認的樣子。
# ★ 「這台是不是前代 Classic（Marlin）機」的**單一判準**（2026-09-11，牌 c-0911-MCH-01）。
#   抽成一處的理由＝今天實抓：verify 用口徑窮舉表判 Classic、產生器用本正則判，兩邊漂開之後
#   新加的 0.25／0.2 變體在 verify 那邊被當成 Klipper 機，90 項紅（見 SOP_參數入版紀律 §S-6）。
#   凡是「Fast 專屬的正規化」要略過前代機，一律用這一個，不要再各處手寫正則。
CLASSIC_MACHINE_RE = re.compile(r"^(EDU|DUAL|PING 2|PING 3)")
BED_TRI_R_IN, BED_TRI_R_OUT = 100.0, 150.0
BED_TRI_APEX_DEG = 90.0     # 尖端方位：90°＝朝 +Y（後方）、平邊朝前（門側）＝Eric 圖面
BED_TRI_ARC_STEP = 2.0      # 圓弧取樣間隔（度）；39 點，與原 72 點圓同量級

def rounded_triangle_area(r_in=BED_TRI_R_IN, r_out=BED_TRI_R_OUT,
                          apex_deg=BED_TRI_APEX_DEG, step=BED_TRI_ARC_STEP):
    """正三角形(內切圓 r_in) ∩ 圓(r_out) 的邊界點列，回傳 "XxY" 字串（同 printable_area 格式）。
    直邊不必補點：相鄰兩段圓弧的端點連線本身就是那條直邊（引擎以點列連成多邊形）。"""
    alpha = math.degrees(math.acos(r_in / r_out))       # 圓與邊的交點離切點的圓心角＝48.1897°
    out = []
    for c in (apex_deg, apex_deg + 120.0, apex_deg + 240.0):
        start, end = c - (60.0 - alpha), c + (60.0 - alpha)
        n = max(2, int(round((end - start) / step)) + 1)
        for i in range(n):
            a = math.radians(start + (end - start) * i / (n - 1))
            out.append("%gx%g" % (round(r_out * math.cos(a), 4), round(r_out * math.sin(a), 4)))
    return out
def apply_bed_override(model, mac):
    ov = BED_OVERRIDE.get(model)
    if not ov:
        return
    if isinstance(mac.get("printable_area"), list):
        if ov.get("area_polygon") == "rounded_triangle":   # FD300 關門（0811 改床形）
            mac["printable_area"] = rounded_triangle_area()
        else:
            mac["printable_area"] = scale_circle_area(mac["printable_area"], ov["area_diameter"])
    if "height" in ov:                       # FD300 關門：高度不變＝不帶 height 鍵
        mac["printable_height"] = ov["height"]
    # 床形不對稱時，把真正的盤心明講給引擎（Eric 2026-08-11 夜裁「走乙案」）——
    # 不寫的話 3DBed.cpp 會拿 printable_area 外框中心當盤心，圓盤就被畫歪。
    # ⚠ 只有 area_polygon 這類非圓非矩形的床需要；圓床/矩形床外框中心＝盤心，不寫＝行為不變。
    if ov.get("bed_model_center"):
        mac["bed_model_offset"] = [ov["bed_model_center"]]
    sg = mac.get("machine_start_gcode")
    if isinstance(sg, str):   # 預擠線 Y 往床心平移（門關直徑計），避免門關時超出床
        mac["machine_start_gcode"] = re.sub(
            r"Y(-1[34][0-9])", lambda m: "Y%d" % (int(m.group(1)) + ov["prime_y_shift"]), sg)

# ★ FD300/FP300 預擠「左側弧線」幾何定稿（2026-07-14 Eric 雙料實機定稿，取代 2026-07-09 初版
# 掃角 195-225°/R140-134/E15——全部作廢）：弧中點方位角 209°、弦長固定 100mm（半角＝asin(50/R)）、
# 半徑由外而內 144/142/140/138（間距 2mm）、I/J＝圓心(0,0)−弧起點、每弧 E＝直線版同量
#（0.4 基準 E30，既有口徑流量比：0.2→16／0.25→20／0.6→45）、Z＝各口徑首層高、
# 末弧 G1 Z1 E(e−1)（抬升＋回抽 1mm）。座標採定稿 gcode 字面值（規格檔
# _切片規則同步_來自pingslicer_FD300弧線預擠幾何定稿_20260714.md）——實機驗過，勿以公式重算竄動；
# 半徑固定故全口徑共用同一組座標，只有 Z/E 隨口徑。
_FD300_ARC_PTS = {  # r: (a=188.7° 側點, b=229.3° 側點)；G3 走 a→b、G2 走 b→a
    144: ((-142.35, -21.74), (-93.88, -109.21)),
    142: ((-140.49, -20.70), (-92.00, -108.17)),
    140: ((-138.63, -19.67), (-90.13, -107.13)),
    138: ((-136.77, -18.63), (-88.26, -106.09)),
}
def _fd300_arc_block(radii_tools, z, e):
    """radii_tools=[(半徑, T字串或None), ...] 由外而內；偶數索引 G3 去、奇數索引 G2 回。"""
    lines = []
    for i, (r, tool) in enumerate(radii_tools):
        a, b = _FD300_ARC_PTS[r]
        start, end, cmd = (a, b, "G3") if i % 2 == 0 else (b, a, "G2")
        if tool is not None:
            lines.append(tool)
        lines.append("G0 F%d X%.2f Y%.2f Z%s" % (8000 if i == 0 else 800, start[0], start[1], z))
        lines.append("G92 E0")
        lines.append("%s F800 X%.2f Y%.2f I%.2f J%.2f E%s" % (cmd, end[0], end[1], -start[0], -start[1], e))
        if i < len(radii_tools) - 1:
            lines.append("G92 E0")
    lines.append("G1 Z1 E%g" % (float(e) - 1))
    lines.append("G92 E0")
    return "\n".join(lines)

# ★ 預擠段正規化（2026-07-12 Eric 抓錯）：①預擠段「中途」travel 誤植 F8000（多一個 0，
# 熱料拖行過快）→ 除第一個接近 travel 保留 F8000 外，其餘 G0 F8000 一律改 F800。
# ②T0/T1 交替預擠的最後一條改成 T0（起印主料免多一次換刀）——交替序整組對調
#（T0,T1,T0,T1 → T1,T0,T1,T0）。僅動獨立成行的 T0/T1（M104/M107 的 T0 參數不受影響）；
# 3in1 的 T4/T3 與 FF 四色不在此規則（Eric 限定 T0/T1 交替）。
_T01_LINE = re.compile(r"^(T[01])\s*$", re.M)
def normalize_prime_lines(mac):
    sg = mac.get("machine_start_gcode")
    if not isinstance(sg, str) or "G0" not in sg:
        return
    # ① 中途 travel F8000 → F800（保留第一個）
    parts = sg.split("G0 F8000")
    if len(parts) > 2:
        sg = parts[0] + "G0 F8000" + "G0 F800".join(parts[1:])
    # ② T0/T1 交替 → 對調使結尾為 T0
    tools = _T01_LINE.findall(sg)
    if len(tools) >= 2 and set(tools) == {"T0", "T1"} and tools[-1] == "T1":
        sg = _T01_LINE.sub(lambda m: "T1" if m.group(1) == "T0" else "T0", sg)
    mac["machine_start_gcode"] = sg

def apply_fd300_prime_arc(model, mac):
    # 白名單（Eric 2026-07-14「目前只針對 FD300/FP300 即可」）：FD300 非 Pro 三模式＋FP300 新納入
    #（同 300 床殼直線預擠一樣撞門）。FD300 Pro／E 系、FP300 E 未提及＝不套；FD450+/FF 無門問題。
    # ⚠ P200+/FP200 絕不可納入：衍生自 FP300 但床只有 250（半徑 125）——R144 弧會超出床外；
    # 它的預擠本就按門關 200 另算（apply_bed_override 預擠內移），此處用「全名精確比對」防繼承。
    if model not in ("FD300", "FD300 單料頭", "FD300 同進", "FP300"):
        return
    sg = mac.get("machine_start_gcode")
    if not isinstance(sg, str) or "G28 ;Home" not in sg or "Y-140" not in sg:
        return
    m_z = re.search(r"G0 F8000 [^\n]*?Z([\d.]+)", sg)
    m_e = re.search(r"G1 F800 [^\n]*?E([\d.]+)", sg)
    if not m_z or not m_e:
        return
    z, e = m_z.group(1), m_e.group(1)   # 取直線版的首層高與每線 E（口徑流量比已在源檔）
    head = sg.split("G28 ;Home", 1)[0] + "G28 ;Home"
    if "M6050" in sg:
        head += "\nM6050 S0.5"
    if "\nT1\n" in sg:   # 雙料：4 弧交替、最後一條 T0（2026-07-12 Eric 定＝起印主料免換刀）
        block = _fd300_arc_block([(144, "T1"), (142, "T0"), (140, "T1"), (138, "T0")], z, e)
    else:                # 單料頭/同進/FP300：2 弧（外側 R144/142，由 4 弧定稿裁減、待實機驗）
        block = _fd300_arc_block([(144, None), (142, None)], z, e)
    mac["machine_start_gcode"] = head + "\n" + block

# ★ 預擠點升溫——【2026-07-20 Eric 裁回退・停用】實測失敗：冷噴頭先移到預擠第一點才升溫，
# 升溫過程殘料滲出、在第一點原地積一坨（前面沒有清噴頭步驟）；需配套「清噴頭」等其他機制
# 驗證通過才重新納入。函式與 Classic 守衛留存備查（main 4e 呼叫已停用），首發＝主線 04725e02。
# —— 以下為原設計說明（2026-07-19 Eric 裁定：開印前不預熱噴頭——預熱會滴料還要清料；主線移植）：
# header 只留熱床（M140/M190，前加 M117 熱床加熱中提示），G28 歸位＋移動全程冷噴頭；
# 移到預擠第一點（第一個接近 travel G0 F8000）後才 M109 升溫等到溫（M117 噴頭加熱中），
# 到溫後恆溫等 60 秒才預擠——每秒一則 M117 倒數（Eric UX 準則 FBK-21：顯示即時、不留資訊空白）。
# 結尾 M117 清提示。中文 M117 已於主線 CLI 實切驗證（PlaceholderParser 純文字行不炸）。
# 引擎不自動補溫（custom_gcode_sets_temperature 只查 custom gcode 內有無 M104/M109）。
# post-pass 套 machine/*.json（見 main 4e）；冪等（marker＝M117 噴頭加熱中）。
# ⚠ Classic 前代 8 機（DUAL/EDU/PING 2xx/3xx，Marlin）不套——隔離原則，Eric 未裁前保守排除。
_HEAT_HEAD = re.compile(r"^M10[49] S\[nozzle_temperature_initial_layer\][^\n]*\n", re.M)
_CLASSIC_PREFIX = ("DUAL ", "EDU ", "PING 2", "PING 3")
def apply_deferred_heating(mac):
    sg = mac.get("machine_start_gcode")
    if not isinstance(sg, str) or "M117 噴頭加熱中" in sg:
        return False
    # 交付源 gcode 髒空白（FF 四色：行尾/行首空白＋空行）→ 先逐行 strip 統一（乾淨檔＝零變化）
    sg = "\n".join(l.strip() for l in sg.split("\n") if l.strip())
    m109 = re.search(r"^M109 S\[nozzle_temperature_initial_layer\][^\n]*$", sg, re.M)
    if not m109 or not re.search(r"^G0 F8000 ", sg, re.M):
        return False
    m109_line = m109.group(0)
    sg = _HEAT_HEAD.sub("", sg)                                      # header 去 M104/M109
    sg = sg.replace("\nM140 S[", "\nM117 熱床加熱中\nM140 S[", 1)    # 熱床等待提示
    heat = ["M117 噴頭加熱中", m109_line]
    for s in range(60, 0, -1):                                       # 到溫後 60 秒恆溫，每秒倒數
        heat += ["M117 恆溫等待 %d 秒" % s, "G4 P1000"]
    heat.append("M117 預擠中")
    trav = re.search(r"^G0 F8000 [^\n]*$", sg, re.M)                 # 預擠第一點的接近 travel
    sg = sg[:trav.end()] + "\n" + "\n".join(heat) + sg[trav.end():]
    mac["machine_start_gcode"] = sg.rstrip("\n") + "\nM117"          # 收尾清空面板提示
    return True

def tier_of(base):
    # 300 級＝加速度3000＋一般流量（小機單/雙噴頭）；450+＝1500＋高流量（大機標配高流量噴頭）
    # P200+（過渡版）物理上是 250 小機單噴頭，與 FP300 同級（一般流量、勿套高流量）
    return "300" if base.startswith(("FD300","FP300","P200+")) else "450"

# 檔名主體（模式前綴之後的部分）——**唯一真實來源**。
# ⚠ 抽成常數的理由：2026-07-26 已連續兩次踩到「規則改了但某個角落沒跟上」
#（照片磚範本、Classic emit）。凡要改檔名格式，只改這一行。
FILENAME_BASE = "{printer_model}({nozzle_diameter[0]})_{input_filename_base}_{print_time_half_h}_{total_weight_g}.gcode"

def filename_tpl(mode_key):
    """輸出檔名模板：**模式_列印設備(口徑)_檔名_時間_重量**（2026-07-23 Eric 改版，取代 0610 版）。
    模式共 **7 種**（Eric 2026-07-26 定稿）：易拆／雙色／單料／四色／3in1／同進／照片磚。
    雙料依「槽2是否支撐材」自動判：易拆(裝SUP)／雙色(裝一般料)；單料頭/FP=單料。
    ⚠ **沒有「經典」模式**——Classic 前代機用它實際的模式（雙料→易拆、單料→單料）；
      機型名本身已帶 DUAL/EDU/PING 2xx 等字樣，再標「經典」對客戶無意義（Eric 2026-07-26 裁）。
    時間 {print_time_half_h}（0.5H 無條件進位）、重量 {total_weight_g}（整數克進位）
    ——兩佔位符 2026-07-23 進 Print.cpp，需該版之後 binary（profile 與 binary 必須同車出貨）。
    ⚠ 前綴一律包進 code block 字串字面值 {"X_"}：PlaceholderParser 模板的 rule 邊界
    （開頭、} 之後）遇非 ASCII 即 throw（pre-skip skipper）、裸中文前綴會炸
    「Non-ASCII7 characters...」；字串字面值是 lexeme[utf8char]、中文合法。"""
    base = FILENAME_BASE
    # 2026-07-23 Eric 改版：模式_列印設備(口徑)_檔名_時間0.5H_重量g（佔位符需同日後 binary，profile 與 binary 同車）
    if mode_key in EASY_COMBOS: return '{"易拆_"}' + base   # 組合別製程→前綴直判，免模板條件式（集合＝EASY_COMBOS 單一真實來源）
    if mode_key in ("PLA+PLA", "ABS+ABS"): return '{"雙色_"}' + base
    # Mix_ → 同進_（Eric 2026-07-26 裁）：7 個前綴裡只有這個是英文，與 易拆/雙色/單料/四色/經典
    # 不一致，同事看檔名會覺得不整齊。已查無消費者——切片端無人讀此前綴，機台端判混色是看
    # gcode 內容的 M6051/M6052 而非檔名 ⇒ 純顯示層改名，零功能影響。
    if mode_key == "同進":  return '{"同進_"}' + base
    if mode_key == "四色":  return '{"四色_"}' + base
    if mode_key == "3in1":  return '{"3in1_"}' + base   # FF 範本製程（emit_ff_extra 套用）
    # 照片磚自成一個模式（Eric 2026-07-26 裁）：它雖然跑在同進機上，但對使用者是獨立產品，
    # 檔名直接標「照片磚」比標「同進」有意義。
    if mode_key == "照片磚": return '{"照片磚_"}' + base
    return '{"單料_"}' + base   # 單料頭 / FP300

def proc_overrides(kind, base, is_single_mode):
    """軟體端裁定值 override（FF 不套）。
    2026-06-11 大清理（吃當日交付驗證）：加速度(3000/1500)、scarf=none、速度兩線新裁定
    （雙料/FP 60-80-150、單料頭/同進 50-60-150、口徑連動填充）——源檔已全套→override 拿掉
    （「源檔套完→拿掉 embed override」原則）。
    接縫與兩個加速度由 normalize_unified_values 集中套用，此處不再覆寫。"""
    return {}

# ★ 正式製程統一值（0715 立；①②於 2026-08-26 由 Eric 改裁上蓋，規格
# _切片規則同步_來自orca_主線保守加速度與接縫_20260715.md 之①②已失效）：
# ①首層流量比 **1**（🔴 Eric 2026-08-26 改裁，取代 0715 的 1.1）——原話：
#   「之前的測試有可能是因為高度沒有校好，導致我們用增加流量來補償。現在高度的設定那邊程式有修正了…
#     流量 1.1 反而太多了，因此我們再改回原來的 1。」⇒ 1.1 本來就是**替高度誤差做的補償**，
#   補償對象消失後就該退場。`set_other_flow_ratios` 維持開（其餘流量比仍是預設 1）。
# ②接縫位置＝**背面 back**（🔴 Eric 2026-08-26 改裁，取代 0715 的 aligned）
#   ⚠ enum 對映：UI「背面」=back；「對齊」=aligned；「背部對齊」=aligned_back。
#   ⚠ **照片磚 5 支不跟**（本函式本來就跳過照片磚）——它有自己的 0720 裁定「背面→對齊（V 溝配套）」，
#     那是為 V 溝幾何特調的，不在本次改裁範圍。Classic 前代則跟進（見 emit_classic，承 0725「跟進 F 系」）。
# ③空駛加速度 5000（20000 會造成馬達錯位／失步；空駛速度 250 不變）
# 全 PING 製程套（含棧板雙生＝從已正規化的 proc 派生自然繼承）；DL1016 不在 repo 自然跳過；
# 照片磚特調跳過（seam/換料路徑特調，已回報參數端）。
def normalize_unified_values(proc, ff=False):
    proc["set_other_flow_ratios"] = "1"
    proc["first_layer_flow_ratio"] = "1"
    proc["seam_position"] = "back"
    proc["travel_acceleration"] = "5000"
    # ④ jerk 對齊機器上限 7（Eric 2026-07-20 裁「jerk 改 7」）：機器檔動力學改 Klipper 實值後
    # FD/FP 上限 7、製程 40 → 每切必跳「抖動設定已超過…自動限制」警告；改 7 輸出不變
    #（本就被夾到 7，Klipper 又忽略 M205）＝純清警告。FF 上限 56＞40 不跳警告、維持 40
    #（改 7 反而真降 FF 轉角速）→ ff=True 跳過。Classic 前代不走本函式（Marlin 隔離，
    # jerk "0"=停用不得動）；照片磚不走本函式、FD300 照片磚由範本自帶 7。
    if not ff:
        for k in ("default_jerk", "infill_jerk", "initial_layer_jerk", "inner_wall_jerk",
                  "outer_wall_jerk", "top_surface_jerk", "travel_jerk"):
            if k in proc:
                proc[k] = "7"
    # ⑤ 爬坡品質（Eric 2026-07-24 裁「一併加入所有的參數」；工程端 FD300 同進 0.4
    # 「爬坡測試」A/B 對照實證懸空品質高提升）：懸空處降速開＋四段 50/50/25/10
    # （10%/25%/50%/75%；25% 段沿用原值 50＝對照兩側同值）＋橋接流量 0.95。
    # 線材側配套「懸空冷卻觸發閾值 25%」＝4b-2c sweep。
    # ⚠ 單位＝mm/s（coFloatOrPercent 裸數字；ratio_over=outer_wall_speed，要用 % 須帶符號）。
    # 值源自 Orca 端 A/B 對照實印檔，非 V2.1 換算——Cura 無四段結構（僅單一
    # wall_overhang_speed_factor 百分比），故不涉跨基準換算。
    # 照片磚/Classic/DL1016 不走本函式＝天然豁免（同 jerk 註）。
    # 🔴 2026-08-16 Eric 裁「A+C」並令「同步出貨線」，**推翻上面 0724 那批的降速部分**：
    #    A＝降速開關全庫關閉；C＝最慢那階 10 → **25**（不取 30：50% 段就是 25，
    #    取 30 會讓「懸空更多的那階反而更快」＝順序反了）。
    #    理由＝Eric 實測 0.6 黃銅（大口徑高流量）跑 10 mm/s 時噴頭停留過久、料在裡面滾沸。
    #    ⚠ 0724 的 A/B 實證是在 FD300 同進 0.4（小口徑）做的，本次一併關掉等於也關了那個好處；
    #      已提過 B（只關大口徑）方案，Eric 知情後仍選 A+C。要改回 B＝這裡加一條口徑判斷。
    #    ⚠ 與開發線 ping/v3.5 的 commit afdddff35a 同一裁定、同一組值，兩線必須一致。
    proc["enable_overhang_speed"] = "0"
    proc["overhang_1_4_speed"] = "50"
    proc["overhang_2_4_speed"] = "50"
    proc["overhang_3_4_speed"] = "25"
    proc["overhang_4_4_speed"] = "25"
    proc["bridge_flow"] = "0.95"
    # ⑥ 支撐臨界角 35（Eric 2026-07-25 裁，推翻 07-24 的 60）。照片磚/Classic/DL1016 豁免同上。
    # 🔴 這是 Orca 基準值（自水平量、越大支撐越多），= Cura/V2.1 的 55。
    #    兩線基準相反：Orca = 90 − Cura。改這個值前必讀 ping-slicer/orca-sync.md「Cura → Orca key 對照」。
    # ⚠ 07-24 的 60 係把 V2.1 語意的「支撐角 60」直接寫進 Orca key（實等於 Cura 30）＝支撐暴增
    #    （層高 0.3：判懸空的逐層外擴門檻 0.52mm→0.17mm，敏感度約 3 倍），現場回報「60 支撐太多」。
    #    ⚠ 出貨線從未被 60 汙染（本規則係首次新增，原全庫 30＝V3.0 底稿值）。
    proc["support_threshold_angle"] = "35"
    # ⑦ 增厚內部橋接關（Eric 2026-09-29 裁，開發中清單 #35；牌 c-0929-CAR-03）。原話：「增厚內部橋接選項需要取消
    #    若打勾 在內部橋接的部分層高會變厚 流量會變多」（T065 截圖 FD800 Pro 同進 0.6／0.3mm：內部橋接層線高 0.58）。
    #    源頭＝Orca 預設 true，參數端 GUI 匯出的 config 全帶 "1"、本檔照抄 ⇒ 371 支製程全明寫 1，查無任何裁定。
    #    關掉後內部橋接照一般層高＋上面 ⑤ 的 bridge_flow 0.95 印；增厚外部橋接 thick_bridges 本來就全 0，不動。
    #    ⚠ 照片磚不跟（Eric 同日 Q1「照建議」）：磚體 0% 填充、頂部第一層內部橋接要跨整個空腔＝增厚正在幫忙；
    #      照片磚本來就不走本函式（emit_phototile），要改得去那裡。Classic 前代複製 Fast 已正規化的製程＝自然跟進。
    proc["thick_internal_bridges"] = "0"
    return proc

# ★ 支撐介面值（Eric 2026-08-04 裁，V2.1「密度表示」→Orca「間距表示」換算；蓋 0714「間距一律 0.1」）：
# 頂部接觸面層數 4（0714 不變）；頂部接觸面間距分家族——
# ①一般支撐（同料，密度決定好不好拆）＝70% 密度等效口徑連動：Orca 間距＝線與線淨間隙、
#   密度＝線寬/(間距+線寬)（SupportParameters.hpp:104，同 0722 主體線距 ×9 的推導）
#   ⇒ 70% 需間距＝口徑×3/7 取兩位（0.25→0.11、0.4→0.17、0.6→0.26＝Eric 截圖錨值、1.0→0.43）。
#   分子用口徑名目值（FF 微調線寬不入分子，同 0722 家規）。替換集合含單料/同進 V3.0 源值 0.04
#   與 0714 產物 0.1（Eric 示範正是把同進 0.6 的 0.04 改 0.26）。
# ②易拆家族（SUP/PVA 專用支撐料，介面密＝表面品質、不影響拆）＝維持既值不動：
#   PLA+SUP/PVA 0.1（0714 規則照舊）、ABS+SUP 黃金 0.04、3in1 實心 0（承 0714/0722「不蓋」先例）。
# 支撐首層密度 raft_first_layer_density（支撐貼板首層；與 raft 共用鍵）＝10% 全庫（Eric 0804 裁；
#   主體類規則全庫套＝同 0722 ×9 先例，含易拆/3in1 範本 30%）；raft 機種（ABS 系/棧板 raft_layers≥1）
#   ＝raft 首層＝貼床要抓床，維持 100% 不動——呼叫點須在 combo_overrides 之後（raft_layers 已定）。
# Classic 前代由 Fast 複製自然繼承（支撐屬切片行為、韌體無關）；DL1016 不在 repo 自然跳過。
# ★ 易拆家族的權威組合集（單一真實來源）——檔名前綴判定與支撐 Z 間距共用同一份。
# ⛔ 不要在別處另寫一份 `cb.endswith("+SUP")`：那會**漏掉 PLA+PVA**（易拆水溶也是專用支撐料、
#    同樣不相熔）。本專案今天已經被「同一件事寫兩份、改一份忘另一份」咬過兩次（0816 改名連坐、
#    0813 槽位料改了幾何沒改），故此處集中定義。
EASY_COMBOS = ("PLA+SUP", "ABS+SUP", "PLA+PVA")

# ★ 支撐 Z 間距全庫統一（Eric 2026-08-17 裁：「一層層高」→**一般家族全部 0.2**）
# 起因＝Eric 截圖：FF800 四料本體機的頂/底 Z 間距是 0，而同為「一般」的 FD300 卻是 0.3。
# 全檔盤點現況散成五種值（0／0.125／0.2／0.3／0.5），來源有三個、彼此不知道對方存在：
#   ①雙料機走 combo_overrides：+SUP→0、其餘→**層高**（0.125／0.3／0.5 就是這樣來的）
#   ②**非雙料機不跑 combo_overrides**（呼叫點有 `if is_dual_machine`）⇒ 沿用母檔攤平值
#     （多數 0.2；FF600／FF800 四料本體機是 0）
#   ③fdm_process_common 只有 top=0.2、**bottom 整個缺鍵**
# ⇒ 本函式**無條件跑在所有機型**，把三個來源收斂成兩個值：易拆 0／一般 0.2。
# ⚠ 易拆的 0 不可動——「易拆＝沒有間隙」是 Eric 2026-08-11 定名時的語意本身（見 COMBO_HEAD 註解）。
# 🔴 順帶修掉一個真缺陷：FF600 三口徑＋FF800 0.6／1.0 那 5 支原本是 0——0813 裁1 把第 4 槽由
#   SupPLA 改成 PLA（＝支撐變同料、會相熔）卻沒同步改幾何值，Z 仍留在易拆的 0 ⇒ 支撐熔在件上拆不下來。
#   教訓與 0816「改名＝改分類鍵」同型：**改了分類的依據，就要改跟著分類走的值**，而 verify 不會紅。
def normalize_support_z(proc, easy_release):
    z = "0" if easy_release else "0.2"
    proc["support_top_z_distance"]    = z
    proc["support_bottom_z_distance"] = z
    return proc

def normalize_support_interface(proc, nozzle=None, easy_release=False):
    if proc.get("support_interface_top_layers") in ("1", "2"):
        proc["support_interface_top_layers"] = "4"
    if easy_release or nozzle is None:
        if proc.get("support_interface_spacing") in ("0.2", "0.4", "0.5", "1"):
            proc["support_interface_spacing"] = "0.1"
    else:
        if proc.get("support_interface_spacing") in ("0.04", "0.1", "0.2", "0.4", "0.5", "1"):
            proc["support_interface_spacing"] = "%g" % round(float(nozzle) * 3 / 7, 2)
    if str(proc.get("raft_layers", "0")) == "0":
        proc["raft_first_layer_density"] = "10%"
    return proc

# ★ 支撐幾何口徑連動（Eric 2026-07-17 裁；線距 2026-07-22 裁 ×9 新規蓋舊規）：
# 樹狀支撐分支直徑＝口徑×10；主體圖案線距＝口徑×9（支撐密度 10%＝Cura 線全庫密度等效。
# Orca 線距=線間淨間隙、密度=線寬/(線距+線寬) → ×9；7/17 舊規 ×8=12.5% 作廢）。
# 分子用口徑名目值（FF 微調線寬不入分子，0.4→3.6/0.6→5.4/1.0→9）；全口徑含 0.2/0.25 照公式；
# 照片磚維持獨立特調不套；Classic 由 Fast 複製自然繼承。
def normalize_support_geometry(proc, nozzle):
    # 分支直徑 2026-07-25 Eric 裁 ×10→×12（保守配方；引擎上限 10 ⇒ 1.0 口徑取 10）。
    proc["tree_support_branch_diameter"] = "%g" % min(float(nozzle) * 12, 10.0)
    # 分支距離 2026-07-25 新增＝口徑×6（原固定 5）：與直徑同為口徑連動 ⇒ 直徑/距離恆為 2.0，
    # 各口徑得到相同支撐幾何（固定 5 會讓 0.25 口徑比例失衡到 0.5＝分支不融合）。引擎範圍 1~10。
    proc["tree_support_branch_distance"] = "%g" % (float(nozzle) * 6)
    proc["support_base_pattern_spacing"] = "%g" % (float(nozzle) * 9)
    return proc


# ★ 樹狀支撐保守配方（Eric 2026-07-25 裁・主線 fa3ecb90 移植）
# 前提：預設支撐維持 normal(auto)+snug 不變；本組只在**使用者手動把樣式切成「混合樹」**後生效。
# 選型依據＝Orca 官方 tooltip：slim/organic 會積極合併分支大量省料，而 **hybrid 在大面積平懸空下
#   產生「類似普通支撐的結構」** ⇒ 最貼近 PING 已實機驗證的 normal+snug 行為；且易拆系（+SUP/3in1）
#   Z 間距 0＋專用支撐料（介面料槽 2）＝靠材料不相熔剝離，密實介面鋪得完整，正對症。
#   單料頭/同進系 Z 間距 0.2＋無專用料（同料）＝靠空氣間隙剝離，樹狀為點接觸 ⇒ 能用但非首選。
# ⚠ 欄位分組是引擎硬分的（ConfigManipulation.cpp:750-758）：branch_angle/distance/diameter 與
#   auto_brim/brim_width 屬「normal tree」（hybrid 吃）；帶 _organic 後綴者與 tip_diameter/
#   top_rate/angle_slow/branch_diameter_angle 屬 organic 專屬（切 hybrid 後不生效）。
def normalize_tree_support(proc):
    # 甲組：混合樹會吃的（保守＝穩定優先，代價為費料、難拆）
    proc["tree_support_branch_angle"] = "30"      # 原 40（原廠值）；角度小＝分支更垂直＝更不易垮。範圍 0~60
    proc["tree_support_auto_brim"] = "0"          # 必須關，brim_width 才生效
    #   （TreeSupport.cpp:2068 `!auto_brim ? tree_brim_width : 自動計算` ＝開著時手設值被完全忽略，
    #    且 ConfigManipulation.cpp:760 會把該欄位灰掉不可編輯）
    proc["tree_support_brim_width"] = "10"        # 原 3；樹狀為高瘦結構，底盤加寬＝不倒
    # ★ Eric 2026-07-27 裁「支撐牆數改零」（UI 支撐牆數＝本鍵，普通/樹狀共用・Tab.cpp:2663）：
    #   普通支撐 0＝無牆（SupportParameters.hpp:113 with_sheath=false＝本裁目的）；
    #   樹狀 0＝auto（TreeSupport.cpp:1552 需要處才加；organic 與 2424 路徑 max(1,·) 保底 1）
    #   ⇒ 0725「樹狀牆圈維持一圈」同鍵被本裁上蓋為 auto（無法分治，Eric 回報時已明示）。
    proc["tree_support_wall_count"] = "0"
    # tree_support_with_infill 不寫＝維持繼承 false（Eric 裁「填充維持空心」）
    # 乙組：organic 防呆（使用者忘記切樣式時會吃到——snug+樹狀會被引擎退回 default＝有機樹）
    # 🔴 diameter_organic 2→2.6 是 bug 修：Print.cpp:1532 硬性要求 ≥2×支撐線寬，
    #    FF600/FF800 的 1.0 口徑線寬 1.02 ⇒ 需 ≥2.04，原值 2 會讓那 4 支勾樹狀即切片報錯。
    proc["tree_support_branch_diameter_organic"] = "2.6"
    proc["tree_support_branch_angle_organic"] = "40"   # 原 60＝引擎上限（最水平＝最易垮），回原廠 40
    return proc

# ★ 普通支撐配方（Eric 2026-07-22 七裁＋同日二裁擴及易拆・主線 c7d22ac8/e0eace04 移植）：
# 行為四項（類型/獨立層高/樣式/圖案）全支撐同套；幾何 XY 分流：一般=口徑×1、
# 易拆維持 7/14 裁（PLA+SUP/3in1=口徑×0.75、ABS+SUP 黃金 0.5）。
# 照片磚/DL1016 特調豁免（不經此函式）；Classic 製程由 Fast 複製自然繼承（支撐屬切片行為、韌體無關）。
def normalize_support_recipe(proc, nozzle, easy_release=False):
    proc["support_type"] = "normal(auto)"
    proc["independent_support_layer_height"] = "0"
    proc["support_style"] = "snug"
    proc["support_base_pattern"] = "rectilinear"
    if not easy_release:
        proc["support_object_xy_distance"] = "%g" % round(float(nozzle) * 1.0, 2)
    return proc

# ★ 牆速/填充正規化（2026-07-05，吃參數端規格 _切片規則同步_來自pingslicer_牆速填充_20260703.md）
# Eric 定「公司不在意快、在意穩定品質」→ 牆（外/內）降速求品質、吞吐靠填充高速＋高加速。
# 全 Fast 系列（FD/FF/FP）標準製程統一：
#   外牆 60（統一，含單料/同進/四色）；內牆 min(現值,80)（只降不升——單料/同進內 60 不升）；
#   填充 100（2026-07-19 Eric 裁 150→100 全機型，新規蓋舊規）；
#   填充加速度 sparse_infill_acceleration=5000（2026-07-15 由 10000 下修，避免錯位／失步）。
#   首層速度 initial_layer_speed 不動；solid/top/gap 不在規格 → 交回源檔（順帶對齊 Cura V2.1）。
# ⚠ 此規（2026-07-03）取代舊 HF_SPEED「速度＝口徑×流量上限、75/100/150 暫定」的 2026-06-11 裁定
#   （新規蓋舊規：牆慢統一 60，不再逐口徑寫死高流量速度）。
# 例外（不套、維持材料特例）：PA-CF crater（材料專屬製程另出，is_pacf=True 跳過）；
#   TPE 軟料 40/70（目前無 TPE 製程檔，故①不觸及；未來若出 TPE 製程一樣要跳過）。
def normalize_fast_speed(proc, is_pacf=False, preserve_sparse_acceleration=False):
    if is_pacf:
        return proc   # PA-CF 走自己的速度組，見 pacf_overrides（0826 起實作，值＝Eric 實印 60/60/60）
    try:
        cur_inner = float(proc.get("inner_wall_speed", "80"))
    except (TypeError, ValueError):
        cur_inner = 80.0
    proc["outer_wall_speed"] = "60"
    proc["inner_wall_speed"] = "%g" % min(cur_inner, 80.0)
    proc["sparse_infill_speed"] = "100"
    if not preserve_sparse_acceleration:
        proc["sparse_infill_acceleration"] = "5000"
    return proc

# ★ 照片磚製程「速度 ×2 ＋ 絨毛表面」（Eric 2026-09-12 裁「當正式預設」，牌 `c-0912-PTI-08`）
#   來歷＝0912 實切並上機的那一件：50 mm 雙料白狗磚（FD300 同進照片磚 0.4），
#   250 層／`verify_cycle_gcode --mode dual` PASS／與 1× 版 M6051 1816 及噴溫回抽牆頂底逐項相同。
#
# 🔴 三件實測事實，動這段之前先知道：
#   ① **「×2」不等於「快兩倍」，多數口徑其實被線材流量上限夾住。**
#      Orca 的 `filament_max_volumetric_speed` 是硬上限，超過就**靜默降速**、不報錯。
#      照片磚線材：`PING PLA(照片磚 FD300)` 繼承 `fdm_filament_pla` ＝ **12 mm³/s**；
#      `PING PLA(照片磚)`／`PING PLA(照片磚 FD高流量)` ＝ **30**。
#      體積流量 ≈ 線寬 × 層高 × 速度（照片磚線寬＝口徑×1.0）⇒ 外牆 120 時：
#        · FD300 0.4 ：0.4×0.2 ×120 ＝ 9.6  < 12 ⇒ **真的生效**（Eric 驗的就是這台）
#        · FD300 0.6 ：0.6×0.3 ×120 ＝ 21.6 > 12 ⇒ 夾到 ~67 mm/s（≈原本 60，等於沒變）
#        · Pro/FF 0.6：0.6×0.35×120 ＝ 25.2 < 30 ⇒ 生效
#        · 1.0 口徑  ：1.0×0.45~0.5×120 ＝ 54~60 > 30 ⇒ 夾到 ~67；**原本 75 就已經超了**
#      ⇒ 真正受益的是 0.4（與部分 0.6）；1.0 口徑改了等於沒改。**不會壞，只是沒效果。**
#   ② **估時反而變長。** 同一件 50 mm 磚實測：1× ＝ 1h59m18s、2×＋絨毛 ＝ 2h11m38s（**+10.4%**）。
#      絨毛讓外牆路徑點數暴增、細碎移動吃不到設定速度（受加速度限制）
#      ⇒ **這組的價值在表面觸感，不在速度**，已當面回報 Eric、他仍裁「當預設」。
#   ③ **sparse 改了幾乎沒差**：照片磚 `sparse_infill_density` ＝ 0（實心），沒有稀疏填充可跑。
#
# ⚠️ **首層 40→80 是唯一有實機風險、而且切片驗不出來的一項。**
#    全庫規則 `normalize_fast_speed` 明文「首層速度 `initial_layer_speed` 不動」（首層慢＝附著），
#    本規則照 Eric 0912 的參數清單逐項照做 ⇒ **照片磚成為該慣例的唯一例外**。
#    ✅ **已處理（Eric 2026-09-12 晚二裁）**：首層不跟著 ×2，明設 **60**（見 `PT_INITIAL_LAYER_SPEED`）。
#    0912 那件實印用的是首層 80、Eric 回「品質還可以」，但他仍選保守值 60；再動要有新裁定。
#
# 只動「絕對值、且屬列印動作」的速度鍵；**百分比相對值一律不動**
#（`internal_bridge_speed` 150%／`small_perimeter_speed` 50%／`scarf_joint_speed` 100%／
#  `initial_layer_travel_speed` 100%——它們的基準是 `outer_wall_speed`，基準翻倍它們自然跟著翻）。
# 也不動 `travel_speed`（空駛，與流量無關）、`ironing_speed`、`wipe_*`（非磚體表面）。
PT_2X_SPEED_KEYS = (
    "outer_wall_speed", "inner_wall_speed", "top_surface_speed", "internal_solid_infill_speed",
    "sparse_infill_speed",
    "gap_infill_speed", "bridge_speed", "skirt_speed",
    "overhang_1_4_speed", "overhang_2_4_speed", "overhang_3_4_speed", "overhang_4_4_speed",
    "support_speed", "support_interface_speed",
)
# 🔴 首層**不跟著 ×2**（Eric 2026-09-12 晚二裁，牌 `c-0912-PTI-09`）。
#   原話：「首層如果是 80 的話，確實有點風險，改成 60 好了」。
#   背景＝首層慢才附著，這是整組參數裡**唯一切片驗不出來、只能靠實機**的風險項：
#   0912 那件實印（首層 80）Eric 回「目前列印的品質還可以」，但他仍選保守值。
#   60 ＝ 範本 40 的 1.5×，落在「全庫慣例 40」與「×2 的 80」之間。
#   ⚠️ 要再動這個值一定要有 Eric 新的一次裁定，不是實作者順手改。
PT_INITIAL_LAYER_SPEED = "60"

PT_FUZZY = {"fuzzy_skin": "external", "fuzzy_skin_thickness": "0.3",
            "fuzzy_skin_point_distance": "0.8", "fuzzy_skin_first_layer": "0"}


def apply_phototile_2x_fuzzy(proc):
    """在 normalize_fast_speed 之後套照片磚專屬的 2× 速度＋絨毛。**只給照片磚製程用**。"""
    for k in PT_2X_SPEED_KEYS:
        v = proc.get(k)
        if v is None:
            continue
        s = str(v).strip()
        if s.endswith("%"):
            continue   # 相對值不動（基準翻倍時它自己會跟著翻）
        try:
            proc[k] = "%g" % (float(s) * 2.0)
        except (TypeError, ValueError):
            continue   # 不是數字就原樣留著，不猜
    # 首層獨立設值（不在 ×2 清單裡）——見 PT_INITIAL_LAYER_SPEED 上面那段裁定。
    proc["initial_layer_speed"] = PT_INITIAL_LAYER_SPEED
    proc["initial_layer_infill_speed"] = PT_INITIAL_LAYER_SPEED
    proc.update(PT_FUZZY)
    return proc

# ★ 換料塔預設（2026-07-08 Eric 拍板・規格 _切片規則同步_來自pingslicer_換料塔與棧板雙版本_20260708.md）：
# 全庫統一 寬 30→15＋外牆肋條（rib）。肋寬/圓角吃引擎預設（肋寬 8；寬 15 時被引擎夾到 7.5
# ＝正常行為，PrintConfig.cpp wipe_tower_rib_width tooltip）。單料機不顯示換料塔、寫入無副作用
# → 不分機型全寫（含 FF 範本/照片磚範本）。
def normalize_prime_tower(proc):
    # 寬度 2026-07-17 Eric 改裁 15→25（新規蓋舊規）
    proc["prime_tower_width"] = "25"
    # 牆體 2026-07-29 Eric 改裁 rib→cone＋頂角 30＋最快列印速度 60（新規蓋舊規、肋條裁定退役；
    # 速度＝引擎預設 90 下修 60、錐體自帶底部圓角助穩，cone_angle 引擎預設同為 30＝明寫鎖定）
    proc["wipe_tower_wall_type"] = "cone"
    proc["wipe_tower_cone_angle"] = "30"
    proc["wipe_tower_max_purge_speed"] = "60"
    return proc

# ★ 內外牆加速度（2026-07-29 Eric 裁「提高表面品質與穩定性」）：全機型統一 1500。
# 與 normalize_prime_tower 同三呼叫點＝覆蓋全部 emit 製程（0.125mm 細層批原 3000 一併統一）；
# Classic 前代隨 emit_classic Marlin 隔離歸 0 照舊（既有裁定不變）＝實效 F 系全家＋照片磚。
def normalize_wall_accel(proc):
    proc["outer_wall_acceleration"] = "1500"
    proc["inner_wall_acceleration"] = "1500"
    # ★ 牆體列印方向固定逆時針（Eric 2026-07-30 裁「外牆的順序一律改成逆時針(固定的)，
    # 至少這樣可以提升品質」）。機制＝PerimeterGenerator.cpp:1424 —— auto 時偵測到陡懸空會
    # 呼叫 reorient_perimeters() 反轉整層迴路，設 ccw 直接跳過該段 ⇒ 層間方向恆一致。
    # ⚠ 實測記錄（0730 側孔盒 gcode 矩陣）：本測試件上 auto 與 ccw 輸出逐項相同（PING 的牆
    # 在 auto 下本就全 CCW；殘留的 7 條 CW 是孔洞迴路＝幾何約定、wall_direction 管不到）。
    # 價值在懸空多的模型（會觸發 reorient 的情形），非本次孔帶紋路的解。
    proc["wall_direction"] = "ccw"
    return proc

# ★ 棧板雙版本製程（2026-07-08 Eric 拍板，同上規格檔）：FD 單料頭/同進＋FP300 出「_棧板」雙生
#（raft 六鍵＝既有 ABS+SUP 黃金配方定稿值，與 0.2mm ABS+SUP @FD300 (0.4).json 全等）。
#  2026-09-17 Eric 同日兩裁：先只改易拆+筏層、後裁「三族都改 3」⇒ 本表 raft_layers 同步為 3，
#    六鍵再度與 ABS+SUP 全等（牌 c-0917-REL-02；舊值 2 出自 2026-06-10 V3.0「最佳 ABS」定稿）。
# 裁決：①FF 全系/DL1016/雙料組合不出 ②切回一般版不自動換回 PLA（Tab.cpp 單向連動）
# ③雙料 ABS+SUP/ABS+ABS 維持現名（功能上已是棧板版）。
# setting_id 一律排在全庫既有 id 之後（主迴圈收集、最後統一 emit），既有 id 零位移。
PALLET_OVERRIDES = {"raft_layers": "3", "raft_contact_distance": "0.1", "raft_expansion": "1.5",
                    "raft_first_layer_density": "100%", "raft_first_layer_expansion": "6",
                    "initial_layer_line_width": "150%"}

# ★ 高流量製程組（Eric 2026-07-30 裁・客戶（建誌）FF800 同進 0.6 實測參數移植；
#   來源 P800參數-建誌.zip「FF_0.6_PLA_T200(800)」、巡檢＝FF800高流量參數_巡檢報告_20260730.html）：
#   六類速度全跟客戶（外100/內125/填充150/頂面150/實心150/支撐與介面100）＋首層也開 100
#  （Eric 追裁、推翻「保留 40」建議）；加速度逐項取保守 min(客戶, PING 現值)——客戶檔僅明寫
#   print 2000/travel 2000/wall0 1500、其餘 Cura 繼承 print=2000 ⇒ 實際變更僅 travel 5000→2000
#   ＋sparse_infill 5000→2000（default 1500/首層 500/頂面 800/內外牆 1500 現值更保守、全維持）；
#   頂底厚 0.8→1.2（客戶值；高速印薄頂易露填充。layers 4 承基底＝下限語意，1.0 口徑實得 4 層）；
#   層高照口徑連動 0.5×口徑（0.6＝客戶實測 0.3/0.35；0.4/1.0 推 0.2/0.25、0.5/0.55，首層＝層高+0.05）。
#   其餘全維持 PING 現值（支撐角 35／gyroid／aligned／溫度／回抽——客戶的 Cura 慣例不吃，
#   巡檢報告逐項裁定；支撐角客戶 Cura 60＝Orca 30，0724 已踩過的換算坑、維持 35 實測收斂值）。
#   範圍＝FF800 同進三口徑（0.4/0.6/1.0）；FF600/四色/3in1 等實印驗過再擴（verify 有範圍鎖）。
#   派生＝emit_ff_extra 正規化完成後複製雙生、id 統一接尾（4a-6，同棧板/PVA 模式＝既有 id 零位移）。
#   ⚠ 1.0 口徑 150×0.5×1.02≈76mm³/s 超四料線材上限 30 ⇒ 引擎自動夾速（≈60mm/s）＝預期行為
#  （現行 0.45mm 檔 100 速同樣超上限被夾，非本批新增風險）。
HF_PROC_LAYER = {"0.4": ("0.2", "0.25"), "0.6": ("0.3", "0.35"), "1.0": ("0.5", "0.55")}
HF_PROC_OVERRIDES = {
    "outer_wall_speed": "100", "inner_wall_speed": "125",
    "sparse_infill_speed": "150", "top_surface_speed": "150",
    "internal_solid_infill_speed": "150",
    "support_speed": "100", "support_interface_speed": "100",
    "initial_layer_speed": "100", "initial_layer_infill_speed": "100",
    "travel_acceleration": "2000", "sparse_infill_acceleration": "2000",
    "top_shell_thickness": "1.2", "bottom_shell_thickness": "1.2",
}
_HF_FF800_RE = re.compile(r"@FF800 同進 \(([\d.]+)\)\s*$")

def make_hf_twin(proc):
    """從正規化完成的 FF800 同進製程派生高流量雙生（setting_id 由 4a-6 接尾統一給）。"""
    nz = _HF_FF800_RE.search(proc["name"]).group(1)
    lh, flh = HF_PROC_LAYER[nz]
    tw = dict(proc)
    tw.update(HF_PROC_OVERRIDES)
    tw["layer_height"] = lh
    tw["initial_layer_print_height"] = flh
    tw["name"] = "%smm 高流量 @FF800 同進 (%s)" % (lh, nz)
    return tw

# ---------- 3. 交付檔解析 ----------
def parse_dir(src_base, dirname):
    """回傳 {(nozzle, mode): config}；mode ∈ {dual, 單料頭, 同進, 四色}（dual=PLA+SUP 母檔）"""
    prefix = dirname.replace(" ", "_") + "_"
    d = os.path.join(src_base, dirname)
    out = {}
    for fn in sorted(os.listdir(d)):
        if not fn.endswith("_project_settings.config"): continue
        body = fn[len(prefix):-len("_project_settings.config")]
        nozzle = body.split("_")[0]
        rest = body[len(nozzle)+1:]
        # PETG 檔（單料頭/同進_PETG，2026-06-11 補 48 檔）：與 PLA 版差異 100% 在 filament 層
        # （235/床75/風扇50/密度1.27，已驗證製程 key 零差異）→ 不出製程，只建 PING PETG - 235 線材
        # （235 支已於 2026-07-19 Eric 裁刪：留 PING PETG＋PING PETG - 高流量噴頭(230)）
        if "PETG" in rest: continue
        if   rest in ("PLA+SUP","PLA+PLA","ABS+SUP","ABS+ABS"): mode = rest  # 雙料 4 組合各自成製程
        elif rest.startswith("單料頭"):   mode = "單料頭"
        elif rest.startswith("同進"):     mode = "同進"
        elif rest.startswith("四色"):     mode = "四色"
        else: continue
        out[(nozzle, mode)] = json.load(io.open(os.path.join(d, fn), encoding="utf-8"))
    return out

DUAL_COMBOS = ["PLA+SUP", "PLA+PLA", "ABS+SUP", "ABS+ABS"]

# ---------- 3b. 上游【已淘汰】鍵：有聲擋下（Eric 2026-08-27 裁「A 案」，牌 c-0827-SIT-05）----------
# 取代 c-0827-SIT-03 那種「加進 SKIP 靜默丟掉」的做法。靜默的問題是雙向的：Eric 的裁定會被無聲
# 還原、工程端真要開新鍵也會被無聲吃掉，而且沒有人會知道「來源那批是舊版 slicer 匯出的」。
#
# ⚠️ 判準【刻意寫死】，不要改成「自動從 handle_legacy 推導」——2026-08-27 實測過：
#    handle_legacy 本體提到 66 個 opt_key，扣掉 s_Preset_*_options 權威清單後剩 38 個
#    （濾網確實有鑑別力、篩掉 28 個），但那 38 個裡的 different_settings_to_system 會讓
#    【現行乾淨的 98 檔 98/98 全部誤報】——它是合法的專案層鍵、本來就在 SKIP 裡。
#    ⇒ 自動判準會讓每一次 regen 都紅，而會誤報的閘門一定被關掉，關掉的閘門等於沒有。
#
# 加新項＝在下面加一行 {舊鍵: 替代鍵}，並確認它不會打到現行來源（跑一次本檢查即可）。
DEPRECATED = {
    # 上游 PR #9924（2025-06-22）把「實心填充逐層轉向」換成 solid_infill_rotate_template
    # （預設 "" ＝關）。舊鍵在 PrintConfig.cpp 只剩 handle_legacy 會轉換 ⇒ 來源檔再出現它，
    # 代表那批是 2025-06-22 以前的 slicer 匯出的（該值是舊版自帶的預設，不是誰在 UI 勾出來的）。
    "rotate_solid_infill_direction": "solid_infill_rotate_template",
}

def check_deprecated(src_base):
    """來源含上游已淘汰的鍵 ⇒ 印出每一個檔名後中止。

    🔴 必須在 main() 動任何東西【之前】呼叫：main() 第一步（4a）就把既有的 machine/process
       JSON 全部刪掉，閘門擺在後面等於 repo 先被掏空才報錯。

    掃【兩種】來源——只掃第一種會漏，而漏掉的那種真的爆過：
      (甲) 參數端交付＝FAMS 會讀的機型夾底下【所有】 *_project_settings.config，
           含 parse_dir 會跳過的 PETG（不出製程，但一樣是「舊版 slicer 匯出」的證據）。
      (乙) repo 內建範本＝tools/ping/base/**/*.json（ff_extra／phototile）。
           這些【不經過 cat()／split()】：json.load → normalize_*（只設值、從不刪鍵）→
           jdump（純 json.dump、零過濾）直接落進 resources/profiles/PING/ ⇒ SKIP 從未涵蓋它們。
           實錄：0.25mm 易拆 @FF600 3in1 (0.4).json 走過 ee7db186d8(清乾淨,0819) → 
           37bf3101da(「收 regen 漂移」帶回來,0826) → eb69a91d81(再清,0827)，清→regen→又回來。
    """
    def _scan_json(path, where, out):
        cfg = json.load(io.open(path, encoding="utf-8"))
        for old in sorted(DEPRECATED):
            if old in cfg:
                out.append((where, old))

    # ---- 甲：參數端交付 ----
    src_hits, seen, nsrc = [], set(), 0
    for dirname in [f[0] for f in FAMS] + ["FF800 Pro", "FF600 Pro"]:
        if dirname in seen:
            continue
        seen.add(dirname)
        d = os.path.join(src_base, dirname)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.endswith("_project_settings.config"):
                nsrc += 1
                _scan_json(os.path.join(d, fn), "%s/%s" % (dirname, fn), src_hits)
    # 掃不到的機型夾要講出來——不然綠燈會被讀成「整批交付都乾淨」，實際上只代表
    # 「產生器會讀的那幾夾乾淨」。E 系列等不在 FAMS 的夾，產生器本來就不讀。
    skipped = sorted(n for n in os.listdir(src_base)
                     if os.path.isdir(os.path.join(src_base, n))
                     and n not in seen and not n.startswith("_"))

    # ---- 乙：repo 內建範本 ----
    tpl_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "base")
    tpl_hits, ntpl = [], 0
    for dirpath, _dirs, files in os.walk(tpl_root):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            ntpl += 1
            rel = os.path.relpath(os.path.join(dirpath, fn), tpl_root).replace(os.sep, "/")
            _scan_json(os.path.join(dirpath, fn), "tools/ping/base/" + rel, tpl_hits)

    scope = "交付 %d 夾 %d 檔＋範本 %d 檔" % (len(seen), nsrc, ntpl)
    if skipped:
        scope += "；未掃 %d 夾（不在 FAMS、產生器不讀）：%s" % (len(skipped), "、".join(skipped))
    if not src_hits and not tpl_hits:
        print("  已淘汰鍵檢查：無命中（%s）" % scope)
        return

    print("")
    print("✗ 含上游【已淘汰】的參數鍵，已中止——一個檔都沒有動：")
    if src_hits:
        print("  ── 參數端交付 %d 處 ──" % len(src_hits))
        for where, old in src_hits:
            print("    %s" % where)
            print("        %s  ⇒  改用 %s" % (old, DEPRECATED[old]))
        print("    這通常代表那批交付是舊版 slicer 匯出的（舊鍵是它自帶的預設值，不是誰在 UI")
        print("    勾出來的，所以「請工程師關掉」關不掉）。先確認工程端的 slicer 版本 → 重新匯出。")
    if tpl_hits:
        print("  ── repo 內建範本 %d 處 ──" % len(tpl_hits))
        for where, old in tpl_hits:
            print("    %s" % where)
            print("        %s  ⇒  改用 %s" % (old, DEPRECATED[old]))
        print("    範本不經過 cat()／split()，是直接 jdump 出去的 ⇒ 要在 repo 裡把那一行刪掉，")
        print("    不是去改交付來源。")
    print("")
    print("  共 %d 處（%s）。" % (len(src_hits) + len(tpl_hits), scope))
    print("  真的要放行才把該鍵從 DEPRECATED 拿掉；不要改加進 SKIP，那會變回靜默。")
    raise SystemExit(1)

# ★ 組合製程功能歸類名（Eric 2026-07-29 裁「材料對→功能名」＋兩追裁：PVA 留第五支、字面照原話；
# Codex gpt-5.6-sol 四輪雙審「可定稿」＝計畫 v2+v3+v4 疊加，軌跡 _審查_組合製程功能歸類改名_*）。
# 顯示名唯一產名入口：pname()／PVA twin／Classic 母檔讀取／machine default 全走本表；
# 內部 cb token（"PLA+SUP" 等）不動＝easy_release／raft／檔名前綴／combo_overrides 照舊。
# ★ 0730 改名批的五類名（**歷史值**，只用來產 renamed_from 回溯鏈，不再是現行名）
COMBO_DISPLAY_0730 = {"PLA+SUP": "易拆(Z0)", "PLA+PVA": "易拆(Z0)水溶", "ABS+SUP": "易拆(Z0)+棧板",
                      "PLA+PLA": "雙料(Z隙)", "ABS+ABS": "雙料(Z隙)+棧板"}

# ★ 現行名（Eric 2026-08-11 兩裁合併）：
#   ①「棧板」→「筏層」——參數欄位本來就叫筏層（raft），製程名寫棧板＝誤字。
#   ②拿掉 (Z0)／(Z隙)／「雙料」——Eric 原話：「Z0 跟 Z隙 其實是參數內的設定，但大家比較瞭解的
#     就是『易拆』（也就是沒有間隙）。那一般則是會有間隙，所以雙料也拿掉，這樣也可以針對四料
#     跟單料就統一的名稱。」
#   ⇒ 產出的 head 段（@ 之前）：
#       PLA+SUP  「{lh}mm 易拆」          PLA+PVA 「{lh}mm 易拆水溶」   ABS+SUP「{lh}mm 易拆+筏層」
#       PLA+PLA  「{lh}mm」（**無 token**） ABS+ABS 「{lh}mm_筏層」（**無 token、走 _筏層 分支**）
#   ⇒ 統一達成：雙料一般版＝`0.2mm @FD300 (0.4)`，與單料頭 `0.2mm @FD300 單料頭 (0.4)`、
#     四料 `0.3mm @FF600 (0.6)` 完全同形；筏層版三類機統一 `{lh}mm_筏層 @…`。
#   ⚠ **PLA+PLA 拿掉 token 會讓 Tab.cpp 的 combo 解析放棄**（`find_last_of(' ')` 找不到空白就 return）
#     ⇒ Eric 0811 裁「補一條『雙料機無 token 就當 PLA+PLA』」，實作在 Tab.cpp（跨層護欄有鎖）。
COMBO_HEAD = {"PLA+SUP": "%smm 易拆", "PLA+PVA": "%smm 易拆水溶", "ABS+SUP": "%smm 易拆+筏層",
              "PLA+PLA": "%smm",      "ABS+ABS": "%smm_筏層"}

def combo_head(lh, cb):
    """製程名 @ 之前的 head 段（唯一產名入口）。"""
    return COMBO_HEAD.get(cb, "%smm " + cb) % lh

def combo_pname(lh, cb, model, nz):
    return "%s @%s (%s)" % (combo_head(lh, cb), model, nz)

def combo_renamed_from(lh, cb, model, nz):
    # 舊全名回溯鏈（renamed_from＝**分號分隔「字串」**鐵則——array 會讓
    # PresetBundle.cpp:4098 的 unescape_strings_cstyle 收到 array、nlohmann 直接丟）。
    # 0811 起本鏈兩條：①材料對原名（~0730）②0730 五類名（0730~0811）。
    # ⚠ 刻意不收「舊去@別名」：同層高的去@形態跨 6 台機共用（例「0.2mm PLA+SUP」＝六支同名）
    #   ＝不唯一，塞入會撞 renamed_from 舊名唯一性護欄、引擎 rename map 也是 1:1 先到先贏。
    return ";".join(["%smm %s @%s (%s)" % (lh, cb, model, nz),
                     "%smm %s @%s (%s)" % (lh, COMBO_DISPLAY_0730[cb], model, nz)])

def combo_overrides(combo, layer_height, nozzle):
    """V3.0 組合別製程差異復原（2026-06-10 使用者規格＋V3.0「最佳 ABS」定稿實證）：
    - 支撐介面：有 SUP＝z 距離 0（貼緊、靠支撐料好剝）；無 SUP＝1 層層高（留縫好拆）
    - Raft：ABS 系（ABS+SUP 易拆+筏層／ABS+ABS 雙料筏層）＝3 層（Eric 2026-09-17 實測後裁「三族都改 3」；
      舊值 2 出自 2026-06-10 V3.0「最佳 ABS」定稿）、PLA 系＝0
    - ABS+SUP 另套 V3.0 黃金支撐配方（normal/主體料1/界面料2/界面4·2層/間距0.04/xy0.5）
    - PLA+SUP 支撐幾何（Eric 2026-07-14 裁）：XY=口徑×0.75、支撐/物件第一層間隙=口徑/3
      （易拆支撐口徑公式；ABS+SUP 維持黃金配方 xy0.5 不套）"""
    o = {}
    if combo.endswith("+SUP"):
        o.update({"support_top_z_distance": "0", "support_bottom_z_distance": "0"})
    else:
        o.update({"support_top_z_distance": layer_height, "support_bottom_z_distance": layer_height})
    if combo == "PLA+SUP":
        o.update({"support_object_xy_distance": "%g" % round(float(nozzle) * 0.75, 2),
                  "support_object_first_layer_gap": "%g" % round(float(nozzle) / 3.0, 2)})
    if combo.startswith("ABS"):
        o["raft_layers"] = "3"     # ABS 系兩族（ABS+SUP／ABS+ABS）同值；單料 _筏層 雙生另由 PALLET_OVERRIDES 給 3（Eric 2026-09-17 後裁，牌 c-0917-REL-02）
        # ABS 首層線寬 1.5×口徑（「最佳ABS(更新後)」定稿 0.4 噴嘴=0.6；百分比隨口徑縮放）
        o["initial_layer_line_width"] = "150%"
    if combo == "ABS+SUP":
        o.update({"support_type": "normal(auto)", "support_base_pattern": "rectilinear",
                  "support_filament": "1", "support_interface_filament": "2",
                  "support_interface_top_layers": "4", "support_interface_bottom_layers": "2",
                  "support_interface_spacing": "0.04", "support_bottom_interface_spacing": "0",
                  "support_object_xy_distance": "0.5"})
    return o


# ★ PLA+PVA 專屬製程（Eric 2026-07-25 裁「出」・主線 0a5d1df3 移植；
#   值＝V2.1 定稿案 DPro_0.6_T210_PVA+PLA 對帳，劉勝賢 2026-07-24 提供 3mf）
# 產法＝從同口徑 PLA+SUP 製程雙生派生（同棧板雙生模式）：易拆幾何（Z0／XY 口徑×0.75）、
#   支撐料槽 2、速度/層高家規全部自然繼承，只覆蓋「案值與家規不同」的四項。
# ⚠ 兩項為跨基準換算值（同支撐角教訓，見 orca-sync.md），標待工程端實機驗證：
#   ①支撐角：案值 Cura 40 ＝ Orca 50（Orca ＝ 90 − Cura）＝比全庫 35 多支撐，屬水溶支撐合理特例
#   ②brim：案值「20 條」係 Cura 條數制，Orca 為 mm ⇒ 20 條 × 線寬 ≒ 口徑×20
# 未套（家規優先，刻意）：層高（案 0.35@0.6 vs 家規口徑×0.5＝0.3）、內牆/填充速度
#   （案「全 60」vs 家規外60/內≤80/填100 的吞吐設計；外牆與首層本就同值）。
# ★ PA-CF 專屬製程（Eric 2026-08-26 四裁・起因＝他提供 FP300 0.4 的 PA-CF 實測較佳組
#   `一般參數.3mf`／`薄壁參數.3mf`，逐鍵比對出 27 項偏離標準）。
# 🔴 為什麼到今天才有：2026-07-03 切片參數線發過《PA-CF 專屬製程（crater 火山口）規格》，
#   Orca 線只落地了 normalize_fast_speed(is_pacf=...) 的「跳過」殼子，`pacf_overrides` 從未存在、
#   is_pacf=True 也從未被呼叫 ⇒ FP300 印 PA-CF 一直吃通用（PLA 骨架）製程。本次補上。
# Q1 甲：帶「材料相關」9 鍵；支撐 6 鍵一律回全庫預設——樹狀是「這個件」的選擇、不是材料特性。
# Q2 A ：只出 FP300 三口徑（0.2/0.4/0.6）；一般版＋樹狀版各一 ⇒ 6 支。
# ⚠ 值＝Eric 實印 ground truth（60/60/60），**刻意不採 0703 規格的 50/80/80**——後者是 Cura 線
#   底檔搬來的紙上值、Orca 線從未實印過（確定感紀律：實測勝推導）。
# 🔴 Eric 2026-08-26 三裁：「碳纖料只有在 FP300 或者是 FD300 單料頭」⇒ 範圍由 FP300 擴為這兩台。
#    （0826 Q2 原裁「只出 FP300」的理由是「只有 FP300 0.4 有實印」；他當天就在 FD300 上印 PA-CF，
#      理由已鬆動，本裁直接把實際使用機型講清楚。）
# ⚠ 事實落差、值仍共用：**FP300＝火山口-高流量、FD300 單料頭＝一般流量**，噴頭流量形式不同；
#    依 Q3 甲「線材維持單一支 245 全機通用」＋本裁，兩機共用同一組 pacf_overrides()。
#    若日後 FD300 單料頭實印回報偏差，第一顆旋鈕＝在這裡改成 per-model 值。
PACF_MODELS = ("FP300", "FD300 單料頭", "FD300 Pro 單料頭",
                "FD450 Pro 單料頭", "FD600 Pro 單料頭", "FD800 Pro 單料頭")   # 0827 Eric「三台同類一起」
# 🔴 Eric 2026-08-26 四裁「FD300 Pro 單料頭跟 DUAL 單料頭也一起補上」。
#    DUAL 四台的單料頭變體**不在這張表**——它們是 Classic 前代機、由 emit_classic 的
#    DUAL 變體迴圈產出，不走主迴圈；掛鉤在那裡（搜 PACF_CLASSIC）。
# ✅ 2026-08-27 Eric 裁「三台同類一起」⇒ FD450／FD600／FD800 Pro 單料頭已補齊；**單料頭機至此全數涵蓋**。

def pacf_overrides():
    return {
        "inner_wall_speed":           "60",    # 全庫 80 → 與外牆同速，消除內外牆速差造成的壁厚不勻
        "sparse_infill_speed":        "60",    # 全庫 100 → CF 磨嘴、高速擠出跟不上
        "initial_layer_speed":        "25",    # 全庫 40  → PA 系翹曲料的關鍵
        "initial_layer_infill_speed": "30",    # 全庫 40
        "first_layer_flow_ratio":     "1",     # 全庫 1.1 → 首層多擠 10% 會積料、刮噴頭
        "brim_object_gap":            "0.2",   # 全庫 0.1 → PA 又硬又韌，0.1 剝不下來
        "sparse_infill_density":      "10%",   # 全庫 15% → CF 件剛性夠，不靠填充撐
        "only_one_wall_top":          "1",     # 全庫 0   → 頂面平坦度主旋鈕
        "seam_position":              "back",  # 全庫 aligned → 縫固定藏背面
    }

# 樹狀版＝一般版只換支撐類型。`support_style` 必須明寫 "default"：
#   ①ConfigManipulation.cpp:476-479 會把「snug（全庫值）＋tree」判為非法配對、自動改回 default
#     ⇒ 不明寫的話使用者一開檔就吃到「設定已被修正」提示。
#   ②SupportParameters.hpp:180-183「tree＋default ⇒ smsTreeOrganic」＝Eric 實印那組就是**有機樹狀**，
#     吃既有 _organic 防呆值（直徑 2.6／角度 40），**不吃** 0725 那組 hybrid 保守配方（分屬不同鍵組）。
#   ③支撐臨界角維持全庫 35（Eric 實測用 30＝更保守，屬 Q1 甲「支撐回全庫」的範圍，未帶）。
PACF_TREE_OVERRIDES = {"support_type": "tree(auto)", "support_style": "default"}

# ★ 高速製程「0.2mm 高速」（Eric 2026-10-08：把 FD600 Pro 單料頭 ABS-CF 兔子試印那組參數建進庫；牌 c-1008-ACF-01）
#   範圍＝FD600 Pro 單料頭 0.4 一台一口徑；只套「速度／加速度／填充圖案」，不動支撐、牆數、填充密度（件相關）。
#   值＝FLSUN 參考值（見 Klipper 05市場研究/FLSUN實跑對照_20260929/09_*／10_*）夾在機台 5000 加速度內；
#   實際速度由線材 `ABS-CF` 的流量上限 12 mm³/s 蓋住（內牆／填充約 168 mm/s）。D2710F 實印兔子 46.04 分（現行 84.6 分）。
#   🔴 沒校過 PA（0.08 庫值）；流量上限 12 是保守估值。
ABSCF_FAST_MODELS = ("FD600 Pro 單料頭",)
ABSCF_FAST_NZ = "0.4"
ABSCF_FAST_LH = "0.2"
ABSCF_FAST_TAG = "高速"
# 不掛 PING 品牌前綴的自家線材（預勾 post-pass 只認 "PING " 開頭，這裡明列才會被預勾；Eric 2026-10-08）
NON_BRAND_FILAMENTS = {"ABS-CF"}
ABSCF_FILAMENT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "base", "abscf", "filament")

def abscf_fast_overrides():
    return {
        "outer_wall_speed":            "120",
        "inner_wall_speed":            "230",
        "sparse_infill_speed":         "280",
        "internal_solid_infill_speed": "250",
        "top_surface_speed":           "150",
        "gap_infill_speed":            "100",
        "support_speed":               "150",
        "support_interface_speed":     "80",
        "travel_speed":                "400",
        "default_acceleration":        "5000",
        "outer_wall_acceleration":     "3000",
        "inner_wall_acceleration":     "5000",
        "top_surface_acceleration":    "3000",
        "sparse_infill_pattern":       "grid",
    }

# ★ 易拆樹狀製程（Eric 2026-09-19 兩輪 grill 裁；牌 c-0919-ETR-01）
#   Q1 乙：易拆（PLA+SUP）全部雙料本體機 × 全口徑＝21 支（FD300／FD300 Pro／FD300 關門 × 0.25/0.4/0.6、
#          FD450／600／800 Pro × 0.25/0.4/0.6/1.0）；水溶／筏層樹狀版與 FF 3in1 不做。
#   Q2 乙（Eric 2026-09-19 改裁，推翻同日 Q2 甲＝混合樹；0725「手動切樹狀用混合樹」不適用本族）：**有機樹**（support_style **明寫 organic**。
#          ⚠ 不寫 default：default 在樹狀下引擎也解成有機樹（SupportParameters.hpp:180-183、Print.cpp 兩處檢查同視），
#          行為相同，但 UI 樣式欄顯示「預設 (網格/有機)」——Eric 0919 GUI 實看抓到「沒有選到有機樹」⇒ 改明寫）。起因＝原裁甲案「混合樹」CLI 實切（平板／懸臂兩模型）長成與普通支撐
#          幾乎相同的區塊、不長樹幹；有機樹才長出一根根樹幹。吃 _organic 鍵組（直徑 2.6／角度 40），
#          不吃 0725 hybrid 保守配方。代價：大面積平懸空（平板模型）支撐路徑 56→77 m、估時 45→82 分。
#   Q5／Q6 甲：支撐＋支撐面速度 **50**（兩線同值）——4b-7「支撐速度下限 60」post-pass 必須豁免本族，
#          否則會被拉回 60（豁免名單＝4b-7 的 SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS，verify 同名一份＋鎖 exact 50）。
#   其餘鍵全部＝同口徑「易拆」（派生時複製），只差上面四鍵＋name／setting_id（新品無 renamed_from）。
#   token「易拆樹狀」＝Tab.cpp 認得的第四個易拆家族 token（歸易拆＋自動帶 PLA-210＋SupPLA），跨層護欄在 verify。
EASY_TREE_TOKEN = "易拆樹狀"
EASY_TREE_OVERRIDES = {"support_type": "tree(auto)", "support_style": "organic",
                       "support_speed": "50", "support_interface_speed": "50"}

def easy_tree_pname(lh, model, nz):
    return "%smm %s @%s (%s)" % (lh, EASY_TREE_TOKEN, model, nz)


def pva_overrides(nozzle):
    nz = float(nozzle)
    return {
        "support_threshold_angle": "50",                              # 案值 Cura 40 換算
        "support_base_pattern_spacing": "%g" % round(nz * 19, 2),     # 支撐密度 5%（案值；家規 10%＝×9）
        #   Orca 以線距表述密度：密度＝線寬/(線距+線寬) ⇒ 5% 需線距＝線寬×19（分子用口徑名目值，同 0722 家規）
        "prime_tower_width": "45",                                    # 案值（劉勝賢現行；家規 25）
        "brim_width": "%g" % round(nz * 20, 2),                       # 案值 20 條換算
    }

def normalize_support_mode(proc, model_name):
    """機型預設支撐模式（Eric 2026-07-15）：
    - FD300 全家族／所有模式＝樹狀
    - FF600 基本款與 3in1＝普通
    - FF600 同進＝樹狀
    放在組合別覆寫之後，避免 ABS+SUP 黃金配方把 FD300 改回普通。"""
    if model_name.startswith("FD300"):
        proc["support_type"] = "tree(auto)"
    elif model_name.startswith("FF600 同進"):
        proc["support_type"] = "tree(auto)"
    elif model_name.startswith("FF600"):
        proc["support_type"] = "normal(auto)"
    return proc

def emit_ff_extra(mm_list, mac_list, proc_list, gm, gp):
    """範本複製法：把已驗證的 FF 同進/3in1 machine/process/filament/cover 併入產出。
    - machine（type=machine 的口徑變體）：重指 setting_id（續 gm 計數避免撞號）→ mac_list
    - machine_model（底檔）：→ mm_list
    - process：重指 setting_id（續 gp）→ proc_list
    - filament（3in1 專用 PLA(3in1)/SupPLA(3in1)，含 T4/T3 filament_start_gcode、淺灰支撐色）→ 回傳 ff_fil
    - cover：複製到 PINGDIR（受 4c 孤兒清除保護，見 ff_models）
    回傳 (gm, gp, ff_fil, ff_models, hf_twins)。範本本身已驗證，不再改值、只重編 setting_id。"""
    ff_fil, ff_models = [], []
    hf_twins = []   # ★ 高流量雙生（2026-07-30）：FF800 同進三口徑、正規化後派生、id 由 4a-6 接尾
    n_mac = n_proc = n_cov = 0
    for fn in sorted(os.listdir(os.path.join(FF_EXTRA, "machine"))):
        d = json.load(io.open(os.path.join(FF_EXTRA, "machine", fn), encoding="utf-8"))
        name = d["name"]
        # 0712 改名鏈（高流量 @FF → …）＋🆕 0816：**四料本體機**（非同進/3in1/照片磚）
        # 四槽改指「高流量噴頭」支——範本機不經 def_fil_ff()，要在這裡接住。
        rename_ff_filament_refs(d, quad=not any(t in name for t in ("同進", "3in1", "照片磚")))
        if d.get("type") == "machine_model":
            apply_bed_texture(d)          # 範本凍結的是舊 PNG；床貼圖一律對齊 BED_TEXTURE（見該函式）
            mm_list.append({"name": name, "sub_path": "machine/%s.json" % name}); ff_models.append(name)
        else:
            d["setting_id"] = "PINGM%03d" % gm; gm += 1
            mac_list.append({"name": name, "sub_path": "machine/%s.json" % name}); n_mac += 1
        jdump(os.path.join(PINGDIR, "machine", "%s.json" % name), d)
    for fn in sorted(os.listdir(os.path.join(FF_EXTRA, "process"))):
        d = json.load(io.open(os.path.join(FF_EXTRA, "process", fn), encoding="utf-8"))
        normalize_fast_speed(d)   # FF 範本製程同套牆速正規化（75/100/150 與 100/100/100 → 60/80/100）
        normalize_prime_tower(d)  # 換料塔統一（0708 立；0717 寬 25；0729 錐體30/速60）
        normalize_wall_accel(d)   # 內外牆加速度 1500（2026-07-29 Eric 裁）
        normalize_unified_values(d, ff=True)  # 正式製程統一值；FF 範本 jerk 維持 40（上限 56 不警告）
        compatible = d.get("compatible_printers", []) or []
        if compatible:
            normalize_support_mode(d, compatible[0])
        m_nz = re.search(r"\(([\d.]+)\)\s*$", d["name"])   # 名尾口徑，如 "0.35mm @FF600 3in1 (0.6)"
        if m_nz:
            normalize_support_geometry(d, m_nz.group(1))  # 樹狀直徑×10＋主體線距×9（2026-07-17/0722，FF 範本同套）
            normalize_support_recipe(d, m_nz.group(1), easy_release=("3in1" in d["name"]))  # FF 同進套普通支撐配方；3in1 易拆跳過 XY
            normalize_tree_support(d)  # 樹狀保守配方＋organic 防呆（2026-07-25）
            # 介面間距 70% 等效＋首層密度 10%（0804 起 FF 範本同套：同進 0.04/四色 0.1→口徑×3/7、
            # 首層 100%/30%→10%；3in1 easy_release＝介面實心 0 維持）。0714「範本不套」自此作廢。
            normalize_support_interface(d, m_nz.group(1), easy_release=("3in1" in d["name"]))
        d["filename_format"] = filename_tpl("3in1" if "3in1" in d["name"] else "同進")  # 檔名新格式（2026-07-23）FF 範本同套
        # ★ 高流量雙生派生點（2026-07-30 Eric 裁）：FF800 同進限定（排除 3in1／FF600），
        #   在全部正規化完成之後複製＝速度/加速度家規先套滿、再被高流量定案值覆蓋
        if "3in1" not in d["name"] and _HF_FF800_RE.search(d["name"]):
            hf_twins.append(make_hf_twin(d))
        d["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % d["name"]), d)
        proc_list.append({"name": d["name"], "sub_path": "process/%s.json" % d["name"]}); n_proc += 1
    for fn in sorted(os.listdir(os.path.join(FF_EXTRA, "filament"))):
        d = json.load(io.open(os.path.join(FF_EXTRA, "filament", fn), encoding="utf-8"))
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % d["name"]), d)
        ff_fil.append({"name": d["name"], "sub_path": "filament/%s.json" % d["name"]})
    for fn in os.listdir(os.path.join(FF_EXTRA, "cover")):
        shutil.copy2(os.path.join(FF_EXTRA, "cover", fn), os.path.join(PINGDIR, fn)); n_cov += 1
    print("  ff_extra 併入：machine=%d + machine_model=%d，process=%d，filament=%d，cover=%d（高流量雙生收集 %d）"
          % (n_mac, len(ff_models), n_proc, len(ff_fil), n_cov, len(hf_twins)))
    return gm, gp, ff_fil, ff_models, hf_twins

def emit_phototile(mm_list, mac_list, proc_list, gm, gp):
    """照片磚範本併入（比照 emit_ff_extra 範本複製法）：machine_model×2＋口徑變體×5＋製程×5＋cover×2。
    製程套 normalize_fast_speed 的牆速／填充速度，但保留照片磚專屬 sparse acceleration；
    範本源值 75/100 是 %APPDATA% 建置當時未同步正規化的舊值→進 repo 時對齊。
    其餘照範本不改值、只重編 setting_id。回傳 (gm, gp, pt_models)。"""
    pt_models = []
    for fn in sorted(os.listdir(os.path.join(PHOTOTILE, "machine"))):
        d = json.load(io.open(os.path.join(PHOTOTILE, "machine", fn), encoding="utf-8"))
        rename_ff_filament_refs(d, pt=True)   # 照片磚 64 槽 → 照片磚專用支（0816 裁「甲」）
        if d.get("type") == "machine_model":
            apply_bed_texture(d)          # 同 emit_ff_extra：範本凍結的貼圖不算數
            jdump(os.path.join(PINGDIR, "machine", "%s.json" % d["name"]), d)
            mm_list.append({"name": d["name"], "sub_path": "machine/%s.json" % d["name"]})
            pt_models.append(d["name"])
    """【2026-08-05 保留號段】照片磚排在配號序列前段，後面還有棧板雙生／PLA+PVA／高流量三批。
       **在這裡多插一支，後面每一支的 PINGP/PINGM 都會往後推**——加 FF600 三支時實測把 verify 的
       id baseline 打紅 18 條（0.125~0.5mm 易拆(Z0)水溶全家族整齊 +3）。那份 baseline 的用途正是
       「證明改名沒有移動既有 setting_id」，**不該為了加一台新機去鬆綁它**。
       解法：後來新增的照片磚機型從**保留號段**取號（PINGM/PINGP 900 起），不動共用計數器
       ⇒ 既有 id 一個都不變、baseline 不用重錄。代價＝號碼不連續，換取既有 preset 身分穩定。
       日後再加照片磚機型比照辦理。"""
    _pt_res_m = _pt_res_p = 900
    # §2.8：**後來新增**的照片磚機型一律走保留號段，不動共用計數器
    #（否則會把後面「棧板雙生／PLA+PVA／高流量」三批整批往後推，打紅 id baseline 的 90 條守衛）。
    _PT_RESERVED = ("FF600 同進照片磚", "FD300 Pro 同進照片磚", "FD450 Pro 同進照片磚",
                    "FD600 Pro 同進照片磚", "FD800 Pro 同進照片磚")
    def _is_reserved(nm):
        return any(nm.startswith(r) or ("@" + r) in nm for r in _PT_RESERVED)
    for name in PHOTOTILE_MACHINES:
        d = json.load(io.open(os.path.join(PHOTOTILE, "machine", "%s.json" % name), encoding="utf-8"))
        rename_ff_filament_refs(d, pt=True)   # 照片磚 64 槽 → 照片磚專用支（0816 裁「甲」）
        if name.startswith("FD300 同進照片磚"):   # FD300 硬體同款門 → 預擠同套左側弧線
            apply_fd300_prime_arc("FD300 同進", d)
        if _is_reserved(name):
            d["setting_id"] = "PINGM%03d" % _pt_res_m; _pt_res_m += 1
        else:
            d["setting_id"] = "PINGM%03d" % gm; gm += 1
        jdump(os.path.join(PINGDIR, "machine", "%s.json" % name), d)
        mac_list.append({"name": name, "sub_path": "machine/%s.json" % name})
    for name in PHOTOTILE_PROCS:
        d = json.load(io.open(os.path.join(PHOTOTILE, "process", "%s.json" % name), encoding="utf-8"))
        normalize_fast_speed(d, preserve_sparse_acceleration=True)
        # 🆕 2026-09-12 Eric 裁「當正式預設」（牌 c-0912-PTI-08）：速度 ×2 ＋ 絨毛表面。
        #    必須在 normalize_fast_speed **之後**——它會把 outer/inner/sparse 壓回 60/≤80/100，
        #    先乘後壓就整組被吃掉（順序反了不會報錯，只會靜默失效）。
        apply_phototile_2x_fuzzy(d)
        # 照片磚特調稀疏填充加速度＝10000（verify 期望；0715 主線下修 5000 不套照片磚）。
        # 範本源檔殘留 '100%' 舊值（%APPDATA% 建置當時的相對值）→ 比照範本速度值「進 repo 時對齊」。
        d["sparse_infill_acceleration"] = "10000"
        normalize_prime_tower(d)  # 統一寫（照片磚 enable_prime_tower=0、無副作用）
        normalize_wall_accel(d)   # 內外牆加速度 1500（照片磚同套；2026-07-29）
        # ★ 支撐參數全部統一（Eric 2026-07-25 裁「照片磚其實不會用到支撐，把支撐參數全部統一，
        #   就不會有差異了」）——取消照片磚既有的支撐豁免（0714 介面／0717 幾何／0722 七裁／0725 角度）。
        #   實測 enable_support=1（開啟）但磚體平貼床無懸空面 ⇒ 引擎不生成支撐、統一為純消除差異。
        #   影響：type tree(auto)→normal(auto)、style default→snug（原組合＝有機樹）、角度 30→35、
        #   線距 2.5→口徑×9、XY 0.3→口徑×1、獨立支撐層高 1→0。
        #   照片磚不含 "+SUP" ⇒ 依家規判定為「一般支撐」（easy_release=False）。
        m_nz_pt = re.search(r"\(([\d.]+)\)\s*$", name)
        if m_nz_pt:
            normalize_support_geometry(d, m_nz_pt.group(1))
            normalize_support_recipe(d, m_nz_pt.group(1))
        normalize_tree_support(d)
        # 介面間距 70% 等效＋首層密度 10%（0804；照片磚承 0725「支撐參數全部統一」＝0.04/100% 一併換，
        # enable_support=0＋磚體平貼床＝零功能影響、純消除差異）
        normalize_support_interface(d, m_nz_pt.group(1) if m_nz_pt else None)
        d["support_threshold_angle"] = "35"  # 與全庫同值（原 30 豁免取消）
        # ★ 支撐開關關掉（Eric 2026-07-25 追裁）：0725 首輪回報「enable_support 其實是 1」＝
        #   雖然磚體平貼床不會生成支撐、實務無影響，但「開著卻永遠不生成」本身會誤導使用者，
        #   且一旦有人把磚立起來或加高就會意外長支撐。照片磚不需要支撐 ⇒ 開關直接關。
        d["enable_support"] = "0"
        # ★ 牆 2 圈＋線寬 1.0×口徑（Eric 2026-09-09 先裁牆 1／1.5×＝牌 c-0909-PTP-01，2026-09-10 實印後回改牆 2／1.0×＝牌 c-0910-WT-09）：
        #   只動預設／首層／外牆／內牆四鍵；頂面／稀疏／實心填充／支撐線寬不動。
        #   0909 牆 1／線寬 1.5× 實印出現牽絲（0.4 噴嘴擠 0.6 線寬＝流量 1.5×）與色塊內不均（單圈外牆＝混色比偏差直接是外皮）。
        #   結構保留、只改倍率與圈數；再要改回去只動下面兩行。2026-09-12 隨照片磚整區移植進出貨線。
        if m_nz_pt:
            _pt_lw = "%g" % round(float(m_nz_pt.group(1)) * 1.0, 2)
            d["wall_loops"] = "2"
            for _k in ("line_width", "initial_layer_line_width", "outer_wall_line_width", "inner_wall_line_width"):
                d[_k] = _pt_lw
        # ★ 頂底鋪滿 4 層、中間維持空心（Eric 2026-09-09「頂底層的部分能夠鋪滿至少三四層，而內部維持一樣的樣式」，牌 c-0909-PTP-02）：
        #   top/bottom_shell_layers 0→4（厚度鍵 0.8 不動，Orca 取層數與厚度較大者）。0% 填充 ⇒ 頂 4 層是內部橋接＋實心，中間照舊只有牆。
        d["top_shell_layers"] = "4"
        d["bottom_shell_layers"] = "4"
        # 檔名：照片磚自成一模式（Eric 2026-07-26 裁）⇒ `照片磚_機型(口徑)_檔名_時間_重量`。
        # ⚠ 照片磚製程從範本檔複製、**不走 filename_tpl()**，範本裡連主體都還是 0610 舊佔位符
        #   （{filament_type}_{total_weight_str}_{print_time_hm}）⇒ 這裡整條覆蓋才會真的跟上。
        # ⚠ 同一類坑 0726 一天踩三次（DL1016 注入源／本處範本源／Classic emit）：
        #   **產生器規則函式掃不到的來源要各自處理**，改全庫規則後務必回頭數數量對不對。
        d["filename_format"] = filename_tpl("照片磚")
        # ⚠ 支撐以外的正式製程統一值仍「不套」照片磚：維持 back＋seam_gap0、travel 3000。
        # 保留號段（見本函式 PHOTOTILE_MACHINES 迴圈前的說明）：後加的照片磚製程不動共用計數器
        if _is_reserved(name):
            d["setting_id"] = "PINGP%03d" % _pt_res_p; _pt_res_p += 1
        else:
            d["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % name), d)
        proc_list.append({"name": name, "sub_path": "process/%s.json" % name})
    for fn in os.listdir(os.path.join(PHOTOTILE, "cover")):
        shutil.copy2(os.path.join(PHOTOTILE, "cover", fn), os.path.join(PINGDIR, fn))
    print("  phototile 併入：machine=%d + machine_model=%d，process=%d，cover=2"
          % (len(PHOTOTILE_MACHINES), len(pt_models), len(PHOTOTILE_PROCS)))
    return gm, gp, pt_models

CLASSIC_PLA_210 = "PING PLA - Classic 210"
CLASSIC_PLA_220 = "PING PLA - Classic 220"
CLASSIC_SUP_PLA = "PING SupPLA - Classic"
CLASSIC_EDU_PLA = "PING PLA - EDU Classic"
# ★ Classic 全套（Eric 2026-08-07 裁「全套跟 Fast 對齊」）——命名沿用既有「PING <材料> - Classic」型
CLASSIC_ABS     = "PING ABS - Classic"
CLASSIC_SUP_ABS = "PING SupABS - Classic"
CLASSIC_PETG    = "PING PETG - Classic"
CLASSIC_PACF    = "PING PA-CF - Classic"
CLASSIC_PVA     = "PING PVA - Classic"
CLASSIC_TPE_210 = "PING TPE - Classic 210"   # 帶溫度尾碼＝與 PLA - Classic 210 命名一致
CLASSIC_SUP_TPE = "PING SupTPE - Classic"

def _fill_array(d, key, value):
    """保留 Orca 來源 preset 的槽位數，只替換每槽值。"""
    old = d.get(key)
    d[key] = [str(value)] * (len(old) if isinstance(old, list) and old else 1)

def _classic_filament(base_name, name, setting_id, temperature, bed_temperature, is_support=False):
    d = json.load(io.open(os.path.join(PINGDIR, "filament", "%s.json" % base_name), encoding="utf-8"))
    # 母檔的舊名相容標記不得帶進衍生支（0728 基礎支改名首驗抓到：Classic 210/EDU 跟著母檔
    # 帶 renamed_from "PING PLA" ＝兩支搶同一舊名、引擎解析任挑一支＝地雷；verify 有唯一性護欄）
    d.pop("renamed_from", None)
    d.update({"type":"filament", "name":name, "alias":name, "from":"system", "instantiation":"true",
              "setting_id":setting_id, "filament_id":setting_id})
    # 回抽只由 Classic machine preset 控制。不可讓材料層覆蓋，也不可送 Klipper 指令。
    for key in list(d):
        # 🔴 2026-08-26 修：原判斷只接 `filament_retract*`，接不到 **`filament_deretraction_speed`**
        #    （裝填速度也是回抽鍵，字首是 filament_de…）⇒ 0819 一般流量 sweep 幫基底線材設的 30
        #    會被 Classic 衍生支一起抄走 ⇒ **違反 Eric 0807「赤兔不能吃韌體回抽、材料層不得覆蓋回抽」**，
        #    而且 verify 的同名護欄有一模一樣的漏洞、驗得過。收 regen 漂移時 9 支 Classic 一起現形。
        if key.startswith("filament_retract") or key.startswith("filament_deretraction") \
                or key in ("filament_wipe", "filament_wipe_distance",
                                                          "filament_z_hop", "filament_z_hop_types"):
            d.pop(key, None)
    # ★ Classic 前代**改為跟進** F 系新工藝（Eric 2026-07-25 裁「Classic 套新工藝」，
    #   推翻 0719「Classic 不套預擠點升溫」延伸來的保守豁免）：
    #   懸空冷卻觸發閾值 25%（0724 爬坡品質批・線材側配套）自母檔複製時直接帶進來，不再 pop。
    #   ⚠ 舊 pop 之所以必要，是因為 _classic_filament 讀的是**磁碟上的母檔**（非冪等來源）；
    #     現在既然要套，就讓它自然繼承母檔值即可——但也因此 Classic 的該鍵**跟著母檔走**，
    #     日後若想再豁免，必須在這裡 pop 而不是在 sweep 端排除（此坑保留記錄）。
    d["filament_start_gcode"] = ["; Classic Marlin filament - machine retraction only\n"]
    d["enable_pressure_advance"] = ["0"]
    d["pressure_advance"] = ["0"]
    for key in ("adaptive_pressure_advance", "adaptive_pressure_advance_bridges",
                "adaptive_pressure_advance_overhangs"):
        if key in d:
            d[key] = ["0"]
    d["nozzle_temperature"] = [str(temperature)]
    d["nozzle_temperature_initial_layer"] = [str(temperature)]
    for key in ("hot_plate_temp", "hot_plate_temp_initial_layer", "cool_plate_temp",
                "cool_plate_temp_initial_layer", "textured_plate_temp", "textured_plate_temp_initial_layer",
                "eng_plate_temp", "eng_plate_temp_initial_layer"):
        if key in d or bed_temperature == 0:
            d[key] = [str(bed_temperature)]
    if is_support:
        d["filament_is_support"] = ["1"]
    return d

def emit_classic(mm_list, mac_list, proc_list, nozzles_of, gm, gp, pacf_twins=None):
    """V3.6 Classic：由已產生且可載入的 Fast preset 複製結構，再套 V2.1 舊機參數。"""
    classic_filaments = [
        # 母檔名跟進基礎支改名（0728）：BASE_PLA_NEW＝磁碟實檔名（_classic_filament 讀檔）
        _classic_filament(BASE_PLA_NEW,     CLASSIC_PLA_210, "PINGFILCLASSIC210", 210, 60),
        _classic_filament("PING PLA - 220", CLASSIC_PLA_220, "PINGFILCLASSIC220", 220, 60),
        _classic_filament("PING SupPLA",    CLASSIC_SUP_PLA, "PINGFILCLASSICSUP", 220, 60, True),
        _classic_filament(BASE_PLA_NEW,     CLASSIC_EDU_PLA, "PINGFILCLASSICEDU", 210, 0),
        # ★ Classic 全套跟 Fast 對齊（Eric 2026-08-07 裁，兩問兩答：①材料「全套跟 Fast 對齊」
        #   ②「改成只預勾 Classic 料」）。起因＝0807 早上「Classic 也全族補齊」把 Fast 13 支預勾給
        #   前代機，但 **Fast 線材的 filament_start_gcode 帶 Klipper 專用 SET_RETRACTION**，
        #   前代赤兔／Marlin 板不認；Eric 追加事實：「**赤兔不能吃韌體回抽**」。
        #   解法＝各材料開 Classic 版，由本工廠自動：pop 掉所有 filament_retract*／wipe／z_hop
        #   （⇒ 回抽全部由 Classic machine preset 控制，材料層不覆蓋）、start gcode 換成不帶
        #   Klipper 指令的註解行、PA 全關。溫度／床溫**照母檔實值**不另訂（＝「跟 Fast 對齊」）。
        _classic_filament("PING ABS",       CLASSIC_ABS,     "PINGFILCLASSICABS",    250, 100),
        _classic_filament("PING SupABS",    CLASSIC_SUP_ABS, "PINGFILCLASSICSUPABS", 250, 100, True),
        _classic_filament("PING PETG",      CLASSIC_PETG,    "PINGFILCLASSICPETG",   235, 75),
        _classic_filament("PING PA-CF",     CLASSIC_PACF,    "PINGFILCLASSICPACF",   245, 60),
        # 🔴 Eric 2026-08-26：255/70 是 2026-08-22 建這支時從當時的 `PING PA-CF` 抄來的
        #    （值一模一樣＝抄襲痕跡，非 Classic 專屬設計）。他當天裁 PA-CF 噴溫 255→245、床溫 70→60，
        #    當時我把 Classic 版列為「待呈」沒動；本次他要在 DUAL 單料頭（＝Classic 機）印 PA-CF，
        #    再留著 70 等於明知故犯 ⇒ 一併對齊。噴溫/床溫是材料屬性，與 Marlin/赤兔隔離無關。
        _classic_filament("PING PVA",       CLASSIC_PVA,     "PINGFILCLASSICPVA",    210, 60, True),
        _classic_filament("PING TPE - 210", CLASSIC_TPE_210, "PINGFILCLASSICTPE",    210, 60),
        _classic_filament("PING SupTPE",    CLASSIC_SUP_TPE, "PINGFILCLASSICSUPTPE", 210, 60, True),
    ]
    classic_fil_list = []
    for d in classic_filaments:
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % d["name"]), d)
        classic_fil_list.append({"name":d["name"], "sub_path":"filament/%s.json" % d["name"]})

    accel_keys = ("default_acceleration", "outer_wall_acceleration", "inner_wall_acceleration",
                  "sparse_infill_acceleration", "internal_solid_infill_acceleration",
                  "top_surface_acceleration", "initial_layer_acceleration", "bridge_acceleration",
                  "travel_acceleration")
    jerk_keys = ("default_jerk", "outer_wall_jerk", "inner_wall_jerk", "infill_jerk",
                 "top_surface_jerk", "initial_layer_jerk", "travel_jerk")

    def _classic_defaults(spec):
        return ([CLASSIC_PLA_220, CLASSIC_SUP_PLA] if spec["dual"] else
                [CLASSIC_EDU_PLA] if not spec["heated_bed"] else [CLASSIC_PLA_210])

    # ★ Classic base 補口徑（Eric 2026-09-01 裁：Q1＝丙〔跟 FD 來源走〕、Q2＝四台一起）。
    #   把「一台一個口徑」的 emit 主體抽成本函式：**既有口徑照舊由下面第一段迴圈呼叫，順序一字未動
    #   ⇒ PINGM/PINGP id 零位移**；新補的口徑放到所有既有 Classic（含 0726 那批變體）之後才呼叫，
    #   完全比照當時「變體放在既有 8 台迴圈之後＝既有 id 零位移」的作法。
    def _emit_one(spec, nz, src_nz, layer, initial, keep_src_speeds=False):
        nonlocal gm, gp
        name = spec["name"]
        src_mac_name = "%s %s nozzle" % (spec["src_model"], src_nz)
        src_mac = os.path.join(PINGDIR, "machine", "%s.json" % src_mac_name)
        mac = json.load(io.open(src_mac, encoding="utf-8"))
        mac_name = "%s %s nozzle" % (name, nz)
        proc_name = "%smm @%s (%s)" % (layer, name, nz)
        defaults = _classic_defaults(spec)

        mac.update({"type":"machine", "name":mac_name, "alias":name, "from":"system",
                    "instantiation":"true", "setting_id":(_nz_take_m() if _nz_res(name, nz) else "PINGM%03d" % gm),
                    "printer_model":name, "printer_variant":nz,
                    "default_print_profile":proc_name, "default_filament_profile":defaults,
                    "gcode_flavor":"marlin", "emit_machine_limits_to_gcode":"0",
                    # 🔴 相對擠出（2026-09-01・牌 c-0901-EXT-01／c-0901-CLS-01）：原本這裡寫死 "0"＝絕對擠出，
                    #   但機器檔的 before_layer_change_gcode 帶 G92 E0 ⇒ Print.cpp:1602-1620 直接擋下切片
                    #  （「G92 E0 was found in before_layer_gcode, which is incompatible with absolute
                    #    extruder addressing」）。Classic 從 2026-07-15 加進來那天起就切不了，0901 Eric 實機撞到。
                    #   同一段規則反向也成立：**相對擠出＋Marlin flavor 反而「要求」層變要有 G92 E0**（防浮點精度流失）
                    #   ⇒ 正解是改擠出模式，G92 E0 一行都不要刪。Eric 0901 裁「這個要勾起來」。
                    "use_firmware_retraction":"0", "use_relative_e_distances":"1",
                    "machine_pause_gcode":"M0", "disable_m73":"1",
                    "printable_height":spec["height"],
                    "single_extruder_multi_material":"1" if spec["dual"] else "0"})
        if isinstance(mac.get("printable_area"), list):
            mac["printable_area"] = scale_circle_area_from(
                mac["printable_area"], spec["src_diameter"], spec["diameter"])
        for key, value in (("retraction_length", spec["retract"]),
                           ("retraction_speed", spec["retract_speed"]),
                           ("deretraction_speed", spec["retract_speed"]),
                           ("retract_length_toolchange", spec["retract"]),
                           ("retract_restart_extra", "0"),
                           ("retraction_minimum_travel", "1"),
                           ("z_hop", "0.5"), ("wipe", "0"),
                           ("retract_before_wipe", "0%")):
            _fill_array(mac, key, value)
        mac["default_filament_colors"] = (["#EA4E16", "#D3D3D3"] if spec["dual"] else ["#EA4E16"])
        mac["filament_colors"] = list(mac["default_filament_colors"])
        _fill_array(mac, "max_layer_height", "%g" % (0.75 * float(nz)))
        _fill_array(mac, "min_layer_height", "0.1")

        # 0729 Klipper 同步單 #128（Eric 令）：赤兔板無 T 工具語意——溫度行一律不帶 T
        #（雙料共用一顆熱嘴；單料同去＝V2.1 後處理本就滅全部 M104/M109 T 行）
        heat = ["M104 S[nozzle_temperature_initial_layer]"]
        if spec["heated_bed"]:
            heat = ["M140 S[bed_temperature_initial_layer_single]"] + heat + [
                "M190 S[bed_temperature_initial_layer_single]"]
        heat += ["M109 S[nozzle_temperature_initial_layer]"]
        prime_e = "5" if spec["dual"] else "3"
        mac["machine_start_gcode"] = "\n".join(
            ["G21", "G90", "M82"] + heat + ["G28 ;Home", "G92 E0", "G1 F200 E%s" % prime_e, "G92 E0"])
        if spec["dual"]:
            # Classic 雙料換刀＝M6050 原生輸出（0729 同步單硬規則）：S=第一路比例（S1=工具0全開、
            # S0=工具1）、P0 常規檔固定帶。模板含 M6050 ⇒ C++ custom_gcode_changes_tool 認定已換刀
            # ⇒ 兩呼叫端不再補裸 Tn（含起印首次選刀）；兩路同溫 220 ⇒ 引擎無中途變溫指令。
            mac["change_filament_gcode"] = "{if next_extruder == 0}M6050 S1 P0{else}M6050 S0 P0{endif}"
        end = ["G91", "G1 Z10 E-1 F9000", "M104 S0"]
        if spec["heated_bed"]:
            end.append("M140 S0")
        mac["machine_end_gcode"] = "\n".join(end + ["G90", "G28 X0 Y0", "M84"])

        jdump(os.path.join(PINGDIR, "machine", "%s.json" % mac_name), mac)
        mac_list.append({"name":mac_name, "sub_path":"machine/%s.json" % mac_name})
        if not _nz_res(name, nz): gm += 1

        # 來源製程的檔名帶的是 **FD 母檔自己的層高**（不是 Classic 的），所以要用 src_nz 去查。
        # 原本寫死 `"0.2" if nz=="0.4" else "0.3"`＝只涵蓋 0.4／0.6 兩種；0901 補 0.25 與 1.0
        # 之後那個寫法會去找不存在的檔（例：`0.3mm … (1.0)`），故改成完整對照表。
        src_layer = SRC_LAYER_BY_NOZZLE[src_nz]
        src_proc_name = (combo_pname(src_layer, "PLA+SUP", spec["src_model"], src_nz)
                         if spec["dual"] else
                         ("%smm @%s (%s)" % (src_layer, spec["src_model"], src_nz)))
        proc = json.load(io.open(os.path.join(PINGDIR, "process", "%s.json" % src_proc_name), encoding="utf-8"))
        # 0730 改名批：Fast 母檔帶 renamed_from（易拆(Z0) 的舊名）——複製時必剝，否則 Classic
        # 製程冒領同一舊名＝重複認領地雷（0728 _classic_filament 線材版同族坑、verify 首跑實抓）。
        proc.pop("renamed_from", None)
        proc.update({"type":"process", "name":proc_name, "from":"system", "instantiation":"true",
                     "setting_id":(_nz_take_p() if _nz_res(name, nz) else "PINGP%03d" % gp), "inherits":"fdm_process_ping_common",
                     "compatible_printers":[mac_name], "layer_height":layer,
                     "initial_layer_print_height":initial, "seam_position":"back",
                     "support_type":"normal(auto)", "accel_to_decel_enable":"0",
                     # 檔名（Eric 2026-07-26 兩裁）：①跟進新格式 ②**取消「經典_」前綴**——
                     # 機型名本身已帶 DUAL/EDU/PING 2xx 字樣，再標「經典」對客戶無意義、他也不懂這個詞。
                     # ⇒ Classic 用它**實際的模式**：雙料機槽2 裝 Classic SupPLA ＝易拆；單料機＝單料。
                     #   例：`易拆_DUAL 600(0.6)_3DBenchy_3H_38g.gcode`
                     # ⚠ {print_time_half_h}/{total_weight_g} 是 2026-07-23 才進 Print.cpp 的佔位符，
                     #   舊 binary 吃到會炸 ⇒ profile 與 binary 必須同車出貨（既有耦合鐵則）。
                     "filename_format": filename_tpl("PLA+SUP" if spec["dual"] else "單料")})
        for key in accel_keys + jerk_keys:
            proc[key] = "0"
        # 🔴 速度來源（Eric 2026-09-01 裁「照 FD 字面」）：
        #   既有口徑沿用 Classic 專屬 speed_class（dual04／dual06 等，出貨中的值不動）；
        #   **2026-09-01 新補的口徑則保留 FD 母檔的速度、不覆寫**——與 Eric 0727 對同進／單料頭
        #   變體的裁定同一條（「參數繼承照 FD＝速度/層高/支撐全 FD 值」）。
        #   ⚠ Marlin 隔離不受影響：加速度／jerk 仍在上面全部歸 0、不送 machine limits、
        #     無韌體回抽／PA，回抽用 Classic 值——那幾項與速度是分開的兩件事。
        sc = spec["speed_class"]
        if sc == "single":
            speeds = {"outer_wall_speed":"40", "inner_wall_speed":"40", "sparse_infill_speed":"40",
                      "internal_solid_infill_speed":"40", "top_surface_speed":"40", "gap_infill_speed":"40",
                      "support_speed":"40", "support_interface_speed":"40", "bridge_speed":"40",
                      "initial_layer_speed":"30", "initial_layer_infill_speed":"30", "travel_speed":"250"}
        elif sc == "dual04":
            speeds = {"outer_wall_speed":"40", "inner_wall_speed":"40", "sparse_infill_speed":"40",
                      "internal_solid_infill_speed":"40", "top_surface_speed":"40", "gap_infill_speed":"40",
                      "support_speed":"25", "support_interface_speed":"25", "bridge_speed":"40",
                      "initial_layer_speed":"25", "initial_layer_infill_speed":"25", "travel_speed":"250"}
        else:
            speeds = {"outer_wall_speed":"40", "inner_wall_speed":"80", "sparse_infill_speed":"60",
                      "internal_solid_infill_speed":"60", "top_surface_speed":"40", "gap_infill_speed":"40",
                      "support_speed":"25", "support_interface_speed":"25", "bridge_speed":"40",
                      "initial_layer_speed":"25", "initial_layer_infill_speed":"25", "travel_speed":"250"}
        if not keep_src_speeds:
            proc.update(speeds)
        for key in ("line_width", "outer_wall_line_width", "inner_wall_line_width",
                    "sparse_infill_line_width", "internal_solid_infill_line_width",
                    "top_surface_line_width", "support_line_width"):
            proc[key] = nz
        proc["enable_prime_tower"] = "1" if spec["dual"] else "0"
        # ★ Classic 前代**套 F 系新工藝**（Eric 2026-07-25 裁「Classic 套新工藝」）。
        # Classic 由 Fast 母檔複製而來 ⇒ 母檔已套的新規則直接沿用，不再還原成 V3.6 Classic 舊值。
        # 沿用來源：normalize_unified_values ⑤爬坡品質/⑥支撐臨界角 35、normalize_support_geometry
        #（支撐幾何）、normalize_support_recipe（普通支撐配方）、normalize_tree_support（樹狀配方）。
        # 行為變更（本裁的實質內容，驗收看這幾項）：
        #   懸空降速 關→**開**（爬坡品質 50/50/25/10）、橋接流量 1→**0.95**、
        #   支撐臨界角 30→**35**、樹狀改保守配方（分支直徑 口徑×12 上限 10／距離 口徑×6／
        #   角度 30／auto_brim 關／brim 10／牆圈 1）、organic 防呆 2→2.6・60→40。
        # ⚠ 口徑安全：CLASSIC_SPECS 每台的 nozzle 皆等於 src_nozzle（0.4→0.4、0.6→0.6），
        #   所以母檔算出來的口徑連動值（線距／XY／分支直徑）對 Classic 直接成立，無錯配。
        # ⚠ Marlin 隔離原則仍在：本裁只放行「切片行為」類參數；加速度/jerk 全 "0"、
        #   不送 machine limits、不用韌體回抽／PA 等韌體相關設定在上方維持不變。
        jdump(os.path.join(PINGDIR, "process", "%s.json" % proc_name), proc)
        proc_list.append({"name":proc_name, "sub_path":"process/%s.json" % proc_name})
        if not _nz_res(name, nz): gp += 1

    def _emit_model(spec):
        """machine_model 一台只出一份，`nozzle_diameter` 聚合該台所有口徑（`a;b;c`）。"""
        name = spec["name"]
        nzs = _nozzles_of_spec(spec)
        mm = {"type":"machine_model", "name":name,
              "model_id":"PING_" + name.replace(" ", "_").replace("+", "Plus"),
              "nozzle_diameter":";".join(nzs), "machine_tech":"FFF", "family":"Classic",
              "bed_model":"" if spec["diameter"] in (200, 270) else bed_for(spec["src_model"]),
              "bed_texture":BED_TEXTURE, "hotend_model":"",
              "default_materials":";".join(_classic_defaults(spec))}
        jdump(os.path.join(PINGDIR, "machine", "%s.json" % name), mm)
        if not any(e["name"] == name for e in mm_list):
            mm_list.append({"name":name, "sub_path":"machine/%s.json" % name})
        nozzles_of[name] = list(nzs)

    # 第一段：既有口徑（順序與內容一字未動 ⇒ id 零位移）
    for spec in CLASSIC_SPECS:
        _emit_one(spec, spec["nozzle"], spec["src_nozzle"], spec["layer"], spec["initial"])
        _emit_model(spec)

    print("  Classic V3.6 併入：machine=%d + machine_model=%d，process=%d，filament=%d" %
          (len(CLASSIC_SPECS), len(CLASSIC_SPECS), len(CLASSIC_SPECS), len(classic_fil_list)))

    # ★ Classic DUAL 變體：同進＋單料頭（Eric 2026-07-26 裁「只針對 DUAL 四台」、07-27「照做」）。
    #   放在既有 8 台迴圈之後＝既有 Classic 的 PINGM/PINGP id 零位移（後續棧板/PVA twins id 位移＝預期）。
    #   口徑照 FD 對應機（DUAL 300→FD300 三口徑；450/600/800→FD Pro 三口徑，1.0 照裁定字面照出）；
    #   參數繼承照 FD＝machine/process 從 FD 對應「變體」複製（速度/層高/支撐全 FD 值——與既有
    #   DUAL 雙料本體的 Classic 速度不同體系，Eric 0727 確認照字面）；Marlin 隔離照舊（加速度/jerk
    #   歸 0、不送 limits、無韌體回抽/PA、回抽 Classic 值）。
    #   同進混色＝M6050 S 舊格式（前代 Marlin 韌體只認 M6050）：start gcode 於 G28 後插 M6050 S0.5
    #  （對應 FD 同進 start 的同款行＝預擠兩邊同進）；逐層插碼由 BackgroundSlicingProcess 判
    #   printer_model「DUAL」開頭改傳 M6050（兩線 C++ 對稱；PingColorMix 剝除規則本就含 M6050＝不雙插）。
    #   預擠沿用 Classic 原地擠法（G1 F200 E<n>），不搬 FD 的床邊預擠線（那是 Klipper 床幾何）。
    vm = vp = vmm = 0
    for spec in CLASSIC_SPECS:
        if not spec["dual"]:
            continue
        for variant in ("同進", "單料頭"):
            model = "%s %s" % (spec["name"], variant)
            src_model = "%s %s" % (spec["src_model"], variant)
            vnzs = nozzles_of.get(src_model)
            if not vnzs:
                print("  !! Classic 變體 %s 缺 FD 源機型 %s" % (model, src_model)); continue
            for nz in vnzs:
                src_mac_path = os.path.join(PINGDIR, "machine", "%s %s nozzle.json" % (src_model, nz))
                mac = json.load(io.open(src_mac_path, encoding="utf-8"))
                mac_name = "%s %s nozzle" % (model, nz)
                src_dpp = mac.get("default_print_profile", "")
                proc_name = src_dpp.replace("@%s (" % src_model, "@%s (" % model)
                assert proc_name != src_dpp, "FD 變體 default_print_profile 格式變了: %r" % src_dpp
                mac.update({"type":"machine", "name":mac_name, "alias":model, "from":"system",
                            "instantiation":"true", "setting_id":(_nz_take_m() if _nz_res(model, nz) else "PINGM%03d" % gm),
                            "printer_model":model, "printer_variant":nz,
                            "default_print_profile":proc_name,
                            # 0728 Eric 裁「Classic 變體預設跟」：變體＝單一出料 → Classic 210
                            #（與 FD 連動同邏輯；DUAL 本體雙料維持 Classic 220 見 emit_classic defaults）
                            "default_filament_profile":[CLASSIC_PLA_210],
                            "gcode_flavor":"marlin", "emit_machine_limits_to_gcode":"0",
                            # 🔴 同上（牌 c-0901-EXT-01）：變體也一樣，絕對擠出＋層變 G92 E0 會被擋下切片。
                            "use_firmware_retraction":"0", "use_relative_e_distances":"1",
                            "machine_pause_gcode":"M0", "disable_m73":"1",
                            "printable_height":spec["height"],
                            "single_extruder_multi_material":"0"})
                if isinstance(mac.get("printable_area"), list):
                    mac["printable_area"] = scale_circle_area_from(
                        mac["printable_area"], spec["src_diameter"], spec["diameter"])
                for key, value in (("retraction_length", spec["retract"]),
                                   ("retraction_speed", spec["retract_speed"]),
                                   ("deretraction_speed", spec["retract_speed"]),
                                   ("retract_length_toolchange", spec["retract"]),
                                   ("retract_restart_extra", "0"),
                                   ("retraction_minimum_travel", "1"),
                                   ("z_hop", "0.5"), ("wipe", "0"),
                                   ("retract_before_wipe", "0%")):
                    _fill_array(mac, key, value)
                mac["default_filament_colors"] = ["#EA4E16"]   # 變體＝1 槽（SEMM 0，同 FD 變體）
                mac["filament_colors"] = list(mac["default_filament_colors"])
                _fill_array(mac, "max_layer_height", "%g" % (0.75 * float(nz)))
                _fill_array(mac, "min_layer_height", "0.1")
                heat = ["M104 S[nozzle_temperature_initial_layer]"]
                if spec["heated_bed"]:
                    heat = ["M140 S[bed_temperature_initial_layer_single]"] + heat + [
                        "M190 S[bed_temperature_initial_layer_single]"]
                heat += ["M109 S[nozzle_temperature_initial_layer]"]
                # 同進＝兩馬達同動各半（先 M6050 S0.5 再擠 5）；單料頭＝實體單頭擠 3（同 Classic 單料機）
                sync = ["M6050 S0.5"] if variant == "同進" else []
                prime_e = "5" if variant == "同進" else "3"
                mac["machine_start_gcode"] = "\n".join(
                    ["G21", "G90", "M82"] + heat + ["G28 ;Home"] + sync
                    + ["G92 E0", "G1 F200 E%s" % prime_e, "G92 E0"])
                end = ["G91", "G1 Z10 E-1 F9000", "M104 S0"]
                if spec["heated_bed"]:
                    end.append("M140 S0")
                mac["machine_end_gcode"] = "\n".join(end + ["G90", "G28 X0 Y0", "M84"])
                jdump(os.path.join(PINGDIR, "machine", "%s.json" % mac_name), mac)
                mac_list.append({"name":mac_name, "sub_path":"machine/%s.json" % mac_name})
                vm += 1
                if not _nz_res(model, nz): gm += 1

                proc = json.load(io.open(os.path.join(PINGDIR, "process", "%s.json" % src_dpp),
                                         encoding="utf-8"))
                proc.update({"type":"process", "name":proc_name, "from":"system", "instantiation":"true",
                             "setting_id":(_nz_take_p() if _nz_res(model, nz) else "PINGP%03d" % gp), "inherits":"fdm_process_ping_common",
                             "compatible_printers":[mac_name], "accel_to_decel_enable":"0",
                             "filename_format": filename_tpl("同進" if variant == "同進" else "單料")})
                for key in accel_keys + jerk_keys:
                    proc[key] = "0"
                jdump(os.path.join(PINGDIR, "process", "%s.json" % proc_name), proc)
                proc_list.append({"name":proc_name, "sub_path":"process/%s.json" % proc_name})
                vp += 1
                if not _nz_res(model, nz): gp += 1
                # PACF_CLASSIC — PA-CF 專屬製程（Eric 2026-08-26 四裁「DUAL 單料頭也一起補上」）。
                # DUAL 單料頭是 Classic 前代機、不走主迴圈，故 PACF_MODELS 掃不到，掛鉤在這裡。
                # ⚠ **不在這裡配 setting_id、也不動 gp**：統一交給 4a-7 在全庫最尾 emit ＝既有 id 零位移。
                # 值＝同一組 pacf_overrides()（只動速度/流量/brim/填充/頂面牆/接縫），
                # Marlin 隔離（加速度與 jerk 全 0、無韌體回抽/PA）由上面那圈 accel_keys+jerk_keys 歸 0 自然繼承。
                if variant == "單料頭" and pacf_twins is not None:
                    _pf = dict(proc); _pf.update(pacf_overrides())
                    _pf["name"] = proc_name.replace("mm @", "mm PA-CF @")
                    pacf_twins.append(_pf)
                    _pft = dict(_pf); _pft.update(PACF_TREE_OVERRIDES)
                    _pft["name"] = proc_name.replace("mm @", "mm PA-CF 樹狀 @")
                    pacf_twins.append(_pft)

            mm = {"type":"machine_model", "name":model,
                  "model_id":"PING_" + model.replace(" ", "_"),
                  "nozzle_diameter":";".join(vnzs), "machine_tech":"FFF", "family":"Classic",
                  "bed_model":bed_for(spec["src_model"]), "bed_texture":BED_TEXTURE, "hotend_model":"",
                  "default_materials":CLASSIC_PLA_210}   # 0728 變體預設跟（單一出料＝210）
            jdump(os.path.join(PINGDIR, "machine", "%s.json" % model), mm)
            mm_list.append({"name":model, "sub_path":"machine/%s.json" % model})
            nozzles_of[model] = list(vnzs)   # cover 空白卡（endswith 同進/單料頭）＋側欄縮圖迴圈自動吃
            vmm += 1

    print("  Classic DUAL 變體併入：machine=%d，process=%d，machine_model=%d" % (vm, vp, vmm))

    def _emit_extras(_gm, _gp):
        """
        Classic base 補口徑（Eric 2026-09-01 裁 Q1＝丙／Q2＝四台一起）。

        🔴 **由 main() 在「全庫最尾」呼叫**（4a-8，接在 PA-CF twins 之後），不是在這裡就地跑。
        理由＝`PINGM`／`PINGP` 是流水號：插在中間會讓後面所有 preset 的 setting_id 整批位移
        （實測會動到 124 支既有 preset）。這條紀律是這支檔一路守著的——0726 變體那批放在
        既有 8 台之後、0826 PA-CF twins 統一在最尾 emit，都是同一個理由。

        層高／首層高取 FD 母檔同口徑值（`SRC_LAYER_BY_NOZZLE`／`SRC_INITIAL_BY_NOZZLE`）＝Eric 裁的丙案。
        🔴 **速度＝照 FD 字面**（Eric 2026-09-01 裁）：新補的口徑保留 FD 母檔速度、不套 Classic 的
           speed_class。起因＝既有 base 的 dual04／dual06 是按口徑分的，而 **1.0 沒有對應的
           Classic class**；與其發明第三組數字，不如照 Eric 0727 對變體的同一條裁定走。
           既有口徑不受影響（仍用各自的 speed_class）。
        """
        nonlocal gm, gp
        gm, gp = _gm, _gp
        en = ep = 0
        for spec in CLASSIC_SPECS:
            extras = spec.get("extra_nozzles") or []
            if not extras:
                continue
            for nrow in extras:
                _emit_one(spec, nrow["nozzle"], nrow["src_nozzle"], nrow["layer"], nrow["initial"],
                          keep_src_speeds=True)   # Eric 2026-09-01 裁「照 FD 字面」
                en += 1; ep += 1
            _emit_model(spec)   # 重出一次，nozzle_diameter 聚合成 `a;b;c`
        print("  Classic base 補口徑：machine=%d，process=%d（machine_model 就地聚合）" % (en, ep))
        return gm, gp

    return gm, gp, classic_fil_list, _emit_extras

# ---------- 3z. 預勾線材（default_materials）post-pass ----------
# ★ Eric 2026-08-07 裁「全族補齊」：每台機型的 default_materials ＝「所有與它相容的 PING 線材」。
#   起因＝Eric 實地發現設定精靈「選擇線材」頁只預勾 12 支，`PING PVA`／`PING TPE - 210`／
#   `PING SupTPE` 更是 0/41 台預勾——客戶端一定得自己去勾才看得到我們自家的料。
#
# 為什麼做成 post-pass、而不是各處補字串：預勾清單原本散在 **4 條 emit 路徑**
#（DEFAULT_MATERIALS_FD／def_fil_ff／emit_classic 本體與變體）**＋6 個 base 範本**硬寫，
# 正是 SOP_參數入版紀律 §4「產生器規則函式掃不到的來源」的教科書案例（0726 一天踩三次）。
# 收斂成單一 post-pass 後規則只有一處，日後新增線材／新增機型自動涵蓋，且順手剔除
# filament_list 已無的死名（0725 ABS 整併留下的 `PING ABS - 250`／`PING PolyABS`）。
#
# 兩條族群規則：
#  (a) **族群雙向隔離**（Eric 2026-08-07 二裁「改成只預勾 Classic 料」）：
#      Classic 前代機 → **只**預勾 Classic 版線材；Fast 機 → **只**預勾非 Classic 線材。
#      機型判定照 SOP §9：前代機＝機型名前綴 `EDU|DUAL|PING 2|PING 3`，沒有任何機器叫「Classic」；
#      線材判定＝名字含「Classic」。⚠ Classic 線材沒設 compatible_printers（＝不限機型），
#      光靠相容性推導兩邊都會互相外溢，所以這條隔離是必要的、不是保險。
#  (b) 其餘依各自的 compatible_printers（3in1 支綁 3in1 機、四料高流量支綁 FF 非 3in1）。
#
# 📌 演進軌跡（同一天兩裁，別誤讀成反覆）：
#   0807 早上 Eric 裁「Classic 也全族補齊」＝前代機連 Fast 13 支一起預勾；當時我已具實證告知
#   「Fast 線材 filament_start_gcode 帶 Klipper 專用 SET_RETRACTION、Classic 線材刻意不帶」，
#   Eric 看過後重申照做，登錄為有意識取捨。0807 稍晚 Eric 改採**正解**：
#   「Classic 版線材全套跟 Fast 對齊（+7 支）」＋「前代機改成只預勾 Classic 料」，
#   並補充事實「**赤兔不能吃韌體回抽**」。⇒ 前代機現在有自己的完整 11 支，
#   且 filament_retract* 全由 _classic_filament() pop 掉、回抽只由 Classic machine preset 控制。
CLASSIC_MODEL_RE = re.compile(r"^(EDU|DUAL|PING 2|PING 3)")

def _is_classic_model(name):
    return bool(CLASSIC_MODEL_RE.match(name))

def apply_default_materials(pj):
    """依「線材 compatible_printers × 機型」重算每台 machine_model 的 default_materials。"""
    def _load(sub):
        p = os.path.join(PINGDIR, sub)
        return json.load(io.open(p, encoding="utf-8")) if os.path.isfile(p) else None

    # 1) 可勾的 PING 線材 → compatible_printers（None＝不限機型）。fdm_filament_* 基底
    #    instantiation=false、使用者選不到，不進預勾清單。
    fil_compat = {}
    for e in pj["filament_list"]:
        if not (e["name"].startswith("PING ") or e["name"] in NON_BRAND_FILAMENTS):
            continue
        d = _load(e["sub_path"])
        if not d or d.get("instantiation") != "true":
            continue
        cp = d.get("compatible_printers")
        fil_compat[e["name"]] = set(cp) if isinstance(cp, list) and cp else None
    order = list(fil_compat)   # filament_list 順序＝新增項排序基準（dict 保序）

    # 2) printer_model → 該機型底下的 machine preset 名（口徑變體）
    variants = {}
    for e in pj["machine_list"]:
        d = _load(e["sub_path"])
        if d and d.get("instantiation") == "true" and d.get("printer_model"):
            variants.setdefault(d["printer_model"], set()).add(e["name"])

    changed = added = dropped = n_classic = n_fast = 0
    for e in pj["machine_model_list"]:
        p = os.path.join(PINGDIR, e["sub_path"])
        d = json.load(io.open(p, encoding="utf-8"))
        model = d["name"]
        vs = variants.get(model)
        if not vs:
            print("  ⚠ 預勾 post-pass：機型 %s 找不到任何 machine preset，跳過" % model)
            continue
        classic_model = _is_classic_model(model)
        n_classic += classic_model
        n_fast += (not classic_model)
        want = [n for n in order
                if (fil_compat[n] is None or (fil_compat[n] & vs))          # (b) 相容性
                and (("Classic" in n) == classic_model)]                    # (a) 族群雙向隔離
        old = [x for x in (d.get("default_materials") or "").split(";") if x]
        keep = [x for x in old if x in want]                                # 保序＝最小 diff
        new = keep + [x for x in want if x not in keep]
        dropped += len(old) - len(keep)
        added += len(want) - len(keep)
        if new != old:
            d["default_materials"] = ";".join(new)
            jdump(p, d)
            changed += 1
    # SOP §4 對帳：數量對不上＝有掃不到的來源，不是四捨五入
    print("  預勾線材全族補齊：%d/%d 台機型更新（Fast %d 台／Classic %d 台；"
          "新增 %d 項、剔除死名或不相容 %d 項）"
          % (changed, len(pj["machine_model_list"]), n_fast, n_classic, added, dropped))
    return changed


# ---------- 4. 主流程 ----------

def apply_printer_family_gates(mac_list, mm_list):
    """把「哪一家族的機器看得到哪一批線材」的閘門寫進產出檔。

    抽成獨立函式的理由（2026-09-01・牌 c-0901-GATE-01）：這段是**冪等的 post-pass**，
    只碰 `printer_notes`／`default_materials`／`compatible_printers_condition` 三個欄位。
    抽出來之後就能「只跑這一段」把規則套到現有產出，不必為了一條相容性規則跑全量 regen
    （全量 regen 會重建機型/製程/PING.json，0901 才因為新口徑的 emit 位置害 124 支 id 位移）。
    main() 照樣呼叫它，所以日後任何一次全量 regen 都會帶上這條規則、不會被洗掉。
    """
    PT_NOTE = "PHOTOTILE"
    # 🆕 **照片磚機器層回抽政策（Eric 2026-09-07 裁，取代 0718 的「零回抽」；2026-09-12 移植進出貨線）**
    #   原話：「需要做回抽抬升，回抽的距離採用韌體回抽的參數即可，抬升的高度 0.1，不用太高。」
    #   起因＝短路徑（照片磚某些區塊只繞一圈、或長度 < 1mm）吐料不飽滿，判斷是壓力不足。
    # ⚠ **這是明文推翻既有定案**：零回抽來自 2026-07 的 %APPDATA% 實印驗證檔；變更權在 Eric（新的實印證據）。
    # 值的來源＝**不另訂數字**，直接用同進家族既有的韌體回抽組（retraction_length 1.3／use_firmware_retraction 1），
    #   只有抬升照 Eric 指定改 0.1（一般同進機是 0.4；他說「不用太高」）。
    # 🔴 **`retract_length_toolchange` 刻意維持 0**：照片磚的 Tn 會被後處理換成 M6051/M6052 混色指令，不是真的換料頭。
    # 🔴 **`wipe`／`wipe_on_loops`／`seam_gap` 一律不動**：那是 0718 的接縫定案，與回抽是兩件事。
    # 線材層仍是 `nil`（吃機器層）＝ 0819 那條護欄的形狀不變，只是機器層的值從 0 換成 1.3。
    # 🆕 **2026-09-19 Eric 改裁：抬升 0.1 → 0.4**（牌 c-0919-ZH-01；只推翻上面 0907「抬升 0.1，不用太高」那一句，回抽長度／韌體回抽不變）。
    #   起因＝0919 `.186` 年輕女照片磚實印（100×150×10 mm、FD300 同進照片磚 0.4）：真空跑 8,093 次以 250 mm/s、只抬 0.1 mm
    #   掠過磚頂（其中 2,193 次帶磚薄方向 Y 分量），洗料塔 4,592 次同樣只抬 0.1 ⇒ 磚與塔都倒（Eric：成品不錯，只是沒黏緊）。
    #   頂面只要有 >0.1 mm 的料瘤就會被撞。0.4＝一般同進機既有值；19 台照片磚機（0.4／0.6／1.0 口徑）一律 0.4。
    PT_RETRACTION = {
        "use_firmware_retraction": "1",
        "retraction_length": ["1.3", "1.3"],
        "z_hop": ["0.4"],
    }
    PT_COND = "printer_notes!~/.*PHOTOTILE.*/"

    # 4d-2c. ★ Classic 前代機 × 標準線材 **雙向隔離**（Eric 2026-09-01 裁，牌 c-0901-GATE-01）
    # 起因：Eric 在 FD300 的線材下拉看到 Classic 材料。實查根因＝那幾支 PLA **完全沒有機型閘門**，
    # 唯一的條件式是上面那條「非照片磚機」⇒ Classic 材料對所有非照片磚機都可見。
    # 🔴 **反向才是會印壞東西的那一邊**：Classic 機同樣看得到標準線材，而標準線材的
    #    `filament_start_gcode` 送的是 `SET_RETRACTION`（**Klipper 指令**）——Classic 是 **Marlin**，吃不到。
    # ⚠️ 為什麼不是把兩組線材合併成一支（Eric 原本的提議）：`Classic 210` 與 `210` 差 **12 項**
    #    （start gcode 換成「machine retraction only」／六個回抽鍵設 `nil` 交給機器端／
    #    `pressure_advance` 0.08→0 且 PA 關），`Classic 220` 差 17 項、`EDU Classic` 差 18 項（床溫全 0）。
    #    合併＝把 Klipper 指令灌進 Marlin 機並把 PA 打開。
    # ℹ️ 與 0807「族群雙向隔離」**互補不重複**：那條管 wizard 的**預勾**（`default_materials`），
    #    這條管**下拉選單的可見性**（`compatible_printers_condition`）。
    CLASSIC_NOTE = "CLASSIC"

    # 🔴 用「這批機器存不存在」當閘門，**不要用下面那些 *_m 計數器**：計數器是「本次改了幾支」，
    #    第二次跑時 notes 早就對了 ⇒ 計數 0 ⇒ 條件式整段被跳過。舊寫法 `if pt_m:` 就是這個形狀，
    #    它只在「第一次跑」正確；現在條件式由兩個家族共同決定，那個形狀會變成靜默漏掛。
    _pt_any = any("照片磚" in _e["name"] for _e in mac_list)
    _cl_any = any(_is_classic_model(_e["name"]) for _e in mac_list)

    def _want_filament_cond(_name):
        """線材該吃的條件式：兩個家族各出一條，用 `and` 併。
        文法支援已實查：`PlaceholderParser.cpp:2137` 的 `(kw["and"] | "&&")`。"""
        _parts = []
        if _pt_any:
            _parts.append(PT_COND)
        if _cl_any:
            _parts.append(("printer_notes=~/.*%s.*/" if "Classic" in _name
                           else "printer_notes!~/.*%s.*/") % CLASSIC_NOTE)
        return " and ".join(_parts)

    pt_m = pt_mm = pt_f = cl_m = 0
    for _e in mac_list:
        _is_pt = "照片磚" in _e["name"]
        _is_cl = _is_classic_model(_e["name"])
        if not (_is_pt or _is_cl):
            continue
        # 兩個家族本應互斥（照片磚機是 FD300／FF；Classic 是 EDU/DUAL/PING 2/PING 3 前綴）。
        # 真的同時命中＝命名出事了，寧可當場爆掉也不要靜默把 notes 寫成其中一邊。
        assert not (_is_pt and _is_cl), "機器同時被判為照片磚與 Classic：%s" % _e["name"]
        _fp = os.path.join(PINGDIR, _e["sub_path"].replace("machine/", "machine" + os.sep))
        if not os.path.isfile(_fp):
            continue
        _d = json.load(io.open(_fp, encoding="utf-8"))
        _note = PT_NOTE if _is_pt else CLASSIC_NOTE
        _before = json.dumps(_d, sort_keys=True)
        _d["printer_notes"] = _note
        if _is_pt:
            _d.update(PT_RETRACTION)          # 🆕 0907：照片磚機器層回抽政策（見 PT_RETRACTION 註解）
        if json.dumps(_d, sort_keys=True) != _before:
            jdump(_fp, _d)
            if _is_pt:
                pt_m += 1
            else:
                cl_m += 1
    for _e in mm_list:
        if "照片磚" not in _e["name"]:
            continue
        _fp = os.path.join(PINGDIR, _e["sub_path"].replace("machine/", "machine" + os.sep))
        if not os.path.isfile(_fp):
            continue
        _d = json.load(io.open(_fp, encoding="utf-8"))
        _want = pt_filament_for_model(_e["name"])   # 單一對照表（pt_fil_specs）
        if _d.get("default_materials") != _want:
            _d["default_materials"] = _want
            jdump(_fp, _d); pt_mm += 1
    if _pt_any or _cl_any:   # 兩個家族都不存在＝整段自然跳過，不要平白給線材加條件式
        for _fp in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
            _d = json.load(io.open(_fp, encoding="utf-8"))
            if _d.get("instantiation") != "true":
                continue
            if _d.get("compatible_printers"):
                # 明列優先於條件式 ⇒ 不只是「不加」，而是**主動移除**。
                # 🔴 2026-08-26 實錄：`PING PLA(照片磚 FD300)` 被加上「非照片磚機」條件式＝自我否定。
                #    真因不在這裡——4b-1e 是「複製 PING PLA - 210 再改身分」，0822 幫 210 加的條件式
                #    被一起抄進照片磚專用支。改成主動移除＝**自癒**，不依賴任何 emit 時序。
                if _d.pop("compatible_printers_condition", None) is not None:
                    jdump(_fp, _d); pt_f += 1
                continue
            _want_cond = _want_filament_cond(_d.get("name") or os.path.splitext(os.path.basename(_fp))[0])
            if _d.get("compatible_printers_condition") != _want_cond:
                _d["compatible_printers_condition"] = _want_cond
                jdump(_fp, _d); pt_f += 1
    if pt_m or pt_mm or pt_f or cl_m:
        print("  線材機型閘門（照片磚 0822／Classic 0901）：照片磚 notes %d 台｜Classic notes %d 台｜model 可勾清單 %d 支｜線材條件式 %d 支"
              % (pt_m, cl_m, pt_mm, pt_f))


def main(src_base):
    _nz_reset()   # 保留號段計數器歸位（模組層級，見檔頭 EXT_RESERVED_* 區塊）
    # 4-0. 🔴 已淘汰鍵閘門——必須是第一個敘述：下面 4a 立刻就會刪檔，
    #      擺在後面等於 repo 先被掏空才報錯。（牌 c-0827-SIT-05）
    check_deprecated(src_base)

    # 4a. 清掉舊 machine/process（保留 fdm 基底）
    for sub, keep in (("machine", ("fdm_machine_common.json","fdm_ping_common.json")),
                      ("process", ("fdm_process_common.json","fdm_process_ping_common.json"))):
        d = os.path.join(PINGDIR, sub)
        for f in os.listdir(d):
            if f.endswith(".json") and f not in keep:
                os.remove(os.path.join(d, f))

    # 4a-0. ★ 基礎支改名 sweep（Eric 2026-07-28；常數見 BASE_PLA_OLD/NEW）——放在最前：
    #       emit_classic 以新名讀母檔、4b-2 系列 sweep 掃到的即是新檔；殘檔清除＝regen-durable。
    _oldp = os.path.join(PINGDIR, "filament", BASE_PLA_OLD + ".json")
    _newp = os.path.join(PINGDIR, "filament", BASE_PLA_NEW + ".json")
    if os.path.isfile(_oldp):
        _fd = json.load(io.open(_oldp, encoding="utf-8"))
        _fd.update({"name": BASE_PLA_NEW, "alias": BASE_PLA_NEW,
                    "renamed_from": BASE_PLA_OLD})   # ⚠ 字串（T004 鐵則）
        jdump(_newp, _fd)
        os.remove(_oldp)
        print("  基礎支改名：%s → %s（renamed_from 字串相容、id 不動）" % (BASE_PLA_OLD, BASE_PLA_NEW))

    # 4a-0b. ★ 配料屬性顯式化【第一趟：衍生之前】（Eric 2026-08-13 裁・連動規格批1）
    # 🔴 **為什麼要跑兩趟**（0813 冪等測試實抓）：Classic 線材（`_classic_filament`）與高流量三支
    #   （4b-1b）都是**從磁碟上的母檔複製**再改——程式碼註解自己就標了「非冪等來源」。
    #   若只在衍生之後補鍵：第一輪母檔無鍵 ⇒ 衍生支也無鍵 ⇒ 補在檔尾（`alias` 之後）；
    #   第二輪母檔已帶鍵 ⇒ 衍生支繼承到母檔的位置 ⇒ **同樣內容、鍵序不同** ⇒ 每次 regen 都churn。
    #   先在這裡讓**靜態母檔**帶鍵，衍生支就穩定繼承同一位置；第二趟（4b-2g）再收新建的支。
    #   ⚠ 這是既有的非冪等來源被本批暴露出來，不是本批引入的——修法選「補鍵跑兩趟」而不是
    #     「叫衍生產生器各自 pop」，理由同 4b-6 註解：規則放單一點，日後新增衍生路徑自動涵蓋。
    _backfill_filament_attrs("4a-0b（衍生前）")

    gm = gp = 0
    mm_list, mac_list, proc_list = [], [], []
    nozzles_of = {}   # model -> [nz...]
    pallet_twins = []   # 棧板雙生製程（主迴圈收集、4a-4 統一 emit＝id 排最後）
    pva_twins = []      # PLA+PVA 專屬製程（同上，emit 接在棧板之後＝棧板 id 亦零位移）
    pacf_twins = []     # PA-CF 專屬製程（Eric 0826；emit 在 4a-7＝全庫最尾＝既有 id 零位移）
    easy_tree_twins = []  # 易拆樹狀（Eric 0919；emit 在 4a-9＝接 4a-8 之後的真正全庫最尾）
    abscf_fast_twins = []  # 0.2mm 高速（Eric 1008；emit 在 4a-10＝接 4a-9 之後的真正全庫最尾）

    for dirname, base, kind in FAMS:
        cfgs = parse_dir(src_base, dirname)
        if kind == "dual":
            modes = [("PLA+SUP", base, def_fil_dual_for(base), False),   # 雙料機母檔=PLA+SUP；製程另出 4 組合
                     ("單料頭", base + " 單料頭", def_fil_single_for(base), True),
                     ("同進",   base + " 同進",   def_fil_single_for(base), True)]
        elif kind == "dual1":   # 衍生雙料機只出本體（FD300 關門——Eric 裁不出 關門 同進/單料頭）
            modes = [("PLA+SUP", base, def_fil_dual_for(base), False)]
        elif kind == "single":
            modes = [("單料頭", base, def_fil_single_for(base), True)]
        else:
            modes = [("四色", base, None, False)]

        for mode_key, model, def_fil, is_single in modes:
            nzs = sorted({nz for (nz, mk) in cfgs if mk == mode_key}, key=float)
            _only_nz = BED_OVERRIDE.get(model, {}).get("nozzles")   # 衍生機型限定口徑（P200+ 去 0.2）
            if _only_nz:
                nzs = [n for n in nzs if n in _only_nz]
            if not nzs:
                print("  !! %s 缺 %s config" % (dirname, mode_key)); continue
            nozzles_of[model] = nzs
            for nz in nzs:
                c = cfgs[(nz, mode_key)]
                b = split(c)
                lh = c.get("layer_height", "0.2")
                mac_name = "%s %s nozzle" % (model, nz)
                # 雙料機：製程依 4 組合各出一支（V3.0 行為復原，2026-06-10）；其餘一機一製程
                is_dual_machine = (kind in ("dual", "dual1") and mode_key == "PLA+SUP")
                combos = [cb for cb in DUAL_COMBOS if (nz, cb) in cfgs] if is_dual_machine else [mode_key]
                def pname(cb):
                    return combo_pname(lh, cb, model, nz) if is_dual_machine \
                        else ("%smm @%s (%s)" % (lh, model, nz))
                # machine（雙料取 PLA+SUP 母檔）
                mac = dict(b["M"])
                # PING(2026-06-10)：換層回抽=關（全機型）——花瓶模式換層縫線明顯（使用者規格）
                if isinstance(mac.get("retract_when_changing_layer"), list):
                    mac["retract_when_changing_layer"] = ["0"] * len(mac["retract_when_changing_layer"])
                mac.update({"type":"machine","name":mac_name,"from":"system","instantiation":"true",
                    "setting_id":(_ext_take_m() if model in EXT_RESERVED_MODELS
                                  else _nz_take_m() if _nz_res(model, nz)
                                  else "PINGM%03d"%gm),"printer_model":model,"printer_variant":nz,
                    "default_print_profile":pname(combos[0]),
                    # alias=機型名 → active 標籤顯示乾淨名；口徑走噴嘴 chip(printer_variant)
                    "alias":model})
                mac["default_filament_profile"] = def_fil if def_fil else def_fil_ff(nz)
                # PING(2026-06-16)：max_layer_height 隨口徑連動＝0.75×口徑（OrcaSlicer 慣例）。
                # 修源檔把 0.35 一律套全機型、把 1.0 口徑標準層高 0.5 夾成 0.35 的 bug（陣列長度保留）。
                _mlh = "%g" % round(0.75 * float(nz), 4)
                _old_mlh = mac.get("max_layer_height")
                mac["max_layer_height"] = [_mlh] * (len(_old_mlh) if isinstance(_old_mlh, list) and _old_mlh else 1)
                apply_bed_override(model, mac)   # 衍生機型改列印範圍（FP200：床250/高200/預擠內移）
                normalize_prime_lines(mac)         # 預擠中途 travel F800＋T0/T1 結尾 T0（2026-07-12）
                apply_fd300_prime_arc(model, mac)  # FD300 預擠改左側弧線（防撞門，2026-07-09）
                jdump(os.path.join(PINGDIR,"machine","%s.json"%mac_name), mac)
                mac_list.append({"name":mac_name,"sub_path":"machine/%s.json"%mac_name})
                if not (model in EXT_RESERVED_MODELS or _nz_res(model, nz)): gm += 1
                # processes（inherits 必須指向存在父 preset，絕不可空字串——坑#12）
                for cb in combos:
                    pb = split(cfgs[(nz, cb)])["P"] if is_dual_machine else b["P"]
                    proc = dict(pb)
                    # PING 支撐通用（2026-06-16）：攤平母檔會夾帶 Orca 原始預設 support_base_pattern=rectilinear
                    # 與 tree_support_wall_count=0，蓋掉 fdm_process_ping_common 的「空心／樹狀牆1」→ 先移除讓葉檔
                    # 繼承 common。ABS+SUP 的 V3.0 黃金支撐配方在 combo_overrides 之後另行覆寫，不受影響。
                    proc.pop("support_base_pattern", None)
                    proc.pop("tree_support_wall_count", None)
                    proc.update(proc_overrides(kind, base, is_single))
                    if is_dual_machine:
                        proc.update(combo_overrides(cb, lh, nz))
                    # PING(2026-08-17 Eric 裁)：支撐 Z 間距全庫統一＝易拆 0／一般 0.2。
                    # ⚠ **無條件跑、刻意不放進 is_dual_machine**——非雙料機（單料頭／同進／四料／單料機）
                    #   正是現況最亂的一群（它們根本不跑 combo_overrides）。放進去等於只修一半。
                    # ⚠ 位置必須在 combo_overrides **之後**：後者對 +SUP 也寫這兩個鍵，先跑會被蓋掉。
                    normalize_support_z(proc, easy_release=cb in EASY_COMBOS)
                    normalize_support_mode(proc, model)
                    normalize_fast_speed(proc)   # 牆速/填充正規化（外60/內≤80/填100/accel5000；首層不動）
                    normalize_prime_tower(proc)  # 換料塔統一（0708 立；0717 寬 25；0729 錐體30/速60）
                    normalize_wall_accel(proc)   # 內外牆加速度 1500（2026-07-29 Eric 裁）
                    normalize_unified_values(proc, ff=(kind == "ff"))  # 正式製程統一值；FF 四色 jerk 維持 40
                    # 介面 4 層＋間距 70% 密度等效口徑連動＋首層密度 10%（0804；易拆間距既值不動、
                    # ABS 系 raft_layers＝3 已由 combo_overrides 設定→首層 100% 自然跳過）
                    normalize_support_interface(proc, nz, easy_release=cb.endswith("+SUP"))
                    normalize_support_geometry(proc, nz)  # 樹狀直徑口徑×12(上限10)＋分支距離口徑×6＋主體線距口徑×9（0717/0722/0725）
                    normalize_support_recipe(proc, nz, easy_release=cb.endswith("+SUP"))  # 普通支撐配方（2026-07-22 七裁）
                    normalize_tree_support(proc)  # 樹狀保守配方＋organic 防呆（2026-07-25）
                    proc.update({"type":"process","name":pname(cb),"from":"system","instantiation":"true",
                        "setting_id":(_ext_take_p() if model in EXT_RESERVED_MODELS
                                      else _nz_take_p() if _nz_res(model, nz)
                                      else "PINGP%03d"%gp),"inherits":"fdm_process_ping_common",
                        "compatible_printers":[mac_name],
                        "filename_format": filename_tpl(cb)})
                    if is_dual_machine:   # 功能歸類改名：舊材料對全名入 renamed_from（舊 3mf 回溯）
                        proc["renamed_from"] = combo_renamed_from(lh, cb, model, nz)
                    jdump(os.path.join(PINGDIR,"process","%s.json"%pname(cb)), proc)
                    proc_list.append({"name":pname(cb),"sub_path":"process/%s.json"%pname(cb)})
                    if not (model in EXT_RESERVED_MODELS or _nz_res(model, nz)): gp += 1
                    # 棧板雙生（單料頭/同進/FP 限定；kind=ff 的四色 is_single=False 天然排除）
                    if is_single and not PING_ONLY:
                        tw = dict(proc); tw.update(PALLET_OVERRIDES)
                        # 0811 改名：「_棧板」→「_筏層」（Eric：參數欄位本來就叫筏層）。
                        # renamed_from＝舊名（字串；本支僅一條，0708 出生時就叫 _棧板）。
                        tw["name"] = "%smm_筏層 @%s (%s)" % (lh, model, nz)
                        tw["renamed_from"] = "%smm_棧板 @%s (%s)" % (lh, model, nz)
                        pallet_twins.append(tw)
                    # PLA+PVA 專屬製程雙生（Eric 2026-07-25 裁「出」）：從同口徑 PLA+SUP 派生
                    #（易拆幾何 Z0／XY 口徑×0.75、支撐料槽 2、速度/層高家規全部自然繼承）
                    if is_dual_machine and cb == "PLA+SUP":
                        pv = dict(proc); pv.update(pva_overrides(nz))
                        pv["name"] = combo_pname(lh, "PLA+PVA", model, nz)
                        pv["renamed_from"] = combo_renamed_from(lh, "PLA+PVA", model, nz)
                        pv["filename_format"] = filename_tpl("PLA+PVA")
                        pva_twins.append(pv)
                        # 易拆樹狀雙生（Eric 2026-09-19，牌 c-0919-ETR-01）：同口徑「易拆」只換支撐類型／樣式／速度。
                        # 新品無舊名 ⇒ 拿掉複製來的 renamed_from（留著會讓「易拆」的舊名在 rename map 撞成 1:2）。
                        et = dict(proc); et.update(EASY_TREE_OVERRIDES)
                        et["name"] = easy_tree_pname(lh, model, nz)
                        et.pop("renamed_from", None)
                        easy_tree_twins.append(et)

                    # PA-CF 專屬製程（Eric 2026-08-26 裁）：從同口徑標準製程派生一般版＋樹狀版。
                    # 只掛 PACF_MODELS（現＝FP300）；雙料機不派生（PA-CF 只提供單料頭）。
                    if model in PACF_MODELS and not is_dual_machine:
                        pf = dict(proc); pf.update(pacf_overrides())
                        pf["name"] = "%smm PA-CF @%s (%s)" % (lh, model, nz)
                        pacf_twins.append(pf)
                        pft = dict(pf); pft.update(PACF_TREE_OVERRIDES)
                        pft["name"] = "%smm PA-CF 樹狀 @%s (%s)" % (lh, model, nz)
                        pacf_twins.append(pft)

                    # 0.2mm 高速（Eric 2026-10-08）：只掛 FD600 Pro 單料頭 0.4／層高 0.2。
                    # 新品無舊名 ⇒ 拿掉複製來的 renamed_from（同易拆樹狀的做法，避免 rename map 撞號）。
                    if (model in ABSCF_FAST_MODELS and not is_dual_machine
                            and nz == ABSCF_FAST_NZ and lh == ABSCF_FAST_LH):
                        af = dict(proc); af.update(abscf_fast_overrides())
                        af["name"] = "%smm %s @%s (%s)" % (lh, ABSCF_FAST_TAG, model, nz)
                        af.pop("renamed_from", None)
                        abscf_fast_twins.append(af)

            # machine_model（每個 printer_model 一檔）；nozzle_diameter 併入範本收編口徑（如 FF800 0.4）
            mm_nzs = sorted(set(nzs) | set(EXTRA_MODEL_NOZZLES.get(model, [])), key=float)
            mm = {"type":"machine_model","name":model,
                  "model_id":"PING_"+model.replace(" ","_"),
                  "nozzle_diameter":";".join(mm_nzs),"machine_tech":"FFF","family":"",
                  "bed_model":bed_for(model),
                  "bed_texture":BED_OVERRIDE.get(model,{}).get("bed_texture",BED_TEXTURE),"hotend_model":"",
                  # FF：口徑合一後各口徑同指合併支 → 去重（921921c8 手工清 2 項的 regen-durable 版）
                  "default_materials": (_dedup_semilist(";".join(seed_materials_ff(nzs[0]) + seed_materials_ff(nzs[-1])))
                                        if kind=="ff" else DEFAULT_MATERIALS_FD)}
            jdump(os.path.join(PINGDIR,"machine","%s.json"%model), mm)
            mm_list.append({"name":model,"sub_path":"machine/%s.json"%model})

    # 4a-2. FF 同進/3in1 範本併入（衍生模式、無源 config）。須在 4b 之前跑，
    #       好讓 existing_machines 含 同進/3in1 機台 → 高流量線材 compatible 掛得到。
    if any(f[2] == "ff" for f in FAMS) and not PING_ONLY:
        gm, gp, ff_fil, ff_models, hf_twins = emit_ff_extra(mm_list, mac_list, proc_list, gm, gp)
    else:
        ff_fil, ff_models, hf_twins = [], [], []
    # 4a-3b. V3.6 Classic 前代機：和 Fast 同 bundle，但用獨立 machine/process/filament，
    # 避免 Klipper 指令與高加速度滲入 Marlin 舊板。
    emit_classic_extras = None
    if not PING_ONLY:
        gm, gp, classic_fil, emit_classic_extras = emit_classic(
            mm_list, mac_list, proc_list, nozzles_of, gm, gp, pacf_twins)
    else:
        classic_fil = []

    # 4a-4. 棧板雙生製程統一 emit（setting_id 接在全庫最後＝既有 id 零位移）
    for tw in pallet_twins:
        if _ext_proc(tw["name"]): tw["setting_id"] = _ext_take_p()
        elif _nz_proc(tw["name"]): tw["setting_id"] = _nz_take_p()
        else: tw["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % tw["name"]), tw)
        proc_list.append({"name": tw["name"], "sub_path": "process/%s.json" % tw["name"]})
    if pallet_twins:
        print("  棧板雙生製程：%d 支（PINGP%03d 起）" % (len(pallet_twins), gp - len(pallet_twins)))

    # 4a-5. PLA+PVA 專屬製程統一 emit（接在棧板之後＝既有＋照片磚＋棧板 id 全零位移）
    for pv in pva_twins:
        if _ext_proc(pv["name"]): pv["setting_id"] = _ext_take_p()
        elif _nz_proc(pv["name"]): pv["setting_id"] = _nz_take_p()
        else: pv["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % pv["name"]), pv)
        proc_list.append({"name": pv["name"], "sub_path": "process/%s.json" % pv["name"]})
    if pva_twins:
        print("  PLA+PVA 專屬製程：%d 支（PINGP%03d 起）" % (len(pva_twins), gp - len(pva_twins)))

    # 4a-5. 照片磚範本併入（需 FF800/FD300 家族＝通用版；客戶版 PING_ONLY 跳過）。
    #       須在 4b 之前跑，讓高流量 PLA 的 compatible 掛得到照片磚機。
    #       範本資料夾不存在（如無照片磚的 release 分支）＝自動跳過，同一支產生器兩線通用。
    #       ⚠ 位置＝id 佈局承重牆（2026-07-20 修正）：8c487d82 setting_id 治理把照片磚
    #       重掛「全庫最尾」（機 PINGM074-078＝接 Classic 073 之後、製程 PINGP152-156＝
    #       接棧板雙生 151 之後），故照片磚必須在 Classic 與棧板雙生「之後」emit，
    #       regen 才零位移；先前排在 Classic 前＝每次 regen 都撞號（r6 事故同源）。
    if not PING_ONLY and os.path.isdir(PHOTOTILE):
        gm, gp, pt_models = emit_phototile(mm_list, mac_list, proc_list, gm, gp)
    else:
        pt_models = []
    # 0728 v2 連動：FD300 同進照片磚＝單一出料 → 預設 PLA-220 → PLA-210（機 2＋model 1；
    # 範本值不動、emit 後就地改寫＝regen-durable。FF800 同進照片磚＝FF 系四料高流量、不在範圍）
    _pt210 = 0
    for _pm in ("FD300 同進照片磚 0.4 nozzle", "FD300 同進照片磚 0.6 nozzle", "FD300 同進照片磚"):
        _pp = os.path.join(PINGDIR, "machine", _pm + ".json")
        if not os.path.isfile(_pp):
            continue
        _pd = json.load(io.open(_pp, encoding="utf-8"))
        _hit = False
        _dfp = _pd.get("default_filament_profile")
        if isinstance(_dfp, list) and "PING PLA - 220" in _dfp:
            _pd["default_filament_profile"] = [BASE_PLA_NEW if x == "PING PLA - 220" else x for x in _dfp]
            _hit = True
        _dm = _pd.get("default_materials")
        if isinstance(_dm, str) and "PING PLA - 220" in _dm.split(";"):
            _pd["default_materials"] = ";".join(BASE_PLA_NEW if t == "PING PLA - 220" else t
                                                for t in _dm.split(";"))
            _hit = True
        if _hit:
            jdump(_pp, _pd); _pt210 += 1
    if _pt210:
        print("  照片磚 FD300 同進預設 210：改 %d 檔" % _pt210)

    # 4a-6. 高流量製程組統一 emit（Eric 2026-07-30 裁；id 接尾＝既有＋Classic＋棧板＋PVA＋照片磚
    #        全零位移——⚠ 本線照片磚在 PVA 之後 emit〔id 佈局承重牆〕，高流量必須排照片磚之後）
    for hf in hf_twins:
        if _ext_proc(hf["name"]): hf["setting_id"] = _ext_take_p()
        elif _nz_proc(hf["name"]): hf["setting_id"] = _nz_take_p()
        else: hf["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % hf["name"]), hf)
        proc_list.append({"name": hf["name"], "sub_path": "process/%s.json" % hf["name"]})
    if hf_twins:
        print("  高流量製程組：%d 支（PINGP%03d 起）" % (len(hf_twins), gp - len(hf_twins)))

    # 4a-7. PA-CF 專屬製程統一 emit（Eric 2026-08-26 裁；id 接在全庫最尾＝既有＋Classic＋棧板＋
    #        PVA＋照片磚＋高流量 全零位移。⚠ 新增製程一律排最後，別插隊——r6 撞號事故同源）
    for pf in pacf_twins:
        if _ext_proc(pf["name"]): pf["setting_id"] = _ext_take_p()
        elif _nz_proc(pf["name"]): pf["setting_id"] = _nz_take_p()
        else: pf["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % pf["name"]), pf)
        proc_list.append({"name": pf["name"], "sub_path": "process/%s.json" % pf["name"]})
    if pacf_twins:
        print("  PA-CF 專屬製程：%d 支（PINGP%03d 起）" % (len(pacf_twins), gp - len(pacf_twins)))

    # 4a-8. Classic base 補口徑（Eric 2026-09-01 裁 Q1＝丙／Q2＝四台一起）。
    #        接在 4a-7 之後＝**真正的全庫最尾** ⇒ 既有 preset 的 setting_id 全零位移。
    #        （第一版寫在 emit_classic 裡就地跑，實測位移了 124 支既有 preset，故改成延後 emit。）
    if emit_classic_extras is not None:
        gm, gp = emit_classic_extras(gm, gp)

    # 4a-9. 易拆樹狀製程統一 emit（Eric 2026-09-19，牌 c-0919-ETR-01）。
    #        接在 4a-8 之後＝**真正的全庫最尾** ⇒ 既有 preset（含 4a-8 Classic 補口徑）setting_id 全零位移。
    #        0.25 後加口徑（FD450／600／800 Pro）照既有規則走 1100 段計數器（_nz_proc）＝該段尾端續號。
    #        ⚠ 下一個新增製程族要排在**本段之後**，別插隊（r6 撞號事故同源）。
    for et in easy_tree_twins:
        if _ext_proc(et["name"]): et["setting_id"] = _ext_take_p()
        elif _nz_proc(et["name"]): et["setting_id"] = _nz_take_p()
        else: et["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % et["name"]), et)
        proc_list.append({"name": et["name"], "sub_path": "process/%s.json" % et["name"]})
    if easy_tree_twins:
        print("  易拆樹狀製程：%d 支（主段 PINGP%03d 止）" % (len(easy_tree_twins), gp - 1))

    # 4a-10. 0.2mm 高速製程統一 emit（Eric 2026-10-08，牌 c-1008-ACF-01）。
    #        接在 4a-9 之後＝**真正的全庫最尾** ⇒ 既有 preset setting_id 全零位移。⚠ 下一個新增製程族排在本段之後。
    for af in abscf_fast_twins:
        if _ext_proc(af["name"]): af["setting_id"] = _ext_take_p()
        elif _nz_proc(af["name"]): af["setting_id"] = _nz_take_p()
        else: af["setting_id"] = "PINGP%03d" % gp; gp += 1
        jdump(os.path.join(PINGDIR, "process", "%s.json" % af["name"]), af)
        proc_list.append({"name": af["name"], "sub_path": "process/%s.json" % af["name"]})
    if abscf_fast_twins:
        print("  0.2mm 高速製程：%d 支（主段 PINGP%03d 止）" % (len(abscf_fast_twins), gp - 1))

    # 4b. FF 高流量線材子 preset（口徑別；FF600/FF800 同口徑同值——已驗證；
    #     0.4 僅 FF600 有（2026-06-11 客戶要求新增）→ compatible 只列「實際存在」的機台）
    fil_new = []
    existing_machines = {m["name"] for m in mac_list}
    ff_cfg = {}   # nz -> 四色 config（FF800 優先；FF800 缺的口徑用 FF600 補，如 0.4）
    for fam in ("FF800 Pro", "FF600 Pro") if any(f[2] == "ff" for f in FAMS) else ():
        for (nz, mk), c in parse_dir(src_base, fam).items():
            if mk == "四色":
                ff_cfg.setdefault(nz, c)
    # 2026-07-18 Eric 裁「口徑合一」：四料高流量噴頭 PLA/SupPLA 各合併為一支——
    # 同一支噴頭只換嘴，原 0.4/0.6/1.0 三支的差異鍵統一為：PA 一律開、最大體積流量 30、
    # 床溫 60/60（噴溫 210 承 0717 裁定）。以 0.6 交付檔為基底。
    src_nz = "0.6" if "0.6" in ff_cfg else (sorted(ff_cfg, key=float)[0] if ff_cfg else None)
    if src_nz:
        F = split(ff_cfg[src_nz])["F"]
        for slot, mat, fid, alias, color, sup in (
                (0, "PLA",    "PINGFILHFPLA", FF_FIL_ALIAS["PLA"],    "#EA4E16", False),
                (3, "SupPLA", "PINGFILHFSUP", FF_FIL_ALIAS["SupPLA"], "#D3D3D3", True)):
            fp = fil_at(F, slot, 4)
            fp.update({"type":"filament","name":alias,"alias":alias,"from":"system",
                "instantiation":"true","setting_id":fid,"filament_id":fid,
                "inherits":"fdm_filament_pla",
                "filament_colors":[color],"default_filament_colors":[color],
                "enable_pressure_advance":["1"],
                # 2026-07-18 Eric 裁：四料兩支 PA 統一 0.4（原 SupPLA 承 0.6 基底帶到 0.12＝漏改）
                "pressure_advance":["0.4"],
                # 🆕 2026-08-16 Eric 裁：**流量 30→50 只給同進那支（PLA）**——同進＝四進一出、
                #    盤上只有一個料槽（機器預設就一格），流量需求最大。SupPLA 支 Eric 未點名，
                #    維持 30 不順手改。照片磚豁免走另開的專用支（見下方 PT_FIL_PLA）。
                "filament_max_volumetric_speed":["30" if sup else "50"],
                "hot_plate_temp":["60"],"hot_plate_temp_initial_layer":["60"]})
            # PING(2026-07-26 Eric 裁 A・下拉去重)：四料高流量噴頭支＝FF 四進一出硬體專屬
            #（四色/同進/FF 照片磚 64 槽預設引用實證），與 3in1 支（帶 T012/T3 同步進料
            # gcode、流量 20/12、PA 0.2）互為**不同機構**、不是重複——各綁各的機，下拉
            # 不再互相出現；值與行為分毫不動。清單動態取自本輪 mac_list（含 ff_extra/照片磚
            # ——4b 在其後跑，本檔 4a-3 註解即為此設計）＝regen-durable。
            # 🆕 2026-08-16 改名成「四料同進噴頭」後，可見範圍收斂成**只有同進機**：
            #    四料本體機已改吃「高流量噴頭」支、照片磚機改吃專用支 ⇒ 它們不該再看到這支。
            fp["compatible_printers"] = sorted(
                x["name"] for x in mac_list
                if x["name"].startswith(("FF600", "FF800")) and "同進" in x["name"]
                and "照片磚" not in x["name"] and "3in1" not in x["name"])
            if sup: fp["filament_is_support"] = ["1"]
            # 清洗量維持實機 120（FF 換色需大量清洗；Eric 2026-07-17 裁「不蓋」＝30/60 規則不套 FF）
            # 噴溫一律 210/210（Eric 2026-07-17 裁：0.6 實機 190 塞頭）
            fp["nozzle_temperature_initial_layer"] = ["210"]
            fp["nozzle_temperature"] = ["210"]
            # 🆕 2026-08-16 改名接舊名：⚠ renamed_from ＝**字串**不是 list（T004 鐵則）
            fp["renamed_from"] = FF_FIL_OLD[mat]
            jdump(os.path.join(PINGDIR,"filament","%s.json"%alias), fp)
            fil_new.append({"name":alias,"sub_path":"filament/%s.json"%alias})
            # 🆕 照片磚專用支（Eric 2026-08-16 裁「甲」）：與同進支同源，但**維持改名前的舊值**
            #    ＝流量 30（不吃 50）＋清料 120；compatible 綁死同進照片磚機、不污染其他機的下拉。
            #    ⚠ 刻意不掛 renamed_from——舊名只能有一個接手者，歸給同進支（多數情形）。
            #    ⇒ 已存的照片磚 3mf 專案重開後會指到同進支（流量 50），要手動改回這支。
            #    等 Eric 自己實印照片磚後，再決定要不要讓它跟進 50 或反過來把 50 收掉。
            if not sup:
                pt = json.loads(json.dumps(fp))          # 深拷貝（本檔未 import copy）
                pt.update({"name":PT_FIL_PLA, "alias":PT_FIL_PLA,
                           "setting_id":"PINGFILPTPLA", "filament_id":"PINGFILPTPLA",
                           "filament_max_volumetric_speed":["30"],
                           # 🔴 噴溫 210（Eric 2026-09-29 22:3x 裁「改回 210」＝回退 0910「四料照片磚降到 190」那一裁）。
                           #    證據＝.142（FF600 Lite 四料同進 0.6）溫度對照：190 換成白／黑供料不足、兩個獨立趟次都重現，200 大幅縮短，
                           #    210 消失；210 的四料色彩校正片沒有缺料（SOP 無頭切片 §22-8，牌 c-0929-PTC-03）。回到跟母體同值
                           #   （也回到 2026-07-17「0.6 實機 190 塞頭」那條）。仍明寫不刪鍵：這支的噴溫被單獨裁過，母體將來改要另外裁
                           #   （verify 同步釘 210）。溫度統一鐵律：兩個噴溫鍵一起設。
                           "nozzle_temperature":["210"], "nozzle_temperature_initial_layer":["210"]})
                pt.pop("renamed_from", None)
                pt["compatible_printers"] = sorted(
                    x["name"] for x in mac_list
                    if x["name"].startswith(("FF600", "FF800")) and "照片磚" in x["name"])
                jdump(os.path.join(PINGDIR,"filament","%s.json"%PT_FIL_PLA), pt)
                fil_new.append({"name":PT_FIL_PLA,"sub_path":"filament/%s.json"%PT_FIL_PLA})
    # 舊名檔清除（改名後不留雙份；PING.json 舊條目在 4d 過濾）
    for old in FF_FIL_RENAME:
        oldp = os.path.join(PINGDIR, "filament", "%s.json" % old)
        if os.path.exists(oldp):
            os.remove(oldp); print("  filament 移除(改名):", old)

    # 4b-1b. ★ 高流量噴頭專用線材 2 支（A 案）：承 PLA-220/SupPLA 本體＋樣本 4 鍵覆蓋、
    # 不限機型；SET_RETRACTION 四欄行由基底帶入/4b-2 sweep 保證。
    for base_name, new_name, fid in ((("PING PLA - 220"),  HFN_PLA,  "PINGFILHFNPLA"),
                                     (("PING SupPLA"),     HFN_SUP,  "PINGFILHFNSUP"),
                                     (("PING PETG"),       HFN_PETG, "PINGFILHFNPETG")):
        bp = os.path.join(PINGDIR, "filament", "%s.json" % base_name)
        fd_ = json.load(io.open(bp, encoding="utf-8"))
        fd_.update(HFN_OVERRIDES)
        fd_.update(HFN_EXTRA[new_name])
        fd_.update({"name": new_name, "alias": new_name,   # 獨立 alias 勿與其他 PLA 併組
                    "setting_id": fid, "filament_id": fid})
        fd_.pop("compatible_printers", None)   # 不限機型
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % new_name), fd_)
        fil_new.append({"name": new_name, "sub_path": "filament/%s.json" % new_name})

    # 4b-1c. ★ TPE 軟料一對（Eric 2026-07-18 裁，承 V2.1 Cura 線 TPE 工程定稿）：
    # PING TPE - 210（本體）＋ PING SupTPE（TPU 系支撐料）。
    # ★ 0729 Eric 裁（回報中心單）：TPE 最大體積流量 3.2→**7**＝對齊 SpiderMaker TPE 官方建議
    #   6~8 的起始值 7（原 3.2 係 0718「軟慢」夾速設計＝0.2×0.4 下限速 40，比官方保守）。
    #   ⚠ 行為變更：0.4 口徑實質不再夾速（7÷0.08≈87>製程速度）；0.6 口徑 7÷0.18≈39 仍約 40。
    #   SupTPE 維持 5.5（本裁只點名 TPE）。
    # ★ 0728 Eric 二輪裁「PING TPE - 210(温度也改低一点)」：本體改名帶溫度尾碼＋噴溫 220→210
    #  （初層/其他層一致）；SupTPE 名不動、噴溫跟隨 210（兩側一致原則不變）。
    #   renamed_from ⚠ 字串（T004 鐵則）只掛本體；id 不動（同一支材料身份）；殘檔清除 regen-durable。
    # 床溫承 PLA 慣例 60（Eric 實跑基底）、回抽 3/z-hop 0.6、TPE 風扇 50/SupTPE 100、
    # PA 關（軟料待實測）。不限機型；SET_RETRACTION 行由 4b-2 sweep 保證。
    _old_tpe = os.path.join(PINGDIR, "filament", "PING TPE.json")
    if os.path.isfile(_old_tpe):
        os.remove(_old_tpe)
        print("  TPE 改名：移除殘檔 PING TPE.json（新名 PING TPE - 210、renamed_from 相容）")
    for new_name, fid, is_sup in (("PING TPE - 210", "PINGFILTPE", False),
                                  ("PING SupTPE", "PINGFILSUPTPE", True)):
        fd_ = {"type": "filament", "name": new_name, "alias": new_name, "from": "system",
               "instantiation": "true", "inherits": "fdm_filament_tpu",
               "setting_id": fid, "filament_id": fid,
               "filament_type": ["TPU"],
               "nozzle_temperature_initial_layer": ["210"], "nozzle_temperature": ["210"],
               "hot_plate_temp_initial_layer": ["60"], "hot_plate_temp": ["60"],
               "fan_min_speed": ["100" if is_sup else "50"],
               "fan_max_speed": ["100" if is_sup else "50"],
               "filament_max_volumetric_speed": ["5.5" if is_sup else "7"],
               "filament_retraction_length": ["3"], "filament_z_hop": ["0.6"],
               # 0728 Eric「TPE軟料的回抽 3/30/30」：速度/裝填補 30（原未設＝吃機器層 20/20）
               "filament_retraction_speed": ["30"], "filament_deretraction_speed": ["30"],
               "enable_pressure_advance": ["0"], "pressure_advance": ["0"],
               "slow_down_for_layer_cooling": ["1"], "slow_down_layer_time": ["10"],
               "filament_minimal_purge_on_wipe_tower": ["30"]}
        if is_sup:
            fd_.update({"filament_is_support": ["1"],
                        "filament_colors": ["#D3D3D3"], "default_filament_colors": ["#D3D3D3"]})
        else:
            fd_["renamed_from"] = "PING TPE"   # ⚠ 字串（T004 鐵則）；舊名相容（0728 改名）
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % new_name), fd_)
        fil_new.append({"name": new_name, "sub_path": "filament/%s.json" % new_name})

    # 4b-1d. ★ PVA 水溶支撐線材（Eric 2026-07-24 裁「參考 2.1 追加、一般流量即可」・主線 37cad9cb/48023ae8 移植；
    # 值已對帳 V2.1 定稿案＝劉勝賢提供 D800 Pro(0.6)_PVA+PLA.3mf
    # 〔中華航空案、DPro_0.6_T210_PVA+PLA (0609)、Eric：「比較保守、練出來也不錯」〕）：
    #   噴溫 210/210（V2.1 案全鍵一致 210——PLA 側同降 210 的保守組；蓋掉首版推定 220）、
    #   床 60、風扇 100/100（V2.1 案未定此鍵＝沿 Orca 支撐慣例）、
    #   回抽長度 3＋z-hop 0.6（V2.1 案 PVA 側定稿；速度 30=機器層家規）、
    #   purge 85（V2.1 檔內 75＋劉勝賢現行「+10」＝85；4b-3 同步豁免）、
    #   水溶＋支撐旗標、支撐色 #D3D3D3、最大體積流量 12（V2.1 速度 60×0.35×0.6≈12.6 貼合）、
    #   密度 1.23。額外回填 0.2＋四項統一（4b-2/2b sweep；長度 3=PVA 特例、sweep 豁免同 TPE）。
    # 「一般流量即可」＝不出高流量變體；不限機型（噴頭屬性原則，同 TPE 先例）。
    fd_ = {"type": "filament", "name": "PING PVA", "alias": "PING PVA", "from": "system",
           "instantiation": "true", "inherits": "fdm_filament_pla",
           "setting_id": "PINGFILPVA", "filament_id": "PINGFILPVA",
           "filament_vendor": ["PING"], "filament_type": ["PVA"],
           "filament_soluble": ["1"], "filament_is_support": ["1"],
           "filament_density": ["1.23"],
           "nozzle_temperature_initial_layer": ["210"], "nozzle_temperature": ["210"],
           "hot_plate_temp_initial_layer": ["60"], "hot_plate_temp": ["60"],
           "cool_plate_temp_initial_layer": ["60"], "cool_plate_temp": ["60"],
           "fan_min_speed": ["100"], "fan_max_speed": ["100"],
           "filament_retraction_length": ["3"], "filament_z_hop": ["0.6"],
           "filament_max_volumetric_speed": ["12"],
           "filament_colors": ["#D3D3D3"], "default_filament_colors": ["#D3D3D3"],
           "filament_minimal_purge_on_wipe_tower": ["85"],
           "slow_down_for_layer_cooling": ["1"], "slow_down_layer_time": ["10"]}
    jdump(os.path.join(PINGDIR, "filament", "PING PVA.json"), fd_)
    fil_new.append({"name": "PING PVA", "sub_path": "filament/PING PVA.json"})

    # 4b-1e. ★ FD300 照片磚專用線材（Eric 2026-08-19 裁「給它專用支」）——見 PT_FIL_PLA_FD 註解。
    # 從 `PING PLA - 210` 整支派生（噴溫 210／PA 0.08／流量／清料／色全部原封不動），
    # 只改三件：①身分（name/alias/id）②`compatible_printers` 綁死 FD300 同進照片磚兩台
    # ③`filament_retraction_length` = nil（讓機器層零回抽 0,0 生效；4b-2b 的 is_pt 也會再釘一次）。
    # ⚠ 獨立 alias＝防與 PLA-210 併組後下拉選不到（3in1 教訓）。
    # ⓘ 排在 4b-2b 之前，讓後面的回抽／PA／清料 sweep 一體適用＝regen-durable。
    # 🆕 2026-09-07：本段從「只做 FD300 一支」一般化成**一張表**（pt_fil_specs），因為補機型之後 FD 照片磚
    #   有兩種流量形式（見 PT_FIL_PLA_FDHF 註解）。要再加家族＝在表裡加一列，不必改邏輯。
    for _pt_name, _pt_base, _pt_id, _pt_models in pt_fil_specs():
        _pt_fd_src = os.path.join(PINGDIR, "filament", _pt_base + ".json")
        if not os.path.isfile(_pt_fd_src):
            continue
        # 吃它的機器＝那些 model 底下**實際存在**的各口徑機（不寫死口徑，加口徑不必改這裡）
        _pt_fd_machines = sorted(
            os.path.basename(x)[:-5] for _m in _pt_models
            for x in glob.glob(os.path.join(PINGDIR, "machine", _m + " *nozzle.json")))
        _ptfd = json.load(io.open(_pt_fd_src, encoding="utf-8"))
        _ptfd.update({"name": _pt_name, "alias": _pt_name,
                      "setting_id": _pt_id, "filament_id": _pt_id,
                      "filament_retraction_length": ["nil"]})
        _ptfd.pop("renamed_from", None)          # 舊名只能有一個接手者（PLA-210 已接 "PING PLA"）
        _ptfd["compatible_printers"] = _pt_fd_machines
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % _pt_name), _ptfd)
        fil_new.append({"name": _pt_name, "sub_path": "filament/%s.json" % _pt_name})
        # 照片磚機／model 的 64 槽預設改指專用支。`default_materials` 不在這裡動＝交給 4d-2 的 post-pass 重算。
        _ptfd_n = 0
        for _pm in _pt_fd_machines + _pt_models:
            _pp = os.path.join(PINGDIR, "machine", _pm + ".json")
            if not os.path.isfile(_pp):
                continue
            _pd = json.load(io.open(_pp, encoding="utf-8"))
            _dfp = _pd.get("default_filament_profile")
            if isinstance(_dfp, list) and any(x == _pt_base for x in _dfp):
                _pd["default_filament_profile"] = [_pt_name if x == _pt_base else x
                                                   for x in _dfp]
                jdump(_pp, _pd); _ptfd_n += 1
        print("  照片磚專用線材 %s：綁 %d 台；機／model 槽位改指 %d 檔"
              % (_pt_name, len(_pt_fd_machines), _ptfd_n))

    # 4b-2. ★ 回抽切片控制埋全線材（2026-07-12 Eric B 定案，取代同日 HOLD 的 M207/M208 案；
    # SSOT＝ping-slicer gcode.md「線材起始 G-code」節）：改埋 Klipper 原生 SET_RETRACTION
    # ——免機端 wrapper、全機隊（含舊 C8/單料/四料）原生支援、老檔無指令＝機器 config 預設、
    # 速度單位 mm/s 與 Orca 一致。切片端需開 firmware retraction（G10/G11）照舊。
    # ⚠ 交辦單原文 [retract_length] 非 Orca key（會炸 Variable does not exist）→ 實埋
    #   [retraction_length]；三個佔位符皆經 PrintApply filament_overrides 併入 placeholder
    #  （GH #3649 機制，PrintApply.cpp:1250）＝含線材層覆蓋鏈的有效值（07-12 ①查證）。
    # ⚠ 3in1 線材 T4/T3 進料觸發行必須保留（只追加）。既有 M207/M208 行（HOLD 遺留）就地退場。
    # 冪等：已含 SET_RETRACTION 跳過。放 4b 之後＝重生檔（高流量/3in1）每次 regen 自動補。
    sr_added = 0
    SR_LINE = ("SET_RETRACTION RETRACT_LENGTH=[retraction_length] "
               "RETRACT_SPEED=[retraction_speed] UNRETRACT_EXTRA_LENGTH=[retract_restart_extra] "
               "UNRETRACT_SPEED=[deretraction_speed]")   # 第四欄 2026-07-12 Eric 實測抓缺（裝填速度覆蓋要能下機）
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if "Classic" in fd.get("name", ""):
            continue   # Classic 是 Marlin：不可加入 Klipper SET_RETRACTION
        sg = fd.get("filament_start_gcode")
        cur = (sg[0] if isinstance(sg, list) and sg else sg) or ""
        if "UNRETRACT_SPEED" in cur:
            continue   # 已是四欄新行
        # M207/M208 退場（B 定案）＋舊三欄 SET_RETRACTION 行就地升級成四欄
        cur = "\n".join(l for l in cur.splitlines()
                        if not l.startswith("M207 ") and not l.startswith("M208 ")
                        and not l.startswith("SET_RETRACTION "))
        add = SR_LINE
        if "; Filament gcode" not in cur:
            add = "; Filament gcode\n" + add
        fd["filament_start_gcode"] = [(cur.rstrip() + "\n" + add + "\n") if cur.strip() else (add + "\n")]
        jdump(fp_path, fd)
        sr_added += 1
    if sr_added:
        print("  線材 SET_RETRACTION 回抽控制：+%d 支（M207/M208 退場）" % sr_added)

    # 4b-2b. ★ 線材回抽統一（Eric 2026-07-23 三裁 → **2026-08-19 一般流量整組改寫**）：
    #
    # ①🆕【0819 Eric 令】**一般流量線材**（＝非高流量家族）＝
    #    回抽長度 **2**／回抽速度 **30**／裝填速度 **30**／**額外回填長度取消勾選**。
    #    「取消勾選」＝`filament_retract_restart_extra` 設 `nil`（退回機器層）——
    #    **全機型機器層 `retract_restart_extra` 本來就是 0**，所以 UI 顯示灰色 0＝Eric 要的樣子。
    #    ⇒ 蓋掉 0723 的「基礎支長度收斂繼承 nil」與「一般流量額外回填 0.2」兩條。
    # ②**軟料（TPE／SupTPE）與 PVA 的「長度」維持 3、不動**（Eric 0819 原話「軟料回抽為 3，不改動」；
    #    PVA 同列＝0724 V2.1 案定稿，Eric 0819 二裁「維持 3」）。速度／裝填仍一併 30/30。
    # ③**高流量家族維持 0730 既值 3/30/30/0.6 不動**（Eric 0819 裁「排除」）。家族＝
    #    高流量噴頭／四料同進／(3in1)／FF 照片磚專用支。
    # ④🆕【0819 附帶修復】**照片磚系＝回抽長度 `nil`**，讓機器層零回抽（`0,0`）生效。
    #    🔴 真因：照片磚機的零回抽只寫在機器層，**線材覆蓋會贏**；0730「高流量家族 3/30/30」
    #    那批把 FF 照片磚支掃成 3，**靜默蓋掉零回抽、不會報錯**——本批修回。
    #    FD300 照片磚走 4b-1e 新開的 PT_FIL_PLA_FD（一般流量硬體、不入 is_hf）。
    # ⑤四項統一（以 PLA - 220 為基準）：空駛距離臨界值 3／回抽時擦拭 1／擦拭距離 5／擦拭前回抽量 100%。
    # ⑥🔴 **Classic 前代線材整組豁免**（Eric 2026-08-07 硬體事實：**赤兔不能吃韌體回抽** ⇒
    #    Classic 線材不得在材料層覆蓋任何回抽鍵，一律由 Classic machine preset 控制；
    #    verify_profiles.py「Classic 材料層回抽覆蓋 0807」有護欄擋）。
    #    ⚠ 0819 Eric 曾答「Classic 一起改」，但**那次提問沒把 0807 這條事實端出來**；
    #      實作時被護欄擋下 ⇒ 依「規則優先」全部退回原狀，等 Eric 在知情下重裁。
    # 冪等 sweep；DL1016 不在名單自然豁免。
    rt_touched = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if fd.get("instantiation") != "true":
            continue
        bn = os.path.basename(fp_path)[:-5]
        if "Classic" in bn:
            continue                                      # ⑥ Classic 整組豁免（赤兔無韌體回抽，見上方註）
        before = json.dumps(fd, sort_keys=True)
        fd["filament_retraction_minimum_travel"] = ["3"]
        fd["filament_wipe"] = ["1"]
        fd["filament_wipe_distance"] = ["5"]
        fd["filament_retract_before_wipe"] = ["100%"]
        # 🔴 2026-08-16 改名連坐（本輪第三處）：家族判定一律靠**名字字串**，
        #    「四料高流量噴頭」→「四料同進噴頭」後掉出 is_hf ⇒ 回抽 3/30/30 與額外回填 0.6
        #    會靜默退回一般流量值。照片磚專用支同屬同進硬體，一併納入。
        is_hf = ("高流量" in bn) or ("四料同進" in bn) or ("(照片磚)" in bn) or ("(3in1)" in bn)
        # 🆕 FF600／FF800 **專屬**線材（2026-09-15 Eric 裁回抽 0.8 的範圍）＝is_hf 的真子集。
        #   四支：PING PLA／SupPLA - 四料同進噴頭、PING PLA(3in1)／SupPLA(3in1) - 高流量噴頭。
        #   ⛔ 不含「- 高流量噴頭」那三支（白名單空、走 condition，FD 全系列也吃得到）；
        #   ⛔ 不含照片磚支（由 is_pt 先擋成 nil）。守衛＝verify_profiles.py 同名判定。
        _is_ff_only = ("四料同進" in bn) or ("(3in1)" in bn)
        is_pt = bn in (PT_FIL_PLA, PT_FIL_PLA_FD, PT_FIL_PLA_FDHF)   # ④ 照片磚系＝回抽長度 nil（吃機器層）
        # 🔴 四料照片磚額外回填 0.6 → 0.2（Eric 2026-09-10：「額外裝填 0.6 會擠出蠻多的，幫我把照片磚的額外擠出量改成 0.2 看看」）。
        #    ⛔ **只降四料照片磚那一支**：`PING PLA(照片磚 FD300)` 現行是 `nil`（＝吃機器層 0），設 0.2 是**增加**、方向相反；
        #       `PING PLA(照片磚 FD高流量)` 也是 0.6，Eric 只在四料上看到問題、一路把四料與雙料分開裁，不擅自擴大。
        if bn == PT_FIL_PLA:
            fd["filament_retract_restart_extra"] = ["0.2"]
        else:
            # 🔴 2026-09-15 Eric 裁「整個高流量族 8 支」：0.6 → 0。起因＝廠內 FF600 E Pro Max（.29）
            #    測四料時「會磨到材料」；每次回抽完多推 0.6 mm 回去，在混色頭上就是反覆把料往前頂。
            #    已排除韌體端：.29 唯讀實查 firmware_retraction.unretract_extra_length = 0.0 ⇒ 0.6 只來自這裡。
            #    ⚠ 寫明「0」而不是「nil」：機器層本來就是 0，兩者等效，但明寫讓「這一族是刻意歸零」看得見
            #      （nil 讀起來像忘了設）。⛔ 上面那支 PT_FIL_PLA 的 0.2 是 Eric 0910 自己裁的，不動。
            fd["filament_retract_restart_extra"] = ["0"] if is_hf else ["nil"]
        # 🆕 Eric 2026-08-26 裁（Q4 甲）：PA-CF 回抽 3/40/40——**只改線材層**。
        #    機器層一字不動、韌體回抽維持開：韌體回抽開啟時線材 start gcode 的
        #    SET_RETRACTION 會把 3/40/40 推給 Klipper ⇒ 行為等同他關掉韌體回抽自己下 E 值，
        #    但不會連累 FP300 上的 PLA/ABS（機器層鍵沒有材料維度）。
        _is_pacf = ("PA-CF" in bn)
        fd["filament_retraction_speed"] = ["40"] if _is_pacf else ["30"]
        fd["filament_deretraction_speed"] = ["40"] if _is_pacf else ["30"]
        if is_pt:
            fd["filament_retraction_length"] = ["nil"]    # ④ 吃機器層 0,0＝零回抽
        elif "TPE" in bn or "PVA" in bn:
            pass                                          # ② 長度不動（TPE/SupTPE 0718、PVA 0724 定稿；Classic 兩支本來就沒覆蓋鍵）
        elif _is_pacf:
            fd["filament_retraction_length"] = ["3"]      # 🆕 0826 Eric：PA-CF 高溫滲料，1.3/2 擋不住牽絲
        elif _is_ff_only:
            # 🆕 2026-09-15 Eric 裁「只改 FF 四支，照片磚除外」：FF600／FF800 **專屬**線材 3 → 0.8。
            #    起因＝廠內 FF600 E Pro Max（.29）測四料回報「會磨到材料」；同一輪已先把額外回填
            #    0.6→0（上面 restart_extra 那條），這條是回抽長度本身。
            #    ⛔ **舊裁「高流量家族（含 3in1）回抽長度 3」（0730 立·0819 再確認）在這四支上已被取代**；
            #       其餘 is_hf 支維持 3。
            #    🔴 範圍為什麼是四支不是八支：`PING PLA／SupPLA／PETG - 高流量噴頭` 的
            #       compatible_printers 是**空的**、走 condition `printer_notes!~PHOTOTILE and !~CLASSIC`
            #       ⇒ 除照片磚機與 Classic 外**全機型可用，FD 全系列也吃得到**；改它們會波及 FD，
            #       而 Eric 要的是 FF 那台的手感 ⇒ 只動 FF600／FF800 專屬的四支。
            fd["filament_retraction_length"] = ["0.8"]
        elif is_hf:
            # ③ 0730 高流量家族既值。⚠ 2026-09-15 起這條只剩 FD 共用的三支在吃（FF 四支已被上面接手）。
            fd["filament_retraction_length"] = ["3"]
        else:
            fd["filament_retraction_length"] = ["2"]      # ① 0819 一般流量：繼承 1.3 → 明寫 2
        if json.dumps(fd, sort_keys=True) != before:
            jdump(fp_path, fd); rt_touched += 1
    if rt_touched:
        print("  線材回抽統一（0819 一般流量 2/30/30＋額外回填取消勾選；照片磚零回抽）：%d 支" % rt_touched)

    # 4b-2f. ★ 一般流量 PA 0.08（Eric 2026-07-28 三輪裁「材料如果不是高流量跟火山口或四料，
    # 它的壓力提前是 0.08」）：非高流量／非火山口(PA-CF)／非四料(含 3in1) 的一般流量硬料
    # → enable_pressure_advance 1＋pressure_advance 0.08（蓋 0725 ABS 整併帶進的 0.12＝
    # 未經 PA 塔實測值）。豁免照既有裁定：TPE/SupTPE（軟料 PA 關＝0725 裁待實測、本條不翻案）、
    # Classic（Marlin 無 PA、全關斷言）、DL1016（注入源不在產線）。冪等 sweep＝regen-durable。
    pa_set = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if fd.get("instantiation") != "true":
            continue
        bn = os.path.basename(fp_path)[:-5]
        # 🔴 2026-08-16 改名連坐修正：舊名「四料高流量噴頭」靠字串「高流量」拿到豁免，
        #    改名成「四料同進噴頭」後不含那三個字 ⇒ PA 會被壓成 0.08（實測踩到，非推測）。
        #    Eric 0728 原裁本來就寫「不是高流量跟火山口**或四料**」＝四料本來就在豁免內，
        #    這裡把「四料同進」與照片磚專用支明文列出，不再靠名字碰巧含「高流量」。
        if any(t in bn for t in ("Classic", "高流量", "四料同進", "(照片磚)", "(3in1)", "TPE", "PA-CF")):
            continue
        if fd.get("enable_pressure_advance") != ["1"] or fd.get("pressure_advance") != ["0.08"]:
            fd["enable_pressure_advance"] = ["1"]
            fd["pressure_advance"] = ["0.08"]
            jdump(fp_path, fd); pa_set += 1
    if pa_set:
        print("  一般流量 PA 0.08（高流量/火山口/四料/3in1/TPE/Classic 豁免）：%d 支" % pa_set)

    # 4b-2f2. ★ PA-CF 壓力提前 0.4（Eric 2026-08-26 裁「PA 0.4 入版」）：
    # 4b-2f 把 PA-CF 列為火山口豁免＝**實際上等於沒開 PA**（PING PA-CF 從來沒寫過這兩個鍵，
    # 引擎吃 Orca 預設＝關）。Eric 實印 FP300 0.4 用 0.4 較佳 ⇒ 明寫入版＝regen-durable。
    # ⚠ 全庫噴頭流量形式律是「高流量 0.2／四料高流量 0.4」，FP300＝火山口高流量 → 照律應落 0.2；
    #   本值採實印結果、列為**材料特例**。PA 值本該用 PA 塔校準，已列 T 版待驗項。
    # Classic 豁免（Marlin 無 PA，verify 有全關斷言）。冪等 sweep。
    pa_cf = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        bn = os.path.basename(fp_path)[:-5]
        if "PA-CF" not in bn or "Classic" in bn:
            continue
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if fd.get("instantiation") != "true":
            continue
        if fd.get("enable_pressure_advance") != ["1"] or fd.get("pressure_advance") != ["0.4"]:
            fd["enable_pressure_advance"] = ["1"]
            fd["pressure_advance"] = ["0.4"]
            jdump(fp_path, fd); pa_cf += 1
    if pa_cf:
        print("  PA-CF 壓力提前 0.4（Eric 0826 實印裁定／材料特例）：%d 支" % pa_cf)

    # 4b-2c. ★ 懸空冷卻觸發閾值 25%（Eric 2026-07-24 爬坡品質批・線材側配套・主線 37cad9cb 移植）：
    # 全 PING 線材統一 25%（PLA - 220／ABS - 250 既值 25% 冪等不動；PLA 210 系／SupPLA／
    # PETG／TPE 等原繼承 50%／95% → 收 25%）。overhang_fan_speed 不動（爬坡測試對照未改此鍵，
    # 各支既值/繼承值保留）。冪等 sweep。
    # ⚠ 出貨線特調：**Classic 前代線材豁免**（Marlin 隔離原則，同 0723 回抽統一先例）——
    #   主線註解稱「Classic 不在 PING*.json 名單自然豁免」僅對主線成立；出貨線的 Classic 線材
    #   就是叫 `PING PLA - Classic 210.json` 等，會被本 sweep 掃到，必須明文排除。
    of_set = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        if "Classic" in os.path.basename(fp_path):
            continue   # Classic 前代豁免（出貨線特調）
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        if fd.get("instantiation") != "true":
            continue
        if fd.get("overhang_fan_threshold") != ["25%"]:
            fd["overhang_fan_threshold"] = ["25%"]
            jdump(fp_path, fd)
            of_set += 1
    if of_set:
        print("  線材懸空冷卻觸發閾值 25%%：改 %d 支" % of_set)

    # 4b-2e. ★ 線材顏色 key 正規化（Eric 2026-07-25 實測「切換線材不會給預設的顏色」→ 追碼定讞）
    # 🔴 真因：PING 線材長期寫的是 `filament_colors`／`default_filament_colors`（**美式・複數**），
    #    但 Orca **preset 層**認的是 `filament_colour`／`default_filament_colour`
    #   （**英式・單數**，PrintConfig.cpp:2279/2287）⇒ 引擎完全讀不到、我們設的顏色從未生效。
    # ⚠ 複數版 key 在引擎裡只存在於 AppConfig.cpp（app 設定檔 `presets.filament_colors` ＝ conf 層，
    #    不是 profile 層）——名字像、層級完全不同，是這個坑好發的原因。
    # 連動鏈：側欄槽位色塊＝`clr_picker`（Plater.cpp:2305）→ 切換 preset 時讀 preset 的
    #   `default_filament_colour`（PresetComboBoxes.cpp:241）⇒ key 對了才會隨線材更新。
    # 冪等 sweep：改名後移除舊複數鍵；已是單數者跳過。Classic 線材同套（顏色與 Marlin 隔離無關）。
    ck_fixed = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        touched = False
        # 只保留 `default_filament_colour`＝**線材 preset 唯一合法的顏色鍵**。
        # 🔴 0725 查證：`Preset.cpp:960` 的 s_Preset_filament_options 裡，
        #    `"filament_colour"` 是**被上游註解掉的**（/*"filament_colour", */）
        #    ⇒ 線材檔帶它，引擎載入時會判為 incorrect key 直接剝掉，
        #      並在 log 刷 "contains incorrect keys: filament_colour, which were removed"。
        #    功能上無害（顏色是靠 default_filament_colour 生效），但屬無效殘留 ⇒ 一併清掉。
        if "default_filament_colors" in fd and "default_filament_colour" not in fd:
            fd["default_filament_colour"] = fd["default_filament_colors"]
            touched = True
        elif "filament_colors" in fd and "default_filament_colour" not in fd:
            # 舊檔只有複數版 filament_colors 時，值同樣拿來當預設色來源
            fd["default_filament_colour"] = fd["filament_colors"]
            touched = True
        for dead in ("filament_colors", "default_filament_colors", "filament_colour"):
            if dead in fd:
                fd.pop(dead, None); touched = True
        if touched:
            jdump(fp_path, fd); ck_fixed += 1
    if ck_fixed:
        print("  線材顏色 key 正規化（複數→單數）：%d 支" % ck_fixed)

    # 4b-2g. ★ 配料屬性顯式化【第二趟：衍生之後】——收 4b-1c TPE 等「新建時未帶鍵」的支。
    # 規則本體見 `_backfill_filament_attrs()`；兩趟的理由見 4a-0b 註解。
    _backfill_filament_attrs("4b-2g（衍生後）")

    # 4b-3. ★ 洗料塔最小清理量（Eric 2026-07-17 裁）：全線材 30；SupPLA 系（含高流量噴頭/Classic）60；
    # FF「四料同進噴頭」/「(3in1)」/「(照片磚)」維持特調 120 不動（四色換色需大量清洗，Eric 0717 裁「不蓋」）。
    # 🔴 2026-08-16 改名連動：判定字串由「四料高流量噴頭」改成「四料同進噴頭」＋補照片磚專用支。
    #    **漏改這一行不會報錯**——同進支會被當成一般線材壓成 30，清料量靜默掉四分之三。
    # 放 4b-2 之後同樣吃冪等 sweep：重生檔每次 regen 自動補。
    pv_set = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "PING*.json")):
        bn = os.path.basename(fp_path)
        if "四料同進噴頭" in bn or "(3in1)" in bn or "(照片磚)" in bn:
            continue
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        # PVA＝85（V2.1 案 75＋劉勝賢現行 +10，0724 對帳定稿）；SupPLA 系 60；其餘 30
        want = "85" if "PVA" in bn else ("60" if "SupPLA" in bn else "30")
        cur = fd.get("filament_minimal_purge_on_wipe_tower")
        if (cur[0] if isinstance(cur, list) else cur) == want:
            continue
        fd["filament_minimal_purge_on_wipe_tower"] = [want]
        jdump(fp_path, fd)
        pv_set += 1
    if pv_set:
        print("  線材洗料塔最小清理量 30/60：改 %d 支（FF 四料/3in1 特調 120 不動）" % pv_set)

    # 4b-4. ★ 機器動力學＝Klipper 實值（Eric 2026-07-17 裁「全機隊直接改」）：
    # 時間預估器用機器檔 machine_max_* 模擬，原值 20000/jerk100 是幻想 → 29h 估 vs 33h 實差 14%。
    # 實值來源＝機隊 repo range cfg：FD/FP＝max_velocity 400/max_accel 5000/SCV 5（jerk≈5×√2≈7）；
    # FF＝200/1500/SCV 40（jerk≈56）。只影響時間預估與 M201/M203（Klipper 忽略），不改列印行為。
    # DL1016（無實測值）與 Classic 前代機（Marlin，emit_machine_limits=0 另案）跳過。
    mm_set = 0
    for mp_path in glob.glob(os.path.join(PINGDIR, "machine", "*.json")):
        try:
            md_ = json.load(io.open(mp_path, encoding="utf-8"))
        except Exception:
            continue
        if md_.get("type") != "machine" or "machine_max_acceleration_x" not in md_:
            continue
        mname = md_.get("name", "")
        if "DL1016" in mname or CLASSIC_MACHINE_RE.match(mname):   # 判準見檔頭 CLASSIC_MACHINE_RE
            continue
        V, A, J = ("200", "1500", "56") if "FF" in mname else ("400", "5000", "7")
        want = {"machine_max_speed_x": [V, V], "machine_max_speed_y": [V, V],
                "machine_max_acceleration_x": [A, A], "machine_max_acceleration_y": [A, A],
                "machine_max_acceleration_extruding": [A, A], "machine_max_acceleration_travel": [A, A],
                "machine_max_acceleration_retracting": [A, A],
                "machine_max_jerk_x": [J, J], "machine_max_jerk_y": [J, J]}
        if all(md_.get(k) == v for k, v in want.items()):
            continue
        md_.update(want)
        jdump(mp_path, md_)
        mm_set += 1
    if mm_set:
        print("  機器動力學=Klipper實值：改 %d 台（FD/FP 400/5000/7、FF 200/1500/56）" % mm_set)

    # 4b-5. ★ 冷卻降速統一：slow_down_layer_time（最大風扇臨界·每層列印時間）一律 10 秒
    #（Eric 2026-07-18 裁「擴及所有材料」，原預設 5 與 FF 7/基底 2~8 特調一併統一）。
    #
    # 🔴 **slow_down_for_layer_cooling 1 → 0（Eric 2026-08-07 裁・翻 0718 自己那條）**
    # 原話：「經過實測，所有材料的這個選項請取消打勾。它是在特殊情況下才需要進行勾選，
    #        因此大部分情況下都要取消。」
    # ⇒ ground truth＝實機實測，**不是疏漏、是有依據的翻案**；下一棒看到 0 不要「修正」回 1。
    # 引擎預設是 true（PrintConfig.cpp:1709 set_default_value true），所以必須每支明寫 0 才擋得住。
    # ⚠ `slow_down_layer_time` 維持 10 不動——那顆同時驅動「最大風扇速度臨界值」的風扇轉速插值，
    #    不是只驅動降速；Eric 只指名取消勾選那一格。
    # 🔴 **2026-09-10 Eric 再裁（移植進出貨線 2026-09-11，牌 c-0911-MCH-01）：降速回「開」＋最小列印速度 25，
    #    但照片磚系除外。** 起因＝其他同事回報「尖端成型不好」（Eric 明說不是照片磚線）。
    #    **這不算翻 0807**：0807 原話本來就寫「它是在特殊情況下才需要進行勾選」——0807 關的是「一律開」，
    #    這次開的是「一般件需要」，照片磚反而成了那個要保持關的例外。
    #    ⛔ 照片磚系維持 0、最小速度 10：每層面積小又在低流量下換色，降速會把層時間再拉長。
    #    ⚠ `slow_down_layer_time` 仍維持 10 不動（同 0807：那顆同時驅動風扇轉速插值）。
    #    ⚠ 照片磚系也要**明寫**最小速度，不能只是「不設」：那些支是從母體深拷貝派生的，
    #       母體被設成 25 之後，下一次 regen 派生就把 25 帶進來，「不設」攔不住繼承 ⇒ 值會跨 regen 漂。
    #    🔵 前代 Classic 線材**一起套**（與上面 4b-4c 的 z_hop 相反）：0807 那條的範圍本來就是
    #       「所有材料」，0910 只挖掉照片磚一個例外；而 z_hop 那條 Eric 寫的是「一般機」、
    #       Classic 另有 emit_classic 成組給定的值。兩條範圍不同是因為原裁定範圍就不同，不是筆誤。
    CD_SLOWDOWN    = ["1"]   # ← 一般線材；要全面關回去改這一行
    CD_SLOWDOWN_PT = ["0"]   # ← 照片磚系（例外）
    CD_MIN_SPEED   = ["25"]  # ← 最小列印速度，只在降速生效時有作用
    CD_MIN_SPEED_PT = ["10"] # ← 照片磚系維持原值
    cd_set = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        _is_pt = os.path.basename(fp_path)[:-5] in (PT_FIL_PLA, PT_FIL_PLA_FD, PT_FIL_PLA_FDHF)
        _want  = CD_SLOWDOWN_PT if _is_pt else CD_SLOWDOWN
        _spd   = CD_MIN_SPEED_PT if _is_pt else CD_MIN_SPEED
        if (fd.get("slow_down_for_layer_cooling") == _want and fd.get("slow_down_layer_time") == ["10"]
                and fd.get("slow_down_min_speed") == _spd):
            continue
        fd["slow_down_for_layer_cooling"] = list(_want)
        fd["slow_down_layer_time"] = ["10"]
        fd["slow_down_min_speed"] = list(_spd)
        jdump(fp_path, fd)
        cd_set += 1
    if cd_set:
        print("  冷卻降速統一（降速%s＋層時間 10 秒）：改 %d 支"
              % ("開" if CD_SLOWDOWN == ["1"] else "關", cd_set))

    # 4b-4c. ★ Z 抬升＝口徑（Eric 2026-09-10 裁，牌 c-0910-WT-10；2026-09-11 移植進出貨線 c-0911-MCH-01）：
    #   一般機的 `z_hop` 改成「該機口徑值」——0.4→0.4（本來就是、無變動）／0.6→**0.6**／1.0→**1**。
    #   ⛔ **照片磚機不套**。⚠ 註解跨線搬要對著本線查：開發線的照片磚是 0.1（Eric 2026-09-07 把零回抽
    #      改成韌體回抽 1.3＋抬升 0.1），**但出貨線實查是 0**——0907 那條也還沒搬過來。
    #      本段只負責「不動它」，不負責把它改成 0.1；要改是另一條規則的事。
    #      本段自己排除照片磚，**不倚賴後面覆寫回來的順序**
    #      ——順序是隱形依賴，哪天有人把 PT 段搬到前面，這裡就會靜默把 0.1 蓋成 0.6。
    #   ⛔ **0.2／0.25 口徑不套**：維持 0.4（Eric 同日裁）。照規則它們會從 0.4 **降到** 0.2／0.25，
    #      方向與另外三個相反、撞件風險反升，要套需另測。
    #   🔴 **出貨線專屬加碼：前代 Classic 機也不套，維持 emit_classic 給的 0.5。**
    #      理由＝開發線沒有 Classic，Eric 那條裁定寫「一般機」時不存在這條路徑（同 SOP §S-3 的形狀）。
    #      Classic 是 Marlin 舊板、z_hop 與 wipe 由 emit_classic 成組給定，把它改成 0.6/1.0 等於替
    #      前代機做一個 Eric 沒裁過的行為變更。本檔既有慣例也是這樣（機器動力學、支撐速度都略過 Classic）。
    #      要納入的話改這一行的 `or CLASSIC_MACHINE_RE.match(_bn)` 即可。
    #   ⚠ **線材層會蓋過機器層**：`PING PVA`／`PING SupTPE`／`PING TPE - 210` 三支寫死 filament_z_hop=0.6
    #      ⇒ 在 1.0 機上用這三支實際仍是 0.6。那是材料特性值，本批不動。
    ZHOP_BY_NOZZLE = {"0.6": ["0.6"], "1": ["1"], "1.0": ["1"]}
    zh_set = 0
    for _fp in sorted(glob.glob(os.path.join(PINGDIR, "machine", "*nozzle.json"))):
        _bn = os.path.basename(_fp)[:-5]
        if "照片磚" in _bn or CLASSIC_MACHINE_RE.match(_bn):
            continue
        _d = json.load(io.open(_fp, encoding="utf-8"))
        if _d.get("type") != "machine":
            continue
        _want = ZHOP_BY_NOZZLE.get(str(_d.get("printer_variant")))
        if not _want or _d.get("z_hop") == _want:
            continue
        _d["z_hop"] = list(_want)
        jdump(_fp, _d)
        zh_set += 1
    if zh_set:
        print("  Z 抬升＝口徑（0.6→0.6／1.0→1；照片磚與 Classic 與 0.2/0.25 不套）：改 %d 台" % zh_set)

    # 4b-4d. ★ 機型「支援空氣過濾」「支援控制列印設備內部溫度」一律關（Eric 2026-09-29 裁 Q3 甲「照建議」；
    #   開發中清單 #33，牌 c-0929-NOP-03；報告 `無作用參數盤點_20260929.html` B1／B2）：
    #   PING 沒有任何機型有可控排風扇或艙溫加熱——Klipper 的 M106 不看 P（排風扇的 M106 P3 會打到主冷卻風扇）、
    #   18 台都不認得 M141／M191。兩個鍵原本是交付來源與範本帶進來的 1（本產生器原本 0 處提到它們）⇒
    #   線材頁「排風扇」「列印設備內部溫度」兩組在簡易模式就看得到。改 0 後由 Tab.cpp 的顯示開關把兩組藏起來；
    #   輸出端另由 GCodeWriter 對 Klipper 擋住（兩道各管一件，不互相依賴）。
    #   ⚠ 引擎預設是 true（PrintConfig.cpp）⇒ 缺鍵＝吃預設 1，所以沒有這兩個鍵的葉檔也照寫。
    #   🔵 Classic（Marlin）一起關：Eric 裁的範圍就是 149 支全部（Classic 也沒有這兩種硬體）。
    #   ℹ️ `auxiliary_fan`（輔助冷卻風扇）本來就 149 支全 0，不在本段。守衛＝verify_profiles.py 檢查 10-c。
    SUPPORT_OFF = {"support_air_filtration": "0", "support_chamber_temp_control": "0"}
    so_set = 0
    for _fp in sorted(glob.glob(os.path.join(PINGDIR, "machine", "*.json"))):
        _d = json.load(io.open(_fp, encoding="utf-8"))
        if _d.get("type") != "machine" or _d.get("instantiation") != "true":
            continue
        if all(_d.get(k) == v for k, v in SUPPORT_OFF.items()):
            continue
        _d.update(SUPPORT_OFF)
        jdump(_fp, _d)
        so_set += 1
    if so_set:
        print("  機型「支援空氣過濾／支援控制列印設備內部溫度」一律關：改 %d 台" % so_set)

    # 4b-5b. ★ 線材收縮補償全庫一致＝100%（Eric 2026-08-09 裁 A；回報中心「線材收縮補償將被停用」單）
    #
    # 🔴 引擎規則（`Print.cpp:3623 has_same_shrinkage_compensations`）：**所有用到的料，
    #    `filament_shrink`(XY) 與 `filament_shrinkage_compensation_z` 必須完全相同**，
    #    只要有任一支不同 ⇒ `shrinkage_compensation()` 直接回 Ones() ＝**整個補償停用**並跳警告。
    #    ⚠ 英文原文是 "does not match"（不一致），**沒有門檻**；中文翻譯「差異過大」是誤導
    #      （會讓人以為差一點沒關係）——翻譯修正另案（要動 .po ＋ build）。
    #
    # 真因：全庫 34 支線材裡只有 `PING SupABS` 與 `PING SupABS - Classic` 明寫 99.7%，其餘全 100%
    #（或未寫＝吃引擎預設 100%）⇒ **只要用到 ABS+SupABS 組合就必跳警告**，而那 0.3% 因為被停用
    #  **從來沒有生效過**。99.7% 由 6/11 參數端交付帶入（commit efb7eb22），本鍵原本產生器沒管。
    #
    # ⇒ 統一為 100%：警告消失，**列印行為零改變**（本來就沒在補償）。
    # 日後若真要做收縮補償，**必須整組同時設同一個值**（同一次列印會用到的所有料），否則等於沒設。
    # 🆕 2026-09-11（Eric：「ABS 的收縮率是 99.75%，之前講到 100.25% 是錯誤的」；
    #    回報中心 ABS 收縮率單，劉勝賢回報）：**ABS 族改 99.75%，其餘維持 100%**。
    #    這是上面那條引擎規則的合法用法——約束是「同一次列印用到的所有料要同值」，
    #    不是「全庫要同值」。ABS 件只會用到 ABS 族（ABS 本體＋SupABS 支撐），族內同值 ⇒ 補償生效；
    #    PLA 件只用到 PLA 族（全 100%）⇒ 也一致。ABS×PLA 混用不是既有使用情境。
    #    ⚠ 行為改變：0809 統一 100% 時「列印行為零改變」（本來就沒在補償）；
    #      本次讓 ABS 族的補償**首次真正生效**，ABS 件 XY 會縮 0.25%。這是預期中的尺寸變化。
    #    🔴 ABS 族必須**明寫**這個鍵（原本 PING ABS／ABS(玻璃)／ABS - Classic 沒有這個鍵＝
    #      吃引擎預設 100%），否則族內不一致 ⇒ 引擎把整個補償關掉＝等於沒改。
    SHRINK_UNIFORM = "100%"      # 非 ABS 族的統一值
    SHRINK_ABS = "99.75%"        # ABS 族 XY（Z 不動＝吃引擎預設 100%，族內本就一致）
    ABS_SHRINK_FAMILY = ("PING ABS", "PING ABS(玻璃)", "PING ABS - Classic",
                         "PING SupABS", "PING SupABS - Classic")
    shr_fixed = 0
    for fp_path in glob.glob(os.path.join(PINGDIR, "filament", "*.json")):
        fd = json.load(io.open(fp_path, encoding="utf-8"))
        _name = os.path.basename(fp_path)[:-5]
        _is_abs = _name in ABS_SHRINK_FAMILY
        _xy = SHRINK_ABS if _is_abs else SHRINK_UNIFORM
        changed = False
        # ABS 族：一律明寫 filament_shrink（沒有就補上）；其餘沿用舊行為＝只修正已存在的鍵
        if _is_abs and fd.get("filament_shrink") != [_xy]:
            fd["filament_shrink"] = [_xy]
            changed = True
        for _k in ("filament_shrink", "filament_shrinkage_compensation_z"):
            _want = _xy if _k == "filament_shrink" else SHRINK_UNIFORM
            if _k in fd and fd[_k] != [_want]:
                fd[_k] = [_want]
                changed = True
        if changed:
            jdump(fp_path, fd)
            shr_fixed += 1
            print("  收縮補償統一 %s：%s" % (SHRINK_UNIFORM, os.path.basename(fp_path)))
    if shr_fixed:
        print("  線材收縮補償全庫一致（%s）：改 %d 支" % (SHRINK_UNIFORM, shr_fixed))

    # 4b-6. ★ 支撐首層擴展＋支撐線寬（Eric 2026-08-09 兩裁；純參數、零 C++）
    #
    # 做成 **post-pass**（掃 process/*.json 全量）而不是塞進各 normalize_*：製程有 4+ 條 emit 路徑
    #（主迴圈／ff_extra 範本／照片磚範本／棧板雙生／高流量組／Classic 複製），規則函式掃不到的
    # 來源要各自處理＝規則會漂（0807 預勾線材 post-pass 同理由）。單一規則點 ⇒ 日後新增機型自動涵蓋。
    #
    # ① 首層擴展 raft_first_layer_expansion 3 → 0
    #    Eric 原話：「支撐的首層擴展這邊要設定為 0，它的效果是最好的」＝實印判斷。
    #    ⚠ **這顆鍵同時管「支撐首層」與「棧板(筏)首層」**（引擎 tooltip：「擴展筏和支撐的首層
    #      可以改善和熱床的黏附」，PrintConfig.cpp:4691）⇒ 分家族處理：
    #        raft_layers == 0（支撐用途）→ **0**
    #        raft_layers >= 1（棧板／ABS raft 機種）→ **6**（Eric 2026-09-18 裁「三族都改」3→6，回報中心 周宣仲 9/17 實測；原 0809「保留 3」）
    #      後者是 Eric 0809 明裁「棧板保留 3、其餘歸 0」，與 0804 對兄弟鍵 raft_first_layer_density
    #      的處置同型（raft 靠貼床，外擴是它吃飯的本事）。**下一棒看到棧板是 3 不要「順手統一」。**
    #
    # ② 支撐線寬 support_line_width ＝ 口徑查表「窄一階」
    #    🔴 **本批推翻舊規**：ping-slicer core-rules.md 原訂 `support_line_width = line_width（＝口徑）`，
    #       改版前全庫 218 支 100% 遵守 ⇒ 這不是修 bug，是 Eric 0809 有意識的規則變更（窄支撐好拆）。
    #    Eric 指定三格：0.4→0.35／0.6→0.5／1.0→0.8（原話「用一個好記的數字」）。
    #    0.25→0.2、0.2→0.15 為同規律外推（比例 0.8／0.75），**尚未經實印**，回饋後可改本表一處。
    #    查表鍵一律用**口徑名目值**：FF 高流量線寬＝1.02×口徑（0.41/0.62/1.02），照 0722 支撐線距 ×9
    #    的家規「FF 微調不入分子」，一律歸回 0.4/0.6/1.0。
    #    照片磚製程 enable_support=0（根本不開支撐）＝跟著改無副作用，維持全庫一致好稽核。
    SUP_LW_BY_NOZZLE = {"0.2": "0.15", "0.25": "0.2", "0.4": "0.35", "0.6": "0.5", "1": "0.8"}
    PALLET_RAFT_FIRST_LAYER_EXPANSION = "6"   # ← 筏層值（Eric 2026-09-18「三族都改」3→6；牌 c-0918-ABS-01）；PALLET_OVERRIDES 要同值
    SUPPORT_RAFT_FIRST_LAYER_EXPANSION = "0"  # ← 支撐值（Eric 0809 實印判斷）

    def _nominal_nozzle(lw):
        """線寬 → 口徑名目值（FF 高流量 +2% 也歸回原口徑）；認不出回 None。"""
        try:
            v = float(lw)
        except (TypeError, ValueError):
            return None
        for _n in ("0.2", "0.25", "0.4", "0.6", "1"):
            if abs(v - float(_n)) <= 0.03:   # 1.02 對 1.0 差 0.02；0.2 對 0.25 差 0.05＝不會誤配
                return _n
        return None

    # ③ FF600／FF800 族的七個線寬鍵 ＝ 口徑名目值（Eric 2026-09-15 裁，回報中心 #152）
    #    🔴 **本批推翻「FF 高流量線寬＝1.02×口徑」**（0.41／0.62／1.02，工程端交付值）。
    #       Eric 原話「參數以上面圖片為主」＋附 FF600 同進 0.6 的截圖（全 0.6／支撐 0.5）。
    #       ⇒ 這不是修 bug，是有意識的規則變更：**同一台 FF600 上，四料同進早就是「線寬＝口徑」**
    #         （0.4／0.6／1），而 3in1 與 FF 單料頭還停在 1.02×口徑 ⇒ 同機不同族兩套值。
    #    範圍＝Eric 裁「12 支全拉平」：3in1 六支（FF600／FF800 × 0.4／0.6／1.0）
    #         ＋ FF 單料頭六支（同上）。兩者現值**完全相同**，只改一半會製造新的不一致。
    #    ⛔ **排除照片磚**：`@FF600 同進照片磚` 那六支線寬另有規則（曾經＝1.5×口徑，見下面支撐
    #       post-pass 的 ★ 註解）。它們現值雖然剛好也是口徑、套下去是 no-op，但仍明確排除——
    #       否則日後照片磚線寬再改回 1.5×口徑時，會被這裡靜默洗掉。
    #    ⛔ **不碰 support_line_width**：那是下面 ② 的查表（窄一階 0.35／0.5／0.8），
    #       兩個家族本來就已經一致，不在本裁範圍。
    #    驗收守衛＝verify_profiles.py 的〈FF 族線寬＝口徑〉段（回歸型）。
    FF_LW_KEYS = ("line_width", "initial_layer_line_width", "outer_wall_line_width",
                  "inner_wall_line_width", "top_surface_line_width",
                  "sparse_infill_line_width", "internal_solid_infill_line_width")

    exp_set = lw_set = lw_unknown = 0
    ffw_files = ffw_keys = 0
    for pp_path in sorted(glob.glob(os.path.join(PINGDIR, "process", "*.json"))):
        pdj = json.load(io.open(pp_path, encoding="utf-8"))
        touched = False
        _bn_pp = os.path.basename(pp_path)
        if re.search(r"@FF(?:600|800)\b", _bn_pp) and "照片磚" not in _bn_pp:
            _m_ff_nz = re.search(r"\(([\d.]+)\)\s*\.json$", _bn_pp)
            if _m_ff_nz:
                _ff_want = "%g" % float(_m_ff_nz.group(1))
                _ff_hit = 0
                for _k in FF_LW_KEYS:
                    if _k in pdj and pdj[_k] != _ff_want:
                        pdj[_k] = _ff_want
                        touched = True
                        _ff_hit += 1
                if _ff_hit:
                    ffw_files += 1
                    ffw_keys += _ff_hit
            else:
                print("  ⚠ FF 族線寬 post-pass：%s 檔名認不出口徑，跳過" % _bn_pp)
        if "raft_first_layer_expansion" in pdj:
            want_exp = (PALLET_RAFT_FIRST_LAYER_EXPANSION
                        if str(pdj.get("raft_layers", "0")) != "0"
                        else SUPPORT_RAFT_FIRST_LAYER_EXPANSION)
            if pdj["raft_first_layer_expansion"] != want_exp:
                pdj["raft_first_layer_expansion"] = want_exp
                touched = True
                exp_set += 1
        if "support_line_width" in pdj:
            # ★ 口徑優先從檔名「(口徑)」取（2026-09-09 PTP 棒）：照片磚線寬曾＝1.5×口徑，用 line_width 反推會錯一階。
            _m_nz_nm = re.search(r"\(([\d.]+)\)\s*\.json$", os.path.basename(pp_path))
            _nz_nom = None
            if _m_nz_nm:
                _cand = "%g" % float(_m_nz_nm.group(1))
                _nz_nom = _cand if _cand in SUP_LW_BY_NOZZLE else None
            if _nz_nom is None:
                _nz_nom = _nominal_nozzle(pdj.get("line_width"))
            if _nz_nom is None:
                lw_unknown += 1
                print("  ⚠ 支撐線寬 post-pass：%s 的 line_width=%r 認不出口徑，跳過"
                      % (os.path.basename(pp_path), pdj.get("line_width")))
            else:
                want_lw = SUP_LW_BY_NOZZLE[_nz_nom]
                if pdj["support_line_width"] != want_lw:
                    pdj["support_line_width"] = want_lw
                    touched = True
                    lw_set += 1
        if touched:
            jdump(pp_path, pdj)
    if exp_set or lw_set or lw_unknown:
        print("  支撐首層擴展→0（棧板留 %s）：改 %d 支｜支撐線寬窄一階：改 %d 支｜口徑認不出：%d 支"
              % (PALLET_RAFT_FIRST_LAYER_EXPANSION, exp_set, lw_set, lw_unknown))
    if ffw_files:
        print("  FF 族線寬＝口徑（0915 裁·#152）：改 %d 支／%d 個鍵" % (ffw_files, ffw_keys))

    # 4b-7. ★ 支撐／支撐面速度下限（Eric 2026-08-12 裁）
    #
    # Eric 原話：「支撐與支撐面小於 60 的全部拉到 60」——起點是「列印速度全部放在同一量級」，
    # 收斂後只動支撐這兩鍵：支撐本來就是 Fast 系速度表裡唯一的異類（FD450 Pro 外牆 60／內牆 80／
    # 稀疏 100，支撐卻 40），拉到 60 是把它拉回隊伍裡。
    # ⛔ **不動稀疏填充（100）與內牆（80）**：Eric 明示「不想偏離全庫標準」——`speed_infill=100`
    #    是他 0719 親裁全機型的值，動它等於改規則、要全庫重產。
    #
    # 🔴 **排除 Classic 前代機**（Eric 裁）：Classic 是 Marlin 非 Klipper、無 Input Shaper，整張速度表
    #    本來就慢（EDU 200 全表 40；DUAL 450 外牆 40／頂面 40／支撐 25）⇒ 把支撐拉到 60 會讓
    #    **支撐變成盤上最快的東西**＝內部倒置。這是「整體慢」不是「支撐特別慢」，不能套同一條規則。
    #    比對用 CLASSIC_MODELS 前綴 ⇒ 同進／單料頭變體（如「DUAL 450 同進」）自動涵蓋。
    #
    # ℹ **為什麼不必再擔心大口徑的流量**（Eric 2026-08-12 指正，我方原判斷已撤回）：
    #    Orca 有材料層 `filament_max_volumetric_speed`，超過會自動降速，且全庫線材逐支都有設
    #    （SupPLA 12／PETG 25／PLA-220・SupABS 30／PA-CF 8／SupTPE 5.5）。所以「設定值」是上限意圖、
    #    「實跑值」由引擎取兩者較低者。**Eric 的設計意圖**：全域速度套多材料時軟料會過快，
    #    用材料層上限讓它在安全流量前就停住——這是刻意的保護，不是副作用。
    #    ⇒ 大口徑上把支撐設 60，實際會被壓回材料允許的值；設定值統一不會造成過擠。
    SUPPORT_SPEED_FLOOR = 60.0
    SUPPORT_SPEED_KEYS = ("support_speed", "support_interface_speed")
    # 🔴 豁免名單（Eric 2026-09-19 裁「樹狀支撐的支撐速度 60>50」，牌 c-0919-ETR-01）：
    #    製程名 token（@ 前最後一段）在此名單內 ⇒ 不拉回 60。值由 EASY_TREE_OVERRIDES 給、verify 鎖 exact 50。
    #    verify_profiles.py〔支撐速度〕段有**同名同值**的一份（verify 刻意不 import 產生器＝既有慣例），兩邊一起改。
    #    ⚠ 本段整段搬到開發線時：開發線名＝「易拆(Z0)樹狀」，名單要換成那個字面（token 規則同出貨線）。
    SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS = (EASY_TREE_TOKEN,)   # ＝("易拆樹狀",)

    def _is_floor_exempt(fname):
        return any((" %s @" % t) in fname for t in SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS)

    def _is_classic_process(fname):
        """製程檔名形如「0.3mm 易拆 @EDU 200 (0.6).json」；取 @ 後、( 前的機型名比對前綴。"""
        at = fname.rfind("@")
        if at < 0:
            return False
        m = fname[at + 1:]
        par = m.rfind("(")
        m = (m[:par] if par >= 0 else m).strip()
        return any(m.startswith(c) for c in CLASSIC_MODELS)

    spd_set = spd_skip_classic = spd_skip_easy_tree = spd_missing = 0
    for pp_path in sorted(glob.glob(os.path.join(PINGDIR, "process", "*.json"))):
        base = os.path.basename(pp_path)
        if base.startswith("fdm_"):
            continue
        if _is_classic_process(base):
            spd_skip_classic += 1
            continue
        if _is_floor_exempt(base):     # 豁免名單見上方 SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS
            spd_skip_easy_tree += 1
            continue
        pdj = json.load(io.open(pp_path, encoding="utf-8"))
        touched = False
        for _k in SUPPORT_SPEED_KEYS:
            if _k not in pdj:
                # 葉檔沒有這顆鍵＝值來自繼承鏈。實測全庫皆自帶，真的出現就要人眼看一下，
                # 不要靜默略過（略過＝規則在那支檔上失效，正是「verify 全綠但功能沒生效」那型）。
                spd_missing += 1
                print("  ⚠ 支撐速度 post-pass：%s 沒有 %s（值來自繼承），未處理" % (base, _k))
                continue
            try:
                cur = float(pdj[_k])
            except (TypeError, ValueError):
                print("  ⚠ 支撐速度 post-pass：%s 的 %s=%r 不是數值，跳過" % (base, _k, pdj[_k]))
                continue
            if cur < SUPPORT_SPEED_FLOOR:
                pdj[_k] = "%g" % SUPPORT_SPEED_FLOOR
                touched = True
                spd_set += 1
        if touched:
            jdump(pp_path, pdj)
    print("  支撐/支撐面速度下限 %g：改 %d 項｜Classic 前代機略過 %d 支｜易拆樹狀豁免 %d 支｜鍵不在葉檔 %d 項"
          % (SUPPORT_SPEED_FLOOR, spd_set, spd_skip_classic, spd_skip_easy_tree, spd_missing))

    # 4c. 封面（cover 以機型名解析——坑#11）：
    #     家族基本款=機器照片；單料頭/同進 模式卡=透明空白（2026-06-10 使用者定）；孤兒封面刪除
    # 每家族專屬照片（FD300 Pro 有自己的照片，勿沿用 FD300——取最長前綴匹配）
    cover_src = {"EDU 200":"FP300_cover.png", "PING 200":"FP300_cover.png",
                 "PING 270":"FP300_cover.png", "PING 300+":"FP300_cover.png",
                 "DUAL 300":"FD300_cover.png", "DUAL 450":"FD450 Pro_cover.png",
                 "DUAL 600":"FD600 Pro_cover.png", "DUAL 800":"FD800 Pro_cover.png",
                 "FD300 Pro":"FD300 Pro_cover.png","FD300":"FD300_cover.png",
                 "FP300":"FP300_cover.png","P200+":"FP300_cover.png",
                 "FD450":"FD450 Pro_cover.png","FD600":"FD600 Pro_cover.png",
                 "FD800":"FD800 Pro_cover.png","FF600":"FF600_cover.png","FF800":"FF800_cover.png"}
    def blank_png(path):
        from PIL import Image
        Image.new("RGBA", (600, 600), (0, 0, 0, 0)).save(path)
    cover_sources = set(cover_src.values())  # 保留被引用的來源圖（如 P200+ 借 FP300_cover）
    for f in os.listdir(PINGDIR):           # 刪除不屬於現役機型的封面（ff_extra/照片磚 範本模型受保護）
        if f.endswith("_cover.png") and f[:-len("_cover.png")] not in nozzles_of \
           and f[:-len("_cover.png")] not in (ff_models + pt_models) and f not in cover_sources:
            os.remove(os.path.join(PINGDIR, f)); print("  cover 移除(孤兒):", f)
    for model in nozzles_of:
        dst = os.path.join(PINGDIR, "%s_cover.png" % model)
        if model.endswith(("單料頭", "同進", "關門")):
            blank_png(dst)                   # 模式卡固定空白（每次重生覆寫，確保不殘留照片）
        elif not os.path.exists(dst):
            key = max((k for k in cover_src if model.startswith(k)), key=len)
            shutil.copy2(os.path.join(PINGDIR, cover_src[key]), dst)
            print("  cover: %s_cover.png <- %s" % (model, cover_src[key]))

    # 4c-2. 側欄印表機縮圖 printer_preview_{model_id}.png（Plater.cpp:3969；缺檔=黑方塊）
    #       預設用「家族機器照」重畫（模式變體同實機）；240x240 RGBA 同上游規格。
    #       PREVIEW_OWN_ICON 列的機型例外：逐位元組複製範本、不重畫（理由見該表註解）。
    from PIL import Image
    img_dir = os.path.join(REPO, "resources", "images")
    own_seen, n_own, n_drawn = set(), 0, 0
    for model in list(nozzles_of) + ff_models + pt_models:   # ff_models（同進/3in1）＋照片磚 側欄縮圖也要
        family_cover = cover_src[max((k for k in cover_src if model.startswith(k)), key=len)]
        mm_path = os.path.join(PINGDIR, "machine", "%s.json" % model)
        model_id = json.load(io.open(mm_path, encoding="utf-8"))["model_id"]
        dst = os.path.join(img_dir, "printer_preview_%s.png" % model_id)
        own = PREVIEW_OWN_ICON.get(model_id)
        if own:
            own_seen.add(model_id)
            if os.path.isfile(os.path.join(PREVIEW_OWN, own)):
                shutil.copyfile(os.path.join(PREVIEW_OWN, own), dst)
                n_own += 1
                continue
            print("  ⚠ 側欄縮圖：%s 的專屬圖範本 base/printer_preview/%s 不見了，先用家族封面重畫——Eric 0901 選的圖會被蓋掉"
                  % (model_id, own))
        im = Image.open(os.path.join(PINGDIR, family_cover)).convert("RGBA")
        im.thumbnail((240, 240), Image.LANCZOS)
        canvas = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
        canvas.paste(im, ((240-im.width)//2, (240-im.height)//2), im)
        canvas.save(dst)
        n_drawn += 1
    print("  側欄縮圖：家族封面重畫 %d 支｜專屬圖範本複製 %d 支（對照表 %d 列）" % (n_drawn, n_own, len(PREVIEW_OWN_ICON)))
    for mid in sorted(set(PREVIEW_OWN_ICON) - own_seen):   # 只提示不擋
        print("  ⚠ 側欄縮圖對照表：%s 沒有對到任何機型（改名或下架？），這一列目前沒有作用" % mid)

    # 4d-0. LAY-11（ping-ux）：machine_model_list 同型號變體相鄰成組——
    # 家族依 FAMS 順序，家族內：基本款 → 同進 → 3in1 → 單料頭。
    # 單料頭需要實際換噴頭，放最右以免和 FD300／同進的雙料硬體混在一起（Eric 2026-07-15）。
    # 關門＝FD300 的家族「變體」（排序面）：從 base 候選拿掉，否則精確自我匹配會把它
    # 當成獨立家族排在整個 FD300 家族之後，_variant_rank 的「關門」永遠輪不到。
    fam_bases = [f[1] for f in FAMS if not f[1].endswith("關門")]
    classic_bases = [s["name"] for s in CLASSIC_SPECS]
    # 關門插第三＝Eric 2026-07-26 指定家族順序 FD300／同進／關門／單料頭。
    _variant_rank = {"": 0, "同進": 1, "關門": 2, "3in1": 3, "單料頭": 4}
    def _lay11_key(entry):
        name = entry["name"]
        base = max((b for b in fam_bases if name == b or name.startswith(b + " ")), key=len, default=None)
        if base is None:
            # Classic 家族同樣做 base＋變體分解（0727 DUAL 補同進/單料頭）：
            # 本體 rank 0、變體照 _variant_rank ⇒ 家族內 本體→同進→單料頭，與 F 系同律（LAY-11）。
            cbase = max((b for b in classic_bases if name == b or name.startswith(b + " ")),
                        key=len, default=None)
            if cbase is not None:
                variant = name[len(cbase):].strip()
                return (len(fam_bases) + classic_bases.index(cbase), _variant_rank.get(variant, 9))
            return (len(fam_bases) + len(classic_bases), 9, name)   # 不明機型殿後
        variant = name[len(base):].strip()
        return (fam_bases.index(base), _variant_rank.get(variant, 9))
    mm_list = sorted(mm_list, key=_lay11_key)

    # 4d. PING.json 重建（machine/process 全量重建；filament 保留既有＋新增 FF）
    pj_path = os.path.join(PROF, "PING.json")
    pj = json.load(io.open(pj_path, encoding="utf-8"))
    pj["machine_model_list"] = mm_list
    pj["machine_list"] = ([{"name":"fdm_machine_common","sub_path":"machine/fdm_machine_common.json"},
                           {"name":"fdm_ping_common","sub_path":"machine/fdm_ping_common.json"}]
                          + mac_list)
    pj["process_list"] = ([{"name":"fdm_process_common","sub_path":"process/fdm_process_common.json"},
                           {"name":"fdm_process_ping_common","sub_path":"process/fdm_process_ping_common.json"}]
                          + proc_list)
    # 高流量 @FF 舊名條目過濾（2026-07-12 改名，檔已刪、條目不留＝防斷鏈）
    # ★ ABS 整併（Eric 2026-07-25 裁「ABS 不需要 3 個，1 個就夠」・異常單 #37）：
    # 三支實測＝**溫度/床/風扇/purge 完全相同**（250/100/30·30/30）；`PING PolyABS` 與
    # `PING ABS` **葉檔逐鍵完全一致＝純副本**；`PING ABS - 250` 只因繼承鏈不同
    #（直接繼承 common、跳過 fdm_filament_abs）而多出流量比 0.98／最大流量 30／PA 0.12。
    # ⇒ 保留 `PING ABS` 並收編 ABS-250 的較佳值；另兩支移除，舊名走 renamed_from 相容
    #（Preset.cpp:2084 會拿舊名比對 system profile 的 renamed_from ⇒ 客戶既有 3mf 自動對應）。
    ABS_MERGED_AWAY = {"PING ABS - 250", "PING PolyABS"}
    # ★ 3in1 支口徑合一（Eric 2026-07-26 裁 C・統一值照 0.4/0.6）：六支 @FF 口徑別名併成
    # PLA(3in1)/SupPLA(3in1) 各一支、不綁機（0718 四料先例）。範本已收斂；此處清單過濾＋
    # 殘檔清除＝regen-durable，舊名走 renamed_from（⚠ 字串型別）相容。
    THREE_IN1_MERGED_AWAY = {"PING %s(3in1) @FF %s" % (_s, _nz)
                             for _s in ("PLA", "SupPLA") for _nz in ("0.4", "0.6", "1.0")}
    for _old in sorted(THREE_IN1_MERGED_AWAY):
        _p = os.path.join(PINGDIR, "filament", _old + ".json")
        if os.path.isfile(_p):
            os.remove(_p); print("  3in1 合一：移除殘檔", _old)
    # 0728 改名批：清單條目就地改指新名（保序＝最小 diff；後續 regen 舊名不存在＝no-op）
    # 基礎支（v1）＋TPE 本體（v2 二輪：改名帶溫度尾碼、SupTPE 名不動）
    _renamed_0728 = {BASE_PLA_OLD: BASE_PLA_NEW, "PING TPE": "PING TPE - 210"}
    for x in pj["filament_list"]:
        if x["name"] in _renamed_0728:
            x["name"] = _renamed_0728[x["name"]]
            x["sub_path"] = "filament/%s.json" % x["name"]
    pj["filament_list"] = [x for x in pj["filament_list"]
                           if x["name"] not in FF_FIL_RENAME and x["name"] not in ABS_MERGED_AWAY
                           and x["name"] not in THREE_IN1_MERGED_AWAY]
    # ABS-CF 線材範本（Eric 2026-10-08：加纖獨立成類、名稱不掛 PING 品牌；牌 c-1008-ACF-01）
    abscf_fil = []
    for _fn in sorted(os.listdir(ABSCF_FILAMENT_DIR)):
        _d = json.load(io.open(os.path.join(ABSCF_FILAMENT_DIR, _fn), encoding="utf-8"))
        jdump(os.path.join(PINGDIR, "filament", "%s.json" % _d["name"]), _d)
        abscf_fil.append({"name": _d["name"], "sub_path": "filament/%s.json" % _d["name"]})
    have = {x["name"] for x in pj["filament_list"]}
    pj["filament_list"] += [x for x in (fil_new + ff_fil + classic_fil + abscf_fil) if x["name"] not in have]
    # PING_ONLY 精簡：移除 FF 專用高流量線材（對單機客戶版無意義）——清 list ＋ 刪檔
    if PING_ONLY:
        pj["filament_list"] = [x for x in pj["filament_list"] if "@FF" not in x["name"]]
        for f in glob.glob(os.path.join(PINGDIR, "filament", "*@FF*.json")):
            os.remove(f)
    json.dump(pj, io.open(pj_path,"w",encoding="utf-8"), ensure_ascii=False, indent=4)

    # 4d-2. 預勾線材全族補齊（Eric 2026-08-07 裁）——必須排在 PING.json 重建**之後**，
    #        因為要吃最終的 filament_list／machine_list（含 ff_extra／照片磚／Classic 全部 emit
    #        完畢後的實況）。PING.json 本身不含 default_materials，故不需回寫。
    apply_default_materials(pj)

    # 4d-2b. ★ 照片磚線材相容性收斂（Eric 2026-08-22 裁「甲」）——2026-08-26 **回填產生器（源頭修復）**
    # 🔴 為什麼今天才補：0822 那批**直接改在產出檔上、從沒回產生器** ⇒ 任何一次全量 regen 都會靜默洗掉它
    #    （printer_notes 被源檔的空值蓋掉／default_materials 被上面 4d-2 重建成全清單／線材條件式整批消失）。
    #    2026-08-26 收漂移時實證：regen 完 verify 立刻三個 FAIL。這就是 SOP_參數入版紀律
    #    「直改產出檔沒回產生器＝下次 regen 就被洗掉」的活教材——**改規則要回產生器，不是只改產出檔**。
    # 三件＝0822 那顆 commit（24c1229944）的全部內容：
    #   ①照片磚**機器**（口徑變體）printer_notes = "PHOTOTILE"：這是照片磚機的識別標記，
    #     線材端條件式靠它認人（沒有它，下面第 ③ 條的條件式等於沒作用）。
    #   ②照片磚 **machine_model** 的 default_materials 只留該機的照片磚專用支（FD300 系是另一支）。
    #   ③其餘線材補 compatible_printers_condition ⇒ 照片磚機的線材下拉選不到它們。
    #     ⚠ **明列 compatible_printers 的線材不加**——明列優先於條件式，加了也不生效；
    #       0822 原註已載明「產生器若照舊寫 all_machines 會整個蓋掉」。四料同進/3in1/照片磚專用支都屬此類。
    apply_printer_family_gates(mac_list, mm_list)

    # 4e. ★ 預擠點升溫 post-pass——【2026-07-20 Eric 裁回退・停用，勿重新接上】
    # start gcode 回到 header 升溫舊制（base 排放即舊制，停用後 regen 自然還原）；
    # 重新啟用前需「清噴頭」等機制配套驗證通過（見 apply_deferred_heating 註記）。

    # ★ 支撐 Z 間距最終掃描（Eric 2026-08-17 裁：易拆 0／一般 0.2）
    # 為什麼是「最後掃一遍」而不是在各產出點各寫一次：製程有**四條**產出路徑（主迴圈／ff_extra 併入／
    # Classic V3.6 併入／Classic DUAL 變體併入），後三條是「讀既有檔再 update」⇒ 只改主迴圈會漏。
    # 實測就是這樣：只改主迴圈那版重產後仍有 5 支沒被套到（4 支 DUAL Classic 本體＋0.2mm @FF800 (0.4)）。
    # ⚠ 判準＝製程名含「易拆」**或**「3in1」。
    #   ①「易拆」涵蓋 COMBO_HEAD 產出的三種形態：易拆／易拆水溶／易拆+筏層。
    #   ②🔴 **3in1 必須另外列**——它是易拆家族（第 2 槽 `PING SupPLA(3in1)` 是支撐料、不相熔，
    #     ping-slicer 支撐間距總表明文「3in1(PLA+SUP)＝0 兩線一致無爭議」），但**名字裡沒有「易拆」
    #     兩個字**。實測就是漏掉這 6 支（FF600／FF800 × 三口徑）被誤設成 0.2 ＝ 支撐會熔在件上。
    #   ⚠ 這裡**刻意與 C++ 的 ping_classify_process 不同**：那張 token 表是給**下拉過濾**用的，
    #     未知 token 一律當「一般」是它的 fail-visible 設計（Tab.cpp:333 註解）。本處是**幾何值**，
    #     必須按物理家族判（會不會相熔），兩者目的不同、不可互抄。
    _zt = _zg = 0
    for _e in proc_list:
        _p = os.path.join(PINGDIR, _e["sub_path"].replace("process/", "process" + os.sep))
        if not os.path.exists(_p):
            continue
        _d = json.load(io.open(_p, encoding="utf-8"))
        _nm   = _d.get("name", "")
        _easy = ("易拆" in _nm) or ("3in1" in _nm)
        _z = "0" if _easy else "0.2"
        if _d.get("support_top_z_distance") != _z or _d.get("support_bottom_z_distance") != _z:
            _d["support_top_z_distance"] = _d["support_bottom_z_distance"] = _z
            jdump(_p, _d)
            _zt += 1
        _zg += 1 if _easy else 0
    print("  支撐 Z 間距最終掃描（易拆 0／一般 0.2）：掃 %d 支｜易拆 %d 支｜補正 %d 支"
          % (len(proc_list), _zg, _zt))

    # 🔴 保留號段守衛（2026-09-11，牌 c-0911-MCH-01）：EXT_RESERVED_MODELS 的 id 必須真的落在 950-999。
    #   為什麼要有：950 段只接在 main() 的 emit 點上，emit_classic／emit_ff_extra 沒接。
    #   日後若有人把一個 Classic 或 FF 機型放進 EXT_RESERVED_MODELS，它會安靜地拿主計數器的號、
    #   把全庫 id 推走——那正是保留號段要防的事。與其加兩條測不到的死分支，不如在這裡大聲失敗。
    _bad = []
    for _e in mac_list + proc_list:
        _nm = _e["name"]
        if not (any(_nm.startswith(_r + " ") for _r in EXT_RESERVED_MODELS) or _ext_proc(_nm)):
            continue
        _pth = os.path.join(PINGDIR, _e["sub_path"].replace("/", os.sep))
        _sid = json.load(io.open(_pth, encoding="utf-8")).get("setting_id") or ""
        if not (_sid[5:].isdigit() and EXT_MODEL_START <= int(_sid[5:]) <= 999):
            _bad.append((_nm, _sid))
    if _bad:
        raise SystemExit("[停手] 保留號段機型的 setting_id 沒落在 %d-999，既有 id 會被推走：%s"
                         % (EXT_MODEL_START, _bad[:8]))
    if EXT_RESERVED_MODELS:
        print("  保留號段守衛：%s 的 id 全部落在 %d-999 ✅" % (list(EXT_RESERVED_MODELS), EXT_MODEL_START))

    print("\n產出: machine_model=%d machine=%d process=%d (+FF filament %d)，PING.json 已重建（版號請另行+1）"
          % (len(mm_list), gm, gp, len(fil_new)))

    # ★ 未入版產出提示（2026-09-18 開發線 8004d66a24；2026-09-21 回移出貨線，牌 c-0921-CONV-07）
    #   ——只提示、不擋、不改任何產出值。
    report_untracked_outputs()


# 本支的產出不只 resources/profiles：§4c-2 還會重畫 resources/images/printer_preview_*.png。
# 加新機型時那張縮圖是「新檔」，而 SOP_參數入版紀律 §Q 原本的自檢用 --untracked-files=no ⇒ 看不到。
# 實錄：開發線 0908 加 FP300 關門（724b5fc571）只入了 resources/profiles，縮圖漏入版十天——
#   乾淨 clone 選 FP300 關門 時側欄縮圖沒有機器照。
# 刻意只提示不擋（根 AGENTS〈新增的檢查一律先做提示〉）；git 跑不起來就印一行說明後略過，不讓 regen 失敗。
OUTPUT_DIRS = ("resources/profiles", "resources/images")

def report_untracked_outputs():
    import subprocess
    def _say(s):   # 提示本身絕不能讓 regen 以 UnicodeEncodeError 收尾（主控台非 UTF-8 時）
        enc = getattr(sys.stdout, "encoding", None) or "utf-8"
        print(s.encode(enc, "replace").decode(enc, "replace"))
    where = "、".join(OUTPUT_DIRS)
    try:
        r = subprocess.run(["git", "-C", REPO, "ls-files", "-z", "--others", "--exclude-standard", "--"]
                           + list(OUTPUT_DIRS), capture_output=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as e:
        _say("  未入版產出檢查：略過（git 無法執行：%s）" % e)
        return
    if r.returncode != 0:
        _say("  未入版產出檢查：略過（git exit %d）" % r.returncode)
        return
    paths = [p.decode("utf-8", "replace") for p in r.stdout.split(b"\0") if p]
    if not paths:
        _say("  未入版產出檢查：%s 下 git 未追蹤 0 支" % where)
        return
    _say("\n⚠ 未入版產出：%s 下有 %d 支 git 未追蹤的檔。" % (where, len(paths)))
    _say("  若是這次 regen 新產出的（例如新機型的 printer_preview_*.png／*_cover.png／machine、process json），")
    _say("  請一起入版——只 add resources/profiles 會漏掉 resources/images。")
    for p in paths[:40]:
        _say("    ?? " + p)
    if len(paths) > 40:
        _say("    …另 %d 支" % (len(paths) - 40))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC)
