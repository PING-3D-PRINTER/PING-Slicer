(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.PingPhotoTileSize = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  function roundMm(value, digits = 1) {
    const scale = 10 ** digits;
    return Math.round(value * scale) / scale;
  }

  function clampSizeMm(value, fallback, min = 20, max = 400) {
    const parsed = Number(value);
    const safe = Number.isFinite(parsed) ? parsed : fallback;
    return Math.max(min, Math.min(max, safe));
  }

  // Keep the source image's height/width ratio while respecting the physical-size limits.
  // If a very long panorama cannot keep both sides above `min`, preserving its ratio wins.
  function fitAspectSize({ width, height, aspect, changed = "width", min = 20, max = 400, digits = 1 }) {
    const ratio = Number(aspect);
    if (!Number.isFinite(ratio) || ratio <= 0) {
      return {
        width: roundMm(clampSizeMm(width, 100, min, max), digits),
        height: roundMm(clampSizeMm(height, 75, min, max), digits)
      };
    }

    let nextWidth;
    let nextHeight;
    if (changed === "height") {
      nextHeight = clampSizeMm(height, 75, min, max);
      nextWidth = nextHeight / ratio;
    } else {
      nextWidth = clampSizeMm(width, 100, min, max);
      nextHeight = nextWidth * ratio;
    }

    const shrink = Math.min(1, max / nextWidth, max / nextHeight);
    nextWidth *= shrink;
    nextHeight *= shrink;

    const grow = Math.max(1, min / nextWidth, min / nextHeight);
    if (nextWidth * grow <= max && nextHeight * grow <= max) {
      nextWidth *= grow;
      nextHeight *= grow;
    }

    return {
      width: roundMm(nextWidth, digits),
      height: roundMm(nextHeight, digits)
    };
  }

  // 厚度下限（AIP 刀 2；規格 照片磚_核心規格.md R9-11 Q4～Q7）：磚越高越要厚，不然印的時候會倒。
  // 下限＝高 ÷ 15 往上取到 0.5 mm、最少 10，全部機型都套（Q5）；自動值再疊在料數預設（雙料 10、四料 20）上面。
  // 上限 30→40（Q5：600 mm 高要 40）——同值另住 index.html 的 #tIn max 與 engine.js normalizeRequest 的 thick clamp，改要一起改。
  const THICK_MIN_MM = 2, THICK_MAX_MM = 40, THICK_TIP_RATIO = 15, THICK_TIP_MIN_MM = 10;

  function thickFloorMm(heightMm) {
    const h = Number(heightMm);
    const byHeight = Number.isFinite(h) && h > 0 ? Math.ceil(h / THICK_TIP_RATIO * 2 - 1e-9) / 2 : 0;
    return Math.min(THICK_MAX_MM, Math.max(THICK_TIP_MIN_MM, byHeight));
  }

  function thickAutoMm(defaultMm, heightMm) {
    return Math.min(THICK_MAX_MM, Math.max(Number(defaultMm) || THICK_TIP_MIN_MM, thickFloorMm(heightMm)));
  }

  // 厚度欄下面那一行（文案照原型 v4；index.html 的 ptThickSync 只負責畫）：
  // ①沒自己改＝自動值高過料數預設才講（R-09：自動填的值看得到）②自己改薄於防倒下限＝黃字＋〔改回〕，不擋（Q6）
  // ③改過但不薄＝講是他設的＋〔跟著尺寸自動調〕。
  // ⚠ 黃字只比防倒下限、不比料數預設：四料預設 20 是清料空間（#40），他改成 15 是他的選擇、不是會倒。
  // ⚠〔改回〕寫的是按下去真的會變成的自動值，不是下限（四料矮磚時下限 10、自動值 20，兩者不同）。
  function thickNote({ userSet, thickMm, defaultMm, heightMm }) {
    const dflt = Number(defaultMm) || THICK_TIP_MIN_MM;
    const floor = thickFloorMm(heightMm), auto = thickAutoMm(dflt, heightMm);
    if (!userSet) {
      return auto > dflt
        ? { warn: false, text: `厚度跟著高度調到 ${auto} mm（高 ${roundMm(heightMm)} mm ÷ 15，免得印的時候倒）`, action: null }
        : { warn: false, text: "", action: null };
    }
    if (thickMm < floor) {
      return { warn: true, text: `比防倒建議的 ${floor} mm 薄（高 ÷ 15）——磚這麼高、這麼薄，印的時候容易倒。`, action: `改回 ${auto} mm` };
    }
    return { warn: false, text: `厚度 ${thickMm} mm（你自己設的）`, action: "跟著尺寸自動調" };
  }

  return { clampSizeMm, fitAspectSize, roundMm, thickFloorMm, thickAutoMm, thickNote, THICK_MIN_MM, THICK_MAX_MM };
});
