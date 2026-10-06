"use strict";

/* 照片磚工作室的 CIS 0824〈橘色語意收斂〉守衛：橘只代表「你在這裡」。
   T064 改了 .sub.warn／.btn.primary 兩處；AIP 刀 2（開發中清單 #22）改了另外 4 處（c-0925-ACC-39）＋ Q6 的 AI 小標。
   畫面上長怎樣以無頭 Edge 前後對照截圖為準（SOP GUI §26-7）；這支只防有人把樣式改回去。 */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const web = path.join(__dirname, "..", "resources", "web", "phototile");
const html = fs.readFileSync(path.join(web, "index.html"), "utf8");
const calib = fs.readFileSync(path.join(web, "calibration.html"), "utf8");
const WARN = "#946310";   // ping-cis tokens.json color.warning.fg（amber-700）

const rule = (src, sel) => {
  const at = src.indexOf(sel + "{");
  assert.ok(at >= 0, "找不到樣式 " + sel);
  return src.slice(at, src.indexOf("}", at));
};

// T064（Eric 2026-09-25「Q2 要」）
assert.match(rule(html, ".sub.warn"), new RegExp("color:" + WARN), "警示小字不是警示黃");
assert.match(rule(html, ".btn.primary"), /background:var\(--black\)/, "主要按鈕不是炭黑底");

// ① 「產生」下方狀態列的警示字
assert.match(html, new RegExp("cls==='warn' \\? '" + WARN + "'"), "genStatline 的 warn 不是警示黃");
// ①b 尺寸卡「⚠ 目前高度 N mm——…放得下」（清單外、同性質：警示字，跟 ① 同一條 0824 裁示）
assert.match(html, new RegExp("hint\\.style\\.color = over \\? '" + WARN + "'"), "尺寸卡的高度警示不是警示黃");
// ② 款式卡「需放大到 N mm」「需要四料機台」
assert.match(rule(html, ".scard .pill.no"), new RegExp("color:" + WARN), ".pill.no 不是警示黃");
// ③ 四色校正頁的主要按鈕
assert.match(rule(calib, "button.primary"), /background:var\(--ink\)/, "校正頁主要按鈕不是炭黑底");
// ④ 選中：頂列切換鈕＝橘底線（不是炭黑底）；主角窗的目前主角＝左側橘條（不再借 .primary）
const segOn = rule(html, ".seg button.on");
assert.doesNotMatch(segOn, /background:var\(--black\)/, "頂列選中又長得跟主要按鈕一樣");
assert.match(segOn, /box-shadow:inset 0 -3px 0 var\(--orange\)/, "頂列選中沒有橘底線");
assert.match(rule(html, ".cfmBox .btn.cur"), /box-shadow:inset 3px 0 0 var\(--orange\)/, "目前主角沒有左側橘條");
assert.match(html, /btn\.className='btn'\+\(sub\.id===ptSubject\?' cur':''\)/, "主角窗的目前主角沒用 .cur");
// ⑤ AI 小標
const tagAi = rule(html, ".scard .tag.ai");
assert.doesNotMatch(tagAi, /orange/, "AI 小標還是橘色");
assert.match(tagAi, /border:1px solid #8A8A8A/, "AI 小標不是原型 v4 的中性灰框");

console.log("photo-tile CIS guards: PASS");
