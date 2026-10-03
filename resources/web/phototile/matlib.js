/* =====================================================================
   PhotoTileMatLib — 照片磚「材料庫」（規格 R6-14，Eric 2026-09-22 裁「照建議」）

   要解決的事：校正表原本**只有一格**（鍵 `ping_phototile_calib_dual_v1`），再匯入一張就蓋掉舊的，
   而且**不認材料身分、只比兩個 hex**（`engine.js` 的 `calibMatches()`）⇒ 使用者沒辦法「選一支已經
   校正過的料」，換料就等於重印校正片。本檔把它升成「一支料校一次就記著、之後用選的」。

   四條定死的規則（都是 R6-14 的四個子題，Eric 已裁，不要在這裡重開）：
     ① 🆕 **身分＝料種＋顏色名**（T061，牌 c-0924-ACC-27；R6-14 子題 1 附款＝Eric 2026-09-23 兩輪裁示＋看原型「原型 OK」）。
        料種＝照片磚機印得出來的料種（App 從切片參數給，今天只有 PLA）；顏色名＝人給的名字，必填、任取、**不擋**
        （打的跟庫裡一字不差＝同一支料，相近的只提示——Eric：「如果我沒有選擇它的話，那它就是一隻新的材料」）。
        存料種、不綁某一支線材參數：同一捲料在 FD 與 FF 照片磚機上的 filament_id 不同（發包單〈九〉9-3）。
        🔴 下面這一段（filament_id＋標籤）是 schema 1 的舊規則，**原文保留供追溯**；schema 1 的料讀進來＝「還沒指定料種」
           （`needsSpec`），第一次開新版時問一次（R6-14 子題 2 附款的附款，取代原本的「認領」）。
        （舊）**身分＝`filament_id` ＋ 使用者自填標籤**（子題 1 裁 C）。
        `filament_id`（例 `GPINGABS`）改名不會變——本專案改過多次料名（0725 ABS 三支併一、0729 PLA-210、
        0817 3in1），而 `find_preset()` 不走 `renamed_from`，0725 T004 就因此讓整條連動靜默失效
        ⇒ **不能拿名稱當鍵**。但一個 preset ≠ 一捲料（同一支「PING PLA - 210」可能有白／黑／深灰三捲）
        ⇒ 再加一個使用者自填標籤才分得開。
        🔴 **量到的 hex 只當顯示與防呆，不當鍵**（同一支料不同批、不同機況量到的值會不一樣）。
     ② **庫的儲存單位是 pair（組合），料是 pair 的兩個端點**（子題 3 裁 B）。
        理由：量到的東西本身就是 pair 的資料；`R6-4` 定的失效單位也是 pair；四料 48 格＝6 pairs；
        S=1／S=0 那兩端本來就是單支料的實印色 ⇒ **存 pair 等於免費得到料**。
        反過來存料就得用模型回推 pair——那正是色彩校正要消滅的東西。
     ③ **換料的失效範圍＝只失效「含該支」的 pair**（`R6-4`）：換掉 B ⇒ A/B、B/C、B/D 失效，
        A/C、A/D、C/D **保留、不重測**。
     ④ **遷移不可要求使用者重印**（子題 2 附款）：舊鍵那一格要能被帶進來；舊表沒有 `filament_id`
        ⇒ 進來時 `fid:null`、`source:'legacy-hex'`。**舊鍵不刪**（rollback 用），庫存在之後就不再寫它。
        🆕 T061：「第一次在產圖流程選到時認領」改成「第一次開新版時問一次料種＋顏色名」（子題 2 附款的附款）；
        問過就記在庫的 `legacyAskedAt`，跳過的料清單上標「未指定料種」、旁邊「指定…」隨時補。

   🆕 **schema 2**（T061）：①料多一欄 `type`（料種；缺＝舊資料、待指定）②pair 多 `prev`＝被取代的上一份量測
      （同一組重新量、或兩張舊表指到同一組時，新的當目前、舊的留一份——〈材料庫管理〉Q9；還原的畫面在 T062）
      ③庫多 `legacyAskedAt`。**讀得進 1 與 2、寫一律 2** ⇒ T060 以前的舊版讀到 2 會明講「版本不認得」而**不蓋檔**
      （index.html matlibOnHostLoaded 那道）——比「欄位被舊版默默丟掉」好：那會讓料種指定靜默消失。
   🆕 **T062 那一列（〈九〉9-6 分車表「T062」，牌 c-0924-ACC-30）：schema 不抬、仍是 2**——庫多一欄**選填**的 `archived`（封存名單）。
      為什麼不抬（照 9-7 #4 同一把尺比代價）：T061／T062 讀到這份檔會**忽略** `archived`、其餘照讀 ⇒ 代價只是
      「在舊版裡，封存的料看得到；在舊版裡存過一次庫，封存名單會被洗掉、回新版要再封存一次」，**量測一筆都不會少**。
      抬到 3 的代價是舊版**整份讀不到**（明講「版本不認得」、不蓋檔）＝退回舊版時所有校正過的料都選不到。
      `type` 當年要抬，是因為丟掉它＝料種指定靜默消失（身分跟著變）；封存名單丟了只是「藏起來的又看得到」。

   🔴 **本檔不做色彩數學**。pts 只存「量到的 hex ＋ 那一格的配方 S」，要餵給引擎時用
   `toCalibTable()` 吐回**校正表原本的形狀**（料數／料／量測），讓 `engine.js` 的 `calibParseTable()`
   自己去算 lin——色彩數學只有引擎那一份，這裡不寫第二把尺（同 `dualLadder` 收成一份的理由）。

   儲存轉接層：頁面本來就跑在兩種環境——產品是 WebView（庫要落 `data_dir`，段 B 的 C++ 通道）、
   開發時是一般瀏覽器（`localStorage`）。**這不是預留後路**，既有碼已經是這個形狀
   （`calibTable.persisted` 存不進去時畫面誠實標示）。🔴 存不進去時照樣講出來，不靜默。
   🆕 段 B（車 2，牌 `c-0923-ACC-21`）：產品路徑改落 **App 設定資料夾**（`hostStorage()`，見下方〈庫的家〉一節）；
      瀏覽器直開（開發）仍用 localStorage；兩種都存不進去才是 none（照樣講出來）。
   ===================================================================== */
(function (root, factory) {
  const api = factory(root);
  root.PhotoTileMatLib = api;
  if (typeof module === "object" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function (root) {
"use strict";

const SCHEMA = 2;                 // 🆕 T061：寫一律 2（見檔頭〈schema 2〉）
const SCHEMAS_READ = [1, 2];      // 讀得進的版本：1＝T058～T060 存的（料還沒有料種＝待指定）
const LIB_KEY = 'ping_phototile_matlib_v1';
const LEGACY_KEY = 'ping_phototile_calib_dual_v1';   /* 🔴 只讀不刪：rollback 的唯一退路 */

/* 兩個分隔字元都是控制碼 ⇒ 不可能出現在 filament_id 或使用者填的標籤裡，
   也就不會有「標籤裡剛好有分隔字元 ⇒ 兩支不同的料算成同一支」這種靜默撞鍵。 */
const SEP_FIELD = '';   // 料的 fid 與 label 之間
const SEP_PAIR  = '';   // pair 的兩個端點之間

const KINDS = { DUAL: '雙料 8 階直立條', QUAD: '四料 48 格' };

function isHex(h){ return typeof h === 'string' && /^#[0-9A-Fa-f]{6}$/.test(h); }
function upHex(h){ return isHex(h) ? h.toUpperCase() : null; }

/* ---- 材料身分 ------------------------------------------------------- */
/* 🔴 hex 不進 key（R6-14 子題 1）。
   🆕 T061：有料種的料＝`SEP_TYPE＋料種＋SEP_FIELD＋顏色名`；舊資料（沒有料種）照 schema 1 的 `fid＋SEP_FIELD＋標籤`
   ⇒ 舊資料的 id 一個都不變（瀏覽器儲存區那份、舊鍵那一格才認得出「是同一筆」），而兩種鍵開頭不同、永遠不會撞。
   舊資料的 fid 缺席（＝舊表遷進來的）時 key 只剩標籤，而遷移時標籤預設帶原 hex ⇒ 那種格子仍然彼此分得開，
   但要指定料種＋顏色名之後才有真正的身分。 */
const SEP_TYPE = '\u001d';
function matKey(m){
  if (!m) return '';
  const label = m.label == null ? '' : String(m.label);
  if (m.type) return SEP_TYPE + String(m.type) + SEP_FIELD + label;
  const fid = m.fid == null ? '' : String(m.fid);
  return fid + SEP_FIELD + label;
}
function makeMaterial(m){
  if (!m) throw new Error('材料不可為空');
  const hex = upHex(m.hex);
  const type = (m.type == null ? '' : String(m.type).trim()) || null;
  if (type){
    /* 顏色名＝人給的，必填（〈九〉Q8）；前後空白不算（「白 」與「白」是同一個名字）。 */
    const label = m.label == null ? '' : String(m.label).trim();
    if (!label) throw new Error('指定了料種就要有顏色名');
    return { type, fid: null, label, hex };
  }
  const fid = (m.fid == null || m.fid === '') ? null : String(m.fid);
  const label = (m.label == null || m.label === '') ? (hex || '') : String(m.label);
  if (!fid && !label) throw new Error('材料至少要有 filament_id 或標籤其中之一');
  return { type: null, fid, label, hex };
}
/* 還沒指定料種的料（schema 1 的舊資料）＝要問一次料種＋顏色名（R6-14 子題 2 附款的附款）。 */
function needsSpec(m){ return !!m && !m.type; }
/* pair 的 id ＝ 兩端 key 排序後 join ⇒ **與順序無關**（R6-4 的失效單位就是 pair，
   「A 配 B」與「B 配 A」是同一筆資料，不該在庫裡變成兩筆）。 */
function pairId(a, b){
  const ka = matKey(a), kb = matKey(b);
  return (ka <= kb ? ka + SEP_PAIR + kb : kb + SEP_PAIR + ka);
}

/* ---- 庫本體 --------------------------------------------------------- */
function emptyLib(){ return { schema: SCHEMA, pairs: [], legacyAskedAt: null, archived: [] }; }

/* 一份量測（pts＋何時量的＋種類＋出身）。pair 本身就是「目前那一份」；prev 是被取代的上一份（schema 2）。 */
function normPts(arr){
  const pts = Array.isArray(arr) ? arr.filter(pt =>
    pt && typeof pt.S === 'number' && isFinite(pt.S) && isHex(pt.hex)
  ).map(pt => ({ S: pt.S, hex: pt.hex.toUpperCase() })) : [];
  return pts.sort((x, y) => y.S - x.S);                // S 由大到小＝a 多 → 少（與引擎 calibParseTable 同序）
}
function normVersion(v){
  if (!v || typeof v !== 'object') return null;
  const pts = normPts(v.pts);
  if (pts.length < 2) return null;
  return { pts, measuredAt: typeof v.measuredAt === 'string' ? v.measuredAt : null,
           kind: typeof v.kind === 'string' ? v.kind : '',
           source: v.source === 'legacy-hex' ? 'legacy-hex' : 'readback' };
}

/* 載進來的東西是使用者磁碟上的 JSON，可能被手改壞 ⇒ 一律過這支：壞的丟掉、好的留下，
   **不整份作廢**（整份作廢＝使用者失去所有校正資料，代價遠大於留下幾筆可疑的）。 */
function normalize(obj){
  const lib = emptyLib();
  if (!obj || typeof obj !== 'object') return lib;
  if (SCHEMAS_READ.indexOf(obj.schema) < 0) return lib;   // 版本不認得＝當空庫（不猜格式）
  if (typeof obj.legacyAskedAt === 'string') lib.legacyAskedAt = obj.legacyAskedAt;
  const seen = new Set();
  const src = Array.isArray(obj.pairs) ? obj.pairs : [];
  for (const p of src){
    if (!p || typeof p !== 'object') continue;
    if (!p.a || !p.b) continue;
    let a, b;
    try { a = makeMaterial(p.a); b = makeMaterial(p.b); } catch (e) { continue; }
    const v = normVersion(p);
    if (!v) continue;                                  // 少於 2 點內插不出曲線＝這筆沒有用
    const id = pairId(a, b);
    if (a.type && matKey(a) === matKey(b)) continue;   // 同一支料跟自己一組＝不成立（指定料種時就擋，這裡是防手改壞的檔）
    if (seen.has(id)) continue;                        // 同 id 只留第一筆（upsert 本來就不該產生重複）
    seen.add(id);
    lib.pairs.push({
      id, a, b, kind: v.kind, pts: v.pts, measuredAt: v.measuredAt, source: v.source,
      claimedAt: typeof p.claimedAt === 'string' ? p.claimedAt : null,   // schema 1「認領」留下的紀錄（只保留，不再用）
      prev: normVersion(p.prev),
    });
  }
  /* 🆕 T062：封存名單（身分，不含色）。庫裡已經沒有這支料（沒有任何一組量測）的就不留——名單只是「藏哪幾支」。 */
  const ends = new Set();
  lib.pairs.forEach(p => { ends.add(matKey(p.a)); ends.add(matKey(p.b)); });
  for (const x of Array.isArray(obj.archived) ? obj.archived : []){
    let m;
    try { m = makeMaterial(Object.assign({}, x, { hex: null })); } catch (e) { continue; }
    const k = matKey(m);
    if (ends.has(k) && !lib.archived.some(y => matKey(y) === k)) lib.archived.push(m);
  }
  return lib;
}

function getPair(lib, id){
  if (!lib || !Array.isArray(lib.pairs)) return null;
  return lib.pairs.find(p => p.id === id) || null;
}
function findPair(lib, a, b){ return getPair(lib, pairId(a, b)); }

/* 同一對重量測 ⇒ **取代不新增**（原位取代，庫的順序穩定＝畫面上的清單不會跳動）。
   🆕 schema 2（〈材料庫管理〉Q9 的資料那一半）：量測內容不一樣＝新的取代舊的、**舊的留一份當 prev**
   （產圖一律用新的；還原的畫面在 T062）。內容一樣＝同一次量測，只換中繼資料、prev 照舊。
   進來的那筆自己帶了 prev（例：併另一份庫）就用它的。 */
function upsertPair(lib, pair){
  const norm = normalize({ schema: SCHEMA, pairs: [pair] });
  if (!norm.pairs.length) throw new Error('這筆 pair 不合法（料不完整，或量測點少於 2）');
  const p = norm.pairs[0];
  const i = lib.pairs.findIndex(x => x.id === p.id);
  if (i < 0){ lib.pairs.push(p); return p; }
  const old = lib.pairs[i];
  if (!p.prev) p.prev = ptsSig(old) !== ptsSig(p) ? normVersion(old) : (old.prev || null);
  lib.pairs[i] = p;
  return p;
}

/* 料的清單**從 pair 的端點推出來**，不另存一份（R6-14 子題 3 的理由：存 pair 免費得到料）。 */
/* 🆕 一支料的色取哪一組的（Eric 2026-10-03 實踩後裁「照建議」，牌 c-1003-PT-01）：同一支料在不同組裡量到的色不一樣
   （身分不含色，R6-14 子題 1）。以前一律取「庫裡第一次出現這支料的那一組」——那一組可能是取錯名的舊表、也可能已經封存，
   料單上的色塊就跟實際在用的對不起來（實錄：「深灰」第一次出現在沒套白平衡的舊表 ⇒ 匯入 9/14 那張、把舊那組封存之後，
   色塊還是 #AFAFAA，產圖用的卻是 #707279）。改成照這個順序取：①opt.prefer 指名的那幾組（＝生效中的那組）②沒封存的組
   ③其餘；同一級取最近量的（沒記日期＝最舊），再同取庫裡較後面的（較晚收進來的）。**清單的順序不變**（仍照第一次出現的順序，
   畫面不跳動）。opt.prefer＝Set（pair id）或函式 pair → bool。 */
function listMaterials(lib, opt){
  const out = [], at = new Map(), best = [];
  if (!lib || !Array.isArray(lib.pairs)) return out;
  const ak = archivedKeys(lib), pf = opt && opt.prefer;
  const pref = !pf ? () => false : (typeof pf === 'function' ? pf : p => pf.has(p.id));
  lib.pairs.forEach((p, n) => {
    const rank = pref(p) ? 0 : (ak.has(matKey(p.a)) || ak.has(matKey(p.b)) ? 2 : 1);
    for (const m of [p.a, p.b]){
      const k = matKey(m), cand = { rank, when: p.measuredAt || '', n };
      const i = at.get(k);
      if (i == null){ at.set(k, out.length); best.push(cand); out.push({ key: k, type: m.type || null, fid: m.fid, label: m.label, hex: m.hex }); continue; }
      const cur = best[i];
      if (cand.rank < cur.rank || (cand.rank === cur.rank && (cand.when > cur.when || (cand.when === cur.when && cand.n > cur.n)))){
        out[i].hex = m.hex; best[i] = cand;
      }
    }
  });
  return out;
}
/* 這支料配得到哪些料（＝哪些 pair 量過）。產圖流程要選一組料時就是查這個。 */
function partnersOf(lib, m){
  const k = matKey(m);
  const out = [];
  for (const p of (lib && lib.pairs) || []){
    if (matKey(p.a) === k) out.push({ pair: p, other: p.b });
    else if (matKey(p.b) === k) out.push({ pair: p, other: p.a });
  }
  return out;
}

/* 🔴 R6-4 逐字：換掉 B ⇒ **只**失效含 B 的 pair，其餘保留、不重測。 */
function dropMaterial(lib, m){
  const k = matKey(m);
  const removed = [], kept = [];
  const next = [];
  for (const p of (lib && lib.pairs) || []){
    if (matKey(p.a) === k || matKey(p.b) === k) removed.push(p.id);
    else { kept.push(p.id); next.push(p); }
  }
  lib.pairs = next;
  lib.archived = ((lib && lib.archived) || []).filter(x => matKey(x) !== k);
  return { removed, kept };
}

/* ---- 封存（〈材料庫管理〉Q3 甲；T062，牌 c-0924-ACC-30）----------------------------------
   封存＝從選料清單藏起來、資料保留可復原；**封存料的配對一起藏**。存在庫的 `archived`＝料的身分（料種＋顏色名，
   舊資料＝fid＋標籤；**不存色**——身分不含色，R6-14 子題 1）。料是從 pair 端點推出來的，所以封存只是一張「藏哪幾支」的表，
   不動任何一筆量測。schema 為什麼不抬見檔頭〈schema 2〉的 T062 補註。 */
function idOf(m){ return m.type ? { type: m.type, label: m.label } : { fid: m.fid || null, label: m.label }; }
function archivedKeys(lib){ return new Set(((lib && lib.archived) || []).map(matKey)); }
function isArchived(lib, m){ return archivedKeys(lib).has(matKey(m)); }
/* 回傳有沒有改到（已經是那個狀態＝false）。 */
function setArchived(lib, m, on){
  const k = matKey(m), list = ((lib && lib.archived) || []).filter(x => matKey(x) !== k);
  const had = list.length !== ((lib && lib.archived) || []).length;
  if (on) list.push(makeMaterial(Object.assign(idOf(m), { hex: null })));
  lib.archived = list;
  return had !== !!on;
}
/* 選料用的那一份：含封存料的 pair 全部拿掉（Q3「封存料的配對一起藏」）。沒有封存＝原樣回傳（行為與 T062 以前逐字相同）。 */
function withoutArchived(lib){
  const ak = archivedKeys(lib);
  if (!ak.size) return lib;
  return Object.assign({}, lib, { pairs: lib.pairs.filter(p => !ak.has(matKey(p.a)) && !ak.has(matKey(p.b))) });
}

/* ---- 從「校正表」建 pair ------------------------------------------- */
/* 入口是 engine.calibParseTable() 的產物，**不是**回讀頁的原始 JSON——
   解析（含色彩數學與合法性檢查）只有引擎那一份，本檔不重寫第二份。
   opt：{ materials:[料A, 料B, ...]（身分，可缺）, measuredAt, source } */
function pairFromDual(tbl, opt){
  opt = opt || {};
  if (!tbl || !Array.isArray(tbl.slots) || tbl.slots.length < 2 || !Array.isArray(tbl.pts))
    throw new Error('這不是解析過的雙料校正表');
  const mats = opt.materials || [];
  const a = makeMaterial(Object.assign({ hex: tbl.slots[0] }, mats[0] || {}));
  const b = makeMaterial(Object.assign({ hex: tbl.slots[1] }, mats[1] || {}));
  return {
    id: pairId(a, b), a, b,
    kind: opt.kind || tbl.kind || KINDS.DUAL,
    pts: tbl.pts.map(p => ({ S: p.S, hex: p.hex })),
    measuredAt: opt.measuredAt || null,
    source: opt.source || 'readback',
    claimedAt: null,
  };
}
/* 四料 48 格＝一次量到 6 個 pair（R6-2 附款：六對全有，不得砍成 24 格）。
   tbl ＝ engine.calibParseQuad() 的產物 {slots:[4 hex], pairs:[{i,j,pts}]}。 */
function pairsFromQuad(tbl, opt){
  opt = opt || {};
  if (!tbl || !Array.isArray(tbl.slots) || tbl.slots.length < 4 || !Array.isArray(tbl.pairs))
    throw new Error('這不是解析過的四料校正表');
  const mats = opt.materials || [];
  const mk = i => makeMaterial(Object.assign({ hex: tbl.slots[i] }, mats[i] || {}));
  return tbl.pairs.map(pr => {
    const a = mk(pr.i), b = mk(pr.j);
    return {
      id: pairId(a, b), a, b,
      kind: opt.kind || tbl.kind || KINDS.QUAD,
      pts: pr.pts.map(p => ({ S: p.S, hex: p.hex })),
      measuredAt: opt.measuredAt || null,
      source: opt.source || 'readback',
      claimedAt: null,
    };
  });
}

/* ---- 吐回引擎吃得下的校正表 ---------------------------------------- */
/* 🔴 吐的是**校正表原本的形狀**（料數／料／量測），交給 engine.calibParseTable() 去解析。
   這樣庫裡不必存 lin、不必算色彩數學，而且新舊兩條路（檔案匯入／從庫選）走的是同一支解析器。 */
function toCalibTable(pair){
  if (!pair) throw new Error('沒有這組料的校正資料');
  return {
    格式: 'PING 照片磚色彩校正表 v0（材料庫產出）',
    校正塊種類: pair.kind || '',
    料數: 2,
    料: [{ 槽: 'A', 色: pair.a.hex, 名: pair.a.label },
         { 槽: 'B', 色: pair.b.hex, 名: pair.b.label }],
    量測: pair.pts.map((p, i) => ({ 格: 'C' + (i + 1), 配方: { S: p.S }, 量到色: p.hex })),
  };
}
/* 雙料：從庫裡取這兩支料那一對，照**槽位順序**（m0＝擠出機 1）吐回校正表（車 3 段 F，牌 c-0923-ACC-23）。
   pair 在庫裡的 a/b 順序是量測當下決定的，不保證等於槽位順序 ⇒ 反了就把 S 對回「m0 的佔比」並對調料色；
   不翻＝engine.calibMatches 比料色對不上（整段不套用），或整條曲線頭尾顛倒（靜默錯表，同 toQuadCalibTable 的理由）。 */
function toDualCalibTable(lib, m0, m1){
  const a = makeMaterial(m0), b = makeMaterial(m1);
  const p = findPair(lib, a, b);
  if (!p) return { table: null, pair: null };
  const flip = matKey(p.a) !== matKey(a);
  const A = flip ? p.b : p.a, B = flip ? p.a : p.b;
  return { pair: p, table: {
    格式: 'PING 照片磚色彩校正表 v0（材料庫產出）',
    校正塊種類: p.kind || '',
    料數: 2,
    料: [{ 槽: 'A', 色: A.hex, 名: A.label }, { 槽: 'B', 色: B.hex, 名: B.label }],
    量測: p.pts.map((pt, i) => ({ 格: 'C' + (i + 1),
                                  配方: { S: flip ? Math.round((1 - pt.S) * 100) / 100 : pt.S },
                                  量到色: pt.hex })),
  } };
}
/* 四料：從庫裡湊出這四支料的六對。
   🔴 **湊不齊不補理論值**——湊不到的那一對就是沒有，回傳的 missing 讓呼叫端講給人聽（R6-16）。 */
function toQuadCalibTable(lib, materials){
  if (!Array.isArray(materials) || materials.length < 4) throw new Error('四料要四支料');
  const mats = materials.slice(0, 4).map(makeMaterial);
  const PAIRS = [[0,1],[0,2],[0,3],[1,2],[1,3],[2,3]];
  const 量測 = [], missing = [], present = [];
  PAIRS.forEach(([i, j], r) => {
    const p = findPair(lib, mats[i], mats[j]);
    if (!p){ missing.push({ i, j, row: r + 1 }); return; }
    present.push({ i, j, row: r + 1, id: p.id });
    /* pair 在庫裡的 a/b 順序不保證等於 (i,j)——id 與順序無關，存的時候誰在前面是量測當下決定的。
       ⇒ 這裡要判一次方向，把 S 對回「i 這支料的佔比」，否則整對的曲線會頭尾顛倒（靜默錯表）。 */
    const flip = matKey(p.a) !== matKey(mats[i]);
    p.pts.forEach((pt, c) => {
      const S = flip ? (1 - pt.S) : pt.S;
      const wi = Math.round(S * 100);
      const w = [0, 0, 0, 0];
      w[i] = wi; w[j] = 100 - wi;
      量測.push({ 格: 'R' + (r + 1) + 'C' + (c + 1),
                  配方: { A: w[0], B: w[1], C: w[2], D: w[3] },
                  量到色: pt.hex });
    });
  });
  const table = {
    格式: 'PING 照片磚色彩校正表 v0（材料庫產出）',
    校正塊種類: KINDS.QUAD,
    料數: 4,
    料: mats.map((m, i) => ({ 槽: 'ABCD'[i], 色: m.hex, 名: m.label })),
    量測,
  };
  return { table, missing, present };
}

/* ---- 遷移（R6-14 子題 2 附款） ------------------------------------- */
/* tbl ＝ engine.calibParseTable(舊鍵裡那張表) 的產物。
   🔴 **已經有同一對的資料就不覆蓋**——庫裡那筆可能是後來重新量的（source:'readback'），
   拿舊的蓋掉＝把新量的資料丟掉。遷移因此是冪等的：跑幾次結果都一樣。 */
function migrateLegacy(lib, tbl, opt){
  opt = opt || {};
  const pair = pairFromDual(tbl, {
    materials: [{ fid: null, label: (tbl.slots[0] || '').toUpperCase() },
                { fid: null, label: (tbl.slots[1] || '').toUpperCase() }],
    measuredAt: opt.measuredAt || null,
    source: 'legacy-hex',
    kind: opt.kind || tbl.kind || KINDS.DUAL,
  });
  const exists = getPair(lib, pair.id);
  if (exists) return { added: false, pair: exists, why: '庫裡已經有這一對，不覆蓋' };
  /* 🆕 車 3（段 F，牌 c-0923-ACC-23）：同一組量測點已經在庫裡（任何身分）＝這張舊表遷過了，不再加一次。
     為什麼要比內容：指定料種（當年是「認領」）會換身分 ⇒ id 跟著變，只比 id 的話
     每次開工作室都會把舊鍵那張表再遷進來一次，清單上就多一組以色碼命名的重複項（段 F 瀏覽器實走抓到）。
     🆕 T061：連 prev（被取代的上一份）一起比——舊表被合進別組、退成上一版之後，也不能再以新的一組冒出來。 */
  const sig = ptsSig(pair);
  const same = (lib.pairs || []).find(p => ptsSig(p) === sig || (p.prev && ptsSig(p.prev) === sig));
  if (same) return { added: false, pair: same, why: '這張舊表的量測已經在庫裡（可能已經指定過料種）' };
  upsertPair(lib, pair);
  return { added: true, pair: getPair(lib, pair.id), why: '' };
}

/* ---- 指定料種＋顏色名（T061；R6-14 子題 1 附款＋子題 2 附款的附款）---------------------
   舊資料那一問（第一次開新版）、清單上的「指定…」都走這一支；取代 schema 1 的「認領」（claimPair，已退場）。
   map：舊 key → {type, label}。一次可以指定好幾支（舊資料那一問是一次問完）。
   身分變了 ⇒ id 跟著變（id 是身分算出來的）。兩支舊料指到同一支料＝**合成一支**；合完同一組出現兩份量測
   ⇒ 比較新的當目前、另一份留作 prev（同〈材料庫管理〉的重新校正）。出身（legacy-hex）保留，可追溯。
   🔴 同一組的兩端被指成同一支料＝拿一支料跟自己校正 ⇒ **整批不做**（先用 specifyConflicts 講給人聽）。 */
function specified(map, m){
  const s = map.get(matKey(m));
  return s ? makeMaterial({ type: s.type, label: s.label, hex: m.hex }) : m;
}
function specifyConflicts(lib, map){
  const out = [];
  for (const p of (lib && lib.pairs) || []){
    if (!map.has(matKey(p.a)) && !map.has(matKey(p.b))) continue;
    const a = specified(map, p.a), b = specified(map, p.b);
    if (matKey(a) === matKey(b)) out.push({ pairId: p.id, label: a.label, type: a.type });
  }
  return out;
}
function specifyMaterials(lib, map){
  if (specifyConflicts(lib, map).length) throw new Error('同一組的兩支被指成同一支料');
  const before = new Set(listMaterials(lib).map(m => m.key)), arch = archivedKeys(lib);
  const renamed = new Map();
  map.forEach((v, k) => renamed.set(k, matKey(makeMaterial({ type: v.type, label: v.label }))));
  const next = [];
  for (const p of lib.pairs){
    const a = specified(map, p.a), b = specified(map, p.b);
    const q = Object.assign({}, p, { a, b, id: pairId(a, b) });
    const j = next.findIndex(x => x.id === q.id);
    if (j < 0) next.push(q); else next[j] = mergeVersions(next[j], q);
  }
  lib.pairs = next;
  /* 🆕 T062：封存名單跟著身分走。合成一支時**全部來源都封存著**才算封存——有一支在用，合起來那支就在用
     （例：舊資料指定成「白」，而庫裡的「白」封存著 ⇒ 叫回來，同原型 designate）。 */
  if (arch.size){
    const groups = new Map();                            // 新 key → 合進來的舊 key（連同庫裡原本就叫這個名字的那支）
    renamed.forEach((nk, ok) => {
      if (!groups.has(nk)) groups.set(nk, { srcs: before.has(nk) ? [nk] : [], id: map.get(ok) });
      groups.get(nk).srcs.push(ok);
    });
    const list = (lib.archived || []).filter(x => !renamed.has(matKey(x)) && !groups.has(matKey(x)));
    groups.forEach((g, nk) => { if (g.srcs.every(k => arch.has(k))) list.push(makeMaterial({ type: g.id.type, label: g.id.label })); });
    lib.archived = list;
  }
  return renamed;
}
/* 同一組（同 id）的兩筆合成一筆：四份量測（兩筆各自的目前＋prev）照日期排，最新的當目前、次新的當 prev。
   y 的 a/b 順序可能跟 x 相反 ⇒ 把 y 的 S 翻成「x.a 的佔比」再比（不翻＝曲線頭尾顛倒，同 toDualCalibTable 的理由）。
   沒記日期的排在最後（舊表）；同日期保留 x 的（庫裡較早的那筆，清單不跳動）。 */
/* 一份量測的 S 翻成另一端的佔比（pair 在兩處的 a/b 順序相反時用；不翻＝曲線頭尾顛倒，同 toDualCalibTable 的理由）。 */
function flipVersion(v){
  return !v ? null : Object.assign({}, v, { pts: v.pts.map(t => ({ S: Math.round((1 - t.S) * 100) / 100, hex: t.hex })).sort((m, n) => n.S - m.S) });
}
function mergeVersions(x, y){
  const flip = matKey(x.a) !== matKey(y.a);
  const turn = v => flip ? flipVersion(v) : v;
  const ends = v => ({ ha: v.pts[0].hex, hb: v.pts[v.pts.length - 1].hex });
  const vs = [Object.assign(normVersion(x), { ha: x.a.hex, hb: x.b.hex }),
              x.prev && Object.assign(normVersion(x.prev), ends(x.prev)),
              Object.assign(turn(normVersion(y)), { ha: (flip ? y.b : y.a).hex, hb: (flip ? y.a : y.b).hex }),
              y.prev && Object.assign(turn(normVersion(y.prev)), ends(turn(y.prev)))].filter(Boolean);
  vs.sort((m, n) => (n.measuredAt || '').localeCompare(m.measuredAt || ''));
  const cur = vs[0], prev = vs[1] || null;
  return Object.assign({}, x, {
    a: Object.assign({}, x.a, { hex: cur.ha }), b: Object.assign({}, x.b, { hex: cur.hb }),
    pts: cur.pts, measuredAt: cur.measuredAt, kind: cur.kind, source: cur.source,
    prev: prev ? normVersion(prev) : null,
  });
}

/* ---- 還原上一版／用匯入的那一份（〈材料庫管理〉Q9、Q11；T062，牌 c-0924-ACC-30）-------------------
   把 v 升成這一組「目前那一份」、原本那份退成 prev。同一支做兩件事：
   ①還原（Q9「舊的留一份可還原」）＝ v 就是 prev ⇒ 兩份對調，還原完還能再換回來（原型細節 2）；
   ②匯入撞同一組選「用匯入的」（Q11）＝ v 是匯進來那份（已翻成本機這組的 a/b 方向）⇒ 本機那份留作上一版（原型細節 2）。
   料的色（這組量到的兩端）跟著 v 走：有給 ends 用 ends（匯進來那份宣告的料色），沒給取 pts 兩端（同 mergeVersions）。 */
function promote(lib, id, v, ends){
  const i = ((lib && lib.pairs) || []).findIndex(p => p.id === id);
  const nv = normVersion(v);
  if (i < 0 || !nv) return null;
  const p = lib.pairs[i];
  const ha = (ends && upHex(ends.ha)) || nv.pts[0].hex, hb = (ends && upHex(ends.hb)) || nv.pts[nv.pts.length - 1].hex;
  const q = Object.assign({}, p, { a: Object.assign({}, p.a, { hex: ha }), b: Object.assign({}, p.b, { hex: hb }),
    pts: nv.pts, measuredAt: nv.measuredAt, kind: nv.kind, source: nv.source, prev: normVersion(p) });
  lib.pairs[i] = q;
  return q;
}
function restorePrev(lib, id){ const p = getPair(lib, id); return p && p.prev ? promote(lib, id, p.prev) : null; }

/* ---- 匯入時分類：新的／一模一樣／撞到同一組（Q11 丙＝撞到的逐組問）--------------------------
   本支只分類、不動庫；照使用者的選擇套用在 applyLibImport。incoming 的每一筆都已正規化（有 id）。
   conflict 的 theirs＝匯進來那份、**已翻成本機這組的 a/b 方向**（id 與順序無關，兩台量的時候誰在前面不一定）。
   opt.byContent：id 對不上、但同一次量測已經在本機（任何身分，含上一版）＝一模一樣——同 mergeLib byContent 的理由：
   同一次量測在兩台電腦上可能是不同身分（一台指定過料種、一台還沒），只比 id 會讓清單多一組同樣量測的重複項。 */
function classifyPairs(local, pairs, opt){
  const sigs = opt && opt.byContent ? contentSigs(local) : null;
  const add = [], same = [], conflict = [];
  for (const p of pairs || []){
    const mine = getPair(local, p.id);
    if (!mine){
      if (sigs && (sigs.has(ptsSig(p)) || sigs.has(ptsSig(flipVersion(normVersion(p)))))) same.push(p);
      else add.push(p);
      continue;
    }
    const flip = matKey(mine.a) !== matKey(p.a);
    const theirs = flip ? flipVersion(normVersion(p)) : normVersion(p);
    if (ptsSig(theirs) === ptsSig(mine)) { same.push(p); continue; }
    conflict.push({ id: mine.id, mine, theirs, ends: { ha: (flip ? p.b : p.a).hex, hb: (flip ? p.a : p.b).hex } });
  }
  return { add, same, conflict };
}
/* 材料庫檔（別台「匯出材料庫…」存的）→ 匯入計畫。新進來的料照對方的封存狀態（搬電腦時封存的不該又冒出來）；
   本機已經有的料照本機的（Q11：本機是使用者自己的決定）。legacyAskedAt 不帶（那是這一台問過沒有，不是資料）。 */
function libImportPlan(local, incoming){
  const plan = classifyPairs(local, (incoming && incoming.pairs) || [], { byContent: true });
  const known = new Set(listMaterials(local).map(m => m.key)), ends = new Set();
  plan.add.forEach(p => { ends.add(matKey(p.a)); ends.add(matKey(p.b)); });
  plan.archiveNew = ((incoming && incoming.archived) || []).filter(m => !known.has(matKey(m)) && ends.has(matKey(m)));
  return plan;
}
/* choices[i]＝'theirs'（用匯入的）或其他（保留本機的）；對應 plan.conflict[i]。回傳各類幾組。 */
function applyLibImport(local, plan, choices){
  plan.add.forEach(p => upsertPair(local, p));
  let took = 0;
  plan.conflict.forEach((c, i) => { if ((choices || [])[i] === 'theirs' && promote(local, c.id, c.theirs, c.ends)) took++; });
  (plan.archiveNew || []).forEach(m => setArchived(local, m, true));
  return { added: plan.add.length, same: plan.same.length, took, kept: plan.conflict.length - took, archived: (plan.archiveNew || []).length };
}

/* ---- 「匯入…」一顆鈕自動分辨（Q4 甲）、「匯出材料庫…」（Q4／Q10）-------------------------------
   材料庫檔＝有 schema＋pairs（本版「匯出材料庫…」存的，也吃 App 設定資料夾裡那份 material_library.json）；
   其餘交給引擎的校正表解析器判（本檔不寫第二把尺）。版本比這一版新的材料庫檔＝講明白、不猜格式。 */
function fileKind(obj){
  if (!obj || typeof obj !== 'object' || !Array.isArray(obj.pairs) || !('schema' in obj)) return 'other';
  return SCHEMAS_READ.indexOf(obj.schema) >= 0 ? 'lib' : 'lib-unknown';
}
const EXPORT_FORMAT = 'PING 照片磚材料庫';
/* 整庫一個檔、含封存的（Q4）。不帶 legacyAskedAt（那是這一台問過沒有）；pair 不帶 id／claimedAt（id 讀進來會重算）。 */
function exportText(lib, exportedAt){
  const pairs = ((lib && lib.pairs) || []).map(p => ({ a: p.a, b: p.b, kind: p.kind, pts: p.pts, measuredAt: p.measuredAt,
                                                        source: p.source, prev: p.prev || null }));
  return JSON.stringify({ 格式: EXPORT_FORMAT, exportedAt: exportedAt || null, schema: SCHEMA,
                          archived: ((lib && lib.archived) || []).map(idOf), pairs });
}

/* ---- 庫的家＝App 設定資料夾（段 B；R6-14 子題 2 裁 B：庫落 data_dir） ------------------
   產品路徑：頁面 ↔ C++ 走 `phototile_matlib_*` 四支訊息，檔案落
   `<data_dir>/phototile/material_library.json`（原子寫：.tmp 寫完才換上；換上前把舊檔留一份 .bak）。
   🔴 照**活著的** `phototile_image_begin|chunk|end` 抄四項驗證：①連號 ②塊數 ③總長度 ④上限。
      （`phototile_export_*` 別抄——它現在只剩校正片 3MF 在用，形狀是舊的。）
   🔴 `createSaveAssembler()` ＝ C++ 收件口的**參考模型**：規則只寫這一份，GUI_App.cpp 逐條鏡像；
      單元測的反向案例（亂序／少塊／長度竄改／超上限／壞 base64）打的就是它。
      **一次存檔只回一次結果**（在 end 那一刻）：中途壞掉就記下原因、後面的塊一律不收，到 end 才回報。
   ⚠ 「測試版與出貨版的材料庫各自一份」**只在版本治理第 4 件（測試版獨立設定資料夾）上車之後才成立**：
      那一顆（Eric 2026-09-24 裁做、單獨一車；DR 線分支 `claude/dataroot-0924`）讓測試版改用 `%APPDATA%\PINGSlicer-Internal`，
      第一次開時從正式版只讀複製一份（含網頁儲存區的 Local Storage，舊鍵與車 1 期間的瀏覽器庫會一起帶過去）。
      在那之前（T060 以前、以及那一顆上車前的 T 版）兩版共用 `%APPDATA%\PingSlicer`、材料庫是同一份——
      這也是 schema 2 要讓舊版「讀不懂就明講、不蓋檔」的理由。（2026-09-23 T060 實查訂正；T061 依 DR 線 0924 知會改寫。） */
/* 上限 512 KB 的由來：一對雙料 8 點約 350 bytes、一張四料 48 格（6 對）約 2 KB ⇒ 512 KB 約可放一千五百對，
   遠超過實際會用到的量；而讀檔回程是**一次** RunScript 注入（不分塊），上限壓小就不必再做一套分塊回程。
   超過時送出端先擋、並把原因講出來（不是默默存不進去）。 */
const HOST_MAX_BYTES   = 524288;            // 🔴 同值住 GUI_App.cpp（max_matlib_bytes）；改要一起改
const HOST_MAX_CHUNKS  = 4096;              // 🔴 同上
const HOST_CHUNK_BYTES = 48 * 1024;         // 可被 3 整除 ⇒ 每一塊的 base64 中段不會有 padding
const CMD = { LOAD: 'phototile_matlib_load',
              SAVE_BEGIN: 'phototile_matlib_save_begin',
              SAVE_CHUNK: 'phototile_matlib_save_chunk',
              SAVE_END:   'phototile_matlib_save_end',
              EXPORT:     'phototile_matlib_export' };   // 🆕 T062：「匯出材料庫…」（App 開存檔視窗寫到使用者選的位置）

function utf8Encode(s){
  if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(String(s));
  return Uint8Array.from(Buffer.from(String(s), 'utf8'));
}
/* fatal：壞掉的 UTF-8 直接丟例外，不要靜默換成 U+FFFD（那會把壞檔當成好檔再存回去）。 */
function utf8Decode(u8){ return new TextDecoder('utf-8', { fatal: true }).decode(u8); }
function bytesToB64(u8){
  if (typeof Buffer !== 'undefined') return Buffer.from(u8).toString('base64');
  let s = '';
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}
/* 壞掉的 base64 一律丟例外——不可以靜默解成一段比較短的資料（長度驗證就是靠它才抓得到）。 */
function b64ToBytes(b64){
  if (typeof b64 !== 'string' || b64.length % 4 !== 0 || !/^[A-Za-z0-9+/]*={0,2}$/.test(b64))
    throw new Error('base64 格式不合');
  if (typeof Buffer !== 'undefined') return Uint8Array.from(Buffer.from(b64, 'base64'));
  const s = atob(b64); const u8 = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
  return u8;
}

/* 頁面 → App：把整份庫（JSON 字串）切成 begin／chunk…／end。超過上限在這裡就擋、並講出來。 */
function buildSaveMessages(text, chunkBytes){
  const bytes = utf8Encode(text);
  if (!bytes.length) throw new Error('材料庫內容是空的');
  if (bytes.length > HOST_MAX_BYTES)
    throw new Error('材料庫超過 ' + (HOST_MAX_BYTES / 1024) + ' KB 上限（' + bytes.length + ' bytes）');
  const cb = chunkBytes || HOST_CHUNK_BYTES;
  const chunks = Math.ceil(bytes.length / cb);
  const out = [{ command: CMD.SAVE_BEGIN, data: { size: bytes.length, chunks } }];
  for (let i = 0; i < chunks; i++)
    out.push({ command: CMD.SAVE_CHUNK,
               data: { index: i, base64: bytesToB64(bytes.subarray(i * cb, Math.min(bytes.length, (i + 1) * cb))) } });
  out.push({ command: CMD.SAVE_END, data: { size: bytes.length, chunks } });
  return out;
}

/* 🆕 T062「匯出材料庫…」：頁面 → App 一則訊息（內容由頁面組＝畫面上看到的那一份；App 開存檔視窗、完整地寫，不解讀）。
   為什麼不分塊：上限同存檔（512 KB），一則訊息裝得下；為什麼要 App：嵌入式 WebView 的下載沒有存檔視窗（mac 版甚至不會動），
   「另存 AI 圖…」也是同一條路。上限／壞 base64 的擋法 C++ 照 phototile_matlib_save_* 抄。 */
function buildExportMessage(text, name){
  const bytes = utf8Encode(text);
  if (!bytes.length) throw new Error('材料庫內容是空的');
  if (bytes.length > HOST_MAX_BYTES)
    throw new Error('材料庫超過 ' + (HOST_MAX_BYTES / 1024) + ' KB 上限（' + bytes.length + ' bytes）');
  return { command: CMD.EXPORT, data: { name: String(name || ''), size: bytes.length, base64: bytesToB64(bytes) } };
}

/* C++ 收件口的參考模型。accept(msg) 回傳：
     {ok:true, done:false}              收下這一步
     {ok:true, done:true, bytes}        收齊，可以落檔
     {ok:false, code, why, report}      這一步不收；report:true＝這一刻要回報頁面（只有 end 會是 true）
   code：limit（④上限）／order（①連號）／decode（壞 base64）／length（③總長度）／count（②塊數）／idle（沒有 begin）。
   🔴 中途壞掉＝整批作廢、緩衝丟掉，**絕不半套落檔**（半套＝使用者的校正資料被截斷，而且不報錯）。 */
function createSaveAssembler(maxBytes, maxChunks){
  const cap = maxBytes || HOST_MAX_BYTES, capN = maxChunks || HOST_MAX_CHUNKS;
  let state = 'idle', size = 0, chunks = 0, next = 0, parts = [], got = 0, failCode = '', failWhy = '';
  const clear = () => { size = 0; chunks = 0; next = 0; parts = []; got = 0; };
  const fail = (code, why) => { clear(); state = 'failed'; failCode = code; failWhy = why;
                                return { ok: false, code, why, report: false }; };
  return {
    accept(m){
      const cmd = m && m.command, d = (m && m.data) || {};
      if (cmd === CMD.SAVE_BEGIN){
        clear(); state = 'idle'; failCode = ''; failWhy = '';
        const s = Number(d.size), c = Number(d.chunks);
        if (!Number.isInteger(s) || s <= 0 || s > cap) return fail('limit', '材料庫大小不合法或超過上限（' + d.size + '）');
        if (!Number.isInteger(c) || c <= 0 || c > capN || c > s) return fail('limit', '分塊數不合法（' + d.chunks + '）');
        state = 'active'; size = s; chunks = c;
        return { ok: true, done: false };
      }
      if (cmd === CMD.SAVE_CHUNK){
        if (state !== 'active') return { ok: false, code: state === 'failed' ? failCode : 'idle', why: '這一批已作廢或沒有開始', report: false };
        if (Number(d.index) !== next || typeof d.base64 !== 'string' || !d.base64)
          return fail('order', '分塊亂序或是空的（期望第 ' + next + ' 塊，收到 ' + d.index + '）');
        let u8;
        try { u8 = b64ToBytes(d.base64); } catch (e) { return fail('decode', '分塊內容解不開'); }
        if (!u8.length || got + u8.length > size) return fail('length', '累計長度超過宣告的 ' + size + ' bytes');
        parts.push(u8); got += u8.length; next++;
        return { ok: true, done: false };
      }
      if (cmd === CMD.SAVE_END){
        if (state === 'failed'){ const r = { ok: false, code: failCode, why: failWhy, report: true }; state = 'idle'; return r; }
        if (state !== 'active') return { ok: false, code: 'idle', why: '沒有進行中的存檔', report: true };
        if (next !== chunks){ const r = fail('count', '分塊數不符（收到 ' + next + '／宣告 ' + chunks + '）'); state = 'idle'; r.report = true; return r; }
        if (got !== size){ const r = fail('length', '總長度不符（收到 ' + got + '／宣告 ' + size + '）'); state = 'idle'; r.report = true; return r; }
        const bytes = new Uint8Array(size); let o = 0;
        for (const p of parts){ bytes.set(p, o); o += p.length; }
        clear(); state = 'idle';
        return { ok: true, done: true, bytes };
      }
      return { ok: false, code: 'unknown', why: '不認得的訊息：' + cmd, report: false };
    }
  };
}

/* 讀進來的原文 → 庫。和 load() 的差別：**讀不懂要講出來**（ok:false＋why），不靜默當成空庫——
   App 那一份若被手改壞，呼叫端要知道「不能拿空庫去蓋它」。原文是 null／空字串＝還沒有庫（ok:true）。 */
function parseLib(raw){
  if (raw == null || raw === '') return { ok: true, lib: emptyLib(), why: '' };
  let obj;
  try { obj = JSON.parse(raw); } catch (e) { return { ok: false, lib: emptyLib(), why: '材料庫檔案不是合法的 JSON' }; }
  if (!obj || typeof obj !== 'object' || SCHEMAS_READ.indexOf(obj.schema) < 0)
    return { ok: false, lib: emptyLib(), why: '材料庫檔案的版本不認得（schema ' + (obj && obj.schema) + '）' };
  return { ok: true, lib: normalize(obj), why: '' };
}
/* 一組量測的內容簽名（各點的 S＋量到色）。指定料種會換身分、id 跟著變，但量測點不變 ⇒ 判「是不是同一次校正」要比它。 */
function ptsSig(p){ return (p.pts || []).map(x => x.S + x.hex).join('|'); }
/* 庫裡所有量測的簽名——🆕 T061：連 prev（被取代的上一份）一起算，退成上一版的那份也不能再以新的一組冒出來。 */
function contentSigs(lib){
  const s = new Set();
  for (const p of (lib && lib.pairs) || []){ s.add(ptsSig(p)); if (p.prev) s.add(ptsSig(p.prev)); }
  return s;
}
/* 把 from 裡、into 還沒有的 pair 併進 into。🔴 **不覆蓋**已有的那一筆（into 那筆可能是後來重新量的）。
   用途：①App 讀檔回來之前使用者先匯入的表 ②車 1 期間存在瀏覽器儲存區的庫——都要併進 App 那一份。
   🆕 opt.byContent（T060 跟車實走抓到，牌 c-0923-ACC-25）：同一組量測點已經在 into 裡（任何身分）就不再加。
   為什麼：瀏覽器儲存區那份庫照設計只讀不刪，裡面還是認領前的 id；認領之後 App 那份的 id 變了 ⇒ 只比 id 的話，
   認領過的那組每次開工作室都會以「未認領」再冒出來一次（清單多一組重複項、選到又要再認領一次）。
   跟 migrateLegacy 看內容是同一條理由；②那條路徑一定要帶。 */
function mergeLib(into, from, opt){
  const sigs = opt && opt.byContent ? contentSigs(into) : null;
  let added = 0;
  for (const p of (from && from.pairs) || []){
    if (getPair(into, p.id)) continue;
    if (sigs && sigs.has(ptsSig(p))) continue;
    upsertPair(into, p); added++;
    if (sigs){ sigs.add(ptsSig(p)); if (p.prev) sigs.add(ptsSig(p.prev)); }
  }
  return added;
}

/* App 那一份的儲存實作。send(command, data)＝頁面的 sendSlicerMessage；
   ls＝瀏覽器儲存區，**只為了讀舊資料**（舊鍵、車 1 期間的庫），兩者都要併進 App 那一份，只讀不刪。
   寫入是非同步的：writeLib 回 'pending'，結果由 App 回推 matlibSaved({ok, message})。 */
function hostStorage(send, ls){
  let buf = null;
  const lsGet = k => { try { return ls ? ls.getItem(k) : null; } catch (e) { return null; } };
  return { name: 'App 設定資料夾', isHost: true,
    readLib: () => buf,
    writeLib: s => { const msgs = buildSaveMessages(s); msgs.forEach(m => send(m.command, m.data)); buf = s; return 'pending'; },
    readLegacy: () => lsGet(LEGACY_KEY),
    readBrowserLib: () => lsGet(LIB_KEY),
    requestLoad: () => send(CMD.LOAD, {}),
    /* 🆕 T062：匯出＝結果由 App 回推 matlibExported({ok, path, message})。 */
    exportLib: (text, name) => { const m = buildExportMessage(text, name); send(m.command, m.data); return 'pending'; },
    /* App 回來的讀檔結果 {ok, exists, base64, message} → 放進緩衝；回傳 {ok, why}。 */
    acceptLoad: msg => {
      if (!msg || msg.ok !== true) return { ok: false, why: (msg && msg.message) || 'App 讀不到材料庫' };
      if (!msg.exists){ buf = null; return { ok: true, why: '' }; }
      try { buf = utf8Decode(b64ToBytes(String(msg.base64 || ''))); return { ok: true, why: '' }; }
      catch (e) { buf = null; return { ok: false, why: '材料庫檔案讀回來是壞的（' + (e && e.message || e) + '）' }; }
    } };
}

/* ---- 儲存轉接層 ----------------------------------------------------- */
/* impl＝{ name, readLib():string|null, writeLib(s):void, readLegacy():string|null }
   丟例外＝存不進去，由 save() 翻成 {persisted:false, why}。🔴 不吞例外、不靜默。 */
function memoryStorage(){
  let buf = null, legacy = null;
  return { name: 'memory',
    readLib: () => buf, writeLib: s => { buf = s; },
    readLegacy: () => legacy, _setLegacy: s => { legacy = s; } };
}
function localStorageStorage(ls){
  return { name: 'localStorage',
    readLib: () => ls.getItem(LIB_KEY),
    writeLib: s => ls.setItem(LIB_KEY, s),
    readLegacy: () => ls.getItem(LEGACY_KEY) };
}
/* 存不進去也要能用（讀得到就好），所以「沒有儲存區」不是錯誤，是一個會誠實講出來的狀態。 */
function nullStorage(why){
  return { name: 'none', _why: why,
    readLib: () => null, writeLib: () => { throw new Error(why); }, readLegacy: () => null };
}
function detectStorage(){
  try {
    const ls = (root && root.localStorage) ? root.localStorage : null;
    if (!ls) return nullStorage('這個環境沒有瀏覽器儲存區');
    const probe = '__ptml_probe__';
    ls.setItem(probe, '1'); ls.removeItem(probe);      // 無痕視窗會在這裡丟例外
    return localStorageStorage(ls);
  } catch (e) {
    return nullStorage('瀏覽器儲存區不可寫（' + (e && e.message || e) + '）');
  }
}

let STORAGE = null;
function getStorage(){ if (!STORAGE) STORAGE = detectStorage(); return STORAGE; }
function setStorage(impl){ STORAGE = impl || null; return getStorage(); }

function load(){
  const st = getStorage();
  let raw = null;
  try { raw = st.readLib(); } catch (e) { raw = null; }
  if (!raw) return emptyLib();
  try { return normalize(JSON.parse(raw)); } catch (e) { return emptyLib(); }
}
/* 回傳 {persisted, why}（車 1 的形狀，一個字不動——phototile_calib_test.js 釘住了它）；
   只有 App 那條會多一個 pending:true＝已送給 App、結果還沒回來（App 會回推 matlibSaved）。
   呼叫端**不可把 pending 當成失敗**，也不可把它當成已存好。 */
function save(lib){
  const st = getStorage();
  let r;
  try { r = st.writeLib(JSON.stringify({ schema: SCHEMA, legacyAskedAt: (lib && lib.legacyAskedAt) || null,
                                         archived: ((lib && lib.archived) || []).map(idOf),   // 🆕 T062（選填；schema 不抬，見檔頭）
                                         pairs: (lib && lib.pairs) || [] })); }
  catch (e) { return { persisted: false, why: '存不進' + st.name + '：' + (e && e.message || e) }; }
  if (r === 'pending') return { persisted: false, pending: true, why: '' };
  return { persisted: true, why: '' };
}
/* 🔴 只讀，**不刪**。舊鍵留著＝rollback 的唯一退路；庫存在之後不再寫它（見檔頭規則④）。 */
function readLegacyRaw(){
  const st = getStorage();
  try {
    const s = st.readLegacy ? st.readLegacy() : null;
    return s ? JSON.parse(s) : null;
  } catch (e) { return null; }
}

return {
  SCHEMA, SCHEMAS_READ, LIB_KEY, LEGACY_KEY, KINDS,
  matKey, makeMaterial, pairId, needsSpec,
  emptyLib, normalize, getPair, findPair, upsertPair,
  listMaterials, partnersOf, dropMaterial,
  pairFromDual, pairsFromQuad, toCalibTable, toDualCalibTable, toQuadCalibTable,
  migrateLegacy, specifyConflicts, specifyMaterials, ptsSig, contentSigs,
  archivedKeys, isArchived, setArchived, withoutArchived, flipVersion, promote, restorePrev,
  classifyPairs, libImportPlan, applyLibImport, fileKind, EXPORT_FORMAT, exportText, buildExportMessage,
  memoryStorage, nullStorage, detectStorage, getStorage, setStorage,
  load, save, readLegacyRaw,
  HOST_MAX_BYTES, HOST_MAX_CHUNKS, HOST_CHUNK_BYTES, CMD,
  buildSaveMessages, createSaveAssembler, hostStorage, parseLib, mergeLib,
  utf8Encode, utf8Decode, bytesToB64, b64ToBytes,
};
});
