"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {
  clampSizeMm, fitAspectSize, thickFloorMm, thickAutoMm, thickNote, THICK_MIN_MM, THICK_MAX_MM
} = require("../resources/web/phototile/size_ratio.js");

{
  const result = fitAspectSize({ width: 100, height: 75, aspect: 493 / 376, changed: "width" });
  assert.deepEqual(result, { width: 100, height: 131.1 });
}

{
  const result = fitAspectSize({ width: 100, height: 132, aspect: 493 / 376, changed: "height" });
  assert.deepEqual(result, { width: 100.7, height: 132 });
}

{
  const result = fitAspectSize({ width: 400, height: 75, aspect: 2, changed: "width" });
  assert.deepEqual(result, { width: 200, height: 400 });
}

{
  const result = fitAspectSize({ width: 100, height: 75, aspect: Number.NaN, changed: "width" });
  assert.deepEqual(result, { width: 100, height: 75 });
  assert.equal(clampSizeMm(999, 100), 400);
}

/* AIP 刀 2：厚度下限＝高 ÷ 15 往上取到 0.5、最少 10（規格 R9-11 Q4／Q5）；
   自動值＝max(料數預設〔雙料 10、四料 20〕, 下限)；上限 40。驗收三點＝計畫頁 §06 第 2 列（高 150→10、300→20、600→40）。 */
{
  assert.equal(thickFloorMm(150), 10);
  assert.equal(thickFloorMm(300), 20);
  assert.equal(thickFloorMm(600), 40);
  assert.equal(thickFloorMm(75), 10, "矮磚仍要最少 10");
  assert.equal(thickFloorMm(151), 10.5, "高 ÷ 15 要往上取到 0.5，不是四捨五入");
  assert.equal(thickFloorMm(157.5), 10.5, "剛好整除到 0.5 不得多進一格");
  assert.equal(thickFloorMm(172.6), 12, "11.507 往上取到 12");
  assert.equal(thickFloorMm(800), THICK_MAX_MM, "超過 600 高夾在上限");
  assert.equal(thickFloorMm(Number.NaN), 10);

  assert.equal(thickAutoMm(10, 150), 10);
  assert.equal(thickAutoMm(10, 300), 20);
  assert.equal(thickAutoMm(10, 600), 40);
  assert.equal(thickAutoMm(20, 150), 20, "四料預設 20 比下限厚就照預設（#40）");
  assert.equal(thickAutoMm(20, 300), 20);
  assert.equal(thickAutoMm(20, 450), 30, "四料磚夠高時下限蓋過預設");
  assert.equal(thickAutoMm(20, 800), THICK_MAX_MM);
  assert.equal(THICK_MIN_MM, 2);
  assert.equal(THICK_MAX_MM, 40);
}

/* 厚度欄下面那一行（文案逐字照原型 v4 D:/_sa/aip/proto4_template.html 的 thickInfo）。 */
{
  const note = (userSet, thickMm, defaultMm, heightMm) => thickNote({ userSet, thickMm, defaultMm, heightMm });
  assert.deepEqual(note(false, 10, 10, 150), { warn: false, text: "", action: null }, "自動值＝預設時不多講");
  assert.deepEqual(note(false, 20, 10, 300),
    { warn: false, text: "厚度跟著高度調到 20 mm（高 300 mm ÷ 15，免得印的時候倒）", action: null });
  assert.deepEqual(note(false, 20, 20, 300), { warn: false, text: "", action: null }, "四料 300 高＝預設 20＝不多講");
  assert.deepEqual(note(true, 8, 10, 150),
    { warn: true, text: "比防倒建議的 10 mm 薄（高 ÷ 15）——磚這麼高、這麼薄，印的時候容易倒。", action: "改回 10 mm" });
  assert.deepEqual(note(true, 30, 10, 600),
    { warn: true, text: "比防倒建議的 40 mm 薄（高 ÷ 15）——磚這麼高、這麼薄，印的時候容易倒。", action: "改回 40 mm" });
  assert.deepEqual(note(true, 12, 10, 150), { warn: false, text: "厚度 12 mm（你自己設的）", action: "跟著尺寸自動調" });
  assert.deepEqual(note(true, 10, 10, 150), { warn: false, text: "厚度 10 mm（你自己設的）", action: "跟著尺寸自動調" }, "剛好等於下限不算薄");
  assert.equal(note(true, 15, 20, 150).warn, false, "四料改成 15：比料數預設薄但不比防倒下限薄＝不黃（#40 是他的選擇）");
  assert.equal(note(true, 8, 20, 75).action, "改回 20 mm", "〔改回〕寫按下去真的會變成的自動值，不是下限 10");
}

/* 上限三處要一致（size_ratio.js 常數／index.html 厚度欄 max／engine.js normalizeRequest 的 clamp）：
   只改一處＝頁面讓你打 40、引擎偷偷夾成 30（或反過來），而且沒有任何地方會出聲。 */
{
  const web = path.join(__dirname, "..", "resources", "web", "phototile");
  const html = fs.readFileSync(path.join(web, "index.html"), "utf8");
  const engine = fs.readFileSync(path.join(web, "engine.js"), "utf8");
  const tIn = html.match(/<input[^>]*id="tIn"[^>]*>/);
  assert.ok(tIn, "找不到厚度欄 #tIn");
  assert.equal(Number((tIn[0].match(/\bmax="([\d.]+)"/) || [])[1]), THICK_MAX_MM, "index.html 厚度欄 max 沒跟上 THICK_MAX_MM");
  assert.equal(Number((tIn[0].match(/\bmin="([\d.]+)"/) || [])[1]), THICK_MIN_MM, "index.html 厚度欄 min 沒跟上 THICK_MIN_MM");
  const clamp = engine.match(/clamp\('thick',\s*size\.thickMm,\s*([\d.]+),\s*([\d.]+),/);
  assert.ok(clamp, "找不到 engine.js 的厚度 clamp");
  assert.equal(Number(clamp[1]), THICK_MIN_MM, "engine.js 厚度下限沒跟上");
  assert.equal(Number(clamp[2]), THICK_MAX_MM, "engine.js 厚度上限沒跟上");

  /* 頁面接線：改尺寸、換料數、改厚度欄都要走 ptThickSync（自動值與黃字都在那裡算）；
     手打的值夾在共用常數裡，不再自己寫一個數字。 */
  const body = name => {
    const at = html.indexOf(name);
    assert.ok(at >= 0, "找不到 " + name);
    return html.slice(at, html.indexOf("\n}", at));
  };
  // 要是真的程式行（行首就是呼叫）——只比字串的話，註解掉的那行也會過（突變 m11 實測抓到）
  assert.match(body("function applySizeChange("), /^\s*ptThickSync\(\);/m, "改尺寸沒有重算厚度");
  assert.match(body("function renderSlots("), /^\s*ptThickSync\(\);/m, "換料數沒有重算厚度");
  assert.match(body("document.getElementById('tIn').onchange"), /THICK_MAX_MM/, "厚度欄沒用共用上限");
  const sync = body("function ptThickSync(");
  assert.match(sync, /dflt=PT_THICK_DEFAULT\[slotCount\]/, "自動值沒疊在料數預設上");
  assert.match(sync, /if\(!ptThickUserSet\) params\.thick=S\.thickAutoMm\(dflt, params\.height\)/, "沒自己改時厚度沒跟著自動值");
  assert.match(sync, /S\.thickNote\(\{userSet:ptThickUserSet, thickMm:params\.thick, defaultMm:dflt, heightMm:params\.height\}\)/, "提醒沒用共用的 thickNote");
  assert.match(sync, /classList\.toggle\('warn',n\.warn\)/, "黃字沒接上 .sub.warn");
  assert.match(sync, /ptThickUserSet=false; ptThickSync\(\);/, "〔改回〕沒有回到自動值");
  assert.match(html, /<div class="sub" id="thickInfo"><\/div>/, "厚度提醒那一行不在畫面上");
  assert.ok(!/ptApplyThickDefault/.test(html), "舊的只看料數的厚度函式還在");
  assert.doesNotMatch(html, /size_ratio\.js\?v=20260720b/, "改了 size_ratio.js 要動版本字串（SOP WebView2 §N）");
}

/* AIP 第二班 Q3：自動厚度提醒的高度保留 0.1 mm，整數不補 .0。 */
{
  for (const [heightMm, shown] of [
    [579.8, "579.8"],
    [269.8, "269.8"],
    [299.8, "299.8"],
    [298.5, "298.5"],
    [580, "580"],
    [300, "300"],
    [579.84, "579.8"],
    [579.96, "580"]
  ]) {
    const note = thickNote({ userSet: false, thickMm: 10, defaultMm: 10, heightMm });
    assert.ok(note.text.includes(`高 ${shown} mm ÷ 15`), `高度 ${heightMm} 應顯示高 ${shown} mm ÷ 15`);
  }
  const fractional = thickNote({ userSet: false, thickMm: 10, defaultMm: 10, heightMm: 579.8 });
  assert.doesNotMatch(fractional.text, /高 580 mm/, "579.8 不得顯示成高 580 mm");
  assert.match(fractional.text, /^厚度跟著高度調到 39 mm/, "只改高度顯示，自動厚度仍是 39 mm");
  const integer = thickNote({ userSet: false, thickMm: 10, defaultMm: 10, heightMm: 580 });
  assert.doesNotMatch(integer.text, /580\.0/, "整數高度不補 .0");
  assert.deepEqual(thickNote({ userSet: true, thickMm: 30, defaultMm: 10, heightMm: 579.8 }),
    { warn: true, text: "比防倒建議的 39 mm 薄（高 ÷ 15）——磚這麼高、這麼薄，印的時候容易倒。", action: "改回 39 mm" });
  assert.deepEqual(thickNote({ userSet: true, thickMm: 39, defaultMm: 10, heightMm: 579.8 }),
    { warn: false, text: "厚度 39 mm（你自己設的）", action: "跟著尺寸自動調" });
}

console.log("photo-tile proportional size tests: PASS");
