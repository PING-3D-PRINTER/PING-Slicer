# -*- coding: utf-8 -*-
"""建 FD300 Pro／FD450 Pro／FD600 Pro／FD800 Pro 的「同進照片磚」母版
（Eric 2026-09-07 裁「丙・補機型」，回報中心 #99 的收尾）。

▍為什麼要有這四台
照片磚機原本只有 FD300／FF600／FF800 三個家族，但同進機有更多家族。#99 收斂之後，
解析器改成「同系列優先、沒有同系列才退回家族比對」——FD300 Pro／FD450／600／800 Pro 的
主人做照片磚會被退回 FD300 同進照片磚（床小一號、起始碼是別台的）。補齊機型才是真正的解。

▍產法＝SOP_加機型 §2.7「delta 用已知正確的那一對 diff 出來，不要手打」
  照片磚 overlay ＝ diff(`FD300 同進 <nz>`, `FD300 同進照片磚 <nz>`)  ← 已在版、已出貨、Eric 實印過
  新機底稿     ＝ `FD<X> Pro 同進 <nz>` 的實檔                        ← 已在版、已知正確
  新機         ＝ 底稿 ＋ overlay ＋ 身分欄位
所有家族專屬值（床形多邊形、列印高度、start gcode、default_acceleration…）都從底稿實檔帶，
**一個都不手打**——手打就是臆測，而且錯了不會馬上發現。

▍為什麼這支敢說 overlay 是對的（自我驗證，不是自我推理）
  ★陽性對照：把 overlay 套回 FD300 同進自己，結果必須與**現存的** FD300 同進照片磚
    逐鍵相同（身分欄位除外）。不同 ⇒ overlay 不完整 ⇒ 直接中止，不寫任何檔。
  ★口徑無關性：0.4 與 0.6 各算一次 overlay，兩份必須一致。一致才敢把它套到 1.0
    （FD 家族沒有 1.0 的照片磚參考檔，這是唯一能證明外推安全的證據）。

▍SOP 遵守點
  §2.6 照片磚機 single_extruder_multi_material 必須 = 1（否則 64 槽不生效、選機後只長 2 槽）→ assert。
  §2.5 start_gcode 內零 T 指令（T5 曾造成 GCodeProcessor 索引越界閃退）→ assert。
  §2.8 setting_id 走保留號段，由 embed_params.emit_phototile 統一重編，母版裡的值不承重。

▍層高：沿用 FD 家族既有規則（實查 repo：0.4→0.2mm／0.6→0.3mm／1.0→0.5mm，＝0.5×口徑），
   不是為照片磚新發明的值。

跑法：python tools/ping/build_fd_pro_phototile.py            （只檢查、不寫檔）
      python tools/ping/build_fd_pro_phototile.py --write    （實際產出母版）
"""
import argparse
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PING = os.path.join(REPO, "resources", "profiles", "PING")
PT = os.path.join(HERE, "base", "phototile")

MARK = " 同進照片磚"
BASE = " 同進"

# 參考對（已在版、已出貨、Eric 實印驗證過）
REF_FAMILY = "FD300"
# 新家族 → 要建的口徑。口徑取「該家族同進機有的」∩「照片磚合法口徑 {0.4,0.6,1.0}」
#（合法集合正本＝src/slic3r/GUI/PhotoTileCapability.cpp 的 LEGAL_NOZZLES）
# 🔴 **線材要跟著噴頭流量形式走**（ping-slicer 鐵律：參數＝f(噴頭流量形式, 口徑, 材料)）。
#    實查各家族同進機的 default_filament_profile：
#      FD300／FD300 Pro 同進 ＝ `PING PLA - 210`      → **一般流量**
#      FD450／600／800 Pro 同進 ＝ `PING PLA - 高流量噴頭` → **高流量**
#    ⇒ 高流量那三家族**不能**吃 FD300 那支照片磚線材（它是從 PLA-210 派生、PA 0.08 的一般流量值）。
#    這正是 Eric 2026-08-19 裁「給它專用支」的同一個理由（當時是 FD300 不能吃四料那支）。
#    母版這裡只寫**基底** PLA；換成照片磚專用支由 embed_params 的 4b-1e／4b-1f 統一做
#    （與 FD300 現行機制相同 ⇒ regen-durable）。
NEW_FAMILIES = {
    #  家族          口徑（同進機有的 ∩ 照片磚合法集合）   基底 PLA（＝流量形式）
    "FD300 Pro": (["0.4", "0.6"],           "PING PLA - 210"),        # 0.25 不在照片磚合法集合
    "FD450 Pro": (["0.4", "0.6", "1.0"],    "PING PLA - 雙料高流量噴頭"),
    "FD600 Pro": (["0.4", "0.6", "1.0"],    "PING PLA - 雙料高流量噴頭"),
    "FD800 Pro": (["0.4", "0.6", "1.0"],    "PING PLA - 雙料高流量噴頭"),
}
# FD 家族層高規則（實查 repo 既有 FD 同進製程：0.2/0.3/0.5 ＝ 0.5×口徑）
PROC_LH = {"0.4": "0.2mm", "0.6": "0.3mm", "1.0": "0.5mm"}
PT_SLOTS = 64

# 身分欄位：由本腳本明寫，不參與 overlay 比對（它們本來就每台不同）
MACHINE_IDENTITY = {"name", "alias", "printer_model", "setting_id",
                    "default_print_profile", "default_filament_profile"}
PROCESS_IDENTITY = {"name", "compatible_printers", "setting_id", "filename_format"}
MODEL_IDENTITY = {"name", "model_id", "nozzle_diameter", "default_materials"}


def load(p):
    return json.load(io.open(p, encoding="utf-8"))


def dump(p, d):
    io.open(p, "w", encoding="utf-8").write(json.dumps(d, ensure_ascii=False, indent=4) + "\n")


def machine_path(model, nz):
    return os.path.join(PING, "machine", "%s %s nozzle.json" % (model, nz))


def model_path(model):
    return os.path.join(PING, "machine", "%s.json" % model)


def process_path(lh, model, nz):
    return os.path.join(PING, "process", "%s @%s (%s).json" % (lh, model, nz))


def make_overlay(src, dst, identity):
    """dst 相對 src 的差異，扣掉身分欄位。回傳 (要設的鍵, 要刪的鍵)。"""
    set_keys = {k: v for k, v in dst.items()
                if k not in identity and (k not in src or src[k] != v)}
    del_keys = sorted(set(src) - set(dst) - identity)
    return set_keys, del_keys


def apply_overlay(base, set_keys, del_keys):
    out = dict(base)
    for k in del_keys:
        out.pop(k, None)
    out.update(set_keys)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    problems = []

    # ── 1. 從 FD300 那一對算出「照片磚 overlay」，並驗它與口徑無關 ────────────────
    m_overlays, p_overlays = {}, {}
    for nz in ("0.4", "0.6"):
        base_m = load(machine_path(REF_FAMILY + BASE, nz))
        pt_m = load(machine_path(REF_FAMILY + MARK, nz))
        m_overlays[nz] = make_overlay(base_m, pt_m, MACHINE_IDENTITY)

        lh = PROC_LH[nz]
        base_p = load(process_path(lh, REF_FAMILY + BASE, nz))
        pt_p = load(process_path(lh, REF_FAMILY + MARK, nz))
        p_overlays[nz] = make_overlay(base_p, pt_p, PROCESS_IDENTITY)

    if m_overlays["0.4"] != m_overlays["0.6"]:
        problems.append("機器 overlay 在 0.4 與 0.6 不一致 ⇒ 它與口徑有關，不可外推到 1.0")
    if p_overlays["0.4"] != p_overlays["0.6"]:
        a, b = p_overlays["0.4"][0], p_overlays["0.6"][0]
        diff = sorted(set(a) ^ set(b)) or [k for k in a if a[k] != b.get(k)]
        problems.append("製程 overlay 在 0.4 與 0.6 不一致（%s）⇒ 不可外推到 1.0" % diff)
    m_set, m_del = m_overlays["0.6"]
    p_set, p_del = p_overlays["0.6"]

    print("照片磚 overlay（從 FD300 同進 → FD300 同進照片磚 實檔算出）")
    print("  機器：設 %d 鍵 %s｜刪 %d 鍵 %s" % (len(m_set), sorted(m_set), len(m_del), m_del))
    print("  製程：設 %d 鍵 %s｜刪 %d 鍵 %s" % (len(p_set), sorted(p_set), len(p_del), p_del))
    print("  口徑無關性：0.4 與 0.6 兩份 overlay %s" % ("一致 ✅" if not problems else "不一致 ❌"))

    # ── 2. ★陽性對照：套回 FD300 自己，必須還原成現存的照片磚檔 ──────────────────
    for nz in ("0.4", "0.6"):
        got = apply_overlay(load(machine_path(REF_FAMILY + BASE, nz)), m_set, m_del)
        want = load(machine_path(REF_FAMILY + MARK, nz))
        bad = [k for k in set(got) | set(want)
               if k not in MACHINE_IDENTITY and got.get(k) != want.get(k)]
        if bad:
            problems.append("★陽性對照失敗（機器 %s）：套回 FD300 得不到現存檔，差異鍵 %s" % (nz, bad))

        lh = PROC_LH[nz]
        got = apply_overlay(load(process_path(lh, REF_FAMILY + BASE, nz)), p_set, p_del)
        want = load(process_path(lh, REF_FAMILY + MARK, nz))
        bad = [k for k in set(got) | set(want)
               if k not in PROCESS_IDENTITY and got.get(k) != want.get(k)]
        if bad:
            problems.append("★陽性對照失敗（製程 %s）：套回 FD300 得不到現存檔，差異鍵 %s" % (nz, bad))
    print("  ★陽性對照（overlay 套回 FD300 → 現存檔）：%s"
          % ("逐鍵相同 ✅" if not problems else "失敗 ❌"))

    if problems:
        print("\n中止，不寫任何檔：")
        for p in problems:
            print("  ❌", p)
        return 1

    # ── 3. 產四個家族 ───────────────────────────────────────────────────────────
    out_machines, out_models, out_procs = [], [], []
    for fam, (nozzles, base_pla) in NEW_FAMILIES.items():
        src_model, new_model = fam + BASE, fam + MARK

        # 3a. machine_model 母檔
        mm = load(model_path(src_model))
        mm["name"] = new_model
        mm["model_id"] = "PING_" + new_model.replace(" ", "_")
        mm["nozzle_diameter"] = ";".join(nozzles)
        mm["default_materials"] = base_pla
        out_models.append((new_model, mm))

        # 驗「基底 PLA」不是我填的，而是該家族同進機自己在用的那一支
        _probe = load(machine_path(src_model, nozzles[0])).get("default_filament_profile")
        if _probe != [base_pla]:
            problems.append("%s 的基底 PLA 表填 %r，但實檔是 %r ⇒ 流量形式判斷有誤"
                            % (src_model, base_pla, _probe))

        # 3b. 各口徑 machine
        for nz in nozzles:
            src = machine_path(src_model, nz)
            if not os.path.exists(src):
                problems.append("找不到底稿：%s" % src)
                continue
            d = apply_overlay(load(src), m_set, m_del)
            d["name"] = "%s %s nozzle" % (new_model, nz)
            d["alias"] = new_model
            d["printer_model"] = new_model
            d["default_print_profile"] = "%s @%s (%s)" % (PROC_LH[nz], new_model, nz)
            d["default_filament_profile"] = [base_pla] * PT_SLOTS
            d["setting_id"] = "PINGM999"   # 佔位；真值由 embed_params 的保留號段重編（§2.8）
            out_machines.append((d["name"], d))

            # 3c. 製程
            lh = PROC_LH[nz]
            psrc = process_path(lh, src_model, nz)
            if not os.path.exists(psrc):
                problems.append("找不到製程底稿：%s" % psrc)
                continue
            p = apply_overlay(load(psrc), p_set, p_del)
            p["name"] = "%s @%s (%s)" % (lh, new_model, nz)
            p["compatible_printers"] = ["%s %s nozzle" % (new_model, nz)]
            p["filename_format"] = load(
                process_path(PROC_LH["0.6"], REF_FAMILY + MARK, "0.6"))["filename_format"]
            p["setting_id"] = "PINGP999"   # 同上，佔位
            out_procs.append((p["name"], p))

    # ── 4. SOP assert（不通過就中止，不寫檔）────────────────────────────────────
    for name, d in out_machines:
        if d.get("single_extruder_multi_material") != "1":
            problems.append("§2.6 違反：%s 的 single_extruder_multi_material 不是 1" % name)
        if len(d.get("default_filament_profile", [])) != PT_SLOTS:
            problems.append("§2.6 違反：%s 的料槽數不是 %d" % (name, PT_SLOTS))
        gcode = d.get("machine_start_gcode", "")
        bad_t = [ln for ln in gcode.split("\n") if ln.strip().startswith("T") and ln.strip()[1:2].isdigit()]
        if bad_t:
            problems.append("§2.5 違反：%s 的 start_gcode 含 T 指令 %s" % (name, bad_t))
        if REF_FAMILY + MARK in json.dumps(d, ensure_ascii=False):
            problems.append("殘留來源機型字樣：%s 內出現 %s" % (name, REF_FAMILY + MARK))
        if d.get("retraction_length") != ["0", "0"] or d.get("z_hop") != ["0"]:
            problems.append("照片磚零回抽未套上：%s" % name)

    print("\n產出（乾跑）：machine_model %d／machine %d／process %d"
          % (len(out_models), len(out_machines), len(out_procs)))
    for name, d in out_models:
        print("  model   %-26s 口徑 %-12s 列印高度 %s"
              % (name, d.get("nozzle_diameter"), d.get("printable_height", "(繼承)")))
    for name, d in out_machines:
        print("  machine %-34s SEMM=%s 槽=%d 高=%s"
              % (name, d.get("single_extruder_multi_material"),
                 len(d.get("default_filament_profile", [])), d.get("printable_height")))
    for name, d in out_procs:
        print("  process %-36s 層高=%s 預設加速度=%s"
              % (name, d.get("layer_height"), d.get("default_acceleration")))

    if problems:
        print("\n中止，不寫任何檔：")
        for p in problems:
            print("  ❌", p)
        return 1

    if not args.write:
        print("\n（乾跑結束；要實際寫母版請加 --write）")
        return 0

    for name, d in out_models:
        dump(os.path.join(PT, "machine", "%s.json" % name), d)
    for name, d in out_machines:
        dump(os.path.join(PT, "machine", "%s.json" % name), d)
    for name, d in out_procs:
        dump(os.path.join(PT, "process", "%s.json" % name), d)
    print("\n已寫入母版 %d 檔到 %s" % (len(out_models) + len(out_machines) + len(out_procs), PT))
    print("下一步：把新機型／製程名補進 embed_params.py 的 PHOTOTILE_MACHINES／PHOTOTILE_PROCS，")
    print("        並讓 _is_reserved() 認得它們（§2.8 保留號段），再 regen ＋ verify。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
