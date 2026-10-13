# -*- coding: utf-8 -*-
"""把 repo 裡一台機型的 preset bundle「外科手術式」注入某個 data_dir/system/PING（試用包／客戶機）。

為什麼不整包覆蓋：前代 8 機＋Classic 只活在 %APPDATA%（見 SOP_加機型 §2.8 附記、記憶 appdata-sync-merge-not-replace）。
做法：先備份 → 只加不覆蓋（目標已有同名檔就停）→ PING.json 只插該機型的條目 → 版號不動 → read-back。

用法：
  python inject_model_into_datadir.py --model "FP300 關門"                         # 乾跑（預設）
  python inject_model_into_datadir.py --model "FP300 關門" --apply                 # 實寫
  python inject_model_into_datadir.py --model "FP300 關門" --datadir "D:\\某機\\data_dir" --apply
首例：2026-09-08 FP300 關門 注入 D:\\PING-Slicer-dev-portable（installed 01.00.00.81 / repo 01.00.00.84，版號沿用不抬）。
"""
import argparse, datetime, json, shutil, sys
from pathlib import Path

HERE = Path(__file__).resolve()
REPO_PROFILES = HERE.parents[2] / "resources" / "profiles"

ap = argparse.ArgumentParser()
ap.add_argument("--model", required=True, help="機型全名（PING.json name 欄含此字串者皆算，如 'FP300 關門'）")
ap.add_argument("--datadir", default=r"D:\PING-Slicer-dev-portable\data_dir", help="目標 data_dir（底下要有 system/PING.json）")
ap.add_argument("--apply", action="store_true", help="不加＝乾跑")
a = ap.parse_args()
MODEL, APPLY = a.model, a.apply
DD = Path(a.datadir) / "system"
SECS = ("machine_model_list", "machine_list", "process_list", "filament_list")

repo_pj = json.load(open(REPO_PROFILES / "PING.json", encoding="utf-8"))
dd_pj_path = DD / "PING.json"
assert dd_pj_path.exists(), f"目標沒有 {dd_pj_path}"
dd_pj = json.load(open(dd_pj_path, encoding="utf-8"))
print("installed version", dd_pj.get("version"), "| repo version", repo_pj.get("version"), "（版號不動）")

# 要搬的檔＝PING.json 該機型條目的 sub_path ＋ cover
adds, files = {}, []
cover = f"PING/{MODEL}_cover.png"
if (REPO_PROFILES / cover).exists():
    files.append(cover)
for sec in SECS:
    want = [e for e in repo_pj.get(sec, []) if MODEL in e["name"]]
    have = {e["name"] for e in dd_pj.get(sec, [])}
    adds[sec] = [e for e in want if e["name"] not in have]
    files += ["PING/" + e["sub_path"] for e in want]
    print(f"  {sec}: repo 有 {len(want)} 條 → 要加 {len(adds[sec])} 條")
total = sum(len(v) for v in adds.values())
assert files, f"repo PING.json 沒有含「{MODEL}」的條目"
for f in files:
    assert (REPO_PROFILES / f).exists(), f"repo 缺 {f}"
clash = [f for f in files if (DD / f).exists()]
print(f"檔案 {len(files)} 個，其中目標已有 {len(clash)} 個；PING.json 要加 {total} 條")
if total == 0 and len(clash) == len(files):
    print("目標已完整含此機型，無事可做"); sys.exit(0)
if not APPLY:
    print("乾跑結束（加 --apply 才寫）"); sys.exit(0)
assert not clash, f"目標已有同名檔（只加不覆蓋），停：{clash}"

stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
bk = DD.parent / f"_backup_system_PING_{MODEL.replace(' ', '')}注入_{stamp}"
shutil.copytree(DD / "PING", bk / "PING"); shutil.copy2(dd_pj_path, bk / "PING.json")
print("備份 →", bk)
for f in files:
    dst = DD / f; dst.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(REPO_PROFILES / f, dst)
for sec, lst in adds.items():
    if not lst:
        continue
    L = dd_pj.setdefault(sec, [])
    # 插在同家族條目之後（精靈顯示排序自己算，這裡只求人讀時相鄰）
    fam = MODEL.split(" ")[0]
    idx = max([i for i, e in enumerate(L) if e["name"].startswith(fam)] + [len(L) - 1]) + 1
    for j, e in enumerate(lst):
        L.insert(idx + j, e)
json.dump(dd_pj, open(dd_pj_path, "w", encoding="utf-8"), ensure_ascii=False, indent=4)

chk = json.load(open(dd_pj_path, encoding="utf-8"))
n = sum(1 for sec in SECS for e in chk.get(sec, []) if MODEL in e["name"])
ok = sum((DD / f).exists() for f in files)
print(f"read-back：PING.json 含「{MODEL}」條目 {n} | 檔案就位 {ok} / {len(files)}")
