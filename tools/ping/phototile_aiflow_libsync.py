# -*- coding: utf-8 -*-
"""照片磚 AIP 刀 4：把原型 v4 抽出來的提示詞文字（tools/ping/phototile_aiflow_fixture.json 的 lib）寫進款式庫正本。

先跑 node tools/ping/phototile_aiflow_extract.js（從原型用程式抽、sha256 釘住），再跑這支：
    python tools/ping/phototile_aiflow_libsync.py           # 寫進 款式庫_照片磚.json，再重產 stylelib.js
    python tools/ping/phototile_aiflow_libsync.py --check   # 只檢查：款式庫裡那幾段跟抽出來的逐字相同（exit 0／1）

寫進去的（全是新增，既有欄位一個字都不動）：
  constants.toneRulesZh            中文品味規則（英文那份 constants.toneRules 照舊）
  constants.aiFlow                 給聊天型 AI 的前言、列印細節、中文料色那一行、我的款式的可印性規則（中英）、
                                   形狀字樣、換個說法五句＋「請 AI 重產這個比例」那句（中英）
  styles[*].promptTemplateZh       每個 AI 款式的中文提示詞（英文 promptTemplate 照舊）
🔴 英文 promptTemplate／toneRules 必須跟原型的英文逐字相同、而且這支不改它們：照片磚管線/pipeline.py 讀的就是它們。
   對不上就停，不寫（那代表產品或原型有一邊動過，要先看清楚是哪一邊）。
JSON 照款式庫原本的格式寫回（indent 1、不轉 ASCII、CRLF）；寫法 .tmp → os.replace。
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

REPO = Path(__file__).resolve().parents[2]
FIXTURE = REPO / "tools" / "ping" / "phototile_aiflow_fixture.json"
LIBJSON = REPO / "resources" / "web" / "phototile" / "款式庫_照片磚.json"
VERIFY = REPO / "tools" / "ping" / "verify_phototile_stylelib.py"

AIFLOW_NOTE = ("AIP 刀 4（計畫頁 §03；規格 R9-11）：客戶「複製提示詞 → 用自己的 AI 產圖 → 貼回工作室」那條路用的文字。"
               "全部由 tools/ping/phototile_aiflow_extract.js 從原型 v4 用程式抽出（sha256 見 source），"
               "phototile_aiflow_libsync.py 寫進來；不要手改，改原型再重抽。頁面這端的組法在 aiflow.js（buildPrompt），"
               "金鑰直連（開發者模式）也走同一個組法、只少前言。英文的 promptTemplate／toneRules 不在這裡，照舊給 pipeline.py 讀。")


def dump(obj):
    return (json.dumps(obj, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n").encode("utf-8")


def merged(truth, fx):
    lib = fx["lib"]
    errs = []
    if truth["constants"].get("toneRules") != lib["enCheck"]["toneRules"]:
        errs.append("constants.toneRules 跟原型的英文不一樣")
    ai = [s for s in truth["styles"] if s.get("requiresAI")]
    want = lib["enCheck"]["promptTemplate"]
    if sorted(s["id"] for s in ai) != sorted(want):
        errs.append("AI 款式清單跟原型不一樣：產品 %s／原型 %s" % (sorted(s["id"] for s in ai), sorted(want)))
    for s in ai:
        if s["id"] in want and s.get("promptTemplate") != want[s["id"]]:
            errs.append("%s 的英文 promptTemplate 跟原型不一樣" % s["id"])
    if errs:
        return None, errs
    out = dict(truth)
    const = {}
    for k, v in truth["constants"].items():
        if k in ("toneRulesZh", "aiFlow", "_aiFlow"):
            continue
        const[k] = v
        if k == "toneRules":
            const["toneRulesZh"] = lib["toneRulesZh"]
    flow = {"source": {"template": fx["source"]["template"], "sha256": fx["source"]["sha256"]}}
    flow.update(lib["aiFlow"])
    const["aiFlow"] = flow
    const["_aiFlow"] = AIFLOW_NOTE
    out["constants"] = const
    styles = []
    for s in truth["styles"]:
        n = {}
        for k, v in s.items():
            if k == "promptTemplateZh":
                continue
            n[k] = v
            if k == "promptTemplate" and s.get("requiresAI"):
                n["promptTemplateZh"] = lib["promptTemplateZh"][s["id"]]
        styles.append(n)
    out["styles"] = styles
    return out, []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    fx = json.loads(FIXTURE.read_text(encoding="utf-8"))
    raw = LIBJSON.read_bytes()
    truth = json.loads(raw.decode("utf-8"))
    out, errs = merged(truth, fx)
    if errs:
        for e in errs:
            print("FAIL  " + e)
        return 1
    data = dump(out)
    if args.check:
        if data != raw:
            print("FAIL  款式庫裡的 AI 產圖文字跟原型抽出來的不一致（重跑 extract＋libsync）")
            return 1
        print("OK    款式庫的 AI 產圖文字＝原型 v4 抽出來的（sha256 %s）" % fx["source"]["sha256"][:12])
        return 0
    if data == raw:
        print("SAME  款式庫已經是最新")
    else:
        tmp = LIBJSON.with_name(LIBJSON.name + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, LIBJSON)
        print("WRITE %s（%d → %d bytes）" % (LIBJSON.name, len(raw), len(data)))
    r = subprocess.run([sys.executable, str(VERIFY), "--sync"], cwd=str(REPO))
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
