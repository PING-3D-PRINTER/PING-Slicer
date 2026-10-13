# -*- coding: utf-8 -*-
"""把「既有機型後加口徑」的 preset 外科手術式注入試用包 data_dir/system/PING（比照 tools/ping/inject_model_into_datadir.py，
差別＝以 (機型, 口徑) 為單位，且要改既有 machine_model 檔的 nozzle_diameter 清單）。

做法：先備份整個 system/PING＋PING.json＋PINGSlicer.conf → 新檔只加不覆蓋 → PING.json 只插新條目（版號不動）
      → machine_model 檔 nozzle_diameter 只做「字串加口徑」的定點替換 → conf 的 models 區段對已安裝機型加口徑（文字手術）→ read-back。
用法：python inject_nozzles_into_datadir.py [--apply] [--datadir D:\PING-Slicer-dev-portable\data_dir]
"""
import argparse, datetime, json, shutil, sys, io, re
from pathlib import Path

REPO_PROFILES = Path(r"D:\dev\2026claude\20260604 ORCA客製\PING-Slicer\resources\profiles")
NEW = {  # 機型 → 新口徑（與 embed_params.EXT_RESERVED_NOZZLES 同）
    "FD450 Pro": "0.25", "FD450 Pro 同進": "0.25", "FD450 Pro 單料頭": "0.2",
    "FD600 Pro": "0.25", "FD600 Pro 同進": "0.25", "FD600 Pro 單料頭": "0.2",
    "FD800 Pro": "0.25", "FD800 Pro 同進": "0.25", "FD800 Pro 單料頭": "0.2",
}
ap = argparse.ArgumentParser()
ap.add_argument("--datadir", default=r"D:\PING-Slicer-dev-portable\data_dir")
ap.add_argument("--apply", action="store_true")
a = ap.parse_args()
DD = Path(a.datadir) / "system"; APPLY = a.apply
repo_pj = json.load(open(REPO_PROFILES / "PING.json", encoding="utf-8"))
dd_pj_path = DD / "PING.json"
dd_pj = json.load(open(dd_pj_path, encoding="utf-8"))
print("installed version", dd_pj.get("version"), "| repo version", repo_pj.get("version"), "（installed 版號不動）")

def is_new(name):
    for m, nz in NEW.items():
        if name == "%s %s nozzle" % (m, nz) or ("@%s (%s)" % (m, nz)) in name:
            return True
    return False

adds, files = {}, []
for sec in ("machine_list", "process_list"):
    want = [e for e in repo_pj.get(sec, []) if is_new(e["name"])]
    have = {e["name"] for e in dd_pj.get(sec, [])}
    adds[sec] = [e for e in want if e["name"] not in have]
    files += ["PING/" + e["sub_path"] for e in want]
    print(f"  {sec}: repo 有 {len(want)} 條 → 要加 {len(adds[sec])} 條")
assert len(files) == 42, len(files)
for f in files:
    assert (REPO_PROFILES / f).exists(), f
clash = [f for f in files if (DD / f).exists()]
print(f"檔案 {len(files)} 個，其中目標已有 {len(clash)} 個")

# machine_model 檔：nozzle_diameter 字串加口徑（定點替換，其餘位元組不動）
mm_plan = []
for m, nz in NEW.items():
    fp = DD / "PING" / "machine" / (m + ".json")
    if not fp.exists():
        print("  machine_model 缺", fp.name); continue
    t = fp.read_text(encoding="utf-8")
    mo = re.search(r'"nozzle_diameter": "([^"]*)"', t)
    cur = mo.group(1)
    if nz in cur.split(";"):
        print("  machine_model 已含", m, nz); continue
    new = ";".join(sorted(set(cur.split(";")) | {nz}, key=float))
    mm_plan.append((fp, cur, new))
    print(f"  machine_model {m}: {cur} → {new}")

# conf：models 區段對「已安裝」的機型加口徑（只動那幾個 dict 的 nozzle_diameter 字串）
conf = Path(a.datadir) / "PINGSlicer.conf"
s = conf.read_text(encoding="utf-8")
obj, end = json.JSONDecoder().raw_decode(s)
conf_plan = []
for e in obj.get("models", []):
    m = e.get("model")
    if m in NEW and NEW[m] not in e.get("nozzle_diameter", "").split(";"):
        old = e["nozzle_diameter"]; new = ";".join(sorted(set(old.split(";")) | {NEW[m]}, key=float))
        conf_plan.append((m, old, new))
        print(f"  conf models[{m}]: {old} → {new}")
if not conf_plan:
    print("  conf：大機三家沒有已安裝的機型（精靈勾選後才會出現）")

if not APPLY:
    print("乾跑結束（加 --apply 才寫）"); sys.exit(0)
assert not clash, f"目標已有同名檔（只加不覆蓋），停：{clash}"

stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
bk = DD.parent / f"_backup_system_PING_大機加口徑注入_{stamp}"
shutil.copytree(DD / "PING", bk / "PING"); shutil.copy2(dd_pj_path, bk / "PING.json"); shutil.copy2(conf, bk / "PINGSlicer.conf")
print("備份 →", bk)
for f in files:
    dst = DD / f; dst.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(REPO_PROFILES / f, dst)
for sec, lst in adds.items():
    L = dd_pj.setdefault(sec, [])
    for e in lst:
        fam = e["name"].split(" ")[0] if sec == "machine_list" else e["name"].split("@")[1].split(" ")[0]
        idx = max([i for i, x in enumerate(L) if fam in x["name"]] + [len(L) - 1]) + 1
        L.insert(idx, e)
json.dump(dd_pj, open(dd_pj_path, "w", encoding="utf-8"), ensure_ascii=False, indent=4)
for fp, cur, new in mm_plan:
    t = fp.read_text(encoding="utf-8")
    assert t.count('"nozzle_diameter": "%s"' % cur) == 1
    fp.write_text(t.replace('"nozzle_diameter": "%s"' % cur, '"nozzle_diameter": "%s"' % new), encoding="utf-8", newline="")
# conf 文字手術：只在 models 區段內、只換那一個 dict 的字串（框定區段再動刀）
if conf_plan:
    ms = s.index('"models"'); me = s.index("]", ms)
    seg = s[ms:me]
    for m, old, new in conf_plan:
        pat = re.compile(r'("model": "%s",\s*"nozzle_diameter": ")%s(")' % (re.escape(m), re.escape(old)))
        seg, n = pat.subn(r'\g<1>%s\g<2>' % new, seg)
        assert n == 1, (m, n)
    s2 = s[:ms] + seg + s[me:]
    conf.write_text(s2, encoding="utf-8", newline="")
# read-back
chk = json.load(open(dd_pj_path, encoding="utf-8"))
n = sum(1 for sec in ("machine_list", "process_list") for e in chk.get(sec, []) if is_new(e["name"]))
ok = sum((DD / f).exists() for f in files)
c2, _ = json.JSONDecoder().raw_decode(conf.read_text(encoding="utf-8"))
print(f"read-back：PING.json 新條目 {n}/42 | 檔案就位 {ok}/{len(files)} | machine_model 改 {len(mm_plan)} 檔 | conf models 改 {len(conf_plan)} 條 | conf 可解析 ✓ | installed 版號 {chk.get('version')}")
