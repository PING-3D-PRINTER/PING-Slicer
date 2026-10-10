"""PING profiles 參照完整性驗證（每次 embed regen 後跑）。

檢查：
1. PING.json 所有 sub_path 檔案存在；檔內 name 與清單一致
2. inherits 指向存在的 preset（且絕非空字串——坑#12：空字串=整包 vendor 載入中止）
3. process/filament 的 compatible_printers 指向存在的 machine preset
4. machine 的 default_print_profile / default_filament_profile 指向存在 preset
5. filename_format 不得以非 ASCII 開頭、'}' 後不得緊接非 ASCII
   （PlaceholderParser rule 邊界限制——中文前綴必須包進 {"X_"} 字串字面值）
6. 正式製程保守值固定為：稀疏填充加速度 5000、空駛加速度 5000、接縫 aligned
   照片磚若存在則維持獨立特調：稀疏填充加速度 10000、空駛加速度 3000、接縫 back
7. 支撐預設：FD300 全家族樹狀；FF600／FF600 3in1 普通；FF600 同進樹狀
8. 精靈同家族排列：基本款 → 同進 → 3in1 → 單料頭（單料頭最右）
9. V3.6 Classic 八機型：Marlin、無 M204／machine limits／韌體回抽／PA，回抽值符合 V2.1
"""
import hashlib
import io
import json
import math
import os
import re
import sys
# 輸出一被轉向到檔案／管線，Windows 的 Python 就退回 cp950 ⇒ 印 ✅／🔴 的那行會
# UnicodeEncodeError，**檢查早就跑完了 exit code 卻不可信**（SOP_參數入版紀律 §T 源頭修）。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

PINGDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                       "resources", "profiles", "PING")
ROOT_JSON = PINGDIR + ".json"

errors = []

# ★ 組合製程功能歸類名（Eric 0729 裁；Codex 四輪雙審可定稿）。token 抽取＝與 Tab.cpp
# ping_apply_combo_filaments 同規則（@ 前、最後一個 ASCII 空白後），分類判定一律走本組，
# 不再 substring 猜名（二輪必改 9）。
# ★ 0811 改名批（Eric 兩裁合併：「棧板」→「筏層」＋拿掉 (Z0)／(Z隙)／「雙料」）
#   現行只剩 **三個 token**；另兩類雙料製程**沒有 token**（與單料/四料同形，這正是 Eric 要的統一）：
#     一般雙料（原 雙料(Z隙)）  → 「{lh}mm @…」
#     雙料筏層（原 雙料(Z隙)+棧板）→「{lh}mm_筏層 @…」（與單料的 _筏層 雙生同形）
#   ⇒ 無 token 的兩類**不能靠名字分類**，本檔一律以 renamed_from 鏈尾（PLA+PLA／ABS+ABS）判定，
#     那條鏈由產生器單一入口 combo_renamed_from() 寫入、且下方會逐支 exact 驗。
COMBO_CAT_EASY,   COMBO_CAT_PVA      = "易拆",      "易拆水溶"
COMBO_CAT_EASYPAL                    = "易拆+筏層"
# 🆕 2026-09-19（Eric 兩輪 grill 裁，牌 c-0919-ETR-01）：第四個易拆家族 token「易拆樹狀」。
#   = 同口徑「易拆」只換 support_type=tree(auto)／support_style=tree_hybrid／支撐＋支撐面速度 50。
#   列入 COMBO_TOKENS ⇒ 跨層護欄（兩張連動表鍵集合／token 契約／覆蓋同組機型×口徑／G2 配料屬性）自動涵蓋；
#   它**沒有舊名**（新品）⇒ renamed_from 回溯鏈改驗「不得存在」，專屬斷言見「易拆樹狀族」區塊。
COMBO_CAT_EASYTREE                   = "易拆樹狀"
EASY_TREE_ALLOWED_DIFF = {"name", "setting_id", "renamed_from", "support_type", "support_style",
                          "support_speed", "support_interface_speed"}
COMBO_TOKENS = {COMBO_CAT_EASY, COMBO_CAT_PVA, COMBO_CAT_EASYPAL, COMBO_CAT_EASYTREE}
# 照片磚系線材（冷卻降速 0910 的例外名單）；與 embed_params 的 PT_FIL_PLA／PT_FIL_PLA_FD 同值，
# 兩支檔各自宣告＝既有慣例（verify 刻意不 import 產生器，避免驗證對象驗自己）。
PT_FIL_PLA_V, PT_FIL_PLA_FD_V = "PING PLA(照片磚)", "PING PLA(照片磚 FD300)"
PT_FIL_PLA_FDHF_V = "PING PLA(照片磚 FD高流量)"   # 🆕 0907 #99 丙・雙料高流量家族專用支（0912 移植進出貨線）
# 🆕 0907 #153（Eric 四裁）：3in1 兩支改名成「… - 高流量噴頭」。名字集中在這裡，
#   下方所有 exact 比對一律引用這兩個常數——SOP §N 第 3 條（內聯複製的條件要改成共用變數）。
#   ⚠ 新名仍含 `(3in1)` 與 `高流量` ⇒ is_hf／PA 豁免／清料 120 三處子字串判定**都還命中**，
#     但那是實查過的結果、不是靠巧合（§N 第 2 條：不依賴「新名碰巧也含某關鍵字」）。
TI_FIL_PLA = "PING PLA(3in1) - 高流量噴頭"
TI_FIL_SUP = "PING SupPLA(3in1) - 高流量噴頭"
TI_FIL_OLD = {"PING PLA(3in1)", "PING SupPLA(3in1)"}   # 改名前的名字，不得復活成 preset 名
COMBO_OLD_TOKENS = {"PLA+SUP", "PLA+PVA", "ABS+SUP", "PLA+PLA", "ABS+ABS"}
# 0730 五類名（**歷史值**，只用於 renamed_from 回溯鏈驗證）
COMBO_0730 = {"PLA+SUP": "易拆(Z0)", "PLA+PVA": "易拆(Z0)水溶", "ABS+SUP": "易拆(Z0)+棧板",
              "PLA+PLA": "雙料(Z隙)", "ABS+ABS": "雙料(Z隙)+棧板"}
# 無 token 兩類的內部代號（僅供錯誤訊息可讀；不是製程名的一部分）
CAT_PLAIN_DUAL, CAT_RAFT_DUAL = "(無token・一般雙料)", "(無token・雙料筏層)"
NEW2OLDCB = {COMBO_CAT_EASY: "PLA+SUP", COMBO_CAT_PVA: "PLA+PVA", COMBO_CAT_EASYPAL: "ABS+SUP",
             CAT_PLAIN_DUAL: "PLA+PLA", CAT_RAFT_DUAL: "ABS+ABS"}

def cxx_unescape(s):
    """把 C++ 字面裡的 \\xNN 逸出序列解回 UTF-8 字串（房規：CJK 字面走逸出）。"""
    return re.sub(r'(?:\\x[0-9A-Fa-f]{2})+',
                  lambda m: bytes(int(h, 16) for h in re.findall(r'\\x([0-9A-Fa-f]{2})', m.group(0)))
                            .decode("utf-8", "replace"), s)

def cxx_escape(s):
    """把字串裡的非 ASCII 字元轉成 C++ \\xNN 逸出形（用於比對 C++ 原始碼字面）。"""
    return "".join(c if ord(c) < 128 else "".join("\\x%02X" % b for b in c.encode("utf-8")) for c in s)

def combo_token(name):
    """回傳製程名的組合 token（五新名之一）；非組合製程回 None。"""
    at = name.find("@")
    if at <= 0:
        return None
    head = name[:at].rstrip()
    sp = head.rfind(" ")
    if sp < 0:
        return None
    tok = head[sp + 1:]
    return tok if tok in COMBO_TOKENS else None

def strip_cxx_comments(src):
    """剝掉 C++ 的 // 與 /* */ 註解，供「原始碼字面」型跨層護欄比對用。
    不處理字串字面值裡的 // （本檔用途只比對程式碼 token，誤剝也只會讓護欄更嚴、不會放水）。"""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(src[i]); i += 1
    return "".join(out)

def combo_kind(name, d):
    """回傳製程的功能歸類（三 token 之一，或兩個無 token 雙料代號）；非組合製程回 None。
    0811 起一般雙料／雙料筏層沒有 token（與單料/四料同形）⇒ 改以 renamed_from 鏈尾判定：
    只有這兩類的鏈會以「PLA+PLA @…」／「ABS+ABS @…」開頭（產生器唯一入口寫入、下方逐支 exact 驗）。

    🔴 **3in1 一律回 None（Eric 2026-08-17 改名批後補）**：3in1 是**機器變體**（FF600/FF800 3in1），
    不是五種雙料組合製程之一，走的是 `ff_extra` 範本複製、不是雙料 combo 產線。
    改名前它 head 無 token ⇒ 本函式本來就回 None；改名補上「易拆」後會誤落進雙料組合的三項檢查
    （renamed_from 0730 兩段回溯鏈／介面間距 0.1／易拆支數斷言），而它三項都本來就不適用：
      ①它沒有 0730 那段改名史 ②它的介面間距是**實心 0**（ping-slicer 明訂，非 0.1）③它不算在 18 支內。
    ⇒ 這裡明確排除＝**維持改名前的既有行為**，不是為了讓 verify 變綠而放寬標準。
    （3in1 的支撐 Z 間距＝0 由檔尾「檢查 14」獨立把關，不依賴本函式。）"""
    if "3in1" in name:
        return None
    t = combo_token(name)
    if t:
        return t
    rf = d.get("renamed_from")
    if isinstance(rf, str):
        first = rf.split(";")[0]
        if " PLA+PLA @" in first:
            return CAT_PLAIN_DUAL
        if " ABS+ABS @" in first:
            return CAT_RAFT_DUAL
    return None

# 期望配對 baseline（值＝dump 自 Tab.cpp:177-247 現值；四輪修訂 C——配錯在冊線材也要紅）
EXPECTED_COMBO_MAP = {
    COMBO_CAT_EASY:    ("PING PLA - 210", "PING SupPLA"),
    COMBO_CAT_PVA:     ("PING PLA - 210", "PING PVA"),
    COMBO_CAT_EASYPAL: ("PING ABS", "PING SupABS"),
    COMBO_CAT_EASYTREE: ("PING PLA - 210", "PING SupPLA"),              # 0919：同易拆配料
}
EXPECTED_COMBO_MAP_HF = {
    COMBO_CAT_EASY:    ("PING PLA - 高流量噴頭", "PING SupPLA - 高流量噴頭"),
    COMBO_CAT_PVA:     ("PING PLA - 高流量噴頭", "PING PVA"),
    COMBO_CAT_EASYPAL: ("PING ABS", "PING SupABS"),
    COMBO_CAT_EASYTREE: ("PING PLA - 高流量噴頭", "PING SupPLA - 高流量噴頭"),  # 0919：同易拆配料
}
# 0811：兩個雙料鍵已從 Tab.cpp 的 map 消失——一般雙料改走「空 token 分支」（Eric 裁「雙料機無
# token 就當 PLA+PLA」），雙料筏層改走既有「_筏層 分支」（全槽 ABS）。下方跨層護欄另有專查。
EXPECTED_PLAIN_DUAL    = ("PING PLA - 220", "PING PLA - 220")
EXPECTED_PLAIN_DUAL_HF = ("PING PLA - 高流量噴頭", "PING PLA - 高流量噴頭")
# #39 筏層建議對話框：2026-08-14 批2 R4 依 Eric 0813 裁4 **整支退役**——批2 的材料→製程自動收斂
# （ping_converge_process）已涵蓋同一件事，對話框成了「先問一次、然後系統自己也會做」的重複動作。
# ⇒ 原 EXPECTED_P39 與其跨層護欄一併移除，改由下方 G3-c 反向斷言「它確實不在了、且不得復活」。

CLASSIC = {
    "EDU 200":  {"nozzle":"0.6", "retract":"4", "speed":"30", "height":"200", "bed":False},
    "PING 200": {"nozzle":"0.4", "retract":"2", "speed":"20", "height":"200", "bed":True},
    "PING 270": {"nozzle":"0.4", "retract":"6", "speed":"60", "height":"300", "bed":True},
    "PING 300+":{"nozzle":"0.4", "retract":"2", "speed":"20", "height":"270", "bed":True},
    "DUAL 300": {"nozzle":"0.4", "retract":"2", "speed":"20", "height":"270", "bed":True},
    "DUAL 450": {"nozzle":"0.6", "retract":"3", "speed":"30", "height":"600", "bed":True},
    "DUAL 600": {"nozzle":"0.6", "retract":"3", "speed":"30", "height":"580", "bed":True},
    "DUAL 800": {"nozzle":"0.6", "retract":"3", "speed":"30", "height":"580", "bed":True},
}

# Classic DUAL 變體（Eric 2026-07-26 裁「只針對 DUAL 四台」補同進/單料頭、口徑照 FD 對應機；
# 2026-07-27 實作）：機名 "{model} {變體} {nz} nozzle"。參數繼承照 FD、Marlin 隔離照舊
# ⇒ 變體機必須吃到與本體同一組 Classic 斷言（marlin／回抽 Classic 值／無 Klipper 指令）。
CLASSIC_VARIANT_NOZZLES = {
    "DUAL 300": ("0.25", "0.4", "0.6"),
    "DUAL 450": ("0.4", "0.6", "1.0"),
    "DUAL 600": ("0.4", "0.6", "1.0"),
    "DUAL 800": ("0.4", "0.6", "1.0"),
}
CLASSIC_VARIANTS = ("同進", "單料頭")

_CLASSIC_MACHINE_RE = re.compile(r"^(.+?) ([0-9.]+) nozzle$")


def classic_model_for_machine(name):
    """這台機是不是前代 Classic（Marlin）機。判準＝它是哪一台，不是它裝幾號噴嘴。
    🔴 2026-09-11 實抓（牌 c-0911-NZ-01）：原本是拿 CLASSIC_VARIANT_NOZZLES 窮舉口徑去比對整個機名。
       NZ 棒替 DUAL 450/600/800 加了 同進(0.25)／單料頭(0.2) 之後，那些新變體機在這裡回 None
       ⇒ 被當成 Klipper 機，verify 對它要求 Klipper 動力學與 jerk 7（實測 54+36 項紅）；
       而 embed_params 那邊用 ^(EDU|DUAL|PING 2|PING 3) 正則**正確地**跳過它。
       **兩邊對「什麼是 Classic」的判準不一致，而錯的是這裡**——同 0911 ABS 收縮守衛那一型：
       守衛比引擎嚴，於是擋掉合法的東西。規則要貼著真實約束寫：一台機是不是 Marlin 前代機，
       取決於機型（含同進／單料頭變體）本身，跟口徑無關。
    ⚠ CLASSIC_VARIANT_NOZZLES 沒有廢除，但**只留給「哪些變體機必須存在」那個斷言**（見檔尾
      〈Classic 變體完整性〉）——那個問題才真的需要窮舉口徑。**同一張表被兩個目的共用**，
      其中一個需要窮舉、另一個不需要，正是這次靜默失效的成因：表過期時，
      需要窮舉的那個目的會大聲報錯，不需要窮舉的那個目的則安靜地答錯。
    （原 classic_base_nozzles() 只被本函式呼叫，改判準後即成孤兒，一併移除；
      它原本的註記＝DUAL base 0901 補齊後與變體共用同一組口徑，已併入上面的表註解。）
    """
    m = _CLASSIC_MACHINE_RE.match(name)
    if not m:
        return None
    stem = m.group(1)
    for model in CLASSIC:
        if stem == model or any(stem == f"{model} {v}" for v in CLASSIC_VARIANTS):
            return model
    return None


# 不掛 PING 品牌前綴的自家線材＋高速製程（Eric 2026-10-08，牌 c-1008-ACF-01；與 embed_params.py 同名同值各一份）
_NON_BRAND_FIL = {"ABS-CF"}
_FAST_PROC_TOKEN = "mm 高速 @"          # 0.2mm 高速 @FD600 Pro 單料頭 (0.4)

def err(msg):
    errors.append(msg)


root = json.load(io.open(ROOT_JSON, encoding="utf-8"))
presets = {}   # name -> (kind, dict)

for kind, key in (("machine_model", "machine_model_list"), ("machine", "machine_list"),
                  ("process", "process_list"), ("filament", "filament_list")):
    for it in root.get(key, []):
        p = os.path.join(PINGDIR, it["sub_path"].replace("/", os.sep))
        if not os.path.isfile(p):
            err(f"[missing] {key}: {it['sub_path']}")
            continue
        d = json.load(io.open(p, encoding="utf-8"))
        if d.get("name") != it["name"]:
            err(f"[name mismatch] {it['sub_path']}: json={d.get('name')!r} list={it['name']!r}")
        presets[it["name"]] = (kind, d)

machines = {n for n, (k, _) in presets.items() if k == "machine"}


def expected_support_type(printer_name):
    # 0722 七裁後全池預設普通(自動)；照片磚特調維持 tree(auto) 不查。保留函式供 wizard 一致性檢查
    if "照片磚" in printer_name:
        return None
    if printer_name.startswith(("FD300", "FF600")):
        return "normal(auto)"
    return None

for name, (kind, d) in presets.items():
    if kind == "machine_model":
        continue
    inh = d.get("inherits")
    if inh is not None:
        if inh == "":
            err(f"[inherits EMPTY — fatal 坑#12] {name}")
        elif inh not in presets:
            err(f"[inherits missing] {name} -> {inh!r}")
    for cp in d.get("compatible_printers", []) or []:
        if cp not in machines:
            err(f"[compatible_printers missing] {name} -> {cp!r}")
    if kind == "machine":
        dpp = d.get("default_print_profile")
        if dpp and dpp not in presets:
            err(f"[default_print_profile missing] {name} -> {dpp!r}")
        for f in d.get("default_filament_profile", []) or []:
            if f not in presets:
                err(f"[default_filament_profile missing] {name} -> {f!r}")
        # ★ 檢查 12（Eric 2026-08-16 裁・槽位預設護欄）——四條全部是「已經是這樣、把它釘住」，
        #   不是新行為。動機：這些預設散落在 def_fil_*／ff_extra 範本／照片磚範本三條產線，
        #   之前被改掉都是靜默的（0813 back-fill 事故即為一例）。
        _dfp = d.get("default_filament_profile") or []
        if len(_dfp) >= 2 and "3in1" in name:
            # 12-a 3in1 第二槽＝SupPLA(3in1) 系（3in1＝前三主體＋第四支撐的硬體，支撐支不可換成別支）
            # 🔴 0907 #153 改名連坐：這裡原本硬寫 "PING SupPLA(3in1)"，改名後會直接紅。
            #    名字集中在 TI_FIL_SUP 一處，不再散落（SOP §N 第 3 條：內聯複製的條件要改成共用變數）。
            if _dfp[1] != TI_FIL_SUP:
                err(f"[3in1 第二槽必為 {TI_FIL_SUP}] {name} -> {_dfp[1]!r}")
        elif len(_dfp) == 2 and name.startswith(("FD", "DUAL")):
            # 12-b 雙料機第二槽＝SupPLA 系（各線用各自變體：基礎／高流量噴頭／Classic）
            if "SupPLA" not in _dfp[1]:
                err(f"[雙料機第二槽必為 SupPLA 系] {name} -> {_dfp[1]!r}")
        if name.startswith(("FF600", "FF800")) and not any(
                t in name for t in ("同進", "3in1", "照片磚")):
            # 12-c 四料本體機四槽＝高流量噴頭支（0816 裁：分開進＝各噴頭獨立、不吃同進支）
            if _dfp and set(_dfp) != {"PING PLA - 高流量噴頭"}:
                err(f"[四料本體機四槽必為高流量噴頭支] {name} -> {sorted(set(_dfp))!r}")
        if "同進照片磚" in name and name.startswith(("FF600", "FF800")):
            # 12-d 照片磚機＝照片磚專用支（0816 裁「甲」＝照片磚維持舊值、不吃流量 50）
            if _dfp and set(_dfp) != {"PING PLA(照片磚)"}:
                err(f"[FF 照片磚機必為照片磚專用支] {name} -> {sorted(set(_dfp))!r}")
        # 🆕 檢查 10-b（Eric 2026-09-10 裁「Z 抬升＝口徑」；2026-09-11 移植進出貨線 c-0911-MCH-01）：
        #   Fast 一般機的 z_hop＝該機口徑（0.6→0.6、1.0→1），0.4／0.25／0.2 維持 0.4。
        #   ⛔ 照片磚機與前代 Classic 機不套（Classic 由 emit_classic 成組給 0.5；出貨線專屬排除，
        #      理由見 embed_params 4b-4c 註解——Eric 那條裁定寫「一般機」時開發線根本沒有 Classic）。
        #   ⚠ 本條 2026-09-11 補上：移植當天只做了產生器規則、**漏了守衛**，反向測試（把 0.6 機改回 0.4）
        #      verify 照樣 exit 0 才抓到。規則三件套＝產生器＋守衛＋反向測試，缺一件等於沒做。
        _zh_nz = str((d.get("printer_variant") or "")).strip()
        if ("machine_max_acceleration_x" in d and "照片磚" not in name
                and not classic_model_for_machine(name) and _zh_nz):
            _zh_want = {"0.6": ["0.6"], "1.0": ["1"], "1": ["1"]}.get(_zh_nz, ["0.4"])
            if d.get("z_hop") != _zh_want:
                err(f"[Z 抬升＝口徑 0910] {name}: z_hop={d.get('z_hop')!r}, expected {_zh_want!r}")
        # 🆕 檢查 10-c（Eric 2026-09-29 裁 Q3 甲，開發中清單 #33，牌 c-0929-NOP-03）：
        #   機型「支援空氣過濾」「支援控制列印設備內部溫度」一律明寫 0——PING 沒有可控排風扇也沒有艙溫加熱
        #   （M106 P3 會打到主冷卻風扇、M141／M191 機台不認得）。引擎預設是 true ⇒ 缺鍵＝1，缺鍵也算錯。
        #   Classic 一起：Eric 裁的範圍是 149 支全部。產生器＝embed_params 4b-4d。
        if d.get("instantiation") == "true":
            for key in ("support_air_filtration", "support_chamber_temp_control"):
                if d.get(key) != "0":
                    err(f"[機型支援排風扇／艙溫＝關 0929] {name}: {key}={d.get(key)!r}, expected '0'")
        # 檢查 10（Eric 2026-07-17 裁「全機隊」）：機器動力學＝Klipper 實值（時間預估校正）；
        # DL1016 與 Classic 前代機跳過
        if ("machine_max_acceleration_x" in d and "DL1016" not in name
                and not classic_model_for_machine(name)):
            V, A, J = ("200", "1500", "56") if "FF" in name else ("400", "5000", "7")
            for key, value in (("machine_max_speed_x", [V, V]), ("machine_max_speed_y", [V, V]),
                               ("machine_max_acceleration_x", [A, A]), ("machine_max_acceleration_y", [A, A]),
                               ("machine_max_acceleration_extruding", [A, A]),
                               ("machine_max_acceleration_travel", [A, A]),
                               ("machine_max_acceleration_retracting", [A, A]),
                               ("machine_max_jerk_x", [J, J]), ("machine_max_jerk_y", [J, J])):
                if d.get(key) != value:
                    err(f"[機器動力學 Klipper 實值] {name}: {key}={d.get(key)!r}, expected {value!r}")
        classic_model = classic_model_for_machine(name)
        if classic_model:
            spec = CLASSIC[classic_model]
            expected = {
                "gcode_flavor":"marlin", "emit_machine_limits_to_gcode":"0",
                "use_firmware_retraction":"0", "printable_height":spec["height"],
            }
            for key, value in expected.items():
                if d.get(key) != value:
                    err(f"[Classic machine] {name}: {key}={d.get(key)!r}, expected {value!r}")
            for key, value in (("retraction_length", spec["retract"]),
                               ("retraction_speed", spec["speed"]),
                               ("deretraction_speed", spec["speed"])):
                values = d.get(key, []) or []
                if not values or any(v != value for v in values):
                    err(f"[Classic retraction] {name}: {key}={values!r}, expected all {value!r}")
            # 🔴 擠出模式與層變 G92 E0 是綁在一起的（`libslic3r/Print.cpp:1602-1620`，PrusaSlicer 來的檢查）：
            #    絕對擠出 + 層變有 G92 E0 → 切片直接被擋；相對擠出 + Marlin flavor + 沒有 G92 E0 → 也被擋。
            #    2026-09-01 Eric 實機撞到前者（Classic 32 支從 2026-07-15 加進來就切不了，一直沒人選到）。
            #    ⇒ 在這裡把「切片器的規則」寫成斷言，下次再有人把它設錯就當場有聲擋下。
            layer_reset = "G92 E0" in (d.get("before_layer_change_gcode") or "") \
                or "G92 E0" in (d.get("layer_change_gcode") or "")
            rel_e = d.get("use_relative_e_distances")
            if rel_e != "1":
                err(f"[Classic 擠出模式] {name}: use_relative_e_distances={rel_e!r}, expected '1'"
                    f"（絕對擠出配層變 G92 E0 會被 Print.cpp 擋下切片）")
            if not layer_reset:
                err(f"[Classic 擠出模式] {name}: 層變 G-code 缺 G92 E0"
                    f"（相對擠出＋Marlin 反而要求它存在，防浮點精度流失）")
            start = d.get("machine_start_gcode", "")
            if "SET_RETRACTION" in start or "M204" in start:
                err(f"[Classic Klipper/acceleration command] {name}: machine_start_gcode")
            # M6051/M6052＝Klipper 巨集，前代 Marlin 韌體不認——任何 Classic 機（本體/變體）都不得帶。
            # M6050 舊格式只准出現在「同進」變體（該規則在下方變體完整性區檢查）。
            if "M6051" in start or "M6052" in start:
                err(f"[Classic Klipper mix command] {name}: machine_start_gcode 帶 M6051/M6052")
            # 0729 Klipper #128（Eric 令）：赤兔板無 T 工具語意——Classic 出檔禁 T0/T1。
            # start/end 溫度行不帶 T、無裸 T 行；雙料本體換刀＝change_filament_gcode M6050 模板、
            # 單料模板必空（單料引擎本就不出換刀）。
            _endg = d.get("machine_end_gcode", "")
            for _gk, _gv in (("machine_start_gcode", start), ("machine_end_gcode", _endg)):
                if re.search(r"M10[49][^\n]*\bT\d", _gv):
                    err(f"[Classic no-T 溫度 0729] {name}: {_gk} 帶 M104/M109 T…")
                if re.search(r"(?m)^\s*T\d", _gv):
                    err(f"[Classic no-T 換刀 0729] {name}: {_gk} 帶裸 T 行")
            _cfg = d.get("change_filament_gcode", "")
            # 模板只屬雙料「本體」；同進/單料頭變體＝SEMM=0 單一出料、模板必空（下方變體區另鎖）
            if name.startswith("DUAL") and "同進" not in name and "單料頭" not in name:
                for _tok in ("{if next_extruder == 0}", "M6050 S1 P0", "M6050 S0 P0"):
                    if _tok not in _cfg:
                        err(f"[Classic 雙料 M6050 換刀模板 0729] {name}: change_filament_gcode 缺 {_tok!r}")
                if re.search(r"(?m)^\s*T\d", _cfg):
                    err(f"[Classic 雙料 M6050 換刀模板 0729] {name}: change_filament_gcode 帶裸 T 行")
            elif _cfg:
                err(f"[Classic 單料 change_filament 應空 0729] {name}: {_cfg[:40]!r}")
            if not spec["bed"] and any(cmd in start for cmd in ("M140", "M190")):
                err(f"[EDU heated bed command] {name}: machine_start_gcode")
    if kind == "process":
        if d.get("instantiation") == "true":
            compatible = d.get("compatible_printers", []) or []
            is_classic = any(classic_model_for_machine(p) for p in compatible)
            expected = ({
                # Marlin 隔離維持：加速度全 0、不送 machine limits（與本次「套新工藝」無關）
                "default_acceleration": "0",
                "sparse_infill_acceleration": "0",
                "travel_acceleration": "0",
                # 🔴 Eric 2026-08-26 改裁：接縫全庫 aligned → back；Classic 承 0725「跟進 F 系」一併改。
                "seam_position": "back",
                # ★ Classic 套 F 系新工藝（Eric 2026-07-25 裁）：爬坡品質＋支撐臨界角改與全庫同值
                #   （原本這裡是豁免的，由 emit_classic 還原成 關/1/30；本裁取消該還原）
                # 🔴 2026-08-16 Eric 裁「A+C」：F 系關降速，Classic 依「跟進」精神一起關
                "enable_overhang_speed": "0",
                "overhang_1_4_speed": "50",
                "overhang_2_4_speed": "50",
                "overhang_3_4_speed": "25",
                "overhang_4_4_speed": "25",   # 0816 C：10 → 25（Classic 跟進 F 系）
                "bridge_flow": "0.95",
                "support_threshold_angle": "35",
            } if is_classic else {
                "sparse_infill_acceleration": "10000",
                "travel_acceleration": "3000",
                # 2026-07-20 Eric 裁（照片磚線 a06fed2c）：接縫預設 背面→對齊（V 溝配套）
                "seam_position": "aligned",
                # 支撐參數統一（Eric 2026-07-25 裁）：照片磚支撐豁免取消，角度與全庫同 35
                #（爬坡品質＝速度類，仍維持照片磚特調豁免）
                "support_threshold_angle": "35",
                # 支撐開關關閉（Eric 2026-07-25 追裁）：照片磚不需要支撐，開關直接關，
                # 不再停留在「開著但平貼床永遠不生成」的誤導狀態。
                "enable_support": "0",
                # 🆕 2026-09-12 Eric 裁「當正式預設」（牌 c-0912-PTI-08）：絨毛表面＋速度 ×2。
                #    這裡只查「不隨機型變的那幾個」——normalize_fast_speed 先把 outer/sparse
                #    壓成 60/100、首層範本一律 40、bridge 一律 50，再 ×2 ⇒ 全庫同值。
                #    inner/top/solid/gap/skirt 的範本值因機型而異（FD 60／FF 75~100），
                #    逐機型寫死只會變成第二份範本，改由下面的「≥120」下限守。
                "fuzzy_skin": "external",
                "fuzzy_skin_thickness": "0.3",
                "fuzzy_skin_point_distance": "0.8",
                "outer_wall_speed": "120",
                "sparse_infill_speed": "200",
                # 首層不跟著 ×2：Eric 2026-09-12 晚二裁 80 → 60（牌 c-0912-PTI-09）
                "initial_layer_speed": "60",
                "initial_layer_infill_speed": "60",
                "bridge_speed": "100",
            } if "照片磚" in name else {
                "sparse_infill_acceleration": "5000",
                "travel_acceleration": "5000",
                # 🔴 Eric 2026-08-26 改裁：接縫 aligned → back（取代 0715）。照片磚維持 aligned＝
                #    它有自己的 0720 裁定「背面→對齊（V 溝配套）」，見上一個分支。
                "seam_position": "back",
                # 爬坡品質（Eric 2026-07-24）：懸空降速四段＋橋接流量；照片磚/Classic 豁免
                # ⚠ 單位＝mm/s（裸數字；ratio_over=outer_wall_speed，要用 % 須帶符號）
                # 🔴 2026-08-16 Eric 裁「A+C」推翻降速部分（四段值留著當備援）：
                #    A＝開關全庫關；C＝最慢階 10 → 25（不取 30：會比 50% 段的 25 更快＝順序反）。
                #    ⚠ 要放寬回 "1"／"10" 必須是 **Eric 新的一次裁定**，不是實作者順手改這行。
                "enable_overhang_speed": "0",
                "overhang_1_4_speed": "50",
                "overhang_2_4_speed": "50",
                "overhang_3_4_speed": "25",
                "overhang_4_4_speed": "25",
                "bridge_flow": "0.95",
                # 支撐臨界角 35（Eric 2026-07-25 裁）
                # 🔴 Orca 基準（= Cura/V2.1 的 55）；兩線相反 Orca = 90 − Cura，見 ping-slicer/orca-sync.md
                # ⚠ 出貨線從未被 07-24 的 60 汙染（本規則首次新增，原全庫 30＝V3.0 底稿值）
                "support_threshold_angle": "35",
            })
            # PLA+PVA→「易拆(Z0)水溶」專屬製程（Eric 2026-07-25 裁「出」；0730 改名批 token 判定）：
            # 支撐角 50 ＝ 案值 Cura 40 換算（Orca ＝ 90 − Cura）＝比全庫 35 多支撐＝水溶支撐合理特例。
            _ctok = combo_token(name)
            if _ctok == COMBO_CAT_PVA:
                expected["support_threshold_angle"] = "50"
            # 高流量製程組（Eric 2026-07-30 裁・客戶建誌 FF800 同進實測移植）：加速度逐項取保守
            # ＝travel/sparse 2000（min(客戶 2000, 現值 5000)）⇒ 全庫 5000 斷言對此組豁免；
            # 完整定案值 exact 斷言在檔尾「高流量製程組」區塊（含範圍鎖）。
            if " 高流量 @" in name:
                expected["sparse_infill_acceleration"] = "2000"
                expected["travel_acceleration"] = "2000"
            # PA-CF 專屬製程（Eric 2026-08-26 裁）：接縫固定藏背面＝材料特例。
            # 全庫 aligned 的斷言對這 6 支改要求 back，**閘門沒關**——寫錯值一樣會被抓。
            if " PA-CF " in name or " PA-CF@" in name:
                expected["seam_position"] = "back"
            # jerk：F 系對齊機器上限（Eric 2026-07-20 裁）＝FD/FP 7、FF 40；Classic 維持 0（Marlin 停用）
            expected["default_jerk"] = "0" if is_classic else ("40" if "@FF" in name else "7")
            for key, value in expected.items():
                if d.get(key) != value:
                    err(f"[process safety default] {name}: {key}={d.get(key)!r}, expected {value!r}")
            # 檢查 7/8（Eric 2026-07-17 裁）：支撐幾何口徑連動＋洗料塔寬 25（照片磚不在 release 線）
            m_nz = re.search(r"\(([\d.]+)\)\s*$", name)
            if m_nz:   # 2026-07-25 Eric 裁「支撐參數全部統一」：照片磚不再豁免（原 and "照片磚" not in name）
                nz_v = float(m_nz.group(1))
                # 線距 2026-07-22 裁 ×9（密度 10%＝Cura 全庫等效；蓋 7/17 ×8）
                #   PLA+PVA 專屬製程 ×19（案值密度 5%）
                # 樹狀 2026-07-25 裁保守配方：分支直徑 ×10→×12（引擎上限 10）、新增分支距離 ×6
                _spacing = ("%g" % round(nz_v * 19, 2)) if _ctok == COMBO_CAT_PVA else ("%g" % (nz_v * 9))
                # ★ Classic 前代改為**跟進**新工藝（Eric 2026-07-25 裁）：樹狀配方一併查。
                #   口徑安全＝CLASSIC_SPECS 每台 nozzle == src_nozzle，母檔算的口徑連動值直接成立。
                _cls_proc = any(t in name for t in ("@DUAL", "@PING ", "@EDU"))
                _geo = [("tree_support_branch_diameter", "%g" % min(nz_v * 12, 10.0)),
                        ("tree_support_branch_distance", "%g" % (nz_v * 6)),
                        ("support_base_pattern_spacing", _spacing)]
                for key, value in _geo:
                    if d.get(key) != value:
                        err(f"[support geometry 口徑連動] {name}: {key}={d.get(key)!r}, expected {value!r}")
                # 頂部接觸面間距（Eric 2026-08-04 裁「70% 密度等效」蓋 0714「一律 0.1」）：
                # 一般支撐＝口徑×3/7 取兩位（密度＝線寬/(間距+線寬)⇒70%；0.25→0.11/0.4→0.17/
                # 0.6→0.26＝Eric 截圖錨值/1.0→0.43）；易拆家族既值不動（介面密＝表面品質、不影響拆）：
                # PLA+SUP/PVA 0.1、ABS+SUP 黃金 0.04、3in1 實心 0。Classic 由 Fast 複製繼承＝同查。
                # ⚠ **3in1 必須排在最前面**（Eric 2026-08-17 改名批後調序）：它是更 specific 的規則。
                #   原本排在 COMBO_CAT_EASY 後面能運作，是**依賴「3in1 名字沒有易拆 token」這個巧合**；
                #   0817 把 3in1 改名補上「易拆」後，_ctok 變成 COMBO_CAT_EASY ⇒ 被前一條攔截、
                #   實心 0 被誤判成應為 0.1（實測 4 支紅）。判準排序不可依賴命名巧合。
                if "3in1" in name:
                    _sis = "0"
                elif _ctok == COMBO_CAT_EASYPAL:
                    _sis = "0.04"
                elif _ctok in (COMBO_CAT_EASY, COMBO_CAT_PVA, COMBO_CAT_EASYTREE):
                    _sis = "0.1"
                elif "@DUAL" in name and "同進" not in name and "單料頭" not in name:
                    # Classic 標準雙料（DUAL 300/450/600/800 本體）＝從 Fast 易拆(Z0) 母檔複製、
                    # 槽 2 裝 Classic SupPLA（0726 裁「Classic 用它實際的模式：雙料→易拆」）⇒ 名稱雖無
                    # 易拆 token，仍屬易拆家族＝介面 0.1 不套 70% 等效。
                    # DUAL 同進/單料頭變體＝單一出料同料＝一般家族 → 走下方口徑連動（Classic 單料同）。
                    _sis = "0.1"
                else:
                    _sis = "%g" % round(nz_v * 3 / 7, 2)
                if d.get("support_interface_spacing") != _sis:
                    err(f"[支撐介面 70% 等效 0804] {name}: support_interface_spacing={d.get('support_interface_spacing')!r}, expected {_sis!r}")
                # 支撐首層密度（同鍵服務 raft 首層）＝10% 全庫（0804；主體類規則全庫套＝0722 ×9 先例，
                # 含 3in1 範本 30%→10%）；raft 機種（ABS 系/棧板 raft_layers≥1）＝貼床抓床維持 100%。
                _rfd = "100%" if str(d.get("raft_layers", "0")) != "0" else "10%"
                if d.get("raft_first_layer_density") != _rfd:
                    err(f"[支撐首層密度 0804] {name}: raft_first_layer_density={d.get('raft_first_layer_density')!r}, expected {_rfd!r}")
                # 普通支撐配方（Eric 2026-07-22 七裁；行為四項同日二裁擴及易拆）
                expected_recipe = [("support_type", "normal(auto)"),
                                   ("independent_support_layer_height", "0"),
                                   ("support_style", "snug"),
                                   ("support_base_pattern", "rectilinear")]
                # PA-CF 樹狀版（Eric 2026-08-26 追裁「增加一個製程參數，是樹狀支撐」）：
                # 只有帶「樹狀」token 的 3 支換支撐類型；PA-CF 一般版仍走普通支撐配方（Q1 甲）。
                # style 必須明寫 default：ConfigManipulation.cpp:476-479 把「snug＋tree」判為非法配對、
                # 自動退回 default（SupportParameters.hpp:180-183 ⇒ smsTreeOrganic＝Eric 實印那組），
                # 不明寫的話使用者一開檔就吃到「設定已被修正」提示。
                if " PA-CF 樹狀 @" in name:
                    expected_recipe = [("support_type", "tree(auto)"),
                                       ("independent_support_layer_height", "0"),
                                       ("support_style", "default"),
                                       ("support_base_pattern", "rectilinear")]
                # 易拆樹狀（Eric 2026-09-19 改裁 Q2＝乙＝有機樹）：style 明寫 organic（UI 才顯示「有機樹」；
                # default 行為相同但 UI 顯示「預設 (網格/有機)」＝Eric 0919 GUI 實看抓到）
                # （同 PA-CF 樹狀；吃 _organic 防呆值）。寫成 tree_hybrid／snug 一律紅。
                elif _ctok == COMBO_CAT_EASYTREE:
                    expected_recipe = [("support_type", "tree(auto)"),
                                       ("independent_support_layer_height", "0"),
                                       ("support_style", "organic"),
                                       ("support_base_pattern", "rectilinear")]
                # Classic 前代（@DUAL/@PING/@EDU）＝Fast 母檔複製：雙料複製自 PLA+SUP＝易拆幾何 0.45 正確，XY 不以一般律查
                is_classic = any(t in name for t in ("@DUAL", "@PING ", "@EDU"))
                # 水溶＝易拆類（PVA 與 PLA 不相熔）⇒ XY 走易拆家規 口徑×0.75；
                # 易拆(Z0)/易拆(Z0)+棧板（原 +SUP 家族）不以一般律查（易拆幾何另有 0.45 家規）；
                # 其餘（雙料類/單料/非組合）走一般 ×1。（0730 改名批：token 判定取代 substring）
                if _ctok == COMBO_CAT_PVA:
                    expected_recipe.append(("support_object_xy_distance", "%g" % round(nz_v * 0.75, 2)))
                elif _ctok not in (COMBO_CAT_EASY, COMBO_CAT_EASYPAL, COMBO_CAT_EASYTREE) and "3in1" not in name and not is_classic:
                    expected_recipe.append(("support_object_xy_distance", "%g" % round(nz_v * 1.0, 2)))
                for key, value in expected_recipe:
                    if d.get(key) != value:
                        err(f"[普通支撐配方 0722] {name}: {key}={d.get(key)!r}, expected {value!r}")
                # 樹狀支撐保守配方（Eric 2026-07-25 裁）：只在使用者手動切「混合樹」後生效，
                # 預設 normal(auto)+snug 不受影響。auto_brim 必須為 0，否則 brim_width 被引擎忽略
                #（TreeSupport.cpp:2068）。_organic 兩鍵＝防呆（snug+樹狀會被引擎退回 default＝有機樹）：
                # 🔴 diameter_organic 2.6 是 bug 修——Print.cpp:1532 硬限 ≥2×支撐線寬，
                #    FF 系 1.0 口徑線寬 1.02 需 ≥2.04，舊值 2 會讓那 4 支勾樹狀即切片報錯。
                # wall_count 0＝Eric 2026-07-27 裁「支撐牆數改零」（UI 支撐牆數＝此鍵、普通/樹狀共用：
                # 普通支撐 0=無牆 with_sheath=false；樹狀 0=auto——0725「維持一圈」同鍵被上蓋）
                for key, value in [("tree_support_branch_angle", "30"),
                                   ("tree_support_auto_brim", "0"),
                                   ("tree_support_brim_width", "10"),
                                   ("tree_support_wall_count", "0"),
                                   ("tree_support_branch_diameter_organic", "2.6"),
                                   ("tree_support_branch_angle_organic", "40")]:
                    if d.get(key) != value:
                        err(f"[樹狀支撐保守配方 0725] {name}: {key}={d.get(key)!r}, expected {value!r}")
                # ★ Classic 前代：確認新工藝**確實有套**（Eric 2026-07-25 裁「Classic 套新工藝」，
                #   本斷言由「豁免破功」反轉為「跟進破功」——防哪天有人又把還原塞回 emit_classic）
                #   🔴 2026-08-16 Eric 裁「A+C」＋令「同步出貨線」：F 系的降速開關全庫關閉，
                #      依 0725「Classic 跟進 F 系」的精神，Classic 一起關（DUAL 800 的 0.6／1.0
                #      同樣是大口徑，滾沸的物理一樣成立）⇒ 本斷言的期望值同步改 "0"。
                #      ⚠ 這裡改的是「跟進的對象變了」，**不是取消跟進**——其餘四項仍照舊守著。
                if _cls_proc:
                    for key, value in (("enable_overhang_speed", "0"), ("bridge_flow", "0.95"),
                                       ("support_threshold_angle", "35"),
                                       ("tree_support_branch_angle", "30"),
                                       ("tree_support_branch_diameter", "%g" % min(nz_v * 12, 10.0))):
                        if d.get(key) != value:
                            err(f"[Classic 前代新工藝跟進破功] {name}: {key}={d.get(key)!r}, expected {value!r}")
            # 洗料塔寬：全庫 25（0717 裁）；PLA+PVA 專屬製程 45（案值＝劉勝賢現行）
            _tower = "45" if combo_token(name) == COMBO_CAT_PVA else "25"
            if "照片磚" not in name and d.get("prime_tower_width") != _tower:
                err(f"[洗料塔寬度 {_tower}] {name}: prime_tower_width={d.get('prime_tower_width')!r}")
            # 洗料塔 0729 三裁（Eric）：錐體＋頂角 30＋最快列印速度 60（0708 肋條裁定退役）
            for key, value in (("wipe_tower_wall_type", "cone"),
                               ("wipe_tower_cone_angle", "30"),
                               ("wipe_tower_max_purge_speed", "60")):
                if d.get(key) != value:
                    err(f"[洗料塔錐體 0729] {name}: {key}={d.get(key)!r}, expected {value!r}")
            # 內外牆加速度 0729（Eric「表面品質與穩定性」）：全機型 1500；Classic 維持 Marlin 隔離 0
            _wall_acc = "0" if _cls_proc else "1500"
            for key in ("outer_wall_acceleration", "inner_wall_acceleration"):
                if _FAST_PROC_TOKEN in name:
                    # 高速製程刻意放寬（Eric 1008）：只要求不超過機台加速度上限 5000
                    if not (d.get(key, "").isdigit() and 0 < int(d.get(key)) <= 5000):
                        err(f"[高速製程加速度須 ≤5000] {name}: {key}={d.get(key)!r}")
                elif d.get(key) != _wall_acc:
                    err(f"[內外牆加速度 {_wall_acc}] {name}: {key}={d.get(key)!r}")
            # 牆體列印方向固定逆時針（Eric 2026-07-30 裁）：全庫 ccw，跳過引擎的
            # reorient_perimeters（PerimeterGenerator.cpp:1424）⇒ 層間迴路方向恆一致
            if d.get("wall_direction") != "ccw":
                err(f"[牆方向固定 ccw 0730] {name}: wall_direction={d.get('wall_direction')!r}")
            # ★ 功能歸類五類值鎖（0730 改名批）
            # 🔴 **Z隙規則已於 2026-08-17 由 Eric 改裁：一般家族＝固定 0.2，不再是「一層層高」。**
            #   舊規（0730 三輪更正定的「Z隙＝一層層高」）作廢原因＝它只套得到雙料機（`combo_overrides`
            #   只在 is_dual_machine 跑），非雙料機沿用母檔值 ⇒ 全庫散成 0／0.125／0.2／0.3／0.5 五種。
            #   Eric 0817 看到「FF800 四料本體機是 0、FD300 卻是 0.3」後裁「全部改 0.2」。
            #   ⚠ 易拆 0 不變（「易拆＝沒有間隙」是命名語意本身）。全庫層級的斷言見檔尾檢查 14。
            _vtok = combo_kind(name, d)
            if _vtok:
                _lh_m = re.match(r"([\d.]+)mm", name)
                _lh_v = _lh_m.group(1) if _lh_m else None
                if _vtok in (COMBO_CAT_EASY, COMBO_CAT_PVA, COMBO_CAT_EASYPAL, COMBO_CAT_EASYTREE):
                    for zk in ("support_top_z_distance", "support_bottom_z_distance"):
                        if d.get(zk) != "0":
                            err(f"[功能歸類・易拆 Z0] {name}: {zk}={d.get(zk)!r}")
                else:
                    for zk in ("support_top_z_distance", "support_bottom_z_distance"):
                        if d.get(zk) != "0.2":
                            err(f"[功能歸類・一般 Z隙=0.2] {name}: {zk}={d.get(zk)!r}, expected '0.2'"
                                f"（Eric 2026-08-17 裁：一般家族全庫固定 0.2，取代舊規「一層層高」）")
                # 筏層層數：易拆+筏層＝3／雙料筏層＝3／其餘＝0（單料 _筏層 雙生＝3，見下方 elif）——三族皆 3。
                #   沿革：舊值「ABS 系一律 2」出自 2026-06-10 V3.0「最佳 ABS」定稿；Eric 2026-09-17 同日兩裁，
                #   前裁只改易拆+筏層（牌 c-0917-REL-01）、後裁「三族都改 3」（牌 c-0917-REL-02）——後裁蓋前裁。
                #   對應產生器＝embed_params.py combo_overrides() 的 startswith("ABS") 那行；改值要兩邊一起改。
                _raft = {COMBO_CAT_EASYPAL: "3", CAT_RAFT_DUAL: "3"}.get(_vtok, "0")
                if d.get("raft_layers") != _raft:
                    err(f"[功能歸類・筏層 raft {_raft}] {name}: raft_layers={d.get('raft_layers')!r}, expected {_raft!r}"
                        f"（三族皆 3：易拆+筏層／雙料筏層／單料 _筏層，其餘＝0；Eric 2026-09-17 同日兩裁，後裁蓋前裁）")
                # 0811 起名字裡的「筏層」與 raft_layers 必須同進退（防「改名沒改值」／「改值沒改名」）
                _name_raft = ("+筏層 @" in name) or ("_筏層 @" in name)
                if _name_raft != (_raft != "0"):
                    err(f"[功能歸類・筏層名值不一致] {name}: 名字帶筏層={_name_raft}, raft_layers={d.get('raft_layers')!r}")
                # 易拆樹狀＝0919 新品、沒有改名史 ⇒ **不得帶 renamed_from**（從「易拆」複製來的那條若沒拿掉，
                #   兩支會宣稱同一組舊名＝引擎 rename map 1:1 先到先贏，舊 3mf 可能被接到樹狀版）。
                if _vtok == COMBO_CAT_EASYTREE:
                    if "renamed_from" in d:
                        err(f"[功能歸類・易拆樹狀不得有 renamed_from] {name}: {d.get('renamed_from')!r}")
                else:
                    # renamed_from＝**分號分隔字串**、恰為兩條舊全名（① 材料對原名 ② 0730 五類名）
                    _oldcb = NEW2OLDCB[_vtok]
                    _head_new = name[:name.find("@")].rstrip()          # 例「0.2mm 易拆」「0.2mm_筏層」「0.2mm」
                    _lh_tok = "%smm" % _lh_v
                    _tail = name[name.find("@"):]                        # 「@FD300 (0.4)」
                    _rf_expect = ";".join(["%s %s %s" % (_lh_tok, _oldcb, _tail),
                                           "%s %s %s" % (_lh_tok, COMBO_0730[_oldcb], _tail)])
                    _rf = d.get("renamed_from")
                    if not isinstance(_rf, str) or _rf != _rf_expect:
                        err(f"[功能歸類・renamed_from 回溯鏈] {name}: {_rf!r}, expected {_rf_expect!r}")
            # 單料 _筏層 雙生（產生器 PALLET_OVERRIDES；combo_kind 回 None 的那一群）：raft_layers＝3。
            #   Eric 2026-09-17 同日兩裁：前裁只改易拆+筏層（本族當時維持 2）、後裁「三族都改 3」⇒ 本族同步為 3
            #   （牌 c-0917-REL-02，後裁蓋前裁）。
            #   本斷言的由來：同日反向測試實抓本族的 raft 值原本**沒有任何斷言**（手改值 verify 照綠）⇒ 補上。
            #   日後再改值：產生器 PALLET_OVERRIDES 與這裡兩邊一起改。
            elif "_筏層 @" in name and d.get("raft_layers") != "3":
                err(f"[單料筏層雙生 raft 3] {name}: raft_layers={d.get('raft_layers')!r}, expected '3'"
                    f"（PALLET_OVERRIDES；Eric 2026-09-17 後裁「三族都改 3」）")
            # PA-CF 樹狀版（Eric 2026-08-26 追裁「增加一個製程參數，是樹狀支撐」）：整支的存在意義就是換支撐類型。
            # 機型預設（FD300/FF600＝普通(自動)，0722 七裁）對它不適用；PA-CF **一般版**仍照機型預設查。
            # 易拆樹狀（0919）同理：整支的存在意義就是換成樹狀，機型預設不適用（類型/樣式另由上方配方斷言 exact 鎖）。
            support_expected = set() if (" PA-CF 樹狀 @" in name or _ctok == COMBO_CAT_EASYTREE) else                 {expected_support_type(p) for p in (d.get("compatible_printers", []) or [])}
            support_expected.discard(None)
            if len(support_expected) > 1:
                err(f"[support mode ambiguous] {name}: expected candidates={sorted(support_expected)!r}")
            elif support_expected:
                value = next(iter(support_expected))
                if d.get("support_type") != value:
                    err(f"[support mode default] {name}: support_type={d.get('support_type')!r}, expected {value!r}")
            # PA-CF 樹狀版（Eric 2026-08-26）：整支的存在意義就是換支撐類型，Classic 機同理豁免；
            # **PA-CF 一般版仍被要求 normal(auto)**，閘門沒關（與上面 support mode default 同款處理）。
            if is_classic and " PA-CF 樹狀 @" not in name and d.get("support_type") != "normal(auto)":
                err(f"[Classic support mode] {name}: support_type={d.get('support_type')!r}, expected 'normal(auto)'")
        # 檢查 12（Eric 2026-07-18 裁「只做腳本」）：3in1 線材起始 gcode 必須含 T 指令。
        # 缺 T 時切片器會自動補「槽位序號」T（第2槽→T1）→機上無此巨集→Klipper 只警告不停
        # →三路同步進料靜默失效（0717 同事 T1 事故機制）。T4/T012/T3 皆可（^T 開頭即過）。
        if "(3in1)" in name:
            sg = d.get("filament_start_gcode")
            sg = sg[0] if isinstance(sg, list) and sg else (sg or "")
            if not re.search(r"^\s*T\S+", sg, re.M):
                err(f"[3in1 起始gcode 缺 T 指令 — 同步進料會靜默失效] {name}")
        # 檢查 12b（Eric 2026-07-26 裁 C・3in1 口徑合一＝統一值照 0.4/0.6）：舊口徑尾碼支不得復活
        #（六支已併 PLA(3in1)/SupPLA(3in1) 各一、不綁機；舊名走 renamed_from 字串相容）。
        if "(3in1) @FF" in name:
            err(f"[3in1 口徑合一 0726] {name}: 舊口徑別名支不得存在")
        fmt = d.get("filename_format", "")
        if fmt:
            if ord(fmt[0]) > 127:
                err(f"[filename_format 開頭非 ASCII — PlaceholderParser 會炸] {name}: {fmt[:20]}")
            for i, ch in enumerate(fmt[1:], 1):
                if ord(ch) > 127 and fmt[i - 1] == "}":
                    err(f"[filename_format '}}' 後接非 ASCII — 會炸] {name}: ...{fmt[max(0,i-5):i+5]}...")
    if kind == "filament":
        # 檢查 9（Eric 2026-07-17 裁）：洗料塔最小清理量——一般 30、SupPLA 系 60、
        # FF 四料高流量噴頭/(3in1) 維持特調 120（同日裁「不蓋」）
        if d.get("instantiation") == "true":
            pv = d.get("filament_minimal_purge_on_wipe_tower")
            pv = pv[0] if isinstance(pv, list) and pv else pv
            # PVA＝85（V2.1 案 75＋劉勝賢現行 +10，0724 對帳定稿）；SupPLA 系 60；其餘 30
            # 🔴 2026-08-16 改名：「四料高流量噴頭」→「四料同進噴頭」；照片磚專用支同享 120
            expected_pv = ("120" if ("四料同進噴頭" in name or "(照片磚)" in name or "(3in1)" in name)
                           else "85" if "PVA" in name else "60" if "SupPLA" in name else "30")
            if pv is not None and pv != expected_pv:
                err(f"[洗料塔最小清理量] {name}: {pv!r}, expected {expected_pv!r}")
            # ★ 檢查 13（Eric 2026-08-16 裁・四料同進流量）：同進＝四進一出、單槽，流量 50；
            #   SupPLA 同進支 Eric 未點名＝維持 30；照片磚專用支豁免＝維持 30（等 Eric 實印再定）。
            #   ⚠ 要放寬**必須是 Eric 新的一次裁定**，不是下一棒覺得該一致就改。
            _mvs = d.get("filament_max_volumetric_speed")
            _mvs = _mvs[0] if isinstance(_mvs, list) and _mvs else _mvs
            #   🆕 0907 #153 Eric 裁②：3in1 PLA 支 20→30；SupPLA 支未點名＝維持 12
            #      （同 0816「未點名不順手改」）。同樣要放寬得是 Eric 新的一次裁定。
            _want_mvs = {"PING PLA - 四料同進噴頭": "50",
                         "PING SupPLA - 四料同進噴頭": "30",
                         "PING PLA(照片磚)": "30",
                         TI_FIL_PLA: "30",
                         TI_FIL_SUP: "12"}.get(name)
            if _want_mvs and _mvs != _want_mvs:
                err(f"[四料同進流量 0816／3in1 流量 0907] {name}: {_mvs!r}, expected {_want_mvs!r}")
            # ★ 檢查 13b（0907 #153）：改名後的兩支要有一張**新名 → 期望值**的硬表。
            #   SOP §N 第 4 條的教訓：產生器與驗證器的家族判定若都綁同一個名字字串，
            #   **兩邊會一起錯、一起綠**；唯一擋得住的是這種不靠子字串推導的 exact 表。
            _ti_want = {TI_FIL_PLA: {"filament_minimal_purge_on_wipe_tower": "120",
                                     "filament_retraction_length": "0.8",     # 3 → 0.8（Eric 2026-09-15 裁「只改 FF 四支，照片磚除外」，取代 0730/0819/0907 的 3）
                                     "filament_retract_restart_extra": "0",   # 0.6 → 0（Eric 2026-09-15 裁「整個高流量族 8 支」，取代 0907 硬表值）
                                     "pressure_advance": "0.4",
                                     "nozzle_temperature": "210"},
                        TI_FIL_SUP: {"filament_minimal_purge_on_wipe_tower": "120",
                                     "filament_retraction_length": "0.8",     # 3 → 0.8（同上裁）
                                     "filament_retract_restart_extra": "0",   # 0.6 → 0（Eric 2026-09-15 裁「整個高流量族 8 支」，取代 0907 硬表值）
                                     "pressure_advance": "0.2",
                                     "nozzle_temperature": "210",
                                     "filament_is_support": "1"}}.get(name)
            if _ti_want:
                for _tk, _tv in _ti_want.items():
                    _tg = d.get(_tk)
                    _tg = _tg[0] if isinstance(_tg, list) and _tg else _tg
                    if _tg != _tv:
                        err(f"[3in1 高流量支硬表 0907] {name}: {_tk}={_tg!r}, expected {_tv!r}")
            if name in TI_FIL_OLD:
                err(f"[3in1 舊名不得復活 0907] {name}: 已於 #153 改名，舊名只能待在 renamed_from")
            # 檢查 11：降速層時間一律 10 秒（Eric 2026-07-18 裁「擴及所有材料」）＋
            # 🔴 冷卻降速：**一般線材開、照片磚系關**（Eric 2026-09-10 裁；2026-09-11 移植進出貨線 c-0911-MCH-01）。
            #    沿革：0718「一律開」→ 0807「一律關」（實測翻案）→ 0910「一般件要開、照片磚是例外」。
            #    0910 不算翻 0807——0807 原話就寫「特殊情況才需要勾選」，關掉的是「一律開」；
            #    這次開的是「一般件需要」，照片磚反而成為那個要保持關的例外。起因＝同事回報尖端成型不好。
            #    ⚠ 本條 2026-09-11 才更新：在那之前守衛仍寫死「一律關」，比裁定舊了一天，
            #       移植當下實抓 29 項紅——同 0911 ABS 收縮守衛那一型（守衛比引擎/裁定嚴）。
            #       **規則改了要回頭改守衛**，否則守衛會把合法的新值擋下來。
            _cd_pt = name in (PT_FIL_PLA_V, PT_FIL_PLA_FD_V, PT_FIL_PLA_FDHF_V)
            _cd_want = ["0"] if _cd_pt else ["1"]
            _cd_spd  = ["10"] if _cd_pt else ["25"]
            if d.get("slow_down_for_layer_cooling") != _cd_want:
                err(f"[冷卻降速 0910] {name}: {d.get('slow_down_for_layer_cooling')!r}, expected {_cd_want!r}")
            if d.get("slow_down_min_speed") != _cd_spd:
                err(f"[降速最小速度 0910] {name}: {d.get('slow_down_min_speed')!r}, expected {_cd_spd!r}")
            _sd_want = ["4"] if name in _NON_BRAND_FIL else ["10"]    # ABS-CF 刻意 4 秒（Eric 1008；ABS 不靠層冷卻）
            if d.get("slow_down_layer_time") != _sd_want:
                err(f"[降速層時間非 {_sd_want[0]}] {name}: {d.get('slow_down_layer_time')!r}")
            # 🆕 G1（Eric 2026-08-13 裁・連動規格批1）：配料屬性必須**顯式**帶兩鍵。
            # 為什麼：「選材料→製程自動收斂」的家族軸讀 filament_is_support／filament_soluble
            #   （有支撐材⇒易拆；水溶⇒易拆水溶）。缺鍵時引擎吃 C++ 預設 false／繼承鏈的 0，
            #   **行為看起來正常但護欄驗不到**——屬「缺鍵型靜默」，正是 G2 與 C++ 端要依賴的地基。
            # ⚠ 這裡讀的是**未解 inherits 的原始檔內容**（presets 的建法），所以「顯式」在此可驗。
            # ⚠ 不排除 Classic（家族軸全機種適用；兩鍵是切片語意、非 Klipper 指令，不觸 Marlin 隔離）。
            # 產生器對應：embed_params.py 4b-2g post-pass（缺鍵補 ["0"]、已有顯式值不動）。
            for _ak in ("filament_is_support", "filament_soluble"):
                if d.get(_ak) not in (["0"], ["1"]):
                    err(f"[配料屬性須顯式 0813] {name}: {_ak}={d.get(_ak)!r}, expected ['0'] 或 ['1']")
            # 檢查 13 線材側（Eric 2026-07-24 爬坡品質批）：懸空冷卻觸發閾值全線材 25%
            # ⚠ 0725 補上（Eric 裁「verify 也補」）：本條主線早有、出貨線一直缺＝值兩線一致
            #    但少一道護欄。**刻意放在 Classic 排除區塊之外**——Eric 2026-07-25 裁
            #   「Classic 套新工藝」後，Classic 4 支線材也吃 25%（見 _classic_filament），
            #    全 25 支實測皆為 25% ⇒ 不需任何豁免。
            if d.get("overhang_fan_threshold") != ["25%"]:
                err(f"[懸空冷卻閾值 25% 0724] {name}: {d.get('overhang_fan_threshold')!r}")
            # 🆕 Eric 2026-09-08 令：ABS 家族「懸空與外部橋接區域的冷卻風扇速度」80%→30%（ABS 風扇吹太強會翹；ABS 與 SupABS 都要）。
            #   基底 fdm_filament_abs 同改、葉檔明寫（這裡只看葉檔不解析繼承）。與開發線同一條斷言。
            if "ABS" in name and d.get("overhang_fan_speed") != ["30"]:
                err(f"[ABS 懸空風扇 30% 0908] {name}: overhang_fan_speed={d.get('overhang_fan_speed')!r} 應 ['30']")
            # 🆕 Eric 2026-09-10 裁（牌 c-0910-ABS-01）：ABS 拆 PEI／玻璃。`PING ABS` 名不動＝PEI 版
            #   （C++ Tab.cpp 組合連動硬寫此名，不改名＝不 build）；玻璃版首層床溫 60、其它層 100。
            if name == "PING ABS(玻璃)" and (d.get("hot_plate_temp_initial_layer") != ["60"] or d.get("hot_plate_temp") != ["100"]):
                err(f"[ABS 玻璃床溫 0910] {name}: 首層={d.get('hot_plate_temp_initial_layer')!r} 應 60、其它層={d.get('hot_plate_temp')!r} 應 100")
            # 線材回抽統一（Eric 2026-07-23 三裁 → 0819 一般流量改寫）
            # 🔴 Classic 前代豁免：赤兔不能吃韌體回抽（Eric 0807）⇒ 材料層不得覆蓋回抽，
            #    專屬護欄在檔尾「Classic 材料層回抽覆蓋 0807」。
            if name.startswith("PING") and "Classic" not in name:
                def _v(k):
                    x = d.get(k)
                    return x[0] if isinstance(x, list) and x else x
                for k, want in (("filament_retraction_minimum_travel", "3"), ("filament_wipe", "1"),
                                ("filament_wipe_distance", "5"), ("filament_retract_before_wipe", "100%")):
                    if _v(k) != want:
                        err(f"[線材回抽四項 0723] {name}: {k}={_v(k)!r}, expected {want!r}")
                # 🔴 2026-08-16 改名連坐：舊名「四料高流量噴頭」靠字串「高流量」落進 is_hf，
                #    改名成「四料同進噴頭」後就掉出去、被當一般流量要求 PA 0.08（本輪實測踩到）。
                #    照片磚專用支同理（它就是同進支的照片磚分身）。
                # ⚠ 「PING PLA(照片磚 FD300)」不含「(照片磚)」⇒ 刻意落在一般流量側（FD300＝雙料一般流量）。
                is_hf = (("高流量" in name) or ("四料同進" in name)
                         or ("(照片磚)" in name) or ("(3in1)" in name))
                # 🆕 FF600／FF800 **專屬**線材（2026-09-15 Eric 裁回抽 0.8 的範圍）＝is_hf 的真子集。
                #   四支：PING PLA／SupPLA - 四料同進噴頭、PING PLA(3in1)／SupPLA(3in1) - 高流量噴頭。
                #   ⛔ 不含「PING PLA／SupPLA／PETG - 高流量噴頭」——那三支白名單為空、走 condition
                #      `printer_notes!~PHOTOTILE and !~CLASSIC` ⇒ FD 全系列也吃得到，不是 FF 專屬。
                #   ⛔ 不含照片磚支（它由 is_pt 先擋成 nil）。
                _is_ff_only = ("四料同進" in name) or ("(3in1)" in name)
                # ⚠ 內聯清單，不吃 embed_params 的常數（兩支是獨立程式）。新增照片磚專用線材時兩邊都要加——
                #   0730 就是漏了這一行，照片磚支被掃成回抽 3、靜默蓋掉零回抽 20 天。正本＝embed_params.pt_fil_specs。
                is_pt = name in (PT_FIL_PLA_V, PT_FIL_PLA_FD_V, PT_FIL_PLA_FDHF_V)
                # 🔴 四料照片磚噴溫 210（Eric 2026-09-29 22:3x 裁「改回 210」，回退 0910「降到 190」那一裁）。
                #    只有 `PING PLA(照片磚)`＝FF600／FF800 照片磚六台專用支；雙料照片磚兩支一直是 210（0910「雙料不改」）。
                #    證據＝.142 溫度對照：190 換成白／黑供料不足兩次重現、210 消失（SOP 無頭切片 §22-8）。正本＝embed_params 的 PT_FIL_PLA 那兩行。
                if name == PT_FIL_PLA_V:
                    for _tk in ("nozzle_temperature", "nozzle_temperature_initial_layer"):
                        if _v(_tk) != "210":
                            err(f"[四料照片磚噴溫 210 0929] {name}: {_tk}={_v(_tk)!r}, expected '210'")
                elif name in (PT_FIL_PLA_FD_V, PT_FIL_PLA_FDHF_V):
                    for _tk in ("nozzle_temperature", "nozzle_temperature_initial_layer"):
                        if _v(_tk) != "210":
                            err(f"[雙料照片磚噴溫 210 0910] {name}: {_tk}={_v(_tk)!r}, expected '210'")
                # 🆕 Eric 2026-08-19 令：一般流量「額外回填長度」＝**取消勾選**（nil，退回機器層 0）；
                #    高流量家族維持 0.6。蓋掉 0723 的「一般流量 0.2」。
                # 🔴 四料照片磚 0.2（Eric 2026-09-10「額外裝填 0.6 會擠出蠻多的」）；FD300 照片磚維持 nil。
                # 🔴🔴 **2026-09-15 Eric 裁「整個高流量族 8 支」⇒ 高流量家族 0.6 → 0**
                #      （上面 0819「高流量維持 0.6」與 0910「只降四料照片磚、不擴大」那兩句**已被本裁取代**，
                #       原文留著是演進軌跡，不是現行值）。
                #      起因＝廠內 FF600 E Pro Max（.29）測四料「會磨到材料」：每次回抽完多推 0.6 mm 回去，
                #      在混色頭上就是反覆把料往前頂。已排除韌體端——.29 唯讀實查
                #      firmware_retraction.unretract_extra_length = 0.0 ⇒ 0.6 只來自線材層這裡。
                #      ⛔ PT_FIL_PLA 的 0.2 是 Eric 0910 自己裁的、本次未提 ⇒ 不動。
                _want_extra = "0.2" if name == PT_FIL_PLA_V else ("0" if is_hf else "nil")
                if _v("filament_retract_restart_extra") != _want_extra:
                    err(f"[額外回填 0819] {name}: {_v('filament_retract_restart_extra')!r}, expected {_want_extra!r}")
                # 🆕 Eric 2026-08-19 令：回抽速度／裝填速度全庫 30/30
                # 🆕 Eric 2026-08-26 裁（Q4 甲）：PA-CF＝40/40（只改線材層、機器層不動）。
                _is_pacf = "PA-CF" in name
                _want_spd = "40" if _is_pacf else "30"
                for _k, _label in (("filament_retraction_speed", "回抽速度"),
                                   ("filament_deretraction_speed", "裝填速度")):
                    if _v(_k) != _want_spd:
                        err(f"[回抽速度 0819/0826] {name}: {_label}={_v(_k)!r} 應 {_want_spd}")
                # ★ PA 分流量家族（Eric 2026-07-25 裁「PA 0.12 只限一般流量」→ 2026-07-28 三輪裁
                #   「材料如果不是高流量跟火山口或四料，它的壓力提前是 0.08」＝一般流量 0.12→0.08）
                #   現況表（0728 起、以下為權威）：
                #     一般流量  PLA-220/PLA-210/SupPLA/ABS/SupABS/PETG/PVA ＝ **0.08**（enable 1）
                #     高流量噴頭 PLA／SupPLA／PETG ＝ 0.2
                #     四料高流量 PLA／SupPLA        ＝ 0.4
                #     3in1      PLA 0.4／SupPLA 0.2
                #     火山口     PA-CF ＝ 材料特例維持既值（不在 0.08 範圍）
                #     TPE／SupTPE                   ＝ 關（0）＝0725 裁軟料待實測、0728 不翻案
                #   單向護欄照舊：0.12 不得出現在高流量／3in1 家族。
                #   ⚠ Classic 前代線材另有 PA 全關斷言（Marlin 無 PA），不受本條影響。
                if is_hf and _v("pressure_advance") == "0.12":
                    err(f"[PA 0.12 只限一般流量 0725] {name}: 高流量/3in1 家族不得用 0.12")
                if not is_hf and "TPE" not in name and not _is_pacf:
                    if _v("enable_pressure_advance") != "1" or _v("pressure_advance") != "0.08":
                        err(f"[一般流量 PA 0.08 0728] {name}: enable={_v('enable_pressure_advance')!r} "
                            f"pa={_v('pressure_advance')!r}, expected 1/0.08")
                # 🆕 Eric 2026-08-26 裁：PA-CF＝1/0.4（材料特例；0728 起它只是「豁免 0.08」＝實際沒開）
                if _is_pacf:
                    if _v("enable_pressure_advance") != "1" or _v("pressure_advance") != "0.4":
                        err(f"[PA-CF PA 0.4 0826] {name}: enable={_v('enable_pressure_advance')!r} "
                            f"pa={_v('pressure_advance')!r}, expected 1/0.4")
                # 回抽長度四分支（🆕 0819 改寫）
                if is_pt:
                    # 🔴 照片磚＝零回抽，且零回抽只寫在機器層（retraction_length 0,0）——
                    #    線材覆蓋會贏，所以線材端必須是 nil。0730 那批曾把 FF 照片磚支掃成 3，
                    #    靜默蓋掉零回抽整整 20 天沒人發現；這條護欄就是為了不再發生。
                    if _v("filament_retraction_length") != "nil":
                        err(f"[照片磚零回抽 0819] {name}: {_v('filament_retraction_length')!r} 應 nil（吃機器層 0）")
                elif "TPE" in name or "PVA" in name:
                    # 軟料/PVA 長度維持既值不動（Eric 0819「軟料回抽為 3、不改動」；PVA 0724 定稿）。
                    if _v("filament_retraction_length") != "3":
                        err(f"[TPE/PVA 回抽長度 3] {name}: {_v('filament_retraction_length')!r}")
                elif _is_pacf:
                    # 🆕 2026-08-26 Eric 裁：PA-CF 回抽長度 3（高溫滲料，2 擋不住牽絲）。
                    if _v("filament_retraction_length") != "3":
                        err(f"[PA-CF 回抽長度 3 0826] {name}: {_v('filament_retraction_length')!r} 應 3")
                elif _is_ff_only:
                    # 🆕 2026-09-15 Eric 裁「只改 FF 四支，照片磚除外」：FF600／FF800 **專屬**線材
                    #    回抽長度 3 → 0.8。起因＝廠內 FF600 E Pro Max（.29）測四料回報「會磨到材料」，
                    #    同一輪已先把額外回填 0.6→0（見上面 restart_extra 那條），這是長度本身。
                    #    ⛔ **舊裁「高流量家族（含 3in1）回抽長度 3」（0730 立·0819 再確認）在這四支上
                    #       已被本裁取代**，其餘 is_hf 支（PLA／SupPLA／PETG - 高流量噴頭）維持 3。
                    #    🔴 範圍為什麼是四支不是八支：那三支「- 高流量噴頭」的 compatible_printers 是
                    #       **空的**、走 condition `printer_notes!~PHOTOTILE and !~CLASSIC`
                    #       ⇒ 除照片磚機與 Classic 外**全機型可用，FD 全系列也在內**；改它們會波及 FD。
                    #       Eric 要的是 FF 那台的手感 ⇒ 只動 FF600／FF800 專屬的四支。
                    if _v("filament_retraction_length") != "0.8":
                        err(f"[FF專屬回抽長度 0.8（0915 裁，取代 0730/0819 的 3）] {name}: "
                            f"{_v('filament_retraction_length')!r} 應 0.8")
                elif is_hf:
                    # 2026-07-30 Eric 裁：高流量家族（含 3in1）回抽長度 3；0819「排除不動」再確認。
                    # ⚠ 2026-09-15 起這條**只剩 FD 共用的三支**（PLA／SupPLA／PETG - 高流量噴頭）
                    #   與四料照片磚支在吃；FF600／FF800 專屬四支已被上面那條接手。
                    if _v("filament_retraction_length") != "3":
                        err(f"[高流量家族長度 3（0730 裁·0819 再確認）] {name}: {_v('filament_retraction_length')!r} 應 3")
                else:
                    # 🆕 Eric 2026-08-19 令：一般流量線材回抽長度 2（蓋掉 0723 的「收斂繼承 nil」）
                    if _v("filament_retraction_length") != "2":
                        err(f"[一般流量回抽長度 2 0819] {name}: {_v('filament_retraction_length')!r} 應 2")

# Wizard order is driven by machine_model_list. Verify each family independently so a regen
# cannot silently put the hardware-swap single-head card back between dual-head modes.
variant_rank = {"": 0, "同進": 1, "3in1": 2, "單料頭": 3}
families = {}
for index, item in enumerate(root.get("machine_model_list", [])):
    match = re.match(r"^(.*?)(?: (單料頭|同進|3in1))?$", item["name"])
    base, variant = match.group(1), match.group(2) or ""
    families.setdefault(base, []).append((index, variant))
for base, entries in families.items():
    if not any(variant == "單料頭" for _, variant in entries):
        continue
    ranks = [variant_rank.get(variant, 9) for _, variant in entries]
    if ranks != sorted(ranks) or entries[-1][1] != "單料頭":
        err(f"[wizard model order] {base}: variants={[variant for _, variant in entries]!r}")

# Classic 完整性與專用材料隔離。
model_names = {n for n, (k, _) in presets.items() if k == "machine_model"}
for model, spec in CLASSIC.items():
    if model not in model_names:
        err(f"[Classic model missing] {model}")
    machine = f"{model} {spec['nozzle']} nozzle"
    if machine not in machines:
        err(f"[Classic machine missing] {machine}")
    elif "M605" in presets[machine][1].get("machine_start_gcode", ""):
        # 本體（雙料/單料）start 不得有任何混色指令——M6050 只屬同進變體的預擠同進
        err(f"[Classic 本體 start 不應有 M605x] {machine}")
    if model.startswith("DUAL") and machine in machines:
        # 0728「變體預設跟 210」的對向鎖：DUAL 本體＝雙料 → 首槽維持 Classic 220
        _cdfp = presets[machine][1].get("default_filament_profile")
        if not (isinstance(_cdfp, list) and _cdfp and _cdfp[0] == "PING PLA - Classic 220"):
            err(f"[Classic DUAL 本體首槽維持 Classic 220 0728] {machine}: {_cdfp!r}")

# Classic DUAL 變體完整性＋M6050 舊格式規則（Eric 2026-07-26 裁、07-27 實作）：
# 同進 start 必含「M6050 S0.5」（預擠兩邊同進，對應 FD 同進機的同款行）；
# 單料頭 start 必無任何 M605x；全變體 SEMM=0（同 FD 變體＝Orca 眼裡 1 槽）。
for model, nzs in CLASSIC_VARIANT_NOZZLES.items():
    for variant in CLASSIC_VARIANTS:
        vmodel = f"{model} {variant}"
        if vmodel not in model_names:
            err(f"[Classic 變體 model missing] {vmodel}")
        else:
            # 0728 Eric 裁「Classic 變體預設跟」：變體 model 的可勾清單須含 Classic 210。
            # ⓘ 0807 全族補齊後 default_materials 不再只有一支（Eric 裁 Classic 同辦），
            #    故由「等於」放寬為「必須含」；首位仍是 Classic 210＝預設意旨保留。
            _vdm = [x for x in (presets[vmodel][1].get("default_materials") or "").split(";") if x]
            if not _vdm or _vdm[0] != "PING PLA - Classic 210":
                err(f"[Classic 變體 model 首選 Classic 210 0728·0807 調整] {vmodel}: {_vdm!r}")
        for nz in nzs:
            mname = f"{vmodel} {nz} nozzle"
            entry = presets.get(mname)
            if not entry or entry[0] != "machine":
                err(f"[Classic 變體 machine missing] {mname}")
                continue
            d = entry[1]
            if d.get("single_extruder_multi_material") != "0":
                err(f"[Classic 變體 SEMM] {mname}: "
                    f"{d.get('single_extruder_multi_material')!r}, expected '0'")
            # 0728 Eric 裁「Classic 變體預設跟」：變體＝單一出料 → 預設 Classic 210
            if d.get("default_filament_profile") != ["PING PLA - Classic 210"]:
                err(f"[Classic 變體預設 Classic 210 0728] {mname}: {d.get('default_filament_profile')!r}")
            start = d.get("machine_start_gcode", "")
            if variant == "同進":
                if "M6050 S0.5" not in start:
                    err(f"[Classic 同進 start 缺 M6050 S0.5] {mname}")
            elif "M605" in start:
                err(f"[Classic 單料頭 start 不應有 M605x] {mname}")
            # 0729 Klipper #128：變體同守 no-T（SEMM=0 引擎不出換刀，start 溫度行也不得帶 T）
            if re.search(r"M10[49][^\n]*\bT\d", start) or re.search(r"(?m)^\s*T\d", start):
                err(f"[Classic 變體 no-T 0729] {mname}: machine_start_gcode 帶 T")
            if d.get("change_filament_gcode", ""):
                err(f"[Classic 變體 change_filament 應空 0729] {mname}")
# Classic 全套 11 支（Eric 2026-08-07 裁「全套跟 Fast 對齊」：原 4 支 ＋ 新 7 支）。
# 下面逐支斷言：PA 全關（Marlin 無 PA）＋ start gcode 不得帶 Klipper 的 SET_RETRACTION。
classic_filaments = ("PING PLA - Classic 210", "PING PLA - Classic 220",
                     "PING SupPLA - Classic", "PING PLA - EDU Classic",
                     "PING ABS - Classic", "PING SupABS - Classic", "PING PETG - Classic",
                     "PING PA-CF - Classic", "PING PVA - Classic",
                     "PING TPE - Classic 210", "PING SupTPE - Classic")
for name in classic_filaments:
    entry = presets.get(name)
    if not entry or entry[0] != "filament":
        err(f"[Classic filament missing] {name}")
        continue
    d = entry[1]
    if d.get("enable_pressure_advance") != ["0"]:
        err(f"[Classic pressure advance] {name}: {d.get('enable_pressure_advance')!r}")
    if "SET_RETRACTION" in "\n".join(d.get("filament_start_gcode", []) or []):
        err(f"[Classic Klipper command] {name}: filament_start_gcode")
    # 🔴 赤兔不能吃韌體回抽（Eric 2026-08-07 補充的事實）：Classic 線材**不得**在材料層覆蓋回抽，
    #    一律由 Classic machine preset 控制（_classic_filament() 會 pop 掉這些鍵）。
    # 🔴 2026-08-26 補洞：原本只接 `filament_retract*`，**漏掉 `filament_deretraction_*`**
    #    （裝填速度同屬回抽鍵）⇒ 9 支 Classic 線材帶著它也驗得過。與 _classic_filament 的 pop 判斷同源。
    _leak = sorted(k for k in d if k.startswith("filament_retract")
                   or k.startswith("filament_deretraction")
                   or k in ("filament_wipe", "filament_wipe_distance",
                            "filament_z_hop", "filament_z_hop_types"))
    if _leak:
        err(f"[Classic 材料層回抽覆蓋 0807] {name}: 不得帶 {'、'.join(_leak)}（赤兔無韌體回抽，一律吃機器層）")
edu = presets.get("PING PLA - EDU Classic")
if edu and any(edu[1].get(key) != ["0"] for key in
               ("hot_plate_temp", "hot_plate_temp_initial_layer", "cool_plate_temp", "cool_plate_temp_initial_layer")):
    err("[EDU filament heated bed] PING PLA - EDU Classic must use 0C bed")

# PING PVA 線材本體（與主線 verify 同款；0725 稽核發現出貨線原本完全沒有這組斷言，
# 所以 01148acf 漏掉的回抽長度沒被抓到）。值＝V2.1 定稿案 DPro_0.6_T210_PVA+PLA (0609)
# 對帳（2026-07-24）：210 全鍵／回抽 3／z-hop 0.6／purge 85。
if "PING PVA" not in presets:
    err("[PVA 缺席] PING PVA 不在 PING.json filament_list")
else:
    _k, pd = presets["PING PVA"]
    for k, want in (("filament_type", ["PVA"]), ("filament_soluble", ["1"]),
                    ("filament_is_support", ["1"]), ("nozzle_temperature", ["210"]),
                    ("nozzle_temperature_initial_layer", ["210"]), ("hot_plate_temp", ["60"]),
                    ("fan_max_speed", ["100"]),
                    ("filament_retraction_length", ["3"]), ("filament_z_hop", ["0.6"]),
                    ("filament_minimal_purge_on_wipe_tower", ["85"])):
        if pd.get(k) != want:
            err(f"[PVA 關鍵值] PING PVA: {k}={pd.get(k)!r}, expected {want!r}")

# SupPLA 基礎支噴溫 210（Eric 0729 裁・#32「SUP 與 PLA 同溫」單）：全 SupPLA 家族統一 210
# （高流量/四料/3in1 三支本已 210、基礎支原為唯一 220）。⚠ 連帶：PLA-220＋SupPLA 切片會觸發
#  #33 溫度不一致確認視窗（正確提醒、可繼續）。
_se = presets.get("PING SupPLA")
if not _se:
    err("[SupPLA 缺席] PING SupPLA 不在 filament_list")
elif (_se[1].get("nozzle_temperature") != ["210"]
      or _se[1].get("nozzle_temperature_initial_layer") != ["210"]):
    err(f"[SupPLA 噴溫 210 0729] {_se[1].get('nozzle_temperature')!r}/"
        f"{_se[1].get('nozzle_temperature_initial_layer')!r}, expected ['210']")

# ★ 高流量製程組（Eric 2026-07-30 裁・客戶（建誌）FF800 同進 0.6 實測參數移植；0.4/1.0 口徑連動推）：
# 三支 exact——六類速度全跟客戶（外100/內125/填充150/頂面150/實心150/支撐與介面100）＋首層 100
#（Eric 追裁）＋加速度逐項取保守（實際變更僅 travel/sparse 2000；default 1500/首層 500/頂面 800
#  現值更保守維持）＋層高 0.5×口徑（首層＋0.05）＋頂底厚 1.2；支撐角 35/gyroid/aligned 維持家規
#（客戶 Cura 慣例不吃——support_angle 60 是 Cura 語意＝Orca 30、0724 已踩過的換算坑）。
HF_PROCS = {"0.2mm 高流量 @FF800 同進 (0.4)": ("0.2", "0.25", "0.4"),
            "0.3mm 高流量 @FF800 同進 (0.6)": ("0.3", "0.35", "0.6"),
            "0.5mm 高流量 @FF800 同進 (1.0)": ("0.5", "0.55", "1.0")}
for _hn, (_lh, _flh, _nz) in HF_PROCS.items():
    _he = presets.get(_hn)
    if not _he or _he[0] != "process":
        err(f"[高流量製程組 0730・缺席] {_hn}")
        continue
    _hd = _he[1]
    for _hk, _hw in (("layer_height", _lh), ("initial_layer_print_height", _flh),
                     ("outer_wall_speed", "100"), ("inner_wall_speed", "125"),
                     ("sparse_infill_speed", "150"), ("top_surface_speed", "150"),
                     ("internal_solid_infill_speed", "150"),
                     ("support_speed", "100"), ("support_interface_speed", "100"),
                     ("initial_layer_speed", "100"), ("initial_layer_infill_speed", "100"),
                     ("travel_acceleration", "2000"), ("sparse_infill_acceleration", "2000"),
                     ("default_acceleration", "1500"), ("initial_layer_acceleration", "500"),
                     ("top_surface_acceleration", "800"),
                     ("outer_wall_acceleration", "1500"), ("inner_wall_acceleration", "1500"),
                     ("top_shell_thickness", "1.2"), ("bottom_shell_thickness", "1.2"),
                     ("sparse_infill_pattern", "gyroid"), ("seam_position", "back"),
                     ("support_threshold_angle", "35"), ("instantiation", "true"),
                     ("compatible_printers", ["FF800 同進 %s nozzle" % _nz])):
        if _hd.get(_hk) != _hw:
            err(f"[高流量製程組 0730] {_hn}: {_hk}={_hd.get(_hk)!r} 應 {_hw!r}")
# 範圍鎖：高流量製程現階段僅 FF800 同進三口徑（Eric 裁「實印驗過再擴」——FF600/四色/3in1 出現即紅）
for _n, (_k, _d) in presets.items():
    if _k == "process" and " 高流量 @" in _n and _n not in HF_PROCS:
        err(f"[高流量製程組 0730・範圍外溢] {_n}: 現階段僅 FF800 同進三口徑")

# TPE 一對（Eric 0728 二輪）：本體改名「PING TPE - 210」＋噴溫 220→210（SupTPE 名不動、溫跟 210）；
# 回抽速度 30/30（「TPE軟料的回抽 3/30/30」；長度 3 斷言在上方回抽統一段）
if "PING TPE" in presets:
    err("[TPE 舊名復活 0728v2] PING TPE 應已改名 PING TPE - 210")
if os.path.isfile(os.path.join(PINGDIR, "filament", "PING TPE.json")):
    err("[TPE 舊檔殘留 0728v2] filament/PING TPE.json 應已移除")
for _tn in ("PING TPE - 210", "PING SupTPE"):
    _te = presets.get(_tn)
    if not _te or _te[0] != "filament":
        err(f"[TPE 缺席] {_tn} 不在 PING.json filament_list")
        continue
    for _tk in ("filament_retraction_speed", "filament_deretraction_speed"):
        if _te[1].get(_tk) != ["30"]:
            err(f"[TPE 回抽 3/30/30 0728] {_tn}: {_tk}={_te[1].get(_tk)!r}, expected ['30']")
    for _tk in ("nozzle_temperature", "nozzle_temperature_initial_layer"):
        if _te[1].get(_tk) != ["210"]:
            err(f"[TPE 噴溫 210 0728v2] {_tn}: {_tk}={_te[1].get(_tk)!r}, expected ['210']")
    # 0729 Eric 裁（回報中心單）：TPE 最大體積流量 7（SpiderMaker 官方起始值）；SupTPE 維持 5.5
    _mv_want = ["5.5"] if _tn == "PING SupTPE" else ["7"]
    if _te[1].get("filament_max_volumetric_speed") != _mv_want:
        err(f"[TPE 最大體積流量 0729] {_tn}: {_te[1].get('filament_max_volumetric_speed')!r}, expected {_mv_want!r}")
_te = presets.get("PING TPE - 210")
if _te:
    if _te[1].get("renamed_from") != "PING TPE":
        err(f"[TPE renamed_from 0728v2] PING TPE - 210: {_te[1].get('renamed_from')!r}, expected 'PING TPE'（字串）")
    if _te[1].get("filament_id") != "PINGFILTPE" or _te[1].get("setting_id") != "PINGFILTPE":
        err(f"[TPE id 不得變 0728v2] PING TPE - 210: {_te[1].get('filament_id')!r}/{_te[1].get('setting_id')!r}")

# 基礎支改名（Eric 2026-07-28）：PING PLA → PING PLA - 210；舊名走 renamed_from（字串）相容
if "PING PLA" in presets:
    err("[基礎支舊名復活 0728] PING PLA 應已改名 PING PLA - 210")
if os.path.isfile(os.path.join(PINGDIR, "filament", "PING PLA.json")):
    err("[基礎支舊檔殘留 0728] filament/PING PLA.json 應已移除")
_be = presets.get("PING PLA - 210")
if not _be or _be[0] != "filament":
    err("[基礎支缺席 0728] PING PLA - 210 不在 PING.json filament_list")
else:
    _bd = _be[1]
    if _bd.get("renamed_from") != "PING PLA":
        err(f"[基礎支 renamed_from 0728] PING PLA - 210: {_bd.get('renamed_from')!r}, expected 'PING PLA'（字串）")
    if _bd.get("nozzle_temperature") != ["210"] or _bd.get("nozzle_temperature_initial_layer") != ["210"]:
        err(f"[基礎支噴溫 210] PING PLA - 210: {_bd.get('nozzle_temperature')!r}/{_bd.get('nozzle_temperature_initial_layer')!r}")
    if _bd.get("filament_id") != "GPINGPLA" or _bd.get("setting_id") != "GPINGPLA":
        err(f"[基礎支 id 不得變 0728] PING PLA - 210: {_bd.get('filament_id')!r}/{_bd.get('setting_id')!r}")
# 預設連動定案（Eric 0728 v2「連動」）：單一出料機（FP300×3＋FD300 系 單料頭/同進/同進照片磚）
# 預設一律 PLA - 210；雙料機（FD300/FD300 Pro 標準雙料＋關門＝FD300 雙料變體）首槽維持 PLA - 220；
# 其餘機器不得外溢（P200+ 客戶版/Classic 變體＝不在範圍待裁）。
# 🆕 2026-08-19：FD300 同進照片磚**移出**本集合——它改吃 `PING PLA(照片磚 FD300)` 專用支
#   （Eric 0819 裁「給它專用支」，理由＝零回抽必須靠線材端 nil 才不被覆蓋；見下方專屬護欄）。
#   噴溫仍是 210（專用支從 PLA-210 整支派生），0728 v2 的意旨沒有被推翻，只是落點換到專用支。
_single_out_210 = {"FD300 單料頭", "FD300 Pro 單料頭", "FD300 同進", "FD300 Pro 同進"}
_pt_fd_model = "FD300 同進照片磚"
_PT_FIL_FD = "PING PLA(照片磚 FD300)"
_dual_keep_220 = {"FD300", "FD300 Pro", "FD300 關門"}
for _mn, (_mk, _md) in presets.items():
    if _mk == "machine":
        _dfp = _md.get("default_filament_profile")
        _pmod = _md.get("printer_model", "")
        if _mn.startswith("FP300 ") and _mn.endswith("nozzle"):
            if _dfp != ["PING PLA - 210"]:
                err(f"[FP300 預設 210 0728] {_mn}: {_dfp!r}, expected ['PING PLA - 210']")
        elif _pmod == _pt_fd_model:
            # 🆕 檢查 12 追加（Eric 2026-08-19）：FD300 照片磚機必為照片磚 FD300 專用支。
            # 🔴 動機與 FF 那條同源：照片磚的零回抽只寫在機器層，線材覆蓋會贏 ⇒ 只要槽位被
            #    換回任何一般流量支（例如共用的 PLA-210），零回抽就**靜默失效、不會報錯**。
            if _dfp and set(_dfp) != {_PT_FIL_FD}:
                err(f"[FD300 照片磚機必為照片磚 FD300 專用支 0819] {_mn} -> {sorted(set(_dfp))!r}")
        elif _pmod in _single_out_210:
            if not isinstance(_dfp, list) or "PING PLA - 220" in _dfp or "PING PLA - 210" not in _dfp:
                err(f"[單一出料預設 210 0728v2] {_mn}: {_dfp!r}")
        elif _pmod in _dual_keep_220:
            if not (isinstance(_dfp, list) and _dfp and _dfp[0] == "PING PLA - 220"):
                err(f"[雙料首槽維持 220 0728v2] {_mn}: {_dfp!r}")
        elif isinstance(_dfp, list) and "PING PLA - 210" in _dfp:
            err(f"[預設 210 外溢 0728] {_mn}: 僅 FP300＋FD300 系單一出料應指 PING PLA - 210")
    elif _mk == "machine_model":
        _dmt = (_md.get("default_materials", "") or "").split(";")
        if "PING PLA" in _dmt:
            err(f"[default_materials 舊名未改 0728] {_mn}")
        # 🔁 **被 Eric 2026-08-22「甲」取代**（原條文：照片磚 model 的可勾清單必須含 PLA - 210）。
        #    為什麼取代：0728v2 那條寫在「通用料本來就會出現在照片磚機上」的世界裡（因為它們
        #    compatible_printers 空＝跟誰都相容，再被 0807 rule ② 強制補進可勾清單）。
        #    但那些料的 filament_retraction_length 非 nil，會覆蓋掉機器層的零回抽——與上面
        #    0819 那條「FD300 照片磚機必為照片磚專用支」是同一個理由，只是 0819 只鎖了
        #    default_filament_profile（預設指誰），沒鎖可勾清單（使用者仍選得到）。
        #    Eric 2026-08-22 裁「甲」＝照片磚機的下拉只剩照片磚專用料 ⇒ 210 依定義不可能還在清單裡。
        #    新條文＝可勾清單必須**恰好**是該機型的照片磚專用支，與 0819 對齊成同一把尺。
        _PT_MODEL_FIL = {"FD300 同進照片磚": "PING PLA(照片磚 FD300)",
                         "FF600 同進照片磚": "PING PLA(照片磚)",
                         "FF800 同進照片磚": "PING PLA(照片磚)"}
        if _mn in _PT_MODEL_FIL:
            _want_dmt = {_PT_MODEL_FIL[_mn]}
            if {x for x in _dmt if x} != _want_dmt:
                err(f"[照片磚 model 可勾清單必為專用支 0822甲] {_mn}: "
                    f"{_md.get('default_materials')!r}（應為 {sorted(_want_dmt)!r}）")

# ★ 預勾線材全族補齊護欄（Eric 2026-08-07 裁）——對應 embed_params.py 的
#   apply_default_materials() post-pass。三條斷言：
#   ① 死名：default_materials 每一項都必須是 bundle 內實際存在的線材 preset
#      （0725 ABS 整併後 PING ABS - 250／PING PolyABS 在 FD/FP 清單裡躺了兩週沒人發現，
#        C++ find_preset 找不到就靜默跳過＝壞得無聲）。
#   ② 漏勾：每台機型必須涵蓋「所有與它相容的 PING 線材」——這是 0807 裁定的本體
#      （起因＝PING PVA／TPE - 210／SupTPE 曾 0/41 台預勾，客戶端得自己去勾才看得到自家料）。
#   ③ Classic 隔離：前代 Marlin 專用線材不得外溢到 Fast 機（Classic 4 支沒設
#      compatible_printers，純靠相容性推導會外溢）。機型判定照 SOP §9 前綴，沒有機器叫「Classic」。
_CLASSIC_MODEL_RE = re.compile(r"^(EDU|DUAL|PING 2|PING 3)")
_ping_fils = {n: (d.get("compatible_printers") if isinstance(d.get("compatible_printers"), list)
                  and d.get("compatible_printers") else None)
              for n, (k, d) in presets.items()
              if k == "filament" and (n.startswith("PING ") or n in _NON_BRAND_FIL) and d.get("instantiation") == "true"}

# ★ 照片磚機的線材收斂（Eric 2026-08-22 裁「甲」）——本節是 0807 rule ② 的必要例外。
#   起因：0807 rule ② 要求「每台機型必須涵蓋所有與它相容的 PING 線材」，而通用料的
#   compatible_printers 是空的＝跟誰都相容 ⇒ **照片磚機被這條規則強制預勾了 13 支通用料**。
#   那些料的 filament_retraction_length 非 nil（例：PETG 高流量＝3），會**覆蓋掉機器層的
#   零回抽**（照片磚的硬需求）——正是 0730「FF 照片磚支被掃成 3、零回抽破了 20 天沒人發現」
#   那個坑的另一個入口：這次不是值被寫錯，是不該出現的料能合法待在那台機上。
#   解法＝照片磚機掛 printer_notes 標記 PHOTOTILE，通用料加 compatible_printers_condition
#   把它們排除（C++ 端 Preset.cpp:710 只在「沒有明列 compatible_printers」時才求值條件）。
#   ⚠ 這裡刻意只認**這一條已知條件字串**：出現任何其他條件式就顯性報錯，不要靜默忽略——
#     驗證器看不懂的條件如果被當成「沒有限制」，rule ② 會反過來要求把料加回去。
_PHOTOTILE_MARK = "PHOTOTILE"
_PHOTOTILE_COND = "printer_notes!~/.*%s.*/" % _PHOTOTILE_MARK
# Classic 前代機 × 標準線材雙向隔離（Eric 2026-09-01 裁，牌 c-0901-GATE-01）。
# 產生器（embed_params.apply_printer_family_gates）把兩個家族各出一條、用 `and` 併，
# 所以這裡登記的是「兩個家族的所有合法組合」，而不是單一字串。
# ⚠ 沿用上面那條紀律：**認不得的條件式一律顯性報錯**，不要靜默當成「沒有限制」。
_CLASSIC_MARK = "CLASSIC"
_CLASSIC_YES = "printer_notes=~/.*%s.*/" % _CLASSIC_MARK   # 只有 Classic 機看得到
_CLASSIC_NO = "printer_notes!~/.*%s.*/" % _CLASSIC_MARK    # Classic 機看不到
_fil_cond = {}
for _n, (_k, _d) in presets.items():
    if _k != "filament" or not _n.startswith("PING ") or _d.get("instantiation") != "true":
        continue
    _c = (_d.get("compatible_printers_condition") or "").strip()
    if not _c:
        continue
    _want_parts = [_PHOTOTILE_COND, _CLASSIC_YES if "Classic" in _n else _CLASSIC_NO]
    _allowed = {_PHOTOTILE_COND, " and ".join(_want_parts)}
    if _c not in _allowed:
        # 方向講出來：Classic 料掛成 `!~CLASSIC`（或反過來）＝這支料在自己家的機器上會消失，
        # 而那正是使用者最不會想到要來這裡查的症狀。
        err(f"[未知或方向相反的 compatible_printers_condition] {_n}: {_c!r}；"
            f"依線材名稱應為 {' and '.join(_want_parts)!r}（或僅照片磚那條），"
            f"請先更新本檔再加新條件")
    _fil_cond[_n] = _c
# 哪些 machine preset 掛了 PHOTOTILE 標記
_phototile_machines = {n for n, (k, d) in presets.items()
                       if k == "machine" and _PHOTOTILE_MARK in (d.get("printer_notes") or "")}
# 🆕 **照片磚機器層回抽政策護欄（Eric 2026-09-07 裁，取代 0718 的零回抽；2026-09-12 移植進出貨線）**
#   這組值 2026-07~08 曾被線材層靜默蓋掉 20 天沒人發現。政策改了，**護欄要跟著改而不是拿掉**。
#   🔴 `retract_length_toolchange` 必須維持 0：照片磚的 Tn 是後處理要換成 M6051/M6052 的混色指令，不是真的換料頭。
#   🆕 2026-09-19：抬升 0.1→0.4（Eric 改裁，牌 c-0919-ZH-01；起因＝0919 實印空跑只抬 0.1 mm 掠過磚頂、磚與塔倒）。
_PT_MACHINE_POLICY = {
    "use_firmware_retraction": "1",
    "retraction_length": ["1.3", "1.3"],
    "z_hop": ["0.4"],
    "retract_length_toolchange": ["0", "0"],
}
for _pm in sorted(_phototile_machines):
    _pd = presets[_pm][1]
    for _k, _want in _PT_MACHINE_POLICY.items():
        _got = _pd.get(_k)
        if _got != _want:
            err(f"[照片磚機器層回抽政策 0907] {_pm}: {_k}={_got!r} 應 {_want!r}")
_model_variants = {}
for _n, (_k, _d) in presets.items():
    if _k == "machine" and _d.get("instantiation") == "true" and _d.get("printer_model"):
        _model_variants.setdefault(_d["printer_model"], set()).add(_n)
_chk_fast = _chk_classic = 0
for _mn, (_mk, _md) in presets.items():
    if _mk != "machine_model":
        continue
    _vs = _model_variants.get(_mn)
    if not _vs:
        err(f"[機型無任何 machine preset] {_mn}")
        continue
    _is_classic = bool(_CLASSIC_MODEL_RE.match(_mn))
    _chk_classic += _is_classic
    _chk_fast += (not _is_classic)
    _have = [x for x in (_md.get("default_materials", "") or "").split(";") if x]
    _dead = [x for x in _have if x not in _ping_fils]
    if _dead:
        err(f"[default_materials 死名 0807] {_mn}: {'、'.join(_dead)} 不在 bundle 線材清單內")
    # 族群雙向隔離（Eric 0807 二裁）：Classic 機只預勾 Classic 料、Fast 機只預勾非 Classic 料
    # 條件式排除（0822 甲）：機型的所有 variant 都掛了 PHOTOTILE ⇒ 帶排除條件的料不算相容
    _all_photo = bool(_vs) and _vs <= _phototile_machines
    _want = {n for n, cp in _ping_fils.items()
             if (cp is None or (set(cp) & _vs)) and (("Classic" in n) == _is_classic)
             and not (_all_photo and n in _fil_cond)}
    _missing = sorted(_want - set(_have))
    if _missing:
        err(f"[預勾漏勾 0807] {_mn}: 相容卻沒進 default_materials — {'、'.join(_missing)}")
    _spill = sorted(x for x in _have if ("Classic" in x) != _is_classic)
    if _spill:
        _dir = "Fast 線材外溢前代機" if _is_classic else "Classic 線材外溢 Fast 機"
        err(f"[族群隔離破口 0807·{_dir}] {_mn}: {'、'.join(_spill)}")
# SOP §9 通則：照名字分類的斷言跑完要對數量，對不上＝分類規則錯了、不是資料錯了
if _chk_fast + _chk_classic != len([1 for _k, _ in presets.values() if _k == "machine_model"]):
    err(f"[預勾護欄分類漏台 0807] Fast {_chk_fast}＋Classic {_chk_classic} 未涵蓋全部機型")

# 🔴 型別護欄（0725 T004 事故）：`renamed_from` 必須是**字串**，寫成 JSON 陣列會讓
# PresetBundle.cpp:4098 的 unescape_strings_cstyle 收到 array → nlohmann 丟
# type_error.302「type must be string, but is array」→ 該 filament 檔載入失敗
# → **整包 PING vendor 解析中止** → 使用者開起來沒有任何 PING 機型、跳設定精靈、
#   機器掉成 Default Printer。多個舊名用 **分號** 串接（Config.cpp:146 以 ';' 分隔）。
# 教訓：verify 過去只查「參照與值」，查不到「引擎能不能載入」——這類型別錯是啞的。
# 顏色鍵護欄（0725）：線材 preset 只准 `default_filament_colour`。
# `filament_colour` 在 Preset.cpp:960 的 filament_options 是註解掉的＝引擎會剝掉並刷 log；
# 複數版（filament_colors／default_filament_colors）更是引擎根本不認的舊誤植。
for _n, (_k, _d) in presets.items():
    if _k == "filament":
        for _dead in ("filament_colour", "filament_colors", "default_filament_colors"):
            if _dead in _d:
                err(f"[線材顏色鍵殘留 — 引擎會剝掉] {_n}: 不應有 {_dead}（只留 default_filament_colour）")

for _n, (_k, _d) in presets.items():
    _rf = _d.get("renamed_from")
    if _rf is not None and not isinstance(_rf, str):
        err(f"[renamed_from 型別錯 — 會讓整包 vendor 載入失敗] {_n}: {_rf!r}（須為分號字串）")

# renamed_from 舊名唯一性（0728 基礎支改名首驗實抓：_classic_filament 從母檔複製把
# renamed_from 一起帶進 Classic 210/EDU ⇒ 兩支搶同一舊名、引擎解析任挑一支＝靜默地雷）
_rf_claims = {}
for _n, (_k, _d) in presets.items():
    _rf = _d.get("renamed_from")
    if isinstance(_rf, str):
        for _tok in _rf.split(";"):
            _tok = _tok.strip()
            if _tok:
                _rf_claims.setdefault((_k, _tok), []).append(_n)
for (_k, _tok), _ns in sorted(_rf_claims.items()):
    if len(_ns) > 1:
        err(f"[renamed_from 舊名重複認領] {_tok!r} ({_k}): {_ns!r}")


# ★ 跨層護欄・Classic M6050 換刀抑制（0729 Klipper #128）：change_filament_gcode 用 M6050 模板時，
#   引擎兩個呼叫端（wipe tower 路徑／一般路徑）靠 custom_gcode_changes_tool 判斷「模板是否已換刀」，
#   原版只認行首 T<n> ⇒ M6050 模板會被視為沒換刀而**補發裸 Tn**（赤兔板炸）。C++ 已加 M6050 認定；
#   本護欄鎖住它不被上游同步/重構沖掉。
_gcodecpp = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(PINGDIR))), "src", "libslic3r", "GCode.cpp")
if not os.path.isfile(_gcodecpp):
    err(f"[跨層護欄] 找不到 {_gcodecpp}（路徑推導失效，護欄形同虛設）")
else:
    _gsrc = io.open(_gcodecpp, encoding="utf-8", errors="ignore").read()
    _gm = re.search(r"custom_gcode_changes_tool\s*\([^)]*\)\s*\{(.{0,800})", _gsrc, re.S)
    if not _gm or "M6050" not in _gm.group(1):
        err("[跨層護欄・Classic M6050 換刀抑制] GCode.cpp custom_gcode_changes_tool 未認 M6050 ⇒ 模板後會補裸 Tn")

# ★ 跨層護欄（Eric 2026-07-26 兩爆之後補）：C++ 的「組合製程→線材連動」表必須跟得上 profile。
#   `Tab.cpp` 的 ping_apply_combo_filaments() 用**硬寫的線材名**呼叫 find_preset()，
#   而 find_preset() **不走 renamed_from 回溯**（那是 find_preset2）⇒ 名字對不上就是靜默失效：
#     ①0725 ABS 三支併一後，表裡仍寫「PING ABS - 250」⇒ ABS+ABS／ABS+SUP／棧板連動全啞
#     ②0725 新出 PLA+PVA 製程，沒補進表 ⇒ 選了模式第 2 槽不會變 PVA
#   兩個都是「verify 全綠、成品驗收全過」卻壞掉的類型——因為過去沒有任何一條檢查跨到 C++ 這層。
_repo = os.path.dirname(os.path.dirname(os.path.dirname(PINGDIR)))
_tab = os.path.join(_repo, "src", "slic3r", "GUI", "Tab.cpp")
if not os.path.isfile(_tab):
    err(f"[跨層護欄] 找不到 {_tab}（路徑推導失效，護欄形同虛設）")
else:
    _src = io.open(_tab, encoding="utf-8", errors="ignore").read()

    def _cstr(lit):
        # 把 C 字串字面值（含 \xNN 逸出）還原成 Python str
        out = bytearray(); i = 0
        while i < len(lit):
            if lit[i] == "\\" and i + 1 < len(lit) and lit[i + 1] == "x":
                out.append(int(lit[i + 2:i + 4], 16)); i += 4
            else:
                out.append(ord(lit[i])); i += 1
        return out.decode("utf-8", "replace")

    # 1) 常數表列的線材名都必須存在於 bundle
    _names = {}
    for m in re.finditer(r'constexpr\s+const\s+char\s*\*\s*(PING_\w+)\s*=\s*"((?:[^"\\]|\\.)*)"', _src):
        _names[m.group(1)] = _cstr(m.group(2))
    # 🆕 2026-08-14 批2 R1：PING_TOK_* 是「製程名 token」不是線材名 ⇒ 不能套線材存在性斷言
    #    （會誤紅）。但它們同樣會被改名批打死（0811「棧板」→「筏層」就是先例）⇒ **換判準、不放寬**：
    #    token 必須真的出現在某支製程名裡。
    _tok_names = {k: v for k, v in _names.items() if k.startswith("PING_TOK_")}
    _fil_names = {k: v for k, v in _names.items() if not k.startswith("PING_TOK_")}
    if not _fil_names:
        err("[跨層護欄] Tab.cpp 抓不到任何 PING_* 線材常數（格式變了？護欄失效）")
    if not _tok_names:
        err("[跨層護欄] Tab.cpp 抓不到任何 PING_TOK_* 製程 token 常數（批2 R1 家族分類器失去護欄）")
    for _k, _v in sorted(_fil_names.items()):
        if _v not in presets:
            err(f"[跨層護欄・C++ 線材名對不上 profile] Tab.cpp {_k} = {_v!r} 不在 bundle ⇒ 組合連動會靜默失效")
    # ⚠ PING_TOK_RAFT_OLD 是**刻意保留的舊名**（使用者自存的舊名製程仍要連動得到）⇒ 全庫查無屬正常，
    #   不列入斷言。其餘 token 查無＝分類器對不上實際製程名，家族過濾與自動收斂會靜默失效。
    _proc_names = [_n for _n, (_k2, _d2) in presets.items() if _k2 == "process"]
    for _k, _v in sorted(_tok_names.items()):
        if _k == "PING_TOK_RAFT_OLD":
            continue
        if not any(_v in _pn for _pn in _proc_names):
            err(f"[跨層護欄・C++ 製程 token 對不上 profile] Tab.cpp {_k} = {_v!r} 沒出現在任何製程名"
                " ⇒ 家族分類與配料連動會靜默失效")

    # 2) 功能歸類改名批（0730、Codex 四輪定稿）：兩張連動表**分別**解析、逐張對期望配對 baseline
    #    exact 比對（缺鍵/多鍵/配錯在冊線材皆紅——二輪必改 10）；process token 與兩張 map 雙向相等。
    def _parse_combo_map(src, map_name):
        m = re.search(re.escape(map_name) + r"\s*=\s*\{(.*?)\n\s*\};", src, re.S)
        if not m:
            return None
        body = m.group(1)
        pairs = {}
        for k, v1, v2 in re.findall(r'\{"([^"]+)",\s*\{([A-Za-z_0-9]+),\s*([A-Za-z_0-9]+)\}\}', body):
            pairs[cxx_unescape(k)] = (v1, v2)
        return pairs

    _consts = {k: cxx_unescape(v) for k, v in
               re.findall(r'constexpr const char \*(\w+)\s*=\s*"([^"]*)"', _src)}
    for _map_name, _expected in (("COMBO_FILAMENTS", EXPECTED_COMBO_MAP),
                                 ("COMBO_FILAMENTS_HF", EXPECTED_COMBO_MAP_HF)):
        # ⚠ 先剝註解（2026-09-19 反向測試實抓，牌 c-0919-ETR-01）：原本吃未剝註解的 _src，
        #   把某一列 `//` 註解掉、那列在 C++ 裡已經不存在，本護欄仍把它解析成有效鍵＝假綠
        #   （SOP_改製程名與列印範圍 §4 同型；當時只修了空 token 那條，這裡一直沒修到）。
        _pairs = _parse_combo_map(strip_cxx_comments(_src), _map_name)
        if _pairs is None:
            err(f"[跨層護欄] Tab.cpp 抓不到 {_map_name}（格式變了？護欄失效）")
            continue
        _resolved = {k: (_consts.get(a, a), _consts.get(b, b)) for k, (a, b) in _pairs.items()}
        if set(_resolved) != set(_expected):
            err(f"[跨層護欄・{_map_name} 鍵集合] 實得 {sorted(_resolved)!r} ≠ 期望 {sorted(_expected)!r}")
        for _k, _exp_pair in _expected.items():
            if _k in _resolved and _resolved[_k] != _exp_pair:
                err(f"[跨層護欄・{_map_name} 配對] {_k}: {_resolved[_k]!r} ≠ 期望 {_exp_pair!r}")
        for _k, (_a, _b) in _resolved.items():
            for _fil in (_a, _b):
                if _fil not in presets:
                    err(f"[跨層護欄・{_map_name} 線材不在 bundle] {_k} → {_fil!r}")
    # token 契約（三輪建議 1 落點）：五 token 禁含 ASCII 空白與 @；抽取函數 exact 復原
    for _t in COMBO_TOKENS:
        if " " in _t or "@" in _t:
            err(f"[跨層護欄・token 契約] {_t!r} 含空白或 @＝Tab.cpp 最後空白後擷取會壞")
        if combo_token(f"0.2mm {_t} @FD300 (0.4)") != _t:
            err(f"[跨層護欄・token 契約] {_t!r} 經抽取函數無法 exact 復原")
    # process token ↔ map 雙向相等（每支雙料組合製程都有連動；map 無多餘鍵已在鍵集合查過）
    _proc_tokens = set()
    for _n, (_k, _d) in presets.items():
        if _k == "process" and _d.get("instantiation") == "true":
            _t = combo_token(_n)
            if _t:
                _proc_tokens.add(_t)
    if _proc_tokens != COMBO_TOKENS:
        err(f"[跨層護欄・process token 集合] 實得 {sorted(_proc_tokens)!r} ≠ 期望三類（0811 改名後）")
    # 0811 新增：無 token 的兩類雙料製程必須存在且成對（每台雙料本體機、每口徑各一）——
    # 它們是這次「統一命名」的產物，一旦產生器回頭把 token 加回來，這條會紅。
    _plain = {n for n, (k, dd) in presets.items()
              if k == "process" and dd.get("instantiation") == "true" and combo_kind(n, dd) == CAT_PLAIN_DUAL}
    _rdual = {n for n, (k, dd) in presets.items()
              if k == "process" and dd.get("instantiation") == "true" and combo_kind(n, dd) == CAT_RAFT_DUAL}
    if not _plain or not _rdual:
        err(f"[跨層護欄・無 token 雙料兩類] 一般雙料 {len(_plain)} 支／雙料筏層 {len(_rdual)} 支，兩者都不該是 0")
    elif len(_plain) != len(_rdual):
        err(f"[跨層護欄・無 token 雙料兩類] 支數不成對：一般 {len(_plain)}／筏層 {len(_rdual)}")
    for _n in _plain:
        if combo_token(_n) is not None or "_筏層" in _n:
            err(f"[跨層護欄・一般雙料應無 token] {_n}")
    # Tab.cpp 必須有「空 token ⇒ 雙料機當 PLA+PLA」那條分支（Eric 2026-08-11 裁）。
    # 沒有它，一般雙料版選了不會自動配料＝靜默失效（正是 0725 T004 那型的坑）。
    if os.path.isfile(_tab):
        # ⚠ **必須剝掉註解再比對**（2026-08-11 反向測試實抓）：本守衛原本只做「字串存在」比對，
        #   把 `combo.empty()` 註解掉、行為已死，守衛卻因為註解裡那份仍在而照樣綠 ⇒ 假綠。
        #   同型風險存在於所有「grep 原始碼字面」的跨層護欄，本批先修這一條。
        _tsrc = strip_cxx_comments(io.open(_tab, encoding="utf-8", errors="ignore").read())
        if "combo.empty()" not in _tsrc:
            err("[跨層護欄・空 token 分支] Tab.cpp 找不到 combo.empty() ⇒ 一般雙料版連動會靜默失效")
        for _c in ("PLAIN_DUAL", "PLAIN_DUAL_HF"):
            if _c not in _tsrc:
                err(f"[跨層護欄・空 token 分支] Tab.cpp 缺 {_c} 常數")
        if "filament_presets.size() != 2" not in _tsrc:
            err("[跨層護欄・空 token 分支] Tab.cpp 缺「槽數==2」判準 ⇒ 單料/四料可能被誤套 PLA+PLA")
    # 3) 🆕 G3（2026-08-14 批2・連動規格 R7）：C++ 連動碼存活護欄（取代已退役的 #39 護欄）。
    #    ⚠ 一律先 strip_cxx_comments 再比對——0811 實抓：把 `combo.empty()` 註解掉、行為已死，
    #      守衛卻因為註解裡那份仍在而照樣綠＝假綠。
    #    ⚠ needle 一律帶前後文——0812 實抓：`set(PING_TEST_BUILD` 是 `..._XX` 的子字串，改名照樣綠。
    _G3_NEEDLES = [
        ("src/slic3r/GUI/Tab.cpp", [
            ("PingFamily ping_derive_family()",                   "R1 家族推導本體"),
            ("PingFamily ping_classify_process(",                 "R1 製程名分類本體"),
            # 🆕 0919（牌 c-0919-ETR-01）：易拆樹狀歸易拆家族。常數存在性斷言只查「token 出現在製程名」，
            #    分類那行被刪掉照樣綠（反向測試實抓）⇒ 另鎖這一行；少了它＝SupPLA 盤上下拉看不到樹狀版。
            ("(pn.token == PING_TOK_EASY_TREE)  r.fam = PingFamily::EASY;", "易拆樹狀歸易拆家族（0919）"),
            ("ping_parse_process_name(process_name)",             "R1 配料連動與家族分類共用同一支解析器"),
            ('option<ConfigOptionBools>("filament_is_support")',  "家族軸讀對鍵（⛔ 不可改用 filament_type 判材料角色）"),
            ('option<ConfigOptionBools>("filament_soluble")',     "水溶軸讀對鍵"),
            ("void ping_converge_process()",                      "R3 自動收斂本體"),
            ("if (!cur.is_system) { redraw(); return; }",         "R3-3 非系統支永不被自動換走"),
            ("if (allowed.empty()) { redraw(); return; }",        "R2-2 fail-open（FF 四料／3in1／Classic 零迴歸靠它）"),
            ("s_ping_converge_guard = true;",                     "防迴圈保險絲置位"),
            # 🆕 批3
            ("void ping_backfill_new_slots(size_t old_count)",    "R5-2 擴槽 back-fill 本體（3in1 carry-over 治本）"),
            ("ping_backfill_new_slots(old_n);",                   "R5-2 掛在 PING 槽數同步之後"),
            ("if (user_initiated && m_type == Preset::TYPE_PRINTER && !canceled)",
                                                                  "R5-1 趟尾收斂（含 canceled 守衛）"),
            ("select_preset(preset_name, false, \"\", false, false, true)",
                                                                  "R5-1 Tab combo 使用者手勢呼叫點"),
        ]),
        # ⚠ needle 必須是**純程式碼**：G3 是先 strip_cxx_comments 再比對，
        #   帶註解的 needle（如 `/*user_initiated*/ true`）永遠找不到（本護欄第一版實錯）。
        ("src/slic3r/GUI/Tab.hpp", [
            ("bool user_initiated = false);",                     "R5-1 來源參數宣告（預設 false＝既有呼叫點零改變）"),
            ("void       ping_backfill_new_slots(size_t old_count);",
                                                                  "R5-2 back-fill 對外宣告（兩個槽數同步點共用）"),
        ]),
        ("src/slic3r/GUI/GUI_App.cpp", [
            ("ping_backfill_new_slots(old_filament_count);",      "R5-2 第二個槽數同步點（精靈選機頁/啟動載入）"),
        ]),
        ("src/slic3r/GUI/PresetComboBoxes.cpp", [
            ("ping_derived = ping_derive_family();",              "R2 下拉過濾取推導家族"),
            ("if (ping_filter_active && preset.is_system && i != idx_selected &&",
                                                                  "R2 過濾＋非系統支/選中兩道豁免（缺 is_system＝使用者自存製程會集體消失）"),
        ]),
        ("src/slic3r/GUI/Plater.cpp", [
            ("ping_converge_process();",                          "R3 掛在 on_select_preset（側欄換料主路徑）"),
            ("select_preset(preset_name, false, \"\", false, false, true)",
                                                                  "R5-1 側欄印表機下拉＝使用者手勢"),
            ("select_preset(preset->name, false, \"\", false, false, true)",
                                                                  "R5-1 口徑下拉＝使用者手勢（切口徑那條連動的接入點）"),
        ]),
    ]
    _g3_src = {}
    for _rel, _needles in _G3_NEEDLES:
        _fp = os.path.join(_repo, *_rel.split("/"))
        if not os.path.isfile(_fp):
            err(f"[跨層護欄・G3] 找不到 {_rel}（G3 形同虛設）")
            continue
        _g3_src[_rel] = strip_cxx_comments(io.open(_fp, encoding="utf-8", errors="ignore").read())
        for _needle, _why in _needles:
            if _needle not in _g3_src[_rel]:
                err(f"[跨層護欄・G3 連動碼存活] {_rel} 缺「{_why}」：找不到 {_needle!r}")
    # G3-a 防空轉：Tab.cpp 至少要有「定義 1 處 ＋ 呼叫 ≥1 處」，只剩定義＝連動根本沒接上。
    # ⚠ 必須數**帶左括號**的形式：光數 "ping_converge_process" 會把 log 訊息裡那個字串字面也算進去
    #   （strip_cxx_comments 只剝註解、不剝字串）⇒ 拿掉呼叫後仍有 2 個而假綠（本護欄第一版實錯，
    #   反向測試當場抓到）。定義 `void ping_converge_process()` 與呼叫 `ping_converge_process();`
    #   都帶括號，log 裡的 `"ping_converge_process: ..."` 不帶 ⇒ 恰好只數到真正的程式碼。
    # ⚠ 門檻要隨掛點數量一起長：批2 是「定義＋線材分頁掛點」＝2；批3 又加了「切機趟尾」＝3。
    #   忘了同步調高＝拿掉一個掛點仍達標而假綠（批3 反向測試當場抓到）。取 ≥3 且用最小值語意，
    #   日後新增掛點不會誤紅，但少任何一個現有掛點就會紅。
    _PING_CONVERGE_MIN = 3   # 定義 ＋ 線材分頁掛點（批2）＋ 切機趟尾（批3）
    _t3 = _g3_src.get("src/slic3r/GUI/Tab.cpp", "")
    if _t3 and _t3.count("ping_converge_process(") < _PING_CONVERGE_MIN:
        err("[跨層護欄・G3-a] Tab.cpp 的 ping_converge_process( 只出現 "
            f"{_t3.count('ping_converge_process(')} 次（應 ≥{_PING_CONVERGE_MIN}"
            "＝定義＋線材分頁掛點＋切機趟尾）")
    # G3-b（Eric 0813 裁5＝b）：dirty 時照走既有「未儲存變更」對話框，
    #      ⛔ 不得用 force_select 繞過——那是無提示直接丟棄使用者的修改，比彈窗更糟。
    if _t3:
        _cv = _t3.find("void ping_converge_process()")
        if _cv >= 0:
            # ⚠ 範圍必須收斂到**函式本體**：Tab::select_preset 本身就定義在同檔後面、且合法使用
            #   force_select ⇒ 搜到檔尾會必定誤紅（本護欄第一版實錯）。頂層函式的收尾大括號在第 0 欄。
            _end  = _t3.find("\n}", _cv)
            _body = _t3[_cv:_end if _end > 0 else len(_t3)]
            if "force_select" in _body:
                err("[跨層護欄・G3-b 裁5＝b] ping_converge_process 本體出現 force_select"
                    "＝繞過未儲存變更對話框（Eric 0813 明禁：那是無提示丟棄使用者的修改）")
    # 🆕 G3-d（批3 R5-3・順序釘死）：補槽 → load_current_preset() → 趟尾收斂。
    # 🔴 這條是本批**最重要**的護欄：兩段修復都能合法插在同一個錨點，**裝反就等於沒修**，
    #    而且 code review 看不出來（兩種寫法都「看起來對」）⇒ 只能靠護欄釘死。
    #    收斂若先跑：切到 FD 雙料且無快照時，slot2 當下還是 slot1 的複製（PLA）⇒ 判「一般」⇒
    #    收斂到一般製程，接著補槽才把 slot2 填成 SupPLA ⇒ 家族變易拆、製程停在一般＝症狀原地復發。
    if _t3:
        _i_fill = _t3.find("ping_backfill_new_slots(old_n);")
        _i_load = _t3.find("load_current_preset();", _i_fill) if _i_fill >= 0 else -1
        _i_conv = _t3.find("if (user_initiated && m_type == Preset::TYPE_PRINTER && !canceled)")
        if _i_fill < 0 or _i_load < 0 or _i_conv < 0:
            err("[跨層護欄・G3-d 批3 R5-3] 補槽／load_current_preset／趟尾收斂 三個錨點抓不齊"
                f"（fill={_i_fill} load={_i_load} conv={_i_conv}）⇒ 順序護欄形同虛設")
        elif not (_i_fill < _i_load < _i_conv):
            err("[跨層護欄・G3-d 批3 R5-3] **順序錯了**：必須是 補槽 → load_current_preset → 趟尾收斂"
                f"（實際位移 fill={_i_fill} load={_i_load} conv={_i_conv}）"
                "；收斂先跑＝3in1/FD 雙料的家族判定會讀到還沒補的槽，症狀原地復發")
    # G3-c（Eric 0813 裁4）：#39 筏層建議對話框已退役，不得復活（自動收斂已涵蓋＝復活就是重複打擾）。
    _p3 = _g3_src.get("src/slic3r/GUI/PresetComboBoxes.cpp", "")
    if _p3 and "ping_suggest_pallet_for_abs" in _p3:
        err("[跨層護欄・G3-c 裁4] ping_suggest_pallet_for_abs 又出現在 PresetComboBoxes.cpp"
            "（#39 對話框已於批2 退役）")
    # 4) C-12 renamed 回溯（Eric 2026-07-30 裁）：orca_presets 載入端（load_selections＋
    #    update_selections）的 strict 選擇與多料槽 filament_XX 必須帶 renamed resolver——
    #    select_preset_by_name_strict 是 exact-only，系統 preset 改名批後升級版機器 conf
    #    記的舊名會靜默 fallback 掉使用者記住的選擇（C-12 產品級缺口；切機＝主場景）。
    _pbc = os.path.join(_repo, "src", "libslic3r", "PresetBundle.cpp")
    if not os.path.isfile(_pbc):
        err(f"[跨層護欄] 找不到 {_pbc}（C-12 護欄形同虛設）")
    else:
        _pbsrc = io.open(_pbc, encoding="utf-8", errors="ignore").read()
        for _pat, _need in (("get_preset_name_renamed(initial_print_profile_name", 2),
                            ("get_preset_name_renamed(initial_filament_profile_name", 2),
                            ("get_preset_name_renamed(fp_name", 2)):
            _got = _pbsrc.count(_pat)
            if _got < _need:
                err(f"[跨層護欄・C-12 renamed 回溯] PresetBundle.cpp {_pat!r} 出現 {_got} 次"
                    f"（應 ≥{_need}＝load_selections＋update_selections 各一）")

# 🆕 甲案覆蓋盤點（Eric 2026-09-20 裁甲案；2026-09-21 自開發線回移並改寫，牌 c-0921-CONV-07）：
# 離線重算「線材組合 → 允許的製程」，確認「ABS 只能選到筏層製程、PLA 選不到」成立，
# 且**沒有機型會被打成零製程**。把當初做決策時的實查數字釘成回歸測試。
# 為什麼上面的 G3 不夠：G3 驗的是「那幾支 C++ 函式還在」，驗不到「規則套下去之後每台機還剩哪些
#   製程」——那要拿 profile 的 compatible_printers 重算才知道。少一支相容製程＝下拉整個空掉，
#   而 C++ 那層看起來完全正常。
# ⚠ 刻意掛在**模組層**（同 G2 的理由）：本段只吃 profile，不該因為找不到 Tab.cpp 就整條靜默失效。
# 🔴 分類必須與 Tab.cpp 同語意，動了 C++ 那邊就要回來同步改這裡：
#   ping_parse_process_name()：head＝'@' 之前去尾空白；token＝head 末端空白後那一段；
#     raft_suffix＝head 含「_筏層」（現行）或「_棧板」（舊名相容）。
#   ping_classify_process()：易拆水溶→EASY_SOL；易拆+筏層→(EASY, 筏層恆 True)；
#     易拆／易拆樹狀→EASY；其餘（含未知 token 與不合命名規範）→PLAIN，筏層照 raft_suffix。
#   ping_derive_family()：有支撐料→EASY（水溶→EASY_SOL）；筏層＝本體槽全是 ABS。
# ⚠ 「有沒有筏層」在兩個地方用不同判準，不是筆誤：_classify 內用「_筏層／_棧板」（層高後綴，
#   對應 raft_suffix），機器/製程層面用不帶底線的「筏層／棧板」——因為 token「易拆+筏層」不帶底線，
#   只比對底線版會把只有易拆+筏層的機器整台漏掉。
_RAFT_SFX = ("_筏層", "_棧板")

def _check_abs_raft_coverage():
    def _is_raftish(_n):
        return ("筏層" in _n) or ("棧板" in _n)

    def _classify(_name):
        """製程名 →（家族, 筏層）。Tab.cpp ping_classify_process 的離線鏡像。"""
        _at = _name.find("@")
        if _at <= 0:
            return ("PLAIN", False)          # 不符命名規範＝當一般（與未知 token 同待遇）
        _h = _name[:_at].rstrip()
        _rf = any(_s in _h for _s in _RAFT_SFX)
        _sp = _h.rfind(" ")
        _tk = _h[_sp + 1:] if _sp >= 0 else ""
        if _tk == COMBO_CAT_PVA:
            return ("EASY_SOL", _rf)
        if _tk == COMBO_CAT_EASYPAL:
            return ("EASY", True)
        if _tk in (COMBO_CAT_EASY, COMBO_CAT_EASYTREE):
            return ("EASY", _rf)
        return ("PLAIN", _rf)

    _by_printer = {}
    for _n, (_k, _d) in presets.items():
        if _k != "process" or _d.get("instantiation") != "true":
            continue
        for _pr in (_d.get("compatible_printers") or []):
            _by_printer.setdefault(_pr, []).append(_n)
    # ABS 會推導出兩種家族，**看機器有幾槽**：雙料機 ABS＋SupABS ⇒（易拆, 筏層）；
    # 單槽機（單料頭／同進／FP300）只放得下 ABS 本體、ABS+ABS 雙料也一樣 ⇒（一般, 筏層）。
    # 任一台機器只會走到其中一種，故判準＝「兩者聯集非空」，不是「兩者都要有」。
    _ABS_FAMS = (("EASY", True), ("PLAIN", True))
    _PLA_FAMS = (("EASY", False), ("PLAIN", False))
    _checked = 0
    for _pr, _ns in sorted(_by_printer.items()):
        if not [x for x in _ns if _is_raftish(x)]:
            continue                 # 該機沒有筏層製程（FF 全系）＝走 fail-open，不受本規則管
        _checked += 1
        _abs_ok = [x for x in _ns if _classify(x) in _ABS_FAMS]
        _pla_ok = [x for x in _ns if _classify(x) in _PLA_FAMS]
        if not _abs_ok:
            err(f"[跨層護欄・甲案覆蓋] {_pr}：有筏層製程卻推導不出任何 ABS 可用製程（過濾會誤殺）")
        for _x in _abs_ok:
            if not _is_raftish(_x):
                err(f"[跨層護欄・甲案覆蓋] {_pr}：ABS 組合仍可選到非筏層製程 {_x!r}（違反 Eric 0920 裁甲案）")
        if not _pla_ok:
            err(f"[跨層護欄・甲案覆蓋・反向] {_pr}：PLA 組合一支製程都不剩（過濾誤殺非 ABS 路徑）")
        for _x in _pla_ok:
            if _is_raftish(_x):
                err(f"[跨層護欄・甲案覆蓋・反向] {_pr}：PLA 組合竟可選到筏層製程 {_x!r}")
    if _checked == 0:
        err("[跨層護欄・甲案覆蓋・防空轉] 一台帶筏層製程的機器都沒掃到 ⇒ compatible_printers 建法"
            "或筏層命名有變，本段形同虛設")
    print("甲案覆蓋（ABS 只到筏層／PLA 只到非筏層）：帶筏層製程的機型 %d 台逐台離線重算" % _checked)

_check_abs_raft_coverage()

# 🆕 G2（Eric 2026-08-13 裁・連動規格批1）：C++ 連動表的配料 ↔ profile 屬性「語意一致」
# 為什麼：`Tab.cpp` 的 COMBO_FILAMENTS 是「類別預設配料」**捷徑表**（手選製程時自動帶哪兩支），
#   而新連動改用**材料屬性**（filament_is_support／filament_soluble）反推家族。兩者若對不上——
#   例如有人把 SupPLA 的 is_support 改成 0——就會出現「手選易拆製程配好料、屬性卻判成一般家族」
#   的分裂：下拉過濾與自動收斂全錯，而且**兩邊單獨看都正常**，只有交叉比對抓得到。
# ⚠ 刻意掛在**模組層**，不放進上面 Tab.cpp 存在性的 `else:` 區——G2 驗的是「baseline 常數 ↔ profile
#   屬性」，不該因為找不到 Tab.cpp 就整條靜默失效（本檔多處「護欄形同虛設」教訓）。
# ⚠ 依賴 G1：presets 不解 inherits ⇒ 屬性必須是顯式值（embed 4b-2g 補完才成立）。
def _fil_attr(_name):
    """回傳 (filament_is_support, filament_soluble) 的**顯式**值；非線材或不存在回 None。"""
    _e = presets.get(_name)
    if not _e or _e[0] != "filament":
        return None
    return (_e[1].get("filament_is_support"), _e[1].get("filament_soluble"))

_g2_checked = 0
for _mapname, _map in (("COMBO_FILAMENTS", EXPECTED_COMBO_MAP),
                       ("COMBO_FILAMENTS_HF", EXPECTED_COMBO_MAP_HF)):
    for _cat, (_s1, _s2) in _map.items():
        for _slot, _fil in (("槽1", _s1), ("槽2", _s2)):
            _at = _fil_attr(_fil)
            if _at is None:
                err(f"[G2・配料屬性] {_mapname} {_cat} {_slot}：{_fil!r} 不在 bundle 或非線材")
                continue
            _g2_checked += 1
            _is_sup, _sol = _at
            if _slot == "槽1":
                # 槽1＝本體材料，恆非支撐、非水溶（否則筏層軸的「本體全 ABS」判定會算進支撐槽）
                if _is_sup != ["0"] or _sol != ["0"]:
                    err(f"[G2・本體槽屬性] {_mapname} {_cat} 槽1 {_fil}: "
                        f"is_support={_is_sup!r} soluble={_sol!r}, expected ['0']／['0']")
            else:
                # 槽2＝三 token 皆為支撐材；否則家族軸推不出「易拆」
                if _is_sup != ["1"]:
                    err(f"[G2・支撐槽屬性] {_mapname} {_cat} 槽2 {_fil}: "
                        f"is_support={_is_sup!r}, expected ['1']（否則家族軸判不出易拆）")
                # 水溶有且只有「易拆水溶」那一類（PVA）
                _want_sol = ["1"] if _cat == COMBO_CAT_PVA else ["0"]
                if _sol != _want_sol:
                    err(f"[G2・水溶語意] {_mapname} {_cat} 槽2 {_fil}: "
                        f"soluble={_sol!r}, expected {_want_sol!r}")
# 一般雙料（無 token）：兩槽皆本體材料。順手把 EXPECTED_PLAIN_DUAL(_HF) 接起來
# ——這兩個常數自定義以來全檔沒有任何地方用到（0813 實查），等於一直缺這道護欄。
for _dname, _dpair in (("PLAIN_DUAL", EXPECTED_PLAIN_DUAL),
                       ("PLAIN_DUAL_HF", EXPECTED_PLAIN_DUAL_HF)):
    for _fil in _dpair:
        _at = _fil_attr(_fil)
        if _at is None:
            err(f"[G2・配料屬性] {_dname}：{_fil!r} 不在 bundle 或非線材")
            continue
        _g2_checked += 1
        if _at[0] != ["0"] or _at[1] != ["0"]:
            err(f"[G2・一般雙料屬性] {_dname} {_fil}: "
                f"is_support={_at[0]!r} soluble={_at[1]!r}, expected ['0']／['0']")
# 防空轉（同本檔既有範式）：掃到 0 筆＝常數或 presets 建法有變，護欄形同虛設
if _g2_checked == 0:
    err("[G2・防空轉] 配料屬性一條都沒驗到 ⇒ baseline 常數或 presets 建法有變，護欄形同虛設")

# ★ 功能歸類普查（0730 改名批）：五 token × 18 支 exact；舊材料對名歸零
_combo_census = {}
_combo_cover = {}   # token -> {「機型 (口徑)」}；判準改成「三個 token 覆蓋同一組機型×口徑」，見下方
for _n, (_k, _d) in presets.items():
    if _k != "process":
        continue
    # 🔴 3in1 不計入雙料組合普查（Eric 2026-08-17 改名批後補）：它是**機器變體**、走 ff_extra 範本，
    #    不是那 18 支一組的雙料組合製程。改名補上「易拆」token 後會被 combo_token 認到 ⇒ 18 變 24。
    #    ⚠ 同 combo_kind() 的排除理由：維持改名前的既有計數行為，不是放寬標準。
    if "3in1" in _n:
        continue
    _t = combo_token(_n)
    if _t:
        _combo_census[_t] = _combo_census.get(_t, 0) + 1
        _combo_cover.setdefault(_t, set()).add(_n.split(" %s @" % _t, 1)[1])
    for _old in COMBO_OLD_TOKENS:
        if (" %s @" % _old) in _n:
            err(f"[功能歸類・舊材料對名殘留] {_n}")
# 🔴 2026-09-11 改判準（牌 `c-0911-NZ-01`）：原本是「每個 token 恰 18 支」的硬寫數字。
#    那個 18 是「當時有幾組 機型×口徑」的快照，**每加一個口徑就會過期一次**——
#    0909 替 FD450/600/800 Pro 加 0.25 之後它就變 21，而數字本身說不出哪裡不對。
#    真正要守的不變量是：**三個 token 必須覆蓋完全相同的一組「機型 (口徑)」**
#    （每一組雙料機×口徑都要備齊 易拆／易拆水溶／易拆+筏層 三支）。
#    這條比數字嚴：數字相同但少一組、多一組也會被抓；而且加口徑不會讓它過期。
#    ⚠ 另留一道下限，擋「三個 token 一起掉光」這種數字仍相等的退化；
#      合法地拿掉機型時才需要調它，調的時候請連同理由一起改。
_COMBO_COVER_FLOOR = 18   # 下限＝歷史最低（0.25 那批之前的組數），**不是**期望值。
#                            刻意設在 18 而非當前的 21：這支 verify 要能同時跑在「已 regen」與
#                            「未 regen」的樹上（CI 跑的是後者）。設 21 會讓現行出貨線直接紅。
_cov = {_t: _combo_cover.get(_t, set()) for _t in COMBO_TOKENS}
_union = set().union(*_cov.values()) if _cov else set()
for _t in sorted(COMBO_TOKENS):
    _missing = _union - _cov[_t]
    if _missing:
        err(f"[功能歸類・{_t} 覆蓋缺口] 缺 {len(_missing)} 組：{sorted(_missing)[:6]}")
if len(_union) < _COMBO_COVER_FLOOR:
    err(f"[功能歸類・覆蓋組數低於下限] 實得 {len(_union)} < {_COMBO_COVER_FLOOR}")
# id baseline（二輪必改 14／四輪修訂 C）：改名前快照＝舊名→新名→setting_id 90 條 exact，
# 防重構位移／PVA 插回主迴圈／依新名重排 emission（fixture＝regen 前 dump、進 repo）。
_idb_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "combo_rename_id_baseline.json")
if not os.path.isfile(_idb_path):
    err("[功能歸類・id baseline] fixture 不存在（combo_rename_id_baseline.json）")
else:
    _idb = json.load(io.open(_idb_path, encoding="utf-8"))
    if len(_idb) != 90:
        err(f"[功能歸類・id baseline] fixture 應 90 條、實得 {len(_idb)}")
    for _row in _idb:
        _ent = presets.get(_row["new"])
        if not _ent or _ent[0] != "process":
            err(f"[功能歸類・id baseline] 新名不存在：{_row['new']}")
            continue
        if _ent[1].get("setting_id") != _row["setting_id"]:
            err(f"[功能歸類・id baseline] {_row['new']}: setting_id={_ent[1].get('setting_id')!r} ≠ {_row['setting_id']!r}（位移！）")
        # 0811 起 renamed_from 是**分號分隔的回溯鏈**（①材料對原名 ②0730 五類名）；
        # 本 fixture 的 old 欄＝材料對原名＝鏈的第一條，必須 exact 命中（鏈完整性另有專查）。
        _rf_chain = (_ent[1].get("renamed_from") or "").split(";")
        if not _rf_chain or _rf_chain[0] != _row["old"]:
            err(f"[功能歸類・id baseline] {_row['new']}: renamed_from 鏈首={_rf_chain[0] if _rf_chain else None!r}"
                f" ≠ {_row['old']!r}（全鏈={_ent[1].get('renamed_from')!r}）")

# ★ 檢查：FD300 關門的列印範圍＝圓角三角形（Eric 2026-08-11 裁，產生器 rounded_triangle_area）
#   幾何條件（兩條就鎖死形狀）：①三圓角貼合 Ø300 ⇒ 弧上每點離床心恰 150
#                               ②三角形內切圓 Ø200 ⇒ 三條直邊各距床心 100
#   ⚠ **凸性是硬條件**：引擎 BuildVolume 對凹形（Type::Custom）的碰撞判定會退回用凸包
#     （BuildVolume.cpp:78-79）＝凹口不會被擋；凸形才走 Type::Convex 精準路徑。
_tri_machines = [(_n, _d) for _n, (_k, _d) in presets.items()
                 if _k == "machine" and "FD300 關門" in _n and isinstance(_d.get("printable_area"), list)]
if not _tri_machines:
    err("[關門床形] 找不到任何 FD300 關門機型的 printable_area（改床形的閘門形同虛設）")
for _n, _d in _tri_machines:
    _pts = []
    for _p in _d["printable_area"]:
        _x, _y = _p.split("x")
        _pts.append((float(_x), float(_y)))
    if len(_pts) < 12:
        err(f"[關門床形] {_n}: 點數 {len(_pts)} 過少，不像圓角三角形")
        continue
    _r = [math.hypot(x, y) for x, y in _pts]
    if abs(max(_r) - 150.0) > 0.01 or min(_r) < 100.0 - 0.01:
        err(f"[關門床形・圓角貼合 Ø300] {_n}: 離床心距離 min={min(_r):.4f} max={max(_r):.4f}，"
            f"期望 max=150（弧在 Ø300 上）且 min≥100")
    # 內切圓 Ø200：任一點都不得落在半徑 100 的圓內（三直邊恰切於該圓）
    if min(_r) < 99.99:
        err(f"[關門床形・內切圓 Ø200] {_n}: 有點離床心僅 {min(_r):.4f} < 100")
    # 凸性：逐點外積同號（多邊形為凸）
    _cross = []
    for _i in range(len(_pts)):
        _a, _b, _c = _pts[_i], _pts[(_i + 1) % len(_pts)], _pts[(_i + 2) % len(_pts)]
        _cross.append((_b[0] - _a[0]) * (_c[1] - _b[1]) - (_b[1] - _a[1]) * (_c[0] - _b[0]))
    if not (all(_v >= -1e-6 for _v in _cross) or all(_v <= 1e-6 for _v in _cross)):
        err(f"[關門床形・凸性] {_n}: 多邊形非凸 ⇒ 引擎會退回用凸包判定，凹口不會被擋")

# ★ 檢查：床形不對稱的機型必須明講盤心（Eric 2026-08-11 夜裁「走乙案」・T021 眼驗①處置）
#   引擎（`3DBed.cpp` update_model_offset）預設把床身 3D 模型擺在 printable_area 的**外框中心**。
#   床形對稱時外框中心＝盤心（圓床、矩形床皆然，所以上游從沒踩到）；圓角三角形外框 Y[-100,+150]
#   ⇒ 中心 (0,+25) ⇒ 圓盤被往 +Y 畫 25mm。⚠ 純渲染：碰撞判定走 m_build_volume 真實多邊形。
#   對策＝機台 preset 用 `bed_model_offset` 明講盤心；**空值維持引擎原行為 ⇒ 其他機型零影響**。
#   🔴 本閘門刻意寫成**通則而非特例**：任何「非矩形、也非以外框中心為圓心的圓」的床形都必須宣告
#      ⇒ P200+ 之後若也改成三角形卻忘了宣告，會在這裡被擋下來，而不是等使用者看到歪盤。
def _area_is_symmetric(_pts):
    """矩形（4 點）或「以外框中心為圓心的圓」＝外框中心本來就等於盤心，不必宣告。"""
    if len(_pts) == 4:
        return True
    _cx = (min(_p[0] for _p in _pts) + max(_p[0] for _p in _pts)) / 2.0
    _cy = (min(_p[1] for _p in _pts) + max(_p[1] for _p in _pts)) / 2.0
    _rr = [math.hypot(_p[0] - _cx, _p[1] - _cy) for _p in _pts]
    return (max(_rr) - min(_rr)) <= 0.05

_asym_checked = 0
for _n, (_k, _d) in sorted(presets.items()):
    if _k != "machine" or not isinstance(_d.get("printable_area"), list):
        continue
    _pts = []
    for _p in _d["printable_area"]:
        _x, _y = _p.split("x")
        _pts.append((float(_x), float(_y)))
    if len(_pts) < 3 or _area_is_symmetric(_pts):
        continue
    _asym_checked += 1
    _decl = _d.get("bed_model_offset")
    if not (isinstance(_decl, list) and len(_decl) == 1):
        _bx = (min(_p[0] for _p in _pts) + max(_p[0] for _p in _pts)) / 2.0
        _by = (min(_p[1] for _p in _pts) + max(_p[1] for _p in _pts)) / 2.0
        err(f"[床盤盤心] {_n}: 床形不對稱（外框中心 ({_bx:.3f}, {_by:.3f})）卻沒宣告 "
            f"`bed_model_offset` ⇒ 引擎會拿外框中心當盤心，圓盤會被畫歪。"
            f"在 embed_params.py 的 BED_OVERRIDE 補 `bed_model_center`")
        continue
    _ox, _oy = (float(_v) for _v in _decl[0].split("x"))
    # PING 的圓盤機 printable_area 一律以床心 (0,0) 為原點（見 embed_params.py scale_circle_area）
    # ⇒ 盤心恆為原點。日後若出現非中心原點的床，這條要連同該註解一起改，不要只放寬數值。
    if abs(_ox) > 1e-6 or abs(_oy) > 1e-6:
        err(f"[床盤盤心] {_n}: bed_model_offset={_decl[0]!r}，但 PING 圓盤機的盤心恆為床原點 0x0")
if _asym_checked == 0:
    err("[床盤盤心] 沒有掃到任何不對稱床形機型（關門應該要在內）⇒ 閘門形同虛設")

# ★ 檢查：床形不對稱的機型，其床貼圖 logo 必須完全落在可印範圍內（Eric 2026-08-11 夜裁「logo 下移」）
#   機制：床貼圖是「拉滿床形外框、再用床形裁切」（`3DBed.cpp:49-67` init_model_from_poly）
#         u=(x-min_x)/size_x；v_eff=1-(y-min_y)/size_y ⇒ **v=0 是影像上緣＝床 +Y（後方）**。
#   共用圖的 logo 垂直置中，映到三角外框後上緣兩側會被斜邊切掉（實測溢出 5.03mm）
#   ⇒ 關門改用專屬貼圖（原檔往床前緣平移 17mm，**只平移不重畫**＝CIS 鐵則）。
#   🔴 這裡驗的是**幾何結果**不是檔名：拿 fixture 記的**墨跡凸包頂點**反算世界座標，逐點要求在床形內。
#         凸包＝精確（可印區為凸多邊形，最糟點必落在凸包頂點上）；抽樣會漏掉真正最糟的點，0811 反向測試實抓過。
#   CI 沒有 PIL，所以墨跡凸包離線算好放進 `tools/ping/bed_texture_ink_extents.json`，
#   並用 SHA-256 綁住貼圖檔——換了圖沒重跑產生器就會被擋。
_ink_fix_path = os.path.join(PINGDIR, "..", "..", "..", "tools", "ping", "bed_texture_ink_extents.json")
_ink_fix_path = os.path.normpath(os.path.join(_repo, "tools", "ping", "bed_texture_ink_extents.json"))
_ink_fix = {}
if os.path.isfile(_ink_fix_path):
    _ink_fix = json.load(io.open(_ink_fix_path, encoding="utf-8"))
else:
    err(f"[床貼圖] 找不到墨跡 fixture {os.path.basename(_ink_fix_path)}（logo 裁切閘門形同虛設）")

def _poly_clearance(_pts, _x, _y):
    """凸多邊形內側餘裕（mm）；負＝在外面。"""
    _a2 = 0.0
    for _i in range(len(_pts)):
        _p, _q = _pts[_i], _pts[(_i + 1) % len(_pts)]
        _a2 += _p[0] * _q[1] - _q[0] * _p[1]
    _sgn = 1.0 if _a2 > 0 else -1.0          # CCW→1，CW→-1
    _best = None
    for _i in range(len(_pts)):
        _p, _q = _pts[_i], _pts[(_i + 1) % len(_pts)]
        _ex, _ey = _q[0] - _p[0], _q[1] - _p[1]
        _len = math.hypot(_ex, _ey)
        if _len < 1e-9:
            continue
        _d = _sgn * ((_ex * (_y - _p[1]) - _ey * (_x - _p[0])) / _len)
        _best = _d if _best is None else min(_best, _d)
    return _best if _best is not None else -1e9

_tex_checked = 0
for _mn, (_mk, _md) in sorted(presets.items()):
    if _mk != "machine_model":
        continue
    # 找這個機型底下任一支機台 preset 拿 printable_area（同機型各口徑同形，前面閘門已驗過一致）
    _own = [_d for _n2, (_k2, _d) in presets.items()
            if _k2 == "machine" and _n2.startswith(_mn + " ") and isinstance(_d.get("printable_area"), list)]
    if not _own:
        continue
    _pts = []
    for _p in _own[0]["printable_area"]:
        _x, _y = _p.split("x")
        _pts.append((float(_x), float(_y)))
    if len(_pts) < 3 or _area_is_symmetric(_pts):
        continue
    _tex_checked += 1
    _tex = _md.get("bed_texture") or ""
    _fx = _ink_fix.get(_tex)
    if _fx is None:
        err(f"[床貼圖] {_mn}: 床形不對稱卻用了沒登錄墨跡極值的貼圖 {_tex!r} ⇒ logo 會被斜邊切掉。"
            f"跑 tools/ping/make_closeddoor_texture.py 產專屬貼圖並更新 fixture")
        continue
    _tp = os.path.join(PINGDIR, _tex)
    if not os.path.isfile(_tp):
        err(f"[床貼圖] {_mn}: 貼圖檔不存在 {_tex}")
        continue
    _sha = hashlib.sha256(open(_tp, "rb").read()).hexdigest()
    if _sha != _fx.get("sha256"):
        err(f"[床貼圖] {_mn}: {_tex} 的 SHA-256 與 fixture 不符（貼圖換過但沒重跑產生器）")
        continue
    _bx0 = min(_p[0] for _p in _pts); _bx1 = max(_p[0] for _p in _pts)
    _by0 = min(_p[1] for _p in _pts); _by1 = max(_p[1] for _p in _pts)
    _worst, _wpt = None, None
    for _u, _v in _fx.get("ink_hull_uv", []):
        _wx = _bx0 + _u * (_bx1 - _bx0)
        _wy = _by0 + (1.0 - _v) * (_by1 - _by0)
        _c = _poly_clearance(_pts, _wx, _wy)
        if _worst is None or _c < _worst:
            _worst, _wpt = _c, (_wx, _wy)
    print(f"床貼圖 logo 餘裕：{_mn} {_tex} → "
          f"{('無取樣點' if _worst is None else '%+.2f mm' % _worst)}（{len(_fx.get('ink_hull_uv', []))} 點）")
    if _worst is None:
        err(f"[床貼圖] {_mn}: fixture 沒有墨跡凸包頂點")
    elif _worst < 0:
        err(f"[床貼圖] {_mn}: logo 墨跡超出可印範圍 {-_worst:.2f}mm"
            f"（最糟點 ({_wpt[0]:.1f}, {_wpt[1]:.1f})）⇒ 畫面上會被切掉")
if _tex_checked == 0:
    err("[床貼圖] 沒有掃到任何不對稱床形機型（關門應該要在內）⇒ 閘門形同虛設")

# ★ 跨層護欄：`bed_model_offset` 是 profile↔C++ 雙邊契約，任一邊掉了都是「verify 全綠但功能壞」。
#   ⚠ 一律先 strip_cxx_comments()——0811 實測過：註解掉的那份字串會讓 grep 型護欄假綠。
for _rel, _needles in (
    (("src", "slic3r", "GUI", "3DBed.cpp"),
     ["m_bed_model_offset = bed_model_offset", "m_bed_model_offset.size() == 1"]),
    (("src", "libslic3r", "PrintConfig.cpp"),
     ['this->add("bed_model_offset", coPoints)']),
    (("src", "libslic3r", "Preset.cpp"),
     ['"bed_model_offset"']),
    (("src", "slic3r", "GUI", "Plater.cpp"),
     ['option<ConfigOptionPoints>("bed_model_offset")']),
):
    _fp = os.path.join(_repo, *_rel)
    if not os.path.isfile(_fp):
        err(f"[床盤盤心・跨層] 找不到 {os.path.join(*_rel)}")
        continue
    _src = strip_cxx_comments(io.open(_fp, encoding="utf-8", errors="ignore").read())
    for _needle in _needles:
        if _needle not in _src:
            err(f"[床盤盤心・跨層] {os.path.join(*_rel)} 少了 {_needle!r} ⇒ "
                f"profile 宣告了盤心但 C++ 不吃，圓盤照樣歪（靜默失效）")

# ★ 檢查：支撐／支撐面速度下限 60，Classic 前代機除外（Eric 2026-08-12 裁）
#   規則＝把支撐拉回與外牆同量級（Fast 系外牆 60／內牆 80／稀疏 100，支撐 40 是唯一異類）。
#   🔴 Classic 前代機**刻意排除**：Marlin 非 Klipper、無 Input Shaper，整張速度表本來就慢
#      （EDU 200 全表 40；DUAL 450 外牆 40／頂面 40／支撐 25）⇒ 支撐拉到 60 會變成盤上最快的
#      東西＝內部倒置。**下一棒看到 Classic 是 25/40 不要「順手統一」。**
#   ⛔ 本規則只動支撐兩鍵：稀疏填充 100／內牆 80 是全庫標準（Eric 0719 親裁），不得順手改。
_SPD_FLOOR = 60.0
_CLASSIC_PREFIXES = ("EDU 200", "PING 200", "PING 270", "PING 300+",
                     "DUAL 300", "DUAL 450", "DUAL 600", "DUAL 800")
# 🔴 豁免名單（Eric 2026-09-19，牌 c-0919-ETR-01）＝產生器 4b-7 的 SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS **同名同值**
#    （verify 刻意不 import 產生器，兩邊一起改）。名單內的族不套 ≥60，改驗 exact 50。
#    ⚠ 本段搬到開發線時：開發線名＝「易拆(Z0)樹狀」，名單要換成那個字面。
SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS = (COMBO_CAT_EASYTREE,)   # ＝("易拆樹狀",)
_EXEMPT_SUPPORT_SPEED = "50"

def _proc_machine(_name):
    """製程 preset 名形如「0.3mm 易拆 @EDU 200 (0.6)」；取 @ 後、( 前的機型名。"""
    _at = _name.rfind("@")
    if _at < 0:
        return ""
    _m = _name[_at + 1:]
    _p = _m.rfind("(")
    return (_m[:_p] if _p >= 0 else _m).strip()

_spd_bad, _classic_below, _spd_checked, _easy_tree_spd = [], 0, 0, 0
for _n, (_k, _d) in sorted(presets.items()):
    if _k != "process":
        continue
    # 排除 fdm_* 範本母檔：它們不是出貨 preset，且實測 214 支葉檔**全部自帶**這兩顆鍵
    #（含 32 支 Classic）⇒ 母檔的值到不了輸出。post-pass 若遇到葉檔缺鍵會印警告，
    # 那才是要處理的情況，不是在這裡把母檔一起改（母檔是 Classic 與 Fast 共用的）。
    if _n.startswith("fdm_"):
        continue
    _vals = []
    for _key in ("support_speed", "support_interface_speed"):
        try:
            _vals.append((_key, float(_d[_key])))
        except (KeyError, TypeError, ValueError):
            continue
    if not _vals:
        continue
    _is_classic = any(_proc_machine(_n).startswith(_c) for _c in _CLASSIC_PREFIXES)
    if _is_classic:
        if any(_v < _SPD_FLOOR for _key, _v in _vals):
            _classic_below += 1
        continue
    # 🆕 易拆樹狀族（Eric 2026-09-19「樹狀支撐的支撐速度 60>50」＋Q5 甲＝支撐與支撐面兩格）：
    #    刻意低於 0812 下限 ⇒ 不套 ≥60，改 **exact 50**（寫成 60／40／漏一格都紅）。
    if any((" %s @" % _t) in _n for _t in SUPPORT_SPEED_FLOOR_EXEMPT_TOKENS):
        _easy_tree_spd += 1
        for _key in ("support_speed", "support_interface_speed"):
            if _d.get(_key) != _EXEMPT_SUPPORT_SPEED:
                err(f"[支撐速度・易拆樹狀 50] {_n}: {_key}={_d.get(_key)!r}, expected {_EXEMPT_SUPPORT_SPEED!r}（Eric 2026-09-19 裁）")
        continue
    _spd_checked += 1
    for _key, _v in _vals:
        if _v < _SPD_FLOOR:
            _spd_bad.append("%s: %s=%g" % (_n, _key, _v))
if _spd_checked == 0:
    err("[支撐速度] 沒有掃到任何非 Classic 製程 ⇒ 閘門形同虛設")
for _b in _spd_bad[:8]:
    err(f"[支撐速度] {_b} < {_SPD_FLOOR:g}（Eric 0812 裁：非 Classic 一律 ≥60）")
if len(_spd_bad) > 8:
    err(f"[支撐速度] 另有 {len(_spd_bad) - 8} 項未列出")
# 反向：Classic 若一支都不剩 <60，代表排除規則沒生效或被人「順手統一」了 ⇒ 要有人來看
if _classic_below == 0:
    err("[支撐速度・Classic 排除] Classic 前代機已無任何支撐速度 <60 ⇒ "
        "排除規則失效或被順手統一。Classic 是 Marlin 無 Input Shaper，整表本來就慢，"
        "支撐拉到 60 會比外牆還快")
print("支撐速度下限：非 Classic %d 支全數 ≥%g｜Classic 保留 <60 者 %d 支（刻意排除）｜易拆樹狀 exact 50 者 %d 支"
      % (_spd_checked, _SPD_FLOOR, _classic_below, _easy_tree_spd))

# ★ 易拆樹狀族（Eric 2026-09-19 兩輪 grill 裁，牌 c-0919-ETR-01）：每支都必須是「同口徑易拆」的雙生，
#   **只准**差 EASY_TREE_ALLOWED_DIFF 那幾鍵。這條是整族最強的守衛——哪天有人在主迴圈把樹狀版
#   接在別的 normalize 之前、或 post-pass 漏了豁免，其他鍵一偏就紅（不必逐鍵列期望值）。
#   覆蓋組數（每組雙料機×口徑都要有）由上方「功能歸類覆蓋」同一條判準把關。
_et_n = 0
for _n, (_k, _d) in sorted(presets.items()):
    if _k != "process" or combo_token(_n) != COMBO_CAT_EASYTREE:
        continue
    _et_n += 1
    _sib = _n.replace(" %s @" % COMBO_CAT_EASYTREE, " %s @" % COMBO_CAT_EASY, 1)
    _se = presets.get(_sib)
    if not _se or _se[0] != "process":
        err(f"[易拆樹狀・雙生對象不存在] {_n}：找不到 {_sib!r}")
        continue
    _sd = _se[1]
    _dk = sorted(k for k in set(_d) | set(_sd) if _d.get(k) != _sd.get(k) and k not in EASY_TREE_ALLOWED_DIFF)
    if _dk:
        err(f"[易拆樹狀・與易拆雙生的差異超出允許] {_n}: {_dk[:6]}"
            f"（只准差 support_type／support_style／兩個支撐速度）")
if _et_n == 0:
    err("[易拆樹狀・防空轉] 一支易拆樹狀製程都沒掃到 ⇒ 產生器沒產或 token 判定壞了")
print("易拆樹狀：%d 支（逐支與同口徑易拆雙生比對，只准差支撐類型／樣式／速度）" % _et_n)

# ★ 跨層護欄：標題列的廠內測試版版次（Eric 2026-08-12 裁「標題列顯示 Beta T0xx」）
#   鏈路＝`version.inc` 宣告 → `libslic3r_version.h.in` 由 configure_file 代換 → `BBLTopbar::SetTitle` 消費。
#   三處任一掉了都是**靜默失效**：編得過、跑得動、標題就是不顯示版次，而版次正是拿來辨識
#   「手上這顆是哪一版」的東西 ⇒ 沒有它，售服與測試回報會對錯版本。
#   ⚠ 一律先 strip_cxx_comments()（0811 實測過：註解裡的字串會讓 grep 型護欄假綠）。
#   🔴 needle 必須帶**後續字元**才夠精確：只寫 `set(PING_TEST_BUILD` 的話，把宣告改名成
#      `set(PING_TEST_BUILD_XX` 仍會命中（子字串）＝改壞了卻是綠的。0812 反向測試實抓。
_ver_chain = (
    (("version.inc",), ['set(PING_TEST_BUILD "'], False),
    (("src", "libslic3r", "libslic3r_version.h.in"), ["@PING_TEST_BUILD@"], False),
    (("src", "slic3r", "GUI", "BBLTopbar.cpp"),
     ["(*PING_TEST_BUILD)", '" Beta %s", PING_TEST_BUILD)'], True),
)
for _rel, _needles, _strip in _ver_chain:
    _fp = os.path.join(_repo, *_rel)
    if not os.path.isfile(_fp):
        err(f"[標題版次・跨層] 找不到 {os.path.join(*_rel)}")
        continue
    _src = io.open(_fp, encoding="utf-8", errors="ignore").read()
    if _strip:
        _src = strip_cxx_comments(_src)
    for _needle in _needles:
        if _needle not in _src:
            err(f"[標題版次・跨層] {os.path.join(*_rel)} 少了 {_needle!r} ⇒ "
                f"標題列不會顯示廠內測試版版次（靜默失效）")
# ☆ 提示（不擋；根 AGENTS「現階段新增的檢查一律先做提示」）：測試版獨立設定資料夾（Eric 2026-09-24 裁，
#   版本治理第 4 件；牌 c-0924-DR-01）。鏈路＝GUI_App.cpp init_app_config() 測試版改 SetAppName(... "-Internal")
#   ＋首次只讀複製；CMakeLists CPack 測試版裝獨立一格。上游合併若把任一處蓋回原樣＝測試版又跟正式版共用
#   設定資料夾、裝 T 版會先移除正式版——編得過、跑得動、什麼都不會報。
_dr_chain = (
    (("src", "slic3r", "GUI", "GUI_App.cpp"),
     ['SLIC3R_APP_KEY "-Internal"', "ping_seed_test_data_dir(data_dir_path);", "ping_seed_test_webview_storage();"], True),
    (("CMakeLists.txt",),
     ['set (PING_INSTALL_REGISTRY_KEY "PINGSlicer-Internal")',
      'set (CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${PING_INSTALL_REGISTRY_KEY}")'], False),
)
_dr_miss = []
for _rel, _needles, _strip in _dr_chain:
    _fp = os.path.join(_repo, *_rel)
    _src = io.open(_fp, encoding="utf-8", errors="ignore").read() if os.path.isfile(_fp) else ""
    if _strip:
        _src = strip_cxx_comments(_src)
    _dr_miss += [f"{os.path.join(*_rel)}：{_n}" for _n in _needles if _n not in _src]
print("測試版獨立設定資料夾：%s" % ("兩處都在（GUI_App.cpp＋CMakeLists CPack）" if not _dr_miss else
      "⚠ 提示（不擋）少了 " + "；".join(_dr_miss) + " ⇒ 測試版可能又跟正式版共用設定資料夾／裝 T 版會移除正式版"))
# 出貨版守則：PING_TEST_BUILD 有值＝這顆是廠內測試版。不在此擋（值本來就會隨 T 號變），
# 但明示於輸出，讓打包時一眼看到自己在包哪一種。
_vi = io.open(os.path.join(_repo, "version.inc"), encoding="utf-8", errors="ignore").read()
# 🔴 取**最後一個**（2026-09-21，牌 c-0921-CONV-07）：CMake 後面的 set 蓋前面的，而開發線在
#    version.inc 檔尾多一行覆寫（`set(PING_TEST_BUILD "DEV")`，SOP_參數入版紀律 §V 保留清單）。
#    取第一個會在開發線印出出貨線的 T 號＝印的不是 build 實際會用到的那個值。
_mv = re.findall(r'set\(PING_TEST_BUILD\s+"([^"]*)"\s*\)', _vi)
_mv = _mv[-1] if _mv else ""
print("標題列版次：PING_TEST_BUILD = %s"
      % (f"{_mv!r}（廠內測試版）" if _mv else "空字串（出貨版，標題不附加）"))

# ★ 跨層護欄（0727 Classic 變體）：profile 出了 Classic DUAL 同進機型，C++ 若沒有
#   「printer_model DUAL 開頭 → M6050 舊格式」分支，逐層插的會是 M6051（前代 Marlin
#   韌體不認）＝混色靜默失效——與 Tab.cpp 連動表同型的「verify 全綠但功能壞」坑。
_bsp = os.path.join(_repo, "src", "slic3r", "GUI", "BackgroundSlicingProcess.cpp")
if not os.path.isfile(_bsp):
    err(f"[跨層護欄] 找不到 {_bsp}（路徑推導失效，M6050 護欄形同虛設）")
else:
    _bsrc = io.open(_bsp, encoding="utf-8", errors="ignore").read()
    if '"M6050"' not in _bsrc or 'rfind("DUAL", 0)' not in _bsrc:
        err("[跨層護欄] Classic DUAL 的 M6050 舊格式分支不在 BackgroundSlicingProcess.cpp "
            "⇒ Classic 同進逐層混色會插 M6051（前代韌體不認）")

# ★ 檢查 13：線材收縮補償全庫一致（Eric 2026-08-09 裁 A；產生器 4b-5b 的硬閘門）
#   引擎規則（Print.cpp:3623）：所有用到的料 filament_shrink / _z 必須完全相同，
#   否則整個補償停用並跳「線材收縮補償將被停用」警告。全庫同值就永遠不會踩到。
#   ⚠ 未寫該鍵者＝吃引擎預設 100%，與明寫 100% 等價、不算違規。
_shrink_vals = {}
for _name, (_kind, _d) in presets.items():
    if _kind != "filament":
        continue
    for _k in ("filament_shrink", "filament_shrinkage_compensation_z"):
        if _k in _d:
            _shrink_vals.setdefault(_k, {}).setdefault(str(_d[_k]), []).append(_name)
# 🆕 2026-09-11（Eric：「ABS 的收縮率是 99.75%」）：守衛從「全庫 100%」改成「**族內同值**」。
#   引擎的真實約束（Print.cpp:3623 has_same_shrinkage_compensations）是「同一次列印**用到的**
#   所有料要同值」，不是「全庫要同值」——ABS 件只用 ABS 族、PLA 件只用 PLA 族，各族內同值即可。
#   0809 那條「全庫 100%」當時是對的（因為只有 SupABS 兩支是 99.7%＝異常值），但它把
#   **引擎約束**寫成了**比引擎更嚴的規則**，於是擋住了今天這個合法的族別設定。
#   ⚠ 這裡與產生器 4b-5b 的 ABS_SHRINK_FAMILY 是同一份名單，改一邊要改兩邊。
_ABS_SHRINK_FAMILY = ("PING ABS", "PING ABS(玻璃)", "PING ABS - Classic",
                      "PING SupABS", "PING SupABS - Classic")
_SHRINK_EXPECT = {"filament_shrink": {True: "99.75%", False: "100%"},
                  "filament_shrinkage_compensation_z": {True: "100%", False: "100%"}}
for _k, _groups in _shrink_vals.items():
    for _v, _names in _groups.items():
        for _n in _names:
            _want = _SHRINK_EXPECT[_k][_n in _ABS_SHRINK_FAMILY]
            if _v not in ("['%s']" % _want, "['%s']" % _want.rstrip("%")):
                err(f"[收縮補償一致性] {_n} 的 {_k}={_v}，應為 ['{_want}']"
                    f"——{'ABS 族' if _n in _ABS_SHRINK_FAMILY else '非 ABS 族'}的值不對 ⇒ "
                    f"同族多料列印時引擎會整個停用補償並跳警告")
# 族內一致性正向斷言：ABS 族必須全部帶 filament_shrink 且同值（缺鍵＝吃引擎預設 100%＝族內不一致）
_abs_seen = {n: v for v, names in _shrink_vals.get("filament_shrink", {}).items()
             for n in names if n in _ABS_SHRINK_FAMILY}
if len(_abs_seen) != len(_ABS_SHRINK_FAMILY):
    _missing = [n for n in _ABS_SHRINK_FAMILY if n not in _abs_seen]
    err(f"[收縮補償一致性] ABS 族有 {len(_missing)} 支沒有明寫 filament_shrink：{_missing}"
        f"——缺鍵＝吃引擎預設 100%，與族內其他支的 99.75% 不一致 ⇒ 補償會被整個停用")
elif len(set(_abs_seen.values())) != 1:
    err(f"[收縮補償一致性] ABS 族內值不一致：{_abs_seen}")
else:
    print("收縮補償：ABS 族 %d 支同值 %s｜其餘全庫 100%%"
          % (len(_abs_seen), list(_abs_seen.values())[0]))

# ★ 檢查 12：支撐首層擴展＋支撐線寬（Eric 2026-08-09 兩裁；產生器 4b-6 post-pass 的硬閘門）
#   ①raft_first_layer_expansion：raft_layers==0（支撐用途）＝0；raft_layers>=1（棧板/raft）＝6（Eric 2026-09-18「三族都改」3→6；原 0809＝3）
#     ——同一顆鍵管兩件事，分家族是刻意的，不是漏改（Eric 0809 明裁「棧板保留 3、其餘歸 0」）。
#   ②support_line_width＝口徑查表窄一階（0.2→0.15/0.25→0.2/0.4→0.35/0.6→0.5/1.0→0.8）；
#     FF 高流量線寬 1.02×口徑 一律歸回名目口徑查表（同 0722「FF 微調不入分子」家規）。
#     🔴 舊規「support_line_width＝line_width」已於 0809 作廢，看到相等反而是沒套到新規。
_SUP_LW_BY_NOZZLE = {"0.2": "0.15", "0.25": "0.2", "0.4": "0.35", "0.6": "0.5", "1": "0.8"}


def _nominal_nozzle_v(lw):
    try:
        v = float(lw)
    except (TypeError, ValueError):
        return None
    for _n in ("0.2", "0.25", "0.4", "0.6", "1"):
        if abs(v - float(_n)) <= 0.03:
            return _n
    return None


_exp_census = {"支撐0": 0, "筏層6": 0}
_ff_census = [0, 0]   # [合格的線寬鍵數, 掃到的 FF 非照片磚製程數]
for _name, (_kind, _d) in presets.items():
    if _kind != "process" or _name.startswith("fdm_"):
        continue
    if "raft_first_layer_expansion" in _d:
        _is_raft = str(_d.get("raft_layers", "0")) != "0"
        _want = "6" if _is_raft else "0"
        if _d["raft_first_layer_expansion"] != _want:
            err(f"[支撐首層擴展] {_name}: raft_first_layer_expansion="
                f"{_d['raft_first_layer_expansion']!r} ≠ {_want!r}"
                f"（raft_layers={_d.get('raft_layers', '0')!r}）")
        else:
            _exp_census["筏層6" if _is_raft else "支撐0"] += 1
    if "support_line_width" in _d:
        # 口徑優先從製程名「(口徑)」取（2026-09-09 PTP 棒：照片磚線寬曾 1.5×口徑，line_width 反推會錯一階）
        _m_nz = re.search(r"\(([\d.]+)\)\s*$", _name)
        _nzn = None
        if _m_nz:
            _c = "%g" % float(_m_nz.group(1))
            _nzn = _c if _c in _SUP_LW_BY_NOZZLE else None
        if _nzn is None:
            _nzn = _nominal_nozzle_v(_d.get("line_width"))
        if _nzn is None:
            err(f"[支撐線寬] {_name}: line_width={_d.get('line_width')!r} 認不出口徑（查表失效）")
        elif _d["support_line_width"] != _SUP_LW_BY_NOZZLE[_nzn]:
            err(f"[支撐線寬] {_name}: support_line_width={_d['support_line_width']!r} "
                f"≠ {_SUP_LW_BY_NOZZLE[_nzn]!r}（口徑 {_nzn}）")
    # 🆕 FF600／FF800 族線寬＝口徑（Eric 2026-09-15 裁「12 支全拉平」，回報中心 #152）
    #   🔴 **本條取代舊規「FF 高流量線寬＝1.02×口徑」**（0.41／0.62／1.02，工程端交付值）。
    #      起因＝同一台 FF600 上，四料同進早就是「線寬＝口徑」而 3in1／FF 單料頭還停在
    #      1.02×口徑 ⇒ 同機兩套值。Eric 附 FF600 同進 0.6 截圖裁「參數以圖片為主」。
    #      ⚠ 舊規在本檔別處的殘影＝「FF 系 1.0 口徑線寬 1.02 需 ≥2.04」那段註解（樹狀支撐
    #        branch_diameter_organic 2.6）——引擎硬限的是**支撐線寬**（不變，仍 0.8），
    #        線寬變小只會讓那個約束更鬆，**不受本裁影響**，該註解已被本條取代但無須改值。
    #   ⛔ 排除照片磚（線寬另有規則，曾＝1.5×口徑）；support_line_width 歸上面那條管。
    #   ⚠ 回歸型守衛：綠燈＝「沒有新的 FF 製程漏掉拉平」，不等於「線寬值對實印是最佳的」。
    if re.search(r"@FF(?:600|800)\b", _name) and "照片磚" not in _name:
        _m_ffn = re.search(r"\(([\d.]+)\)\s*$", _name)
        if _m_ffn is None:
            err(f"[FF族線寬] {_name}: 製程名認不出口徑 ⇒ 拉平規則掃不到它")
        else:
            _ff_want = "%g" % float(_m_ffn.group(1))
            for _ffk in ("line_width", "initial_layer_line_width", "outer_wall_line_width",
                         "inner_wall_line_width", "top_surface_line_width",
                         "sparse_infill_line_width", "internal_solid_infill_line_width"):
                if _ffk in _d and _d[_ffk] != _ff_want:
                    err(f"[FF族線寬 0915·#152] {_name}: {_ffk}={_d[_ffk]!r} 應 {_ff_want!r}（＝口徑）")
                elif _ffk in _d:
                    _ff_census[0] += 1
            _ff_census[1] += 1
if _exp_census["筏層6"] == 0:
    err("[支撐首層擴展] 全庫找不到任何 raft_layers>=1 的棧板製程 ⇒ 棧板家族消失或判定失效")
if _ff_census[1] == 0:
    err("[FF族線寬 0915·#152] 全庫找不到任何 FF600/FF800 非照片磚製程 ⇒ 判定失效")

# ★ 檢查 14：支撐 Z 間距家族值（Eric 2026-08-17 裁「一般家族全部 0.2」；產生器最終掃描的硬閘門）
#   易拆家族＝0（不相熔、貼緊好剝）／一般家族＝0.2（同料會相熔，要留隙才拆得下來）。
#   🔴 這條的存在理由＝0817 實錄：0813 把 FF 四料本體機第 4 槽由 SupPLA 改成 PLA（支撐變同料），
#      **幾何值沒跟著改**、Z 仍留在易拆的 0 ⇒ 支撐熔在件上；當時 verify 全綠、沒有任何東西擋得住。
#   ⚠ 易拆判定必須含 **3in1**——它是易拆家族（第 2 槽 SupPLA(3in1) 是支撐料）但名字裡沒有「易拆」，
#      本檢查第一版就是漏了它、把 6 支誤判成一般（實測抓到）。
_Z_CENSUS = {"易拆0": 0, "一般0.2": 0}
for _name, (_kind, _d) in presets.items():
    if _kind != "process" or _name.startswith("fdm_"):
        continue
    _easy = ("易拆" in _name) or ("3in1" in _name)
    _want = "0" if _easy else "0.2"
    _fam  = "易拆" if _easy else "一般"
    for _k in ("support_top_z_distance", "support_bottom_z_distance"):
        if _k not in _d:
            err(f"[支撐Z間距] {_name}: 缺鍵 {_k}（{_fam}家族應顯式寫 {_want}，不可靠繼承）")
        elif str(_d[_k]) != _want:
            err(f"[支撐Z間距] {_name}: {_k}={_d[_k]!r} ≠ {_want!r}（{_fam}家族）")
    if _d.get("support_top_z_distance") == _want and _d.get("support_bottom_z_distance") == _want:
        _Z_CENSUS["易拆0" if _easy else "一般0.2"] += 1
if _Z_CENSUS["易拆0"] == 0:
    err("[支撐Z間距] 全庫找不到任何易拆家族製程 ⇒ 易拆家族消失或判定失效")

# ── 另存列印設備的預設名＝printer_model + " " + printer_variant（2026-09-15 Eric 裁「A」）──────
#   PhysicalPrinterDialog 建構子（src/slic3r/GUI/PhysicalPrinterDialog.cpp）用這兩個鍵組出
#   「另存實體列印設備」的預設名。少了 printer_variant 就逐級退回只帶機型 ⇒ 同一機型的不同口徑
#   撞同一個預設名，而撞名只出 Warning、**確定鍵不會被停用** ⇒ 第二次另存直接覆蓋前一顆；
#   留下來那顆的 inherits 綁死在一個口徑上，而製程的 compatible_printers 是逐口徑寫死的
#   ⇒ 自訂機型走 parent 回退只命中那一組 ⇒ 使用者會看到「別的口徑的製程整組不見了」。
#   ⚠ 這是**回歸型**守衛：綠燈＝「沒有新的機型缺這兩個鍵、也沒有新的撞名」，
#     **不等於**「另存動線整體正確」——那要實走 GUI 才算數。
def _resolve_machine_key(_name, _key, _depth=0):
    # 沿 inherits 鏈回溯；葉檔目前都明寫，回溯只是防日後有人改成靠繼承拿值
    if _depth > 8 or _name not in presets:
        return ""
    _d = presets[_name][1]
    _v = _d.get(_key)
    if _v:
        return str(_v)
    _inh = _d.get("inherits")
    return _resolve_machine_key(_inh, _key, _depth + 1) if _inh else ""


_savename_owner = {}
_savename_census = 0
for _name, (_kind, _d) in presets.items():
    if _kind != "machine" or _name.startswith("fdm_"):
        continue
    _model = _resolve_machine_key(_name, "printer_model")
    _variant = _resolve_machine_key(_name, "printer_variant")
    if not _model:
        err(f"[另存預設名] {_name}: 缺 printer_model ⇒ 另存會退回「<preset名> - 複製」")
        continue
    if not _variant:
        err(f"[另存預設名] {_name}: 缺 printer_variant ⇒ 預設名不帶口徑、同機型不同口徑會互相覆蓋")
        continue
    _save = _model + " " + _variant
    if _save in machines:
        err(f"[另存預設名] {_name}: 預設名 {_save!r} 與系統機型同名 ⇒ 會被「不允許覆蓋系統設定」擋死")
    if _save in _savename_owner:
        err(f"[另存預設名] {_name} 與 {_savename_owner[_save]} 的預設名都是 {_save!r} ⇒ 會互相覆蓋")
    else:
        _savename_owner[_save] = _name
        _savename_census += 1
if _savename_census == 0:
    err("[另存預設名] 全庫找不到任何可另存的機型 ⇒ 判定失效")

# ★ 檢查 15：CF（加纖）類線材只在單噴頭機出現（Eric 2026-10-10 裁：「CF 類別只有在單噴頭才會出現」；
#   產生器 4d-1b post-pass 的硬閘門）。兩件事一起釘：
#   ①兩支 CF 線材的 compatible_printers ＝ F 系列單料頭機的全部口徑 preset（多一支少一支都算錯）
#   ②機型層的 default_materials：單料頭機必須兩支都在、其餘機型一支都不准有
#   🔴 為什麼要釘：`PING PA-CF` 原本**沒有** compatible_printers ＝全機通用，這條規則是把它收窄；
#      收窄是靠產生器 post-pass 做的，沒有這個閘門，任何人改動 4d-1b 或 base 範本都會靜默復原，
#      而畫面上只會表現成「雙料機又看得到碳纖料了」——沒有人會注意到。
#   🔴 2026-10-10（牌 c-1010-ACF-04）：**Classic 前代也進射程**。原本這裡寫著「不在本檢查射程內」，
#      T080 就從這個洞漏過去——`PING PA-CF - Classic` 被母檔的 F 系清單滲進相容機型（一台 DUAL 都沒有），
#      12 台 Classic 機的預勾被 4d-2 整批拿掉、Classic 機一支 CF 都選不到，而本檢查全綠。
#      ⇒ 兩個家族各有各的機型全集，少一台多一台都要紅。
_CF_FILS = ("PING PA-CF", "ABS-CF")
_CF_FILS_CLASSIC = ("PING PA-CF - Classic",)
#   🆕 Eric 2026-10-10「三題照建議」：再釘一條型別——下拉的 "CF/GF" 類是用 filament_type 結尾
#   "-CF"／"-GF" 判定的（PresetComboBoxes.cpp），型別一旦被改回 "PA" 這支就會靜默掉回 PA 類，
#   而畫面上只看得出「分類又變了」。Classic 版一起釘（它不收窄相容機型，但型別要一致）。
_CF_TYPE_WANT = {"PING PA-CF": "PA-CF", "PING PA-CF - Classic": "PA-CF", "ABS-CF": "ABS-CF"}


def _cf_is_single_head(_n):
    return (not _n.startswith("DUAL")) and (_n.startswith("FP300") or "單料頭" in _n)


def _cf_is_classic_single_head(_n):
    return _n.startswith("DUAL") and "單料頭" in _n


def _cf_machine_set(_pred):
    return sorted(_n for _n, (_k, _d) in presets.items()
                  if _k == "machine" and not _n.startswith("fdm_")
                  and _n.endswith(" nozzle") and _pred(_n))


_cf_want = _cf_machine_set(_cf_is_single_head)
_cf_want_classic = _cf_machine_set(_cf_is_classic_single_head)
_CF_FAMILIES = (("F 系", _CF_FILS, _cf_want), ("Classic", _CF_FILS_CLASSIC, _cf_want_classic))
for _fam, _fils, _want in _CF_FAMILIES:
    if not _want:
        err(f"[CF單噴頭] 找不到任何{_fam}單料頭機 preset ⇒ 判定失效")
    for _cf in _fils:
        if _cf not in presets:
            err(f"[CF單噴頭] 線材 {_cf!r} 不存在")
            continue
        _got = presets[_cf][1].get("compatible_printers")
        if not isinstance(_got, list):
            err(f"[CF單噴頭] {_cf}: compatible_printers 缺或型別不是 list（{type(_got).__name__}）"
                f" ⇒ 全機通用，碳纖料會出現在雙噴頭機上")
        elif sorted(_got) != _want:
            _extra = sorted(set(_got) - set(_want))
            _miss = sorted(set(_want) - set(_got))
            err(f"[CF單噴頭] {_cf}: compatible_printers 不等於{_fam}單料頭全集"
                f"（多 {len(_extra)}: {_extra[:3]}；少 {len(_miss)}: {_miss[:3]}）")
for _cfn, _cfwant in _CF_TYPE_WANT.items():
    if _cfn not in presets:
        err(f"[CF單噴頭] 線材 {_cfn!r} 不存在（期望型別 {_cfwant}）")
        continue
    _cfty = presets[_cfn][1].get("filament_type")
    _cfty0 = _cfty[0] if isinstance(_cfty, list) and _cfty else _cfty
    if _cfty0 != _cfwant:
        err(f"[CF單噴頭] {_cfn}: filament_type={_cfty0!r} ≠ {_cfwant!r}"
            f" ⇒ 下拉不會把它收進 CF/GF 類（判定＝型別結尾 -CF／-GF）")
_cf_census = {"單料頭": 0, "Classic 單料頭": 0, "其餘": 0}
for _n, (_k, _d) in presets.items():
    if _k != "machine_model":          # 🔴 預勾在機型層（machine_model_list），不在口徑 preset
        continue
    _mats = set(str(_d.get("default_materials") or "").split(";"))
    _has = [_f for _f in _CF_FILS if _f in _mats]
    _hasc = [_f for _f in _CF_FILS_CLASSIC if _f in _mats]
    if _cf_is_single_head(_n):
        _want_here, _ban_here, _bucket = _CF_FILS, _CF_FILS_CLASSIC, "單料頭"
    elif _cf_is_classic_single_head(_n):
        _want_here, _ban_here, _bucket = _CF_FILS_CLASSIC, _CF_FILS, "Classic 單料頭"
    else:
        _want_here, _ban_here, _bucket = (), _CF_FILS + _CF_FILS_CLASSIC, "其餘"
    _missing = sorted(set(_want_here) - _mats)
    _banned = sorted(set(_ban_here) & _mats)
    if _missing:
        err(f"[CF單噴頭] 機型 {_n}: 預勾少了 {_missing}")
    if _banned:
        err(f"[CF單噴頭] 機型 {_n}: 不該預勾卻有 {_banned}")
    if not _missing and not _banned:
        _cf_census[_bucket] += 1

print(f"presets: {len(presets)} | machines: {len(machines)}")
print(f"另存預設名：{_savename_census} 支機型各自唯一（model+variant，零撞系統名）")
print(f"支撐首層擴展：支撐 0 ×{_exp_census['支撐0']}｜筏層 6 ×{_exp_census['筏層6']}")
print(f"FF族線寬＝口徑（0915·#152）：{_ff_census[1]} 支製程／{_ff_census[0]} 個鍵合格")
print(f"支撐Z間距：易拆 0 ×{_Z_CENSUS['易拆0']}｜一般 0.2 ×{_Z_CENSUS['一般0.2']}")
print(f"CF 只在單噴頭：F 系 {len(_CF_FILS)} 支 × {len(_cf_want)} 台口徑 preset"
      f"｜Classic {len(_CF_FILS_CLASSIC)} 支 × {len(_cf_want_classic)} 台"
      f"｜機型預勾 單料頭 {_cf_census['單料頭']} 台／Classic 單料頭 {_cf_census['Classic 單料頭']} 台"
      f"／其餘 {_cf_census['其餘']} 台皆無")
if errors:
    print(f"\n[FAIL] {len(errors)} 個問題：")
    for e in errors:
        print(" ", e)
    sys.exit(1)
print("[OK] 參照完整性全部通過")
