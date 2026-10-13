#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PINGSlicer.conf 高流量線材可見清單修補（2026-07-14）。

背景：2026-07-12 高流量噴頭案把系統線材改名（高流量 @FF → 四料高流量噴頭）＋新增
「PING PLA - 高流量噴頭」「PING SupPLA - 高流量噴頭」，但 Eric 本機 PINGSlicer.conf 的
「filaments 可見清單」沒跟著換 → 系統裡有、側欄下拉看不到（可見清單濾掉）。

做法（文字手術保格式，見 memory 7b/7c）：
  1) 全檔把 6 個舊名全域換成四料新名（連 orca_presets 每機記住的選擇一起換）。
  2) filaments 陣列重建：去舊名、補 2 支高流量噴頭＋6 支四料（缺哪支補哪支）、排序。
  3) 重算尾行 MD5＝標準 md5(LF 正規化後的 JSON 段, 到最後一個 '}')、大寫 hex。

用法：
  python tools/ping/patch_conf_hf_visible.py --check   # 乾跑：驗 MD5 演算法＋列出將發生的變更
  python tools/ping/patch_conf_hf_visible.py           # 實改（PingSlicer 必須已關閉）
"""
import io, os, re, sys, json, hashlib, shutil, subprocess, time

CONF = os.path.join(os.environ["APPDATA"], "PingSlicer", "PINGSlicer.conf")

RENAME = {
    "PING PLA - 高流量 @FF 0.4": "PING PLA - 四料高流量噴頭 0.4",
    "PING PLA - 高流量 @FF 0.6": "PING PLA - 四料高流量噴頭 0.6",
    "PING PLA - 高流量 @FF 1.0": "PING PLA - 四料高流量噴頭 1.0",
    "PING SupPLA - 高流量 @FF 0.4": "PING SupPLA - 四料高流量噴頭 0.4",
    "PING SupPLA - 高流量 @FF 0.6": "PING SupPLA - 四料高流量噴頭 0.6",
    "PING SupPLA - 高流量 @FF 1.0": "PING SupPLA - 四料高流量噴頭 1.0",
}
ADD = [
    "PING PLA - 高流量噴頭",
    "PING SupPLA - 高流量噴頭",
    "PING PLA - 四料高流量噴頭 0.4", "PING PLA - 四料高流量噴頭 0.6", "PING PLA - 四料高流量噴頭 1.0",
    "PING SupPLA - 四料高流量噴頭 0.4", "PING SupPLA - 四料高流量噴頭 0.6", "PING SupPLA - 四料高流量噴頭 1.0",
]

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

    # 1) 全域換名
    n_ren = {}
    for old, new in RENAME.items():
        n_ren[old] = seg.count(old)
        seg = seg.replace(old, new)

    # 2) filaments 陣列重建（排序、去重、補缺）
    bm = re.search(r'("filaments"\s*:\s*\[)(.*?)(\r?\n\s*\])', seg, re.S)
    if not bm:
        print("⚠ 找不到 filaments 陣列，中止。"); sys.exit(2)
    entries = json.loads("[" + bm.group(2).rstrip().rstrip(",") + "]")
    before = list(entries)
    entries = [e for e in entries if e not in RENAME]          # 舊名（若換名後仍殘留）移除
    for a in ADD:
        if a not in entries:
            entries.append(a)
    entries = sorted(set(entries))
    body = "".join("\r\n        %s," % json.dumps(e, ensure_ascii=False) for e in entries)
    body = body.rstrip(",")  # 最後一項不帶逗號
    seg = seg[: bm.start()] + bm.group(1) + body + bm.group(3) + seg[bm.end():]

    new_hash = md5_of_segment(seg)
    new_tail = re.sub(r"([0-9A-Fa-f]{32})", new_hash, tail, count=1)
    print("換名次數：", {k: v for k, v in n_ren.items() if v})
    print("filaments：%d → %d 支" % (len(before), len(entries)))
    for e in entries:
        if e not in before:
            print("  ＋", e)
    for e in before:
        if e not in entries and e not in RENAME:
            print("  －", e)
    if check:
        print("（--check 乾跑，未寫入）"); return

    if app_running():
        print("⚠ PingSlicer 正在執行——關閉後再跑（app 退出時會覆寫 conf）。"); sys.exit(3)
    bak = CONF + ".bak-hfvisible-" + time.strftime("%Y%m%d%H%M%S")
    shutil.copy2(CONF, bak)
    io.open(CONF, "w", encoding="utf-8", newline="").write(seg + new_tail)
    print("已寫入；備份＝%s；新 MD5=%s" % (bak, new_hash))

if __name__ == "__main__":
    main()
