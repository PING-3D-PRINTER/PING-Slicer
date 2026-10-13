# -*- coding: utf-8 -*-
"""大機加口徑：從「已知正確的一對」派生交付 config（SOP_加機型 §2.7 精神：delta 用 diff 出來、不手打）。

  雙料本體 4 組合＋同進（PLA/PETG）→ 0.25：delta = FD300_0.25_X − FD300_0.4_X，套到 <FAM>_0.4_X
  單料頭（PLA/PETG）→ 0.2：delta = FP300_0.2_單料頭_X − FP300_0.4_單料頭_X，套到 <FAM>_0.4_單料頭_X
  交集鍵（同時隨口徑與家族變）只允許：起始 G-code（prime 線 Z／E 隨口徑，Y 隨家族）＋ 4 個名字/id 鍵。
  起始 G-code 的口徑變換先在 FD300／FP300 自己那一對上自測（套到 0.4 要逐字等於交付的 0.25／0.2），過了才用。

用法：python derive_nozzle_configs.py            # 乾跑：只列每檔會改的鍵
      python derive_nozzle_configs.py --apply    # 寫檔（目標已存在就停，不覆蓋）
"""
import json, io, os, re, sys

R = r"D:\dev\2026claude\20260603 切片參數\PING Slicer V3.5\F系列參數"
FAMS = ["FD450 Pro", "FD600 Pro", "FD800 Pro"]
DUAL_X = ["PLA+SUP", "PLA+PLA", "ABS+SUP", "ABS+ABS", "同進_PLA", "同進_PETG"]   # → 0.25（來源對＝FD300）
SINGLE_X = ["單料頭_PLA", "單料頭_PETG"]                                        # → 0.2 （來源對＝FP300）
NAME_KEYS = {"default_print_profile", "print_settings_id", "printer_settings_id", "print_compatible_printers"}
ALLOWED_OVERLAP = NAME_KEYS | {"machine_start_gcode"}
APPLY = "--apply" in sys.argv

def fn(fam, nz, x):
    return os.path.join(R, fam, "%s_%s_%s_project_settings.config" % (fam.replace(" ", "_"), nz, x))

def load(path):
    return json.load(io.open(path, encoding="utf-8"))

def prime_transform(sg, z_from, z_to, e_from, e_to):
    """只動 prime 線那幾行的 Z 與 E（同一行內），以及收尾 G1 Z1 E(e−1)。其餘逐字不動。"""
    out = []
    pat = re.compile(r"^(G[01] F\d+ X-?\d+ Y-?\d+)(?: Z([0-9.]+))?(?: E(\d+))?$")
    for line in sg.splitlines():
        m = pat.match(line)
        if m:
            head, z, e = m.groups()
            if z == z_from: line = line.replace(" Z" + z_from, " Z" + z_to)
            if e == e_from: line = re.sub(r" E%s$" % e_from, " E" + e_to, line)
        elif line == "G1 Z1 E%d" % (int(e_from) - 1):
            line = "G1 Z1 E%d" % (int(e_to) - 1)
        out.append(line)
    return "\n".join(out) + ("\n" if sg.endswith("\n") else "")

def rename(v, nz_from, nz_to):
    if isinstance(v, list):
        return [rename(x, nz_from, nz_to) for x in v]
    return v.replace(nz_from, nz_to) if isinstance(v, str) else v

def derive(fam, x, src_fam, nz_from, nz_to, z_e):
    a = load(fn(src_fam, nz_from, x))          # 來源 0.4
    b = load(fn(src_fam, nz_to, x))            # 來源 目標口徑（已知正確）
    t = load(fn(fam, nz_from, x))              # 目標家族 0.4
    delta = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
    famdiff = sorted(k for k in set(a) | set(t) if a.get(k) != t.get(k))
    overlap = sorted(set(delta) & set(famdiff))
    bad = [k for k in overlap if k not in ALLOWED_OVERLAP]
    if bad:
        raise SystemExit("%s %s：交集出現不允許的鍵 %s（口徑與家族同時影響，要人判）" % (fam, x, bad))
    # 起始 G-code 口徑變換自測：套在來源 0.4 上要逐字等於來源目標口徑
    z_from, z_to, e_from, e_to = z_e
    if prime_transform(a["machine_start_gcode"], z_from, z_to, e_from, e_to) != b["machine_start_gcode"]:
        raise SystemExit("%s %s：起始 G-code 口徑變換自測失敗（來源對 %s %s→%s）" % (fam, x, src_fam, nz_from, nz_to))
    new = dict(t)
    changed = []
    for k in delta:
        if k == "machine_start_gcode":
            new[k] = prime_transform(t[k], z_from, z_to, e_from, e_to)
            if new[k] == t[k]:
                raise SystemExit("%s %s：起始 G-code 沒有任何 prime 行被改到" % (fam, x))
        elif k in NAME_KEYS:
            new[k] = rename(t[k], nz_from, nz_to)
        else:
            new[k] = b[k]                      # 純口徑鍵：直接取來源目標口徑的值
        changed.append((k, t.get(k), new[k]))
    return new, changed, len(delta), len(famdiff), overlap

def main():
    plan = []
    for fam in FAMS:
        for x in DUAL_X:
            plan.append((fam, x, "FD300", "0.4", "0.25", ("0.25", "0.15", "30", "20")))
        for x in SINGLE_X:
            plan.append((fam, x, "FP300", "0.4", "0.2", ("0.25", "0.15", "30", "16")))
    written = 0
    for fam, x, src, nf, nt, ze in plan:
        new, changed, nd, nfam, ov = derive(fam, x, src, nf, nt, ze)
        dst = fn(fam, nt, x)
        print("%-10s %-10s → %s  口徑差 %2d／家族差 %2d／交集 %s" % (fam, x, os.path.basename(dst), nd, nfam, ",".join(ov)))
        for k, o, n in changed:
            if k == "machine_start_gcode":
                print("    %-34s prime 線 Z%s→Z%s E%s→E%s" % (k, ze[0], ze[1], ze[2], ze[3]))
            else:
                print("    %-34s %s -> %s" % (k, str(o)[:30], str(n)[:30]))
        if APPLY:
            if os.path.exists(dst):
                raise SystemExit("目標已存在，不覆蓋：" + dst)
            # 交付夾既有檔的樣式＝2 空格縮排、CRLF、無 BOM（實查 FD600_Pro_0.4_PLA+SUP）
            with io.open(dst, "w", encoding="utf-8", newline="\r\n") as f:
                json.dump(new, f, ensure_ascii=False, indent=2)
                f.write("\n")
            written += 1
    print("plan %d files; written %d%s" % (len(plan), written, "" if APPLY else "（乾跑，未寫）"))

if __name__ == "__main__":
    main()
