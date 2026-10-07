# -*- coding: utf-8 -*-
"""閘門：照片磚工作室頁讀的款式庫（stylelib.js），必須與款式庫 JSON 正本語意相同。

為什麼需要這道閘門
------------------
產品用 `file://` 載工作室頁（`src/slic3r/GUI/WebViewDialog.cpp` 的 phototile URL），
所以頁面**不能 fetch 同目錄的 JSON**（file:// origin 會被 CORS 擋）；`<script src>` 不受影響
（samples.js 同理）⇒ 款式庫由本支從 JSON 產生 `stylelib.js`（`var PT_STYLE_LIB=…;`）。
2026-10-06 以前是內嵌在 index.html 的 `<script id="ptStyleLib" type="application/json">`，
AIP 第二班（牌 c-1006-AIP-01）搬出來：index.html 離讀取上限只剩約 90 B，內嵌那一行就佔 23.8 KB。

代價＝同一份資料存在兩處。**改了 JSON 忘了重產 stylelib.js，畫面不會報錯、只會安靜地用舊款式庫**
——這正是最難發現的一類 bug。這支就是把那個沉默變成一次 build 前的紅燈。
（治理端另有一份同內容：根 repo `20260604 ORCA客製/款式庫_照片磚.json`，給照片磚管線 pipeline.py 讀；
  app repo 碰不到它，同步由改款式庫的那一棒負責。）

用法
----
    python tools/ping/verify_phototile_stylelib.py          # 檢查（exit 0 / 1）
    python tools/ping/verify_phototile_stylelib.py --sync   # 用 JSON 正本重產 stylelib.js
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

REPO = Path(__file__).resolve().parents[2]
WEB = REPO / "resources" / "web" / "phototile"
INDEX = WEB / "index.html"
LIBJSON = WEB / "款式庫_照片磚.json"
LIBJS = WEB / "stylelib.js"
IDENT = "PT_STYLE_LIB"

LIBJS_HEADER = (
    "/* 自動生成，不要手改。\r\n"
    "   正本＝resources/web/phototile/款式庫_照片磚.json\r\n"
    "   重生＝python tools/ping/verify_phototile_stylelib.py --sync\r\n"
    "   為什麼是 .js：工作室是 file:// 載入，fetch() 同目錄 JSON 被 CORS 擋；<script src> 不受影響（同 samples.js）。 */\r\n"
)
DECL = re.compile(r"^var %s=(.*);\s*$" % IDENT, re.M)
LOADS = re.compile(r'<script src="stylelib\.js(\?v=[\w.-]+)?"></script>')


def render_libjs(truth):
    packed = json.dumps(truth, ensure_ascii=False, separators=(",", ":"))
    return LIBJS_HEADER + "var %s=%s;\r\n" % (IDENT, packed)

REQUIRED_STYLE_KEYS = {
    "id", "name", "requiresAI", "subjects", "tones", "mode", "slots",
    "preserves", "drops", "minTileMm", "priority",
    # lockSlots＝料色鎖不鎖（Eric 2026-08-17 裁）。頁面用 `s.lockSlots===false` 判斷，
    # 缺了會被當成 undefined ⇒ 靜默走鎖定分支、本地款式又變回鎖死料色。
    "lockSlots",
}
NOZZLES = {"0.4", "0.6", "1.0"}          # 引擎 NOZZLES 白名單；沒有 0.8


def fail(msg):
    print("FAIL  " + msg)
    return 1


def check_unit_cost(root):
    """C++ 的單價常數必須等於款式庫正本的 unitCostNtd.low。

    為什麼要這條：頁面從 JSON 讀價、C++ 的金鑰對話框要顯示累計金額也需要價
    ⇒ 同一個數字存在兩處。這是本檔開頭那個「改了一邊忘了另一邊、畫面不報錯只是安靜錯」
    的同型風險，差別只在它錯的是**錢**。
    """
    hdr = root / "src" / "slic3r" / "Utils" / "PingAiImage.hpp"
    if not hdr.exists():
        return []                      # 還沒有丙案的線就不管這條
    truth = json.loads(LIBJSON.read_text(encoding="utf-8"))
    want = (truth.get("constants", {}).get("aiImage", {}).get("unitCostNtd", {}) or {}).get("low")
    if want is None:
        return ["款式庫正本缺 constants.aiImage.unitCostNtd.low"]
    m = re.search(r"constexpr\s+double\s+UNIT_COST_NTD_LOW\s*=\s*([0-9.]+)\s*;",
                  hdr.read_text(encoding="utf-8"))
    if not m:
        return ["PingAiImage.hpp 找不到 UNIT_COST_NTD_LOW"]
    got = float(m.group(1))
    if abs(got - float(want)) > 1e-9:
        return ["單價不一致：JSON=%s／PingAiImage.hpp=%s（正本是 JSON）" % (want, got)]
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sync", action="store_true",
                    help="用 JSON 正本重產 stylelib.js")
    args = ap.parse_args()

    bad = 0
    for msg in check_unit_cost(REPO):
        bad |= fail(msg)
    for p in (INDEX, LIBJSON):
        if not p.exists():
            return fail("找不到 %s" % p)

    raw = LIBJSON.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        # 0816 鐵則：這批 JSON 用 PowerShell 文字模式寫回會加 BOM，JSON 立刻解不開
        bad |= fail("%s 有 UTF-8 BOM（不可用 PowerShell 文字模式寫回，改用 Python 二進位）"
                    % LIBJSON.name)
        raw = raw[3:]
    try:
        truth = json.loads(raw.decode("utf-8"))
    except Exception as e:
        return fail("%s 不是合法 JSON：%s" % (LIBJSON.name, e))

    if args.sync:
        data = render_libjs(truth).encode("utf-8")
        tmp = LIBJS.with_name(LIBJS.name + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, LIBJS)
        print("SYNC  %s 已由 %s 重產（%d bytes）" % (LIBJS.name, LIBJSON.name, len(data)))
        return 0

    html = INDEX.read_text(encoding="utf-8")
    if 'id="ptStyleLib"' in html:
        bad |= fail('index.html 還留著舊的內嵌款式庫 <script id="ptStyleLib">（已改讀 stylelib.js，兩份會漂）')
    if len(LOADS.findall(html)) != 1:
        bad |= fail("index.html 要恰好載入一次 stylelib.js（實際 %d 次）" % len(LOADS.findall(html)))
    if not LIBJS.exists():
        return fail("找不到 %s（跑 --sync 產生）" % LIBJS.name)
    m = DECL.search(LIBJS.read_text(encoding="utf-8"))
    if not m:
        return fail("%s 找不到 var %s=…; 那一行" % (LIBJS.name, IDENT))
    try:
        inlined = json.loads(m.group(1))
    except Exception as e:
        return fail("%s 的 %s 不是合法 JSON：%s" % (LIBJS.name, IDENT, e))

    # ① 語意相同（不比字面，容許縮排/鍵序不同——比的是資料）
    if inlined != truth:
        bad |= fail("%s 與 %s 不一致。跑 --sync 重產，或確認哪一邊才是你要的。"
                    % (LIBJS.name, LIBJSON.name))
        for k in sorted(set(inlined) | set(truth)):
            if inlined.get(k) != truth.get(k):
                print("      差異鍵：%s" % k)
    else:
        print("PASS  stylelib.js 與正本語意相同（%d 款式／%d 題材）"
              % (len(truth["styles"]), len(truth["subjects"])))

    # ② 結構完備（頁面邏輯讀得到的鍵一個都不能缺，缺了是畫面壞掉而不是報錯）
    subject_ids = {s["id"] for s in truth["subjects"]}
    for s in truth["styles"]:
        miss = REQUIRED_STYLE_KEYS - set(s)
        if miss:
            bad |= fail("款式 %s 缺鍵：%s" % (s.get("id", "?"), sorted(miss)))
            continue
        unknown = set(s["subjects"]) - subject_ids
        if unknown:
            bad |= fail("款式 %s 指到不存在的題材：%s" % (s["id"], sorted(unknown)))
        if set(s["minTileMm"]) != NOZZLES:
            bad |= fail("款式 %s 的 minTileMm 鍵必須恰為 %s（頁面用 toFixed(1) 查表），實際 %s"
                        % (s["id"], sorted(NOZZLES), sorted(s["minTileMm"])))
        want_slots = 4 if s["mode"] == "quad" else 2
        if len(s["slots"]) != want_slots:
            bad |= fail("款式 %s 是 %s，料色該有 %d 支，實際 %d"
                        % (s["id"], s["mode"], want_slots, len(s["slots"])))
        if not 2 <= s["tones"] <= 8:
            # 色階上限 8＝Eric 2026-08-02 裁（引擎 clamp 同值）
            bad |= fail("款式 %s 的 tones=%s 超出 2~8" % (s["id"], s["tones"]))

    # ③ 每個題材至少要有一個款式，否則使用者選到它會看到空清單
    for sid in sorted(subject_ids):
        if not [s for s in truth["styles"] if sid in s["subjects"]]:
            bad |= fail("題材 %s 沒有任何款式" % sid)

    # ④ 誠實登記：哪些題材在「沒有 AI」時無款可用（階段 A 的真實狀態，不是錯誤）
    local_only = [sid for sid in sorted(subject_ids)
                  if not [s for s in truth["styles"]
                          if sid in s["subjects"] and not s["requiresAI"]]]
    if local_only:
        print("INFO  沒有本地款式的題材（階段 A 會顯示「沒有款式可用」）：%s" % local_only)

    if not bad:
        print("OK    款式庫閘門全過")
    return bad


if __name__ == "__main__":
    sys.exit(main())
