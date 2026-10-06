/* 照片磚 AIP 刀 4：「複製提示詞 → 用自己的 AI 產圖 → 貼回工作室」（aiflow.js）——計畫頁 §06 第 4 列。
 *
 * 比對的對象是**原型 v4 自己的程式碼跑出來的結果**（tools/ping/phototile_aiflow_fixture.json，
 * 由 phototile_aiflow_extract.js 在 vm 裡執行原型抽出、sha256 釘住），不是這裡手抄的字串：
 *   (a) 提示詞中英兩版、前言、列印細節、料色那一行、換個說法五句＋改比例那句：產品組出來的＝原型組出來的，逐字
 *   (b) 各機尺寸上限：產品算式（餵 C++ 送來的 models 形狀）＝計畫頁 §07 那張表＝原型自己的算式（在 vm 裡跑原型原文，逐點比）
 *   (c) 貼回後四項檢查的判斷與門檻
 *   (d) 提示詞語言（21 種介面語言都接得住）
 *   (e) 我的款式（不能跟內建同名、{minPx}／{tones} 存成佔位字）
 *   (f) 金鑰衛生：網頁層不得出現金鑰識別字（規則從 python 閘門本身讀，不另抄一份）
 *   (g) 接線：index.html 的載入順序、拿掉的舊 AI 流程、協定名稱跟 C++ 一致
 * 用法：node tools/ping/phototile_aiflow_test.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const ROOT = path.join(__dirname, '..', '..');
const WEB = path.join(ROOT, 'resources', 'web', 'phototile');
const AF = require(path.join(WEB, 'aiflow.js'));
const E = require(path.join(WEB, 'engine.js'));
const F = require(path.join(WEB, 'matflow.js'));
const SZ = require(path.join(WEB, 'size_ratio.js'));
const FX = JSON.parse(fs.readFileSync(path.join(__dirname, 'phototile_aiflow_fixture.json'), 'utf8'));
const libJs = fs.readFileSync(path.join(WEB, 'stylelib.js'), 'utf8');
const box = {}; vm.runInNewContext(libJs, box);
const LIB = JSON.parse(JSON.stringify(box.PT_STYLE_LIB));   // vm 裡的物件原型不同，deepStrictEqual 會誤判；轉一手
const AFC = LIB.constants.aiFlow;
const idx = fs.readFileSync(path.join(WEB, 'index.html'), 'utf8');
const afSrc = fs.readFileSync(path.join(WEB, 'aiflow.js'), 'utf8');
const afCss = fs.readFileSync(path.join(WEB, 'aiflow.css'), 'utf8');

let pass = 0, fail = 0;
function check(name, fn) {
  try { fn(); pass++; console.log('  ✅ ' + name); }
  catch (e) { fail++; console.log('  ❌ ' + name + '\n     ' + String(e && e.message || e).split('\n').slice(0, 4).join(' | ')); }
}
const enLine = hx => F.aiPaletteLine({ colors: hx });
function productPrompt(p) {
  const li = AF.langInfo(p.uiLang, AFC);
  const style = p.mine ? p.mine : LIB.styles.find(s => s.id === p.style);
  return AF.buildPrompt({ lib: LIB, style, mine: !!p.mine, lang: li.prompt, reply: li.reply, aspect: p.aspect, tileW: p.tileW,
    xMinMm: AF.xMinMm(p.noiseMm, parseFloat(p.nozzle)), zMinMm: AF.zMinMm(E.layerHeightMm(p.mode, parseFloat(p.nozzle))),
    hexes: p.hexes, tones: p.tones, enPalette: enLine });
}

console.log('原型 ' + FX.source.template + '  sha256 ' + FX.source.sha256.slice(0, 16) + '…');

console.log('\n(a) 提示詞文字＝原型逐字（款式庫裡的那幾段＝原型抽出來的；產品組出來的＝原型組出來的）');
check('款式庫 constants.toneRulesZh／aiFlow 各段＝原型抽出來的（逐字）', () => {
  assert.strictEqual(LIB.constants.toneRulesZh, FX.lib.toneRulesZh);
  for (const k of Object.keys(FX.lib.aiFlow)) assert.deepStrictEqual(AFC[k], FX.lib.aiFlow[k], 'aiFlow.' + k + ' 跟原型不一樣');
  assert.strictEqual(AFC.source.sha256, FX.source.sha256, '款式庫記的原型 sha256 跟抽樣本的那份不一樣');
});
check('每個 AI 款式的中文提示詞＝原型；英文 promptTemplate／toneRules 一字未動（pipeline.py 讀它們）', () => {
  const ai = LIB.styles.filter(s => s.requiresAI);
  assert.deepStrictEqual(ai.map(s => s.id).sort(), Object.keys(FX.lib.promptTemplateZh).sort());
  for (const s of ai) {
    assert.strictEqual(s.promptTemplateZh, FX.lib.promptTemplateZh[s.id], s.id + ' 中文版跟原型不一樣');
    assert.strictEqual(s.promptTemplate, FX.lib.enCheck.promptTemplate[s.id], s.id + ' 英文版被改了');
  }
  assert.strictEqual(LIB.constants.toneRules, FX.lib.enCheck.toneRules, 'toneRules 被改了');
  assert(LIB.styles.filter(s => !s.requiresAI).every(s => !('promptTemplateZh' in s)), '本地款式不該有中文提示詞');
});
check('換個說法五句的順序與標籤＝原型結果頁那五顆；改比例那句另放', () => {
  assert.deepStrictEqual(AFC.retry.map(p => p.key), ['color', 'thin', 'bg', 'like', 'again']);
  assert.deepStrictEqual(AFC.retry.map(p => p.label), ['顏色不對', '太細、看不清楚', '背景跟主體黏在一起', '不像（五官或輪廓跑掉）', '就是再產一張']);
  assert.strictEqual(AFC.ratioPhrase.key, 'shape');
});
check('產品組出來的提示詞＝原型組出來的（' + FX.samples.prompts.length + ' 組：8 款 × 中英、簡中／日文回覆、三種形狀、口徑 0.4／0.6／1.0、細橫條兩種寫法、我的款式）', () => {
  let n = 0;
  for (const p of FX.samples.prompts) {
    const got = productPrompt(p);
    if (got !== p.out) {
      let i = 0; while (i < got.length && got[i] === p.out[i]) i++;
      throw new Error((p.style || p.mine.id) + '／' + p.uiLang + ' 第 ' + i + ' 字起不同：產品「' + got.slice(i, i + 40) + '」原型「' + p.out.slice(i, i + 40) + '」');
    }
    n++;
  }
  assert.strictEqual(n, FX.samples.prompts.length);
  const kinds = new Set(FX.samples.prompts.map(p => AF.shapeKey(p.aspect)));
  assert.deepStrictEqual([...kinds].sort(), ['landscape', 'portrait', 'square'], '樣本沒涵蓋三種形狀');
  assert(FX.samples.prompts.some(p => /細的橫條也印得出來|even thin horizontal bands/.test(p.out)) && FX.samples.prompts.some(p => /橫條只要有|only needs to be at least/.test(p.out)), '樣本沒涵蓋列印細節兩種寫法');
});
check('金鑰直連（開發者模式）用同一份、只少前言（R9-11「提示詞本體不另寫一套」）', () => {
  const p = FX.samples.prompts[0], li = AF.langInfo(p.uiLang, AFC), style = LIB.styles.find(s => s.id === p.style);
  const base = { lib: LIB, style, lang: li.prompt, reply: li.reply, aspect: p.aspect, tileW: p.tileW, xMinMm: AF.xMinMm(p.noiseMm, 0.4), zMinMm: 0.2, hexes: p.hexes, tones: p.tones, enPalette: enLine };
  const full = AF.buildPrompt(base), api = AF.buildPrompt(Object.assign({ preface: false }, base));
  assert(full.endsWith(api) && full.length > api.length && !api.startsWith('請用我附上的照片'));
  const iTone = api.indexOf(LIB.constants.toneRulesZh), iPal = api.indexOf('可列印的顏色');
  assert(iTone > 0 && iPal > iTone, '料色那一行沒接在品味規則後面（放上面會被它讓位）');
});
check('換個說法（' + FX.samples.fixes.length + ' 組：六句 × 中英 × 雙料／四料 × 兩種形狀）＝原型', () => {
  for (const f of FX.samples.fixes) {
    const li = AF.langInfo(f.uiLang, AFC), sh = AF.shapeOf(f.aspect, AFC, li.prompt);
    assert.strictEqual(AF.minPxOf(AF.xMinMm(f.noiseMm, parseFloat(f.nozzle)), f.tileW, sh.px), f.minPx, '最細線寬換算跟原型不一樣');
    assert.strictEqual(AF.fixText(AFC, f.key, li.prompt, { hexes: f.hexes, tones: f.tones, minPx: f.minPx, shape: sh }), f.out, f.key + '／' + f.uiLang);
  }
});
check('形狀字樣與門檻（1.2、1/1.2 邊界）＝原型 shape()', () => {
  for (const s of FX.samples.shapes) {
    const L = AF.langInfo(s.uiLang, AFC).prompt, g = AF.shapeOf(s.aspect, AFC, L);
    assert.deepStrictEqual({ txt: g.txt, size: g.size, zh: g.ui, px: g.px }, s.out, s.uiLang + ' ' + s.aspect);
  }
});
check('像素換算（左右＝max(雜訊濾除, 2×口徑)、上下＝一層）＝原型 xMin／zMin／minPx／minPxZ（' + FX.samples.pxs.length + ' 組）', () => {
  for (const p of FX.samples.pxs) {
    const nz = parseFloat(p.nozzle), x = AF.xMinMm(p.noiseMm, nz), z = AF.zMinMm(E.layerHeightMm(p.mode, nz)), px = AF.shapeOf(p.aspect, AFC, 'en').px;
    assert.strictEqual(x, p.xMin); assert.strictEqual(z, p.zMin);
    assert.strictEqual(AF.minPxOf(x, p.tileW, px), p.minPx); assert.strictEqual(AF.minPxOf(z, p.tileW, px), p.minPxZ);
  }
});
check('比例字樣 ratioTxt／cropPct＝原型', () => {
  for (const r of FX.samples.ratios) assert.strictEqual(AF.ratioTxt(r.a), r.out, String(r.a));
  for (const c of FX.samples.crops) assert.strictEqual(AF.cropPct(c.aI, c.aT), c.out);
});
check('英文料色那一行（產品在 matflow.js）＝原型英文版', () => {
  for (const hx of [['#EDEBE6', '#3B3C3E'], ['#EDEBE6', '#D9A55B', '#8A5A34', '#2E2B2A']])
    assert.strictEqual(enLine(hx), AF.fillT(FX.paletteLineEn, { n: hx.length, hexes: hx.join(', ') }));
});

console.log('\n(b) 各機尺寸上限＝計畫頁 §07 的表＝原型自己的算式');
/* 原型的算式原文（MODELS～常數＋M～widestAtTop）在 vm 裡跑；S 只給它要讀的那幾格 */
const P = (() => {
  const ctx = { S: { model: 'dual', towerRule: 'reach', thickManual: false, thick: 10 } };
  vm.createContext(ctx);
  vm.runInContext(FX.formulas.src + '\nglobalThis.__M = { MODELS, TOWER, W_MARGIN, SIZE_MIN, BASE_THICK, THICK_RATIO, THICK_MAX };', ctx);
  return ctx;
})();
const PM = P.__M.MODELS;
/* 原型 MODELS → C++ 送來的 models 形狀（WebViewDialog.cpp：model＝printer_model、bedD、maxH、nozzles、geo{pe,arm,R}） */
const MODELS = Object.keys(PM).filter(k => !PM[k].like).map(k => ({ model: PM[k].name + ' 同進照片磚', mode: PM[k].mode, bedD: PM[k].bedD, maxH: PM[k].maxH, nozzles: PM[k].noz, geo: PM[k].geo, key: k }));
const thick10 = h => SZ.thickAutoMm(10, h), thick20 = h => SZ.thickAutoMm(20, h);
const PLAN = { 'FD300': [250, 300, 299, 250, 250], 'FD300 Pro': [250, 270, null, 250, 250], 'FD450 Pro': [400, 600, null, 400, 400],
  'FD600 Pro': [550, 580, null, 550, 550], 'FD800 Pro': [750, 600, 571, 594, 478], 'FF600': [550, 580, 562, 335, 219], 'FF800': [750, 600, 573, 607, 492] };
check('常數＝原型：寬上限邊距 50、最小 20、循環塔 35／15／8；最小 20＝index.html SIZE_MIN_MM；塔＝index.html buildRequest 的 cycle', () => {
  assert.strictEqual(AF.W_MARGIN, P.__M.W_MARGIN); assert.strictEqual(AF.SIZE_MIN, P.__M.SIZE_MIN);
  assert.deepStrictEqual(AF.TOWER, Object.assign({}, P.__M.TOWER));
  assert(new RegExp('SIZE_MIN_MM=' + AF.SIZE_MIN + ',').test(idx), 'index.html 的 SIZE_MIN_MM 跟 aiflow 不一樣');
  assert(new RegExp('sizeMm:' + AF.TOWER.size + ', gapMm:' + AF.TOWER.gap + ', brimMm:' + AF.TOWER.brim + '\\}').test(idx), '循環塔尺寸跟 index.html buildRequest 不一樣（擺位要算同一座塔）');
  assert.strictEqual(SZ.thickAutoMm(10, 600), P.thickFloor(600)); assert.strictEqual(SZ.thickAutoMm(10, 165), P.thickFloor(165));
});
check('計畫頁 §07：七台的寬／高上限、越往上從哪裡開始收、磚高到上限時最寬（塔改後面／照現在）', () => {
  for (const m of MODELS) {
    const name = AF.modelShort(m.model), want = PLAN[name];
    assert(want, '表上沒有 ' + name);
    const lz = AF.limitZ(m.geo);
    const got = [m.bedD - AF.W_MARGIN, m.maxH, lz >= m.maxH ? null : Math.round(lz), AF.widestAtTop(m, thick10, 'reach'), AF.widestAtTop(m, thick10, 'now')];
    assert.deepStrictEqual(got, want, name);
    if (m.mode === 'quad') assert.deepStrictEqual([AF.widestAtTop(m, thick20, 'reach'), AF.widestAtTop(m, thick20, 'now')], [want[3], want[4]], name + '（四料預設厚 20）');
  }
});
check('🔴 產品算式＝原型算式（vm 裡跑原型原文；七台 × 兩種塔擺法 × 寬高網格的 fitsAt、可達半徑、三種最大寬高）', () => {
  let n = 0;
  for (const m of MODELS) {
    const pm = PM[m.key];
    for (let z = 0; z <= m.maxH + 40; z += 7) { assert(Math.abs(AF.reachAt(z, m) - P.reachAt(z, pm)) < 1e-9, 'reachAt ' + m.key + ' ' + z); n++; }
    for (const rule of ['reach', 'now']) {
      for (let W = 20; W <= m.bedD; W += 13) for (let H = 20; H <= m.maxH + 30; H += 17) {
        const T = thick10(H), a = AF.fitsAt(W, H, T, m, rule), b = P.fitsAt(W, H, T, pm, rule);
        assert.deepStrictEqual(a, Object.assign({}, b), m.key + ' ' + rule + ' ' + W + '×' + H); n++;
      }
      for (const asp of [0.5, 2 / 3, 0.75, 1, 1.25, 1.5, 2]) {
        P.S.towerRule = rule;
        assert.strictEqual(AF.maxWLocked(asp, m, thick10, rule), P.maxWLocked(asp, pm), 'maxWLocked ' + m.key + ' ' + asp + ' ' + rule); n++;
      }
      for (let H = 20; H <= m.maxH; H += 41) { assert.strictEqual(AF.maxWFree(H, m, thick10, rule), P.maxWFree(H, pm)); n++; }
      for (let W = 20; W <= m.bedD - 50; W += 47) { assert.strictEqual(AF.maxHFree(W, m, thick10, rule), P.maxHFree(W, pm)); n++; }
      P.S.towerRule = 'reach';
    }
  }
  assert(n > 3000, '比對點太少：' + n);
});
check('照哪一台算：進來時的機型對得上就用它（長名先比）；對不上／沒送＝同料數最小那台；沒有 models＝不加這一層', () => {
  const pick = (mode, cur) => { const r = AF.pickModel(MODELS, mode, cur); return r && [AF.modelShort(r.mdl.model), r.unknown]; };
  assert.deepStrictEqual(pick('dual', 'FD300 Pro 同進'), ['FD300 Pro', false]);
  assert.deepStrictEqual(pick('dual', 'FD300 同進'), ['FD300', false]);
  assert.deepStrictEqual(pick('dual', 'FD300 同進照片磚'), ['FD300', false]);
  assert.deepStrictEqual(pick('dual', 'FD300 Pro'), ['FD300 Pro', false]);
  assert.deepStrictEqual(pick('quad', 'FF800 同進'), ['FF800', false]);
  assert.deepStrictEqual(pick('dual', null), ['FD300 Pro', true], '不確定是哪台＝同料數最小（原型 dualUnknown＝FD300 Pro）');
  assert.deepStrictEqual(pick('quad', 'XYZ 9000'), ['FF600', true]);
  assert.strictEqual(AF.pickModel([], 'dual', null), null);
  assert.strictEqual(AF.pickModel(undefined, 'dual', null), null);
  assert.strictEqual(AF.modelLabel(AF.pickModel(MODELS, 'dual', null)), '不確定是哪台，先照最小的 FD300 Pro 算');
  assert.strictEqual(AF.modelLabel(AF.pickModel(MODELS, 'quad', 'FF600 同進')), 'FF600 同進照片磚');
  const noGeo = Object.assign({}, MODELS[0], { geo: null });
  assert.strictEqual(AF.reachAt(9999, noGeo), noGeo.bedD / 2, '認不得幾何的機型不該收');
});
check('擋住時講原因（文案照原型 capReason）', () => {
  const pk = AF.pickModel(MODELS, 'quad', 'FF600 同進');
  assert.strictEqual(AF.capReason(551, 300, 20, pk, 'reach'), 'FF600 最寬 550 mm（要留洗料塔的位置）');
  assert.strictEqual(AF.capReason(300, 581, 39, pk, 'reach'), 'FF600 最高 580 mm（機型可印高度）');
  assert.strictEqual(AF.capReason(500, 580, 39, pk, 'reach'), '磚這麼高時，噴頭搆不到盤邊（機台越往上、搆得到的圈越小）');
  assert.strictEqual(AF.capReason(300, 580, 39, pk, 'now'), '磚這麼高時，旁邊的洗料塔印不到頂');
});

console.log('\n(c) 貼回後四項檢查＋一項請人看（只提示、不擋）');
const DEW = LIB.acceptance.deltaE.warn;
const C = o => AF.checkList(Object.assign({ same: false, locked: true, aspI: 1.5, aspT: 1.5, deltaE: 5, warnDE: DEW, w0: 1536, h0: 1024 }, o));
const st = (list, k) => (list.find(c => c.key === k) || {}).state;
check('門檻：比例 |ln| > 0.03、色差 ≥ 款式庫 acceptance.deltaE.warn（12）、長邊 < 900、貼成原圖（64×64 亮度格差 > 12 的不到 5%）', () => {
  assert.strictEqual(DEW, 12, '款式庫的顏色門檻不是 12（R9-11 Q4）');
  assert.strictEqual(st(C({ aspI: 1.5 * Math.exp(0.0299) }), 'shape'), 'ok');
  assert.strictEqual(st(C({ aspI: 1.5 * Math.exp(0.0301) }), 'shape'), 'warn');
  assert.strictEqual(st(C({ aspI: 1.5 / Math.exp(0.0301) }), 'shape'), 'warn');
  assert.strictEqual(st(C({ deltaE: 11.99 }), 'color'), 'ok');
  assert.strictEqual(st(C({ deltaE: 12 }), 'color'), 'warn');
  assert.strictEqual(st(C({ deltaE: null }), 'color'), undefined, '還沒有模擬色差時不該出現顏色那一項');
  assert.strictEqual(st(C({ w0: 899, h0: 600 }), 'small'), 'warn');
  assert.strictEqual(st(C({ w0: 600, h0: 900 }), 'small'), 'ok');
  assert.strictEqual(AF.SAME_N * AF.SAME_N, 4096);
  const a = new Float32Array(4096).fill(100), b = new Float32Array(4096).fill(100);
  for (let i = 0; i < 204; i++) b[i] = 113;     // 204／4096＝4.98%
  assert.strictEqual(AF.sameAsPhoto(1.5, 1.5, a, b), true);
  b[204] = 113;                                   // 205／4096＝5.005%
  assert.strictEqual(AF.sameAsPhoto(1.5, 1.5, a, b), false);
  const c2 = new Float32Array(4096).fill(112);     // 差剛好 12＝不算不同
  assert.strictEqual(AF.sameAsPhoto(1.5, 1.5, a, c2), true);
  assert.strictEqual(AF.sameAsPhoto(1.5 * Math.exp(0.031), 1.5, a, a), false, '比例不同就一定不是原圖');
});
check('判斷：貼成原圖＝只講這一件；比例鎖解除＝講會變形（不比比例）；看四個角那一項一定在、不算件數', () => {
  assert.deepStrictEqual(C({ same: true }).map(c => c.key), ['same']);
  assert.strictEqual(AF.warnCount(C({ same: true })), 1);
  assert.deepStrictEqual(C({ locked: false, aspI: 1 }).map(c => c.key), ['lock', 'color', 'small', 'eye']);
  const all = C({}); assert.deepStrictEqual(all.map(c => c.key), ['shape', 'color', 'small', 'eye']);
  assert.strictEqual(AF.warnCount(all), 0, '全過');
  assert.strictEqual(AF.warnCount(C({ aspI: 1, deltaE: 30, w0: 500, h0: 500 })), 3);
  assert.strictEqual(st(all, 'eye'), 'eye');
});
check('字樣：AI 給的形狀（|ln| < 0.05 叫正方形）', () => {
  assert.strictEqual(AF.aiShapeLabel(1.5), '橫式 3:2'); assert.strictEqual(AF.aiShapeLabel(1), '正方形');
  assert.strictEqual(AF.aiShapeLabel(Math.exp(0.049)), '正方形'); assert.strictEqual(AF.aiShapeLabel(0.75), '直式 3:4');
  assert.strictEqual(AF.aiShapeLabel(687 / 1024), '直式 2:3', 'Gemini 實測回 687×1024');
});
check('畫面文案照原型（結論兩句、四項檢查、換個說法那一段）', () => {
  for (const t of ['✓ 可以印——看中間的列印模擬，滿意就按右上〔產生並載入列印板〕。', '件事建議看一下（不擋你：照樣可以按右上〔產生並載入列印板〕）。',
    '<b>這張跟原圖幾乎一樣</b>——是不是貼到原本的照片了？AI 產的圖要到 AI 那邊的圖上按右鍵「複製圖片」再貼回來。',
    '<b>顏色：AI 用了這組料印不出來的顏色</b>（模擬色差 ', '複製「只用這組料的顏色」的說法', '：細節會糊。到 AI 那邊下載原圖（不要截縮圖）再貼回來。',
    '請看一眼圖的四個角：有 AI 工具的小標誌（浮水印），它會被印出來——換一張或換一個工具。', '自己看了不滿意？換個說法再產一次 ',
    '（跟圖一樣，不變形、不裁掉東西）。', '<b>回來了？</b>把剛剛複製的圖貼上。', '這個畫面不讓按鈕直接讀剪貼簿——請直接按 Ctrl+V，或按〔選擇檔案…〕。'])
    assert(afSrc.includes(t), 'aiflow.js 少了原型那句：' + t);
});

console.log('\n(d) 提示詞語言（繁中、簡中→中文；其他→英文；回覆照介面語言）');
check('zh_TW／zh_CN／zh_HK／en／en_US／ja／de／de_DE／pt_BR／空／亂碼', () => {
  const L = c => { const r = AF.langInfo(c, AFC); return r.prompt + '/' + r.reply; };
  assert.strictEqual(L('zh_TW'), 'zh/繁體中文'); assert.strictEqual(L('zh_CN'), 'zh/簡體中文'); assert.strictEqual(L('zh_HK'), 'zh/繁體中文');
  assert.strictEqual(L('en'), 'en/English'); assert.strictEqual(L('en_US'), 'en/English'); assert.strictEqual(L('ja'), 'en/Japanese');
  assert.strictEqual(L('de'), 'en/German'); assert.strictEqual(L('de_DE'), 'en/German'); assert.strictEqual(L('pt_BR'), 'en/Brazilian Portuguese');
  assert.strictEqual(L(''), 'en/English', '舊宿主沒送＝英文（規格 Q7「缺就英文」）'); assert.strictEqual(L(undefined), 'en/English'); assert.strictEqual(L('xx_YY'), 'en/English');
});
check('App 的 21 種介面語言（resources/i18n）都接得住、不會丟例外（原型只認 4 種）', () => {
  const langs = fs.readdirSync(path.join(ROOT, 'resources', 'i18n')).filter(d => fs.statSync(path.join(ROOT, 'resources', 'i18n', d)).isDirectory());
  assert(langs.length >= 21, 'i18n 只有 ' + langs.length + ' 種');
  for (const l of langs) {
    const r = AF.langInfo(l, AFC);
    assert(r.reply && typeof r.reply === 'string', l + ' 沒有回覆語言');
    assert.strictEqual(r.prompt, /^zh/.test(l) ? 'zh' : 'en', l);
    assert(r.reply !== 'English' || /^en/.test(l), l + ' 落成 English（回覆語言沒照介面語言）');
  }
});

console.log('\n(e) 我的款式');
check('不能跟內建款式同名（原型只比自己做的）、不能跟自己做的別款同名；改自己那一款不算撞', () => {
  const names = LIB.styles.map(s => s.name), mine = [{ id: 'my-a', name: '我的木刻' }];
  assert.strictEqual(AF.validateMine({ id: null, name: '模板風', desc: 'x' }, mine, names), '「模板風」這個名字已經有一款了，換個名字。');
  assert.strictEqual(AF.validateMine({ id: null, name: ' 我的木刻 ', desc: 'x' }, mine, names), '「我的木刻」這個名字已經有一款了，換個名字。');
  assert.strictEqual(AF.validateMine({ id: 'my-a', name: '我的木刻', desc: 'x' }, mine, names), '');
  assert.strictEqual(AF.validateMine({ id: null, name: ' ', desc: '' }, mine, names), '請先補上：名字、畫風描述。');
});
check('從內建款式起頭：{minPx}／{tones} 存成佔位字（原型存成當下的數字），產生提示詞時才換成當下的值', () => {
  const base = LIB.styles.find(s => s.id === 'ridge_layers');
  const d = AF.mineDraftFrom(base, 'zh');
  assert(d.desc.includes('{minPx}') && d.desc.includes('{tones}'), '佔位字被換掉了');
  assert.strictEqual(d.name, '我的' + base.name);
  const rec = AF.mineRecord(Object.assign({}, d, { name: '我的山' }), 'dual', 'zh', new Date('2026-10-06T08:00:00Z'));
  assert(rec.desc.includes('{minPx}') && /^my-[a-z0-9]+$/.test(rec.id) && rec.created === '2026-10-06' && rec.mode === 'dual');
  const pr = AF.buildPrompt({ lib: LIB, style: rec, mine: true, lang: 'zh', reply: '繁體中文', aspect: 1.5, tileW: 100, xMinMm: 1, zMinMm: 0.2, hexes: ['#EDEBE6', '#3B3C3E'], tones: 5, enPalette: enLine });
  assert(!/\{minPx\}|\{tones\}/.test(pr), '提示詞裡還留著佔位字');
  assert(pr.includes(AF.fillT(AFC.customRules.zh, { minPx: 15, tones: 5 })), '我的款式沒接可印性規則');
  assert(!pr.includes(LIB.constants.toneRulesZh), '我的款式不該接品味規則（R9-11 Q10）');
  const pr200 = AF.buildPrompt({ lib: LIB, style: rec, mine: true, lang: 'zh', reply: '繁體中文', aspect: 1.5, tileW: 200, xMinMm: 1, zMinMm: 0.2, hexes: ['#EDEBE6', '#3B3C3E'], tones: 5, enPalette: enLine });
  assert.notStrictEqual(pr, pr200, '改尺寸之後描述裡的最細線寬沒跟著變');
});
check('存檔格式來回一致；壞掉的紀錄丟掉、不讓整份讀不進來', () => {
  const list = [AF.mineRecord({ name: 'A', tones: 3, desc: 'd {tones}', base: 'silhouette' }, 'dual', 'en', new Date(1)),
    AF.mineRecord({ name: 'B', tones: 99, desc: 'e', base: 'flat_warm' }, 'quad', 'zh', new Date(2))];
  assert.strictEqual(list[1].tones, 8, '色階數沒夾在 2～8');
  const back = AF.parseMine(AF.serializeMine(list));
  assert(back.ok); assert.deepStrictEqual(back.list, list);
  const bad = AF.parseMine(JSON.stringify({ styles: [list[0], { id: 'x', name: '' }, { id: 'my-z', name: 'z', desc: 'q', mode: 'tri', tones: 3 }] }));
  assert(bad.ok && bad.list.length === 1 && bad.dropped === 2);
  assert.strictEqual(AF.parseMine('{壞').ok, false);
  const u8 = AF.utf8Bytes('我的款式 ✓'); assert.strictEqual(new TextDecoder().decode(AF.bytesFromB64(AF.b64Bytes(u8))), '我的款式 ✓');
});

console.log('\n(f) 金鑰衛生：網頁層不得出現金鑰識別字（規則讀自 python 閘門本身）');
const gate = fs.readFileSync(path.join(__dirname, 'verify_ai_key_hygiene.py'), 'utf8');
const C3 = (gate.match(/#\s*C3[^\n]*\n[\s\S]*?for pat in \(([^)]*)\)/) || [])[1];
const C3PATS = C3 ? C3.split(',').map(s => s.trim().replace(/^["']|["']$/g, '')).filter(Boolean) : [];
function webFiles(dir) { return fs.readdirSync(dir, { withFileTypes: true }).flatMap(d => d.isDirectory() ? webFiles(path.join(dir, d.name)) : /\.(js|html|css|json)$/.test(d.name) ? [path.join(dir, d.name)] : []); }
check('resources/web 全部檔案都沒有閘門 C3 那幾個識別字（含 aiflow.js／aiflow.css）', () => {
  assert(C3PATS.length >= 4 && C3PATS.includes('apiKey'), '從閘門讀不到 C3 的識別字清單：' + C3PATS);
  const hits = [];
  for (const f of webFiles(path.join(ROOT, 'resources', 'web'))) { const t = fs.readFileSync(f, 'utf8'); for (const p of C3PATS) if (t.includes(p)) hits.push(path.relative(ROOT, f) + '：' + p); }
  assert.deepStrictEqual(hits, []);
  assert(C3PATS.some(p => ('var ' + 'apiKey' + '=1').includes(p)), '陽性對照：掃描抓不到注入的識別字');
});

console.log('\n(g) 接線（index.html／C++ 協定）');
const gui = fs.readFileSync(path.join(ROOT, 'src', 'slic3r', 'GUI', 'GUI_App.cpp'), 'utf8');
check('aiflow.js 在 size_ratio／matflow／stylelib 之後、主程式之前載；aiflow.css 有掛；三支都帶 ?v=', () => {
  const at = n => idx.search(new RegExp('<script src="' + n.replace('.', '\\.') + '\\?v=[\\w.-]+"></script>'));
  const a = at('aiflow.js'); assert(a > 0, 'aiflow.js 沒載或沒帶版本字串');
  for (const n of ['size_ratio.js', 'matflow.js', 'stylelib.js', 'engine.js']) assert(at(n) > 0 && at(n) < a, n + ' 要在 aiflow.js 之前');
  assert(a < idx.search(/<script>\s*\r?\n/), 'aiflow.js 要在主程式之前');
  assert(/<link rel="stylesheet" href="aiflow\.css\?v=[\w.-]+">/.test(idx));
  assert(!/stylelib\.js\?v=20261006"/.test(idx), '款式庫改了（加中文提示詞等），版本字串沒動');
});
check('🔴 舊 AI 流程拿掉：確認窗沒有 AI 分支、沒有單價、卡片沒有「需要先設定金鑰」、頁面沒有金鑰引導（只剩開發者模式那一行）', () => {
  const i0 = idx.indexOf('function ptConfirmApply('), body = idx.slice(i0, idx.indexOf('\nfunction ', i0 + 10));
  assert(i0 > 0 && !/requiresAI|NT\$|cost|cfmCost/.test(body), 'ptConfirmApply 還有 AI 分支／費用');
  assert(!/\.cfmCost\b/.test(idx), '.cfmCost 樣式還在');
  for (const t of ['設定 AI 生圖服務金鑰', '需要先設定 AI 生圖金鑰', "AI 生圖 '+ptAiCostText", 'ptAiPending', "kind:'ai'"]) assert(!idx.includes(t), '還在：' + t);
  assert(idx.includes('用你自己的 AI（ChatGPT 等）產圖，不用金鑰。臉佔畫面越小，AI 越可能自己補五官＝印出來不是本人；臉太小請先裁切再上傳。'), 'AI 卡片文案（R9-7 的理由要留）');
  assert(/if\(s\.requiresAI && u\.kind!=='quad' && window\.PhotoTileAiFlow\)\{ PhotoTileAiFlow\.open\(s\.id\); return; \}/.test(idx), '點 AI 卡沒有直接打開三步');
  assert(/devAvail:\(\)=>ptAiAvail, devGenerate:ptAiGenerate, aiCost:ptAiCostText/.test(idx), '開發者模式那一行不是看「這台有沒有存金鑰」');
  assert(/return pg\.devAvail\(\) \?/.test(afSrc) && afSrc.includes('（內部驗提示詞用；客戶看不到這一行）'));
});
check('🔴 金鑰直連的提示詞走 aiflow（跟客戶複製的同一份、只少前言）；沒選料不產圖', () => {
  const i0 = idx.indexOf('function ptAiGenerate(s, tones){'), body = idx.slice(i0, idx.indexOf('\n}', i0));
  assert(/const prompt=PhotoTileAiFlow\.promptFor\(s, false\);/.test(body), 'ptAiGenerate 沒走 aiflow 的組法');
  assert(/const size=PhotoTileAiFlow\.apiSize\(\);/.test(body) && !/function ptAiSize|function ptAiMinPx/.test(idx), '生圖尺寸跟提示詞裡寫的形狀不是同一個來源');
  assert(/if\(!calibOwnsSlots\(\)\)\{/.test(body), '沒選料也照樣產圖（R6-15：先有顏色）');
  assert(/enPalette:hx=>PhotoTileMatFlow\.aiPaletteLine\(\{colors:hx\}\)/.test(idx), '英文料色那一行不是 matflow 那一份');
  assert(/colors:md=>\{ const i=ptMatInfo\(md\); return i \? i\.colors : null; \}/.test(idx), '料色不是第 1 步選的那組');
});
check('🔴 收圖（貼回與金鑰直連同一條 ptAiAccept）：排壓平在 simulate() 之前、比例鎖住時磚跟著圖改高度（不拉伸）、不改料色', () => {
  const i0 = idx.indexOf('function ptAiAccept(bmp, s, why){'), body = idx.slice(i0, idx.indexOf('\n}', i0)).replace(/\/\*[\s\S]*?\*\//g, '');
  assert(i0 > 0, '沒有 ptAiAccept');
  const iPend = body.indexOf('ptStylePending={styleId:s.id, tones, useCurrent:true}'), iSim = body.indexOf('simulate();');
  assert(iPend > 0 && iSim > iPend, '壓平沒排在 simulate() 之前');
  const iAsp = body.indexOf('imgAspect=bmp.height/bmp.width;'), iSize = body.indexOf("applySizeChange('width', params.width, false, why||'');"), iGrid = body.indexOf('srcBitmap=bmp; srcShow=bmp; buildGrid();');
  assert(iAsp > 0 && iSize > iAsp && iGrid > iSize, '比例沒跟著圖（或在 buildGrid 之後才改）＝拉伸');
  assert(!/slots\[[^\]]*\](\.color)?\s*=[^=]/.test(body), '收圖時改了料槽（R6-16）');
  const a0 = idx.indexOf('async aiEnd(m){'), ae = idx.slice(a0, idx.indexOf('aiError(jobId', a0));
  assert(/PhotoTileAiFlow\.onKeyResult\(bmp, blob, st\)/.test(ae) && !/srcBitmap=bmp/.test(ae), '金鑰直連沒走同一條（還在自己換來源）');
});
check('貼上分流：輸入欄裡＝一般貼上（先判斷）、AI 產圖畫面＝貼回、其他＝換新照片；拖放同一條', () => {
  const i0 = idx.indexOf("document.addEventListener('paste',e=>{"), body = idx.slice(i0, idx.indexOf('\n});', i0));
  const iEdit = body.indexOf("t.tagName==='TEXTAREA'"), iAi = body.indexOf('PhotoTileAiFlow.onPaste(e)'), iLoad = body.indexOf('loadFile(');
  assert(iEdit > 0 && iAi > iEdit && iLoad > iAi, '順序不對');
  assert(/t\.tagName==='INPUT' && !\/\^\(checkbox\|radio\|range\|button\|submit\|file\|color\)\$\/\.test\(t\.type\)/.test(body), '數字欄沒算在「一般貼上」');
  assert((idx.match(/ptDropFile\(/g) || []).length >= 3, '拖放沒走同一條分流');
});
check('協定名稱＝C++（刀 3）：指令、回呼、common_openurl 讀最外層 url', () => {
  for (const c of ['phototile_ai_import_', 'phototile_source_origin', 'phototile_mystyles_load', 'phototile_mystyles_save_'])
    assert(afSrc.includes("'" + c), 'aiflow.js 沒送 ' + c);
  for (const c of ['phototile_ai_import_begin', 'phototile_ai_import_chunk', 'phototile_ai_import_end', 'phototile_source_origin', 'phototile_mystyles_load', 'phototile_mystyles_save_begin', 'phototile_mystyles_save_chunk', 'phototile_mystyles_save_end'])
    assert(gui.includes('"' + c + '"'), 'C++ 沒有 ' + c);
  for (const cb of ['aiImported', 'sourceOrigin', 'mystylesLoaded', 'mystylesSaved']) {
    assert(gui.includes('window.PINGPhotoTile.' + cb + '('), 'C++ 不回 ' + cb);
    assert(new RegExp('\\n  ' + cb + '\\(m\\)\\{ if\\(window\\.PhotoTileAiFlow\\)').test(idx), 'index.html 的 PINGPhotoTile 沒接 ' + cb);
  }
  assert(/root\.get_optional<std::string>\("url"\)/.test(gui.slice(gui.indexOf('"common_openurl"'))), 'C++ common_openurl 不是讀最外層 url');
  assert(/command: 'common_openurl', url: GPT_URL/.test(afSrc), '開 ChatGPT 沒照 common_openurl 的形狀送');
  const iOpen = afSrc.indexOf('root.open(GPT_URL'), iWx = afSrc.indexOf("command: 'common_openurl'");
  assert(iWx > 0 && iOpen > iWx, 'window.open 不是只在沒有 App 時才用（App 裡會把整個工作室換掉）');
  assert(/RAW_CHUNK = 96 \* 1024/.test(afSrc), '分塊大小');
});
check('CIS：橘只標「你在這裡」（目前這一步、目前這張）；警示黃；主要行動沿用炭黑 .btn.primary', () => {
  const orange = afCss.split('\n').filter(l => /var\(--orange\)|#EA4E16/i.test(l)).map(l => l.trim().split('{')[0]);
  assert.deepStrictEqual(orange, ['.afStep.cur', '.afStep.cur .n', '.afThumb.cur'], '橘色用在「你在這裡」以外：' + orange);
  assert(/\.afWarnTx\{color:#946310;\}/.test(afCss) && /\.afBanner\{[^}]*color:#946310/.test(afCss));
  assert(!/\.btn\.primary\{/.test(afCss), 'aiflow.css 自己另定主要按鈕');
});

console.log('\n' + (fail ? '❌ ' : '') + pass + ' 過、' + fail + ' 敗');
process.exit(fail ? 1 : 0);
