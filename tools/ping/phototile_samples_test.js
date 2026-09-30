/* 照片磚內建範例圖・漂移守衛（§11 R11-7／R11-9，牌 c-0916-PTI-04）
 *
 * 守的是什麼：範例圖有兩份——正本 resources/images/phototile_samples/（manifest.json ＋ *.jpg）
 * 與頁面實際吃的 resources/web/phototile/samples.js（data URI，因為工作室走 file://、fetch 被 CORS 擋）。
 * 兩份一漂移，畫面上看不出來：縮圖照樣顯示、載入照樣成功，只是使用者拿到的是舊圖。
 *
 * 🔴 為什麼識別字要從程式碼推導而不是寫死（0916 已付費教訓，見款式庫那支）：
 *    上一次守衛自己抄了錯的識別字（PT_STYLE_LIB vs STYLE_LIB），於是「產品會 ReferenceError」
 *    和「守衛說沒事」同時成立。這裡一律以 samples.js 實際宣告的那個名字為準，
 *    並反向檢查 index.html 有沒有用到別的名字去讀它。
 *
 * 跑法：node tools/ping/phototile_samples_test.js
 */
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const SRC = path.join(ROOT, 'resources', 'images', 'phototile_samples');
const WEB = path.join(ROOT, 'resources', 'web', 'phototile');
const SAMPLES_JS = path.join(WEB, 'samples.js');
const INDEX = path.join(WEB, 'index.html');

let pass = 0, fail = 0;
function ok(cond, msg) {
  if (cond) { pass++; console.log('  ✅ ' + msg); }
  else { fail++; console.log('  ❌ ' + msg); }
}

console.log('照片磚內建範例圖・漂移守衛');

// ---- 1. 正本讀得進來 ----
const man = JSON.parse(fs.readFileSync(path.join(SRC, 'manifest.json'), 'utf8'));
ok(Array.isArray(man.items) && man.items.length > 0, `manifest 有 ${man.items.length} 張`);

const dual = man.items.filter(i => i.mode === 'dual');
const quad = man.items.filter(i => i.mode === 'quad');
ok(dual.length === 7, `雙料 7 張（實際 ${dual.length}）——Eric 2026-09-16「全部先上」9 張；實印 100 mm 後不留的拿掉：狼 2026-09-29、孤樹 2026-09-30（開發中清單 #14）`);
ok(quad.length === 5, `四料 5 張（實際 ${quad.length}）`);
ok(dual.every(i => ['人物', '動物', '風景'].includes(i.cat)),
   '雙料每張都落在人物／動物／風景（R11-3 的分類）');
ok(quad.every(i => ['人物', '動物', '風景'].includes(i.cat)), '四料同上');

// R11-9 的代價：兩批範例都要齊，而且每個分類在兩種料數下都要有東西，
// 否則使用者會撞到「這個分類目前沒有…範例」的空狀態。
for (const cat of ['人物', '動物', '風景']) {
  ok(dual.some(i => i.cat === cat) && quad.some(i => i.cat === cat),
     `分類「${cat}」雙料與四料都有範例（R11-9：兩批都要齊）`);
}

// ---- 2. 檔案都在，而且真的是 JPEG ----
let missing = 0, notJpeg = 0;
for (const it of man.items) {
  const p = path.join(SRC, it.file);
  if (!fs.existsSync(p)) { missing++; continue; }
  const head = fs.readFileSync(p).subarray(0, 3);
  if (!(head[0] === 0xFF && head[1] === 0xD8 && head[2] === 0xFF)) notJpeg++;
}
ok(missing === 0, `manifest 列的圖檔都在（缺 ${missing}）`);
ok(notJpeg === 0, `每個檔都是真的 JPEG（不是 ${notJpeg}）`);

// ---- 3. samples.js 的識別字（從程式碼推導，不寫死）----
const js = fs.readFileSync(SAMPLES_JS, 'utf8');
const declMatch = js.match(/^\s*(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*\[/m);
ok(!!declMatch, 'samples.js 宣告了一個陣列');
const IDENT = declMatch ? declMatch[1] : null;
console.log('     （samples.js 宣告的識別字＝' + IDENT + '）');

const html = fs.readFileSync(INDEX, 'utf8');
ok(/<script\s+src="samples\.js"><\/script>/.test(html), 'index.html 有載入 samples.js');
ok(IDENT && html.includes(IDENT), `index.html 用的是 samples.js 真正宣告的那個名字（${IDENT}）`);

/* 反向：有沒有人用「別的名字」去讀範例？（這正是款式庫那次假綠的形狀） */
const otherIdents = new Set();
const re = /\b([A-Za-z_$][\w$]*)\s*\.\s*(?:filter|map|find|length)\b/g;
let m;
while ((m = re.exec(html)) !== null) {
  const name = m[1];
  if (/^PT_SAMPLE/i.test(name) && name !== IDENT) otherIdents.add(name);
}
ok(otherIdents.size === 0,
   `index.html 沒有用別的 PT_SAMPLE* 名字讀範例${otherIdents.size ? '（發現 ' + [...otherIdents].join('/') + '）' : ''}`);

// ---- 4. samples.js 的內容真的等於圖檔（逐張比 base64）----
let drift = [];
for (const it of man.items) {
  const b64 = fs.readFileSync(path.join(SRC, it.file)).toString('base64');
  if (!js.includes(b64)) drift.push(it.file);
}
ok(drift.length === 0,
   `samples.js 的每張圖都逐位元組等於正本${drift.length ? '（漂移：' + drift.join('、') + '）' : ''}`
   + (drift.length ? ' ⇒ 重跑 python tools/ping/gen_phototile_samples.py' : ''));

const srcCount = (js.match(/"src"\s*:/g) || []).length;
ok(srcCount === man.items.length,
   `samples.js 的張數與 manifest 相同（${srcCount} vs ${man.items.length}）——沒有多出孤兒`);

// ---- 5. 刪除確認窗的鈕序（R11-6／ping-ux LAY-15：刪除在左、取消恆在最右）----
const actsMatch = html.match(/<div class="acts">\s*([\s\S]*?)<\/div>/);
const dlgActs = html.match(/id="ptDelYes"[\s\S]{0,200}?id="ptDelNo"/);
ok(!!dlgActs, 'R11-6：確認窗的 DOM 順序是「刪除」在前、「取消」在後（取消恆在最右）');
ok(html.indexOf('id="ptDelMask"') < html.indexOf('<script src="samples.js">'),
   '確認窗的 HTML 排在腳本之前（排在後面的話初始化時抓不到，onclick 會炸——0916 實際踩到）');

// ---- 6. 圖庫沒被整段弄丟 ----
for (const id of ['ptGal', 'ptGalCats', 'ptGrid', 'ptGalMine', 'ptGalBar', 'ptToast']) {
  ok(html.includes(`id="${id}"`), `版面還在：#${id}`);
}
ok(/function ptGalRender\(\)\s*\{\s*\n?\s*if\(!ptGalReady\)/.test(html),
   'ptGalRender 保留就緒守衛（renderSlots 在本段執行前就會先呼叫一次）');

// ---- 7. 範例自帶題材（牌 c-0918-PTS-01；Eric 2026-09-18 裁「點範例不要再問主角」）----
// 題材 id 從 index.html 的款式庫推導（不寫死——同第 3 段的教訓）；分類與題材要對得上，
// 否則會出現「動物分類的範例被套成人像特寫」這種看起來正常、其實推薦錯款式的狀態。
const libM = html.match(/<script id="ptStyleLib" type="application\/json">([\s\S]*?)<\/script>/);
const LIB = libM ? JSON.parse(libM[1]) : null;
const SUBJ = LIB ? new Set(LIB.subjects.map(s => s.id)) : new Set();
ok(SUBJ.size > 0, `款式庫題材 id 讀得到（${[...SUBJ].join('／')}）`);
const noSubj = man.items.filter(i => !SUBJ.has(i.subject)).map(i => i.file);
ok(noSubj.length === 0, `每張範例都帶款式庫裡真的有的題材${noSubj.length ? '（缺／錯：' + noSubj.join('、') + '）' : ''}`);
const CAT_OK = { '人物': ['portrait_closeup', 'person_full'], '動物': ['pet_short', 'pet_long'], '風景': ['landscape'] };
const catBad = man.items.filter(i => !(CAT_OK[i.cat] || []).includes(i.subject)).map(i => `${i.name}(${i.cat}→${i.subject})`);
ok(catBad.length === 0, `分類與題材對得上${catBad.length ? '（' + catBad.join('、') + '）' : ''}`);
const kitten = man.items.find(i => i.file === 'a_kitten.jpg');
ok(!!kitten && kitten.subject === 'pet_long', '幼貓＝寵物・長毛低對比（Eric 2026-09-18 裁）');
const jsLines = js.split(/\r?\n/);
const subjDrift = man.items.filter(i => {
  const line = jsLines.find(l => l.includes(`"file": "${i.file}"`));
  return !line || !line.includes(`"subject": "${i.subject}"`);
}).map(i => i.file);
ok(subjDrift.length === 0, `samples.js 的題材與 manifest 一致${subjDrift.length ? '（漂移：' + subjDrift.join('、') + '）⇒ 重跑產生器' : ''}`);
ok(/ptPresetSubject=\(!it\.mine && it\.subject/.test(html),
   '點內建範例時把題材帶進載入流程（「我的圖」不帶＝照舊問）');
ok(/if\(preset\) applySubject\(preset\);\s*else if\(typeof ptAskSubject==='function'\) ptAskSubject\(\)\.then\(applySubject\);/.test(html),
   '載入時：有範例題材就直接套、不跳「主角是？」；沒有才問');

console.log(`\n=== 結果：${pass} 過／${fail} 失敗 ===`);
process.exit(fail ? 1 : 0);
