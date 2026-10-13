#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PINGSlicer.conf 四料高流量「口徑合一」修補（2026-07-18）。

背景：Eric 2026-07-18 裁「口徑合一」——四料高流量噴頭 PLA/SupPLA 各由 0.4/0.6/1.0
三支合併為一支（PA 一律開、最大體積流量 30、床溫 60）。系統參數已併，但本機
PINGSlicer.conf 的 filaments 可見清單與 orca_presets 記住的選擇仍是舊口徑名
→ 不補的話合併支在下拉看不到（同 0714 高流量可見性坑）。

做法（同 patch_conf_hf_visible.py 方案，memory 7b/7c）：
  1) 全檔把 6 個口徑名全域換成合併名（含 orca_presets 每機記住的選擇）。
  2) filaments 陣列重建：換名後去重、補缺、排序。
  3) 重算尾行 MD5（標準 md5・LF 正規化 JSON 段・大寫）。

用法：
  python tools/ping/patch_conf_ff_merge.py --check   # 乾跑
  python tools/ping/patch_conf_ff_merge.py           # 實改（PingSlicer 必須已關閉）
"""
import io, os, re, sys, json, hashlib, shutil, subprocess, time

CONF = os.path.join(os.environ["APPDATA"], "PingSlicer", "PINGSlicer.conf")

MERGED_PLA = "PING PLA - 四料高流量噴頭"
MERGED_SUP = "PING SupPLA - 四料高流量噴頭"
RENAME = {}
for _nz in ("0.4", "0.6", "1.0"):
    RENAME["%s %s" % (MERGED_PLA, _nz)] = MERGED_PLA
    RENAME["%s %s" % (MERGED_SUP, _nz)] = MERGED_SUP
ADD = [MERGED_PLA, MERGED_SUP]

def md5_of_segment(seg):
    return hashlib.md5(seg.replace("\r\n", "\n").encode("utf-8")).hexdigest().upper()

def app_running():
    try:
        out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        return "ping-slicer.exe" in out or "pingslicer.exe" in out
    except Exception:
        return False

def main():
    check = "--check" in sys.argv
    t = io.open(CONF, encoding="utf-8", newline="").read()
    last = t.rfind("}")
    seg, tail = t[: last + 1], t[last + 1 :]
    m = re.search(r"# MD5 checksum ([0-9A-Fa-f]{32})", tail)
    cur_hash, calc = (m.group(1).upper() if m else None), md5_of_segment(seg)
    print("尾行 MD5=%s / 重算=%s / %s" % (cur_hash, calc, "一致✓" if cur_hash == calc else "不一致✗"))
    if cur_hash != calc:
        print("⚠ MD5 演算法對不上現檔，中止（防止寫壞）。")
        sys.exit(2)

    # 1) 全域換名（長口徑名→合併短名；合併名是口徑名的前綴，只有帶尾碼的會被換到）
    n_ren = {}
    for old, new in RENAME.items():
        n_ren[old] = seg.count(old)
        seg = seg.replace(old, new)

    # 2) filaments 陣列重建（換名後去重、補缺、排序）
    bm = re.search(r'("filaments"\s*:\s*\[)(.*?)(\r?\n\s*\])', seg, re.S)
    if not bm:
        print("⚠ 找不到 filaments 陣列，中止。"); sys.exit(2)
    entries = json.loads("[" + bm.group(2).rstrip().rstrip(",") + "]")
    before = list(entries)
    for a in ADD:
        if a not in entries:
            entries.append(a)
    entries = sorted(set(entries))
    body = "".join("\r\n        %s," % json.dumps(e, ensure_ascii=False) for e in entries)
    body = body.rstrip(",")
    seg = seg[: bm.start()] + bm.group(1) + body + bm.group(3) + seg[bm.end():]

    new_hash = md5_of_segment(seg)
    new_tail = re.sub(r"([0-9A-Fa-f]{32})", new_hash, tail, count=1)
    print("換名次數：", {k: v for k, v in n_ren.items() if v})
    print("filaments：%d → %d 支" % (len(before), len(entries)))
    if check:
        print("（--check 乾跑，未寫入）"); return

    if app_running():
        print("⚠ PingSlicer 正在執行——關閉後再跑（app 退出時會覆寫 conf）。"); sys.exit(3)
    bak = CONF + ".bak-ffmerge-" + time.strftime("%Y%m%d%H%M%S")
    shutil.copy2(CONF, bak)
    io.open(CONF, "w", encoding="utf-8", newline="").write(seg + new_tail)
    print("已寫入；備份＝%s；新 MD5=%s" % (bak, new_hash))

if __name__ == "__main__":
    main()
