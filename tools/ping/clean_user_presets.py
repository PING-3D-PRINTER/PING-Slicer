#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""清除使用者自建參數（Eric 2026-07-18 需求：「把我之前設定的參數清除掉」）。

範圍（Eric 裁定）：user 夾的「線材＋製程」自建參數；機器 user 預設一律保留
（可能是實際在用的具名機：客戶機/展示/交大…）。

安全設計：
  - 執行前全量 zip 備份整個 user 夾（含 .info），放 %APPDATA%\\PingSlicer\\
  - PingSlicer 必須已關閉（app 退出時會回寫，開著清會被吃回去/狀態錯亂）
  - --check 乾跑列清單不動手；--include-machine 才連機器一起清（預設不清）
  - 刪掉的參數若被各機「記住的選擇」引用，重開後軟體自動退回系統預設（無害）

用法：
  python tools/ping/clean_user_presets.py --check
  python tools/ping/clean_user_presets.py
"""
import io, os, sys, glob, time, zipfile, subprocess

USER_BASE = os.path.join(os.environ["APPDATA"], "PingSlicer", "user")
KINDS = ["filament", "process"] + (["machine"] if "--include-machine" in sys.argv else [])

def app_running():
    try:
        out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
        return "ping-slicer.exe" in out or "pingslicer.exe" in out
    except Exception:
        return False

def main():
    check = "--check" in sys.argv
    targets = []
    for u in glob.glob(os.path.join(USER_BASE, "*")):
        if not os.path.isdir(u):
            continue
        for kind in KINDS:
            for f in sorted(glob.glob(os.path.join(u, kind, "*"))):
                if f.endswith((".json", ".info")):
                    targets.append(f)
    names = sorted({os.path.splitext(os.path.basename(f))[0] for f in targets})
    print("將清除 %d 支（%s）：" % (len(names), "+".join(KINDS)))
    for n in names:
        print("  -", n)
    if check:
        print("（--check 乾跑，未動手）")
        return
    if app_running():
        print("⚠ PingSlicer 正在執行——關閉後再跑。")
        sys.exit(3)
    bak = os.path.join(os.environ["APPDATA"], "PingSlicer",
                       "user_backup_%s.zip" % time.strftime("%Y%m%d%H%M%S"))
    with zipfile.ZipFile(bak, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(USER_BASE):
            for fn in files:
                p = os.path.join(root, fn)
                z.write(p, os.path.relpath(p, USER_BASE))
    for f in targets:
        os.remove(f)
    print("已清除 %d 檔；全量備份＝%s" % (len(targets), bak))

if __name__ == "__main__":
    main()
