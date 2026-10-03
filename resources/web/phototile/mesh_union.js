(function (root, factory) {
  "use strict";
  const api = factory();
  if (typeof module !== "undefined" && module.exports)
    module.exports = api;
  if (root)
    root.PhotoTileMesh = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  function cleanIsolated(labels, w, h) {
    const src = new Uint8Array(labels);
    const out = new Uint8Array(src);
    for (let y = 1; y < h - 1; y++) {
      for (let x = 1; x < w - 1; x++) {
        const at = y * w + x;
        const center = src[at];
        const counts = new Map();
        let same = 0;
        for (let dy = -1; dy <= 1; dy++) {
          for (let dx = -1; dx <= 1; dx++) {
            if (dx === 0 && dy === 0)
              continue;
            const value = src[(y + dy) * w + x + dx];
            if (value === center)
              same++;
            counts.set(value, (counts.get(value) || 0) + 1);
          }
        }
        // Only remove a truly isolated pixel. The old 3x3 majority filter ran
        // twice and could cut a printable one-pixel bridge into two islands.
        if (same !== 0)
          continue;
        let best = center;
        let bestCount = 0;
        for (const [value, count] of counts) {
          if (count > bestCount) {
            best = value;
            bestCount = count;
          }
        }
        if (bestCount >= 5)
          out[at] = best;
      }
    }
    return out;
  }

  // windowMmZ（選填，AIP 刀 1）：上下方向另給視窗（例：一層）；沒給＝上下跟左右同一個 windowMm（＝舊行為）。
  // 開不開平滑仍只看 windowMm（雜訊濾除欄 0＝整步關閉）。
  function smoothLabelNoise(labels, w, h, paletteSize, sx, sz, windowMm, strategy, windowMmZ) {
    const src = new Uint8Array(labels);
    const out = new Uint8Array(src);
    const levels = Math.max(1, Math.min(256, Number(paletteSize) || 1));
    const cellX = Number(sx);
    const cellZ = Number(sz);
    const windowSize = Number(windowMm);
    const mode = strategy === "mode" ? "mode" : "median";
    const disabled = !w || !h || src.length !== w * h || windowSize <= 0 ||
      !Number.isFinite(windowSize) || !Number.isFinite(cellX) || cellX <= 0 ||
      !Number.isFinite(cellZ) || cellZ <= 0;
    if (disabled) {
      return {
        labels: out,
        changedPixels: 0,
        changedAreaMm2: 0,
        radiusX: 0,
        radiusY: 0,
        strategy: mode
      };
    }

    // The entered size is the full physical smoothing window, not its radius.
    // A 10 mm setting therefore examines roughly 10 x 10 mm around each cell.
    const radiusX = Math.min(w - 1, Math.max(0, Math.floor(windowSize / (2 * cellX))));
    const windowZ = (windowMmZ === undefined || windowMmZ === null) ? windowSize : Math.max(0, Number(windowMmZ) || 0);
    const radiusY = Math.min(h - 1, Math.max(0, Math.floor(windowZ / (2 * cellZ))));
    if (!radiusX && !radiusY) {
      return {
        labels: out,
        changedPixels: 0,
        changedAreaMm2: 0,
        radiusX,
        radiusY,
        strategy: mode
      };
    }

    // Sliding categorical histogram: memory is O(width * palette), rather
    // than allocating one full integral image for every grayscale level.
    const columnCounts = new Uint32Array(w * levels);
    function addRow(y, delta) {
      if (y < 0 || y >= h)
        return;
      const row = y * w;
      for (let x = 0; x < w; x++) {
        const label = src[row + x];
        if (label < levels)
          columnCounts[x * levels + label] += delta;
      }
    }
    for (let y = 0; y <= Math.min(h - 1, radiusY); y++)
      addRow(y, 1);

    const histogram = new Uint32Array(levels);
    function addColumn(x, delta) {
      if (x < 0 || x >= w)
        return;
      const base = x * levels;
      for (let k = 0; k < levels; k++)
        histogram[k] += delta * columnCounts[base + k];
    }

    let changedPixels = 0;
    for (let y = 0; y < h; y++) {
      if (y > 0) {
        addRow(y - radiusY - 1, -1);
        addRow(y + radiusY, 1);
      }
      histogram.fill(0);
      for (let x = 0; x <= Math.min(w - 1, radiusX); x++)
        addColumn(x, 1);

      const rowTop = Math.max(0, y - radiusY);
      const rowBottom = Math.min(h - 1, y + radiusY);
      const rowCount = rowBottom - rowTop + 1;
      for (let x = 0; x < w; x++) {
        if (x > 0) {
          addColumn(x - radiusX - 1, -1);
          addColumn(x + radiusX, 1);
        }
        const at = y * w + x;
        const center = src[at] < levels ? src[at] : 0;
        let selected = center;
        if (mode === "mode") {
          let bestCount = histogram[center];
          for (let k = 0; k < levels; k++) {
            const count = histogram[k];
            const betterCount = count > bestCount;
            const closerTone = count === bestCount && Math.abs(k - center) < Math.abs(selected - center);
            if (betterCount || closerTone) {
              selected = k;
              bestCount = count;
            }
          }
        } else {
          const colLeft = Math.max(0, x - radiusX);
          const colRight = Math.min(w - 1, x + radiusX);
          const target = Math.floor(((colRight - colLeft + 1) * rowCount) / 2) + 1;
          let cumulative = 0;
          for (let k = 0; k < levels; k++) {
            cumulative += histogram[k];
            if (cumulative >= target) {
              selected = k;
              break;
            }
          }
        }
        out[at] = selected;
        if (selected !== src[at])
          changedPixels++;
      }
    }

    return {
      labels: out,
      changedPixels,
      changedAreaMm2: changedPixels * cellX * cellZ,
      radiusX,
      radiusY,
      strategy: mode
    };
  }

  function filterSmallComponents(labels, w, h, sx, sz, maxSizeMm, options) {
    const original = new Uint8Array(labels);
    let out = new Uint8Array(original);
    const threshold = Number(maxSizeMm);
    const cellX = Number(sx);
    const cellZ = Number(sz);
    const maxPasses = Math.max(1, Math.min(8, Number(options && options.maxPasses) || 4));
    const disabled = !Number.isFinite(threshold) || threshold <= 0 ||
      !Number.isFinite(cellX) || cellX <= 0 || !Number.isFinite(cellZ) || cellZ <= 0;

    if (!w || !h || original.length !== w * h || disabled) {
      return {
        labels: out,
        removedComponents: 0,
        changedPixels: 0,
        changedAreaMm2: 0,
        passes: 0,
        thresholdMm: Math.max(0, Number.isFinite(threshold) ? threshold : 0)
      };
    }

    let removedComponents = 0;
    let changedPasses = 0;
    const n = w * h;

    for (let pass = 0; pass < maxPasses; pass++) {
      const parent = new Int32Array(n);
      const rank = new Uint8Array(n);
      for (let i = 0; i < n; i++)
        parent[i] = i;

      function find(value) {
        let root = value;
        while (parent[root] !== root)
          root = parent[root];
        while (parent[value] !== value) {
          const next = parent[value];
          parent[value] = root;
          value = next;
        }
        return root;
      }
      function unite(a, b) {
        let ra = find(a);
        let rb = find(b);
        if (ra === rb)
          return;
        if (rank[ra] < rank[rb])
          [ra, rb] = [rb, ra];
        parent[rb] = ra;
        if (rank[ra] === rank[rb])
          rank[ra]++;
      }

      // Use four-neighbour components so a diagonal touch remains two islands,
      // exactly like the later mesh/export connectivity analysis.
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const at = y * w + x;
          if (x > 0 && out[at - 1] === out[at])
            unite(at, at - 1);
          if (y > 0 && out[at - w] === out[at])
            unite(at, at - w);
        }
      }
      for (let i = 0; i < n; i++)
        parent[i] = find(i);

      const counts = new Uint32Array(n);
      // GRID_MAX is 3200 — still well within 16-bit coordinates, which save substantial memory here.
      const minX = new Uint16Array(n);
      const maxX = new Uint16Array(n);
      const minY = new Uint16Array(n);
      const maxY = new Uint16Array(n);
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const root = parent[y * w + x];
          if (counts[root] === 0) {
            minX[root] = maxX[root] = x;
            minY[root] = maxY[root] = y;
          } else {
            if (x < minX[root]) minX[root] = x;
            if (x > maxX[root]) maxX[root] = x;
            if (y < minY[root]) minY[root] = y;
            if (y > maxY[root]) maxY[root] = y;
          }
          counts[root]++;
        }
      }

      const small = new Uint8Array(n);
      const epsilon = 1e-9;
      for (let i = 0; i < n; i++) {
        if (!counts[i])
          continue;
        const widthMm = (maxX[i] - minX[i] + 1) * cellX;
        const heightMm = (maxY[i] - minY[i] + 1) * cellZ;
        if (Math.max(widthMm, heightMm) <= threshold + epsilon)
          small[i] = 1;
      }

      // For each small island, count how much boundary it shares with each
      // adjacent major tone. A single numeric-key map avoids allocating one
      // nested Map per speck on noisy, high-resolution photographs.
      // Only major (over-threshold) neighbours are eligible, which prevents
      // small islands from swapping labels in cycles.
      const boundaryCounts = new Map();
      function addBoundary(smallRoot, majorRoot) {
        const key = smallRoot * 256 + out[majorRoot];
        boundaryCounts.set(key, (boundaryCounts.get(key) || 0) + 1);
      }
      function inspectBoundary(a, b) {
        const ra = parent[a];
        const rb = parent[b];
        if (ra === rb)
          return;
        if (small[ra] && !small[rb])
          addBoundary(ra, rb);
        if (small[rb] && !small[ra])
          addBoundary(rb, ra);
      }
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const at = y * w + x;
          if (x + 1 < w)
            inspectBoundary(at, at + 1);
          if (y + 1 < h)
            inspectBoundary(at, at + w);
        }
      }

      const noLabel = 256;
      const bestLabels = new Uint16Array(n);
      bestLabels.fill(noLabel);
      const bestBoundaries = new Uint32Array(n);
      for (const [key, boundary] of boundaryCounts) {
        const smallRoot = Math.floor(key / 256);
        const label = key - smallRoot * 256;
        const previous = bestLabels[smallRoot];
        const betterBoundary = boundary > bestBoundaries[smallRoot];
        const currentDelta = previous === noLabel ? Infinity : Math.abs(out[smallRoot] - previous);
        const nextDelta = Math.abs(out[smallRoot] - label);
        const betterTone = boundary === bestBoundaries[smallRoot] && nextDelta < currentDelta;
        const stableTie = boundary === bestBoundaries[smallRoot] && nextDelta === currentDelta && label < previous;
        if (previous === noLabel || betterBoundary || betterTone || stableTie) {
          bestLabels[smallRoot] = label;
          bestBoundaries[smallRoot] = boundary;
        }
      }

      let replacementCount = 0;
      for (let i = 0; i < n; i++) {
        if (counts[i] && bestLabels[i] !== noLabel)
          replacementCount++;
      }
      if (!replacementCount)
        break;
      const next = new Uint8Array(out);
      for (let i = 0; i < n; i++) {
        const replacement = bestLabels[parent[i]];
        if (replacement !== noLabel)
          next[i] = replacement;
      }
      out = next;
      removedComponents += replacementCount;
      changedPasses++;
    }

    let changedPixels = 0;
    for (let i = 0; i < n; i++) {
      if (out[i] !== original[i])
        changedPixels++;
    }
    return {
      labels: out,
      removedComponents,
      changedPixels,
      changedAreaMm2: changedPixels * cellX * cellZ,
      passes: changedPasses,
      thresholdMm: threshold
    };
  }

  function collectParts(labels, w, h, paletteSize) {
    const n = w * h;
    const parent = new Int32Array(n);
    for (let i = 0; i < n; i++)
      parent[i] = i;

    function find(value) {
      let root = value;
      while (parent[root] !== root)
        root = parent[root];
      while (parent[value] !== value) {
        const next = parent[value];
        parent[value] = root;
        value = next;
      }
      return root;
    }

    function unite(a, b) {
      const ra = find(a);
      const rb = find(b);
      if (ra !== rb)
        parent[rb] = ra;
    }

    // Four-neighbour connectivity is intentional: diagonal pixels that touch at
    // only one point must remain separate watertight shells.
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const at = y * w + x;
        if (x > 0 && labels[at - 1] === labels[at])
          unite(at, at - 1);
        if (y > 0 && labels[at - w] === labels[at])
          unite(at, at - w);
      }
    }
    for (let i = 0; i < n; i++)
      parent[i] = find(i);

    const runsByLabel = Array.from({ length: paletteSize }, () => []);
    const rootsByLabel = Array.from({ length: paletteSize }, () => new Set());
    let totalRuns = 0;
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w;) {
        const at = y * w + x;
        const k = labels[at];
        const component = parent[at];
        let x1 = x + 1;
        while (x1 < w && labels[y * w + x1] === k)
          x1++;
        if (k < paletteSize) {
          runsByLabel[k].push([y, x, x1, component]);
          rootsByLabel[k].add(component);
          totalRuns++;
        }
        x = x1;
      }
    }

    const parts = [];
    let components = 0;
    for (let k = 0; k < paletteSize; k++) {
      if (!runsByLabel[k].length)
        continue;
      parts.push({ k, runs: runsByLabel[k], components: rootsByLabel[k].size });
      components += rootsByLabel[k].size;
    }
    return { parts, totalRuns, components };
  }

  // seamNotch（Eric 2026-07-20 定「方塊中間做小V」）：{depth,width} mm——在零件「右端交界」
  // 的深度中央切一個內埋 V 溝（空腔，右鄰零件保持平面）：每層迴路多出一個尖銳凹角＝
  // 縫的天然磁鐵，縫藏進磚體正中央、正反面乾淨。只在交界垂直穩定段開溝（上下列同位
  // 交界才開），行間過渡用三角蓋片封死；外緣與過窄零件自動跳過。
  function buildLabelMesh(labels, w, h, k, runs, sx, sz, thickness, seamNotch) {
    const vertices = [];
    const triangles = [];
    const vertexIds = new Map();
    const componentIds = new Set();
    let tiles = 0;

    const round3 = value => Math.round(value * 1000) / 1000;
    const notchOn = !!(seamNotch && seamNotch.depth > 0 && seamNotch.width > 0 && thickness >= seamNotch.width * 2);
    const nD = notchOn ? seamNotch.depth : 0;                  // 溝深（往 -X 切入左零件）
    const nYA = notchOn ? round3(thickness / 2 - seamNotch.width / 2) : 0;
    const nYB = notchOn ? round3(thickness / 2 + seamNotch.width / 2) : 0;
    const nYM = round3(thickness / 2);
    // 交界在 (y,xe) 是否為本零件的右端邊界（xe 左側=k、右側=其他標籤）
    const boundaryAt = (yy, xe) => yy >= 0 && yy < h && xe > 0 && xe < w &&
      labels[yy * w + xe - 1] === k && labels[yy * w + xe] !== k;
    function sectorAtVertex(gx, gz, cellX, cellY) {
      if (!Number.isInteger(gx) || !Number.isInteger(gz))
        return "";
      const boundaryY = h - gz;
      const has = (x, y) => x >= 0 && x < w && y >= 0 && y < h && labels[y * w + x] === k;
      const mask =
        (has(gx - 1, boundaryY - 1) ? 1 : 0) |
        (has(gx, boundaryY - 1) ? 2 : 0) |
        (has(gx - 1, boundaryY) ? 4 : 0) |
        (has(gx, boundaryY) ? 8 : 0);
      if (mask !== 6 && mask !== 9)
        return "";
      const north = cellY < boundaryY;
      const west = cellX < gx;
      const quadrant = north ? (west ? 1 : 2) : (west ? 4 : 8);
      return `|q${quadrant}`;
    }
    function vertex(component, gx, side, gz, cellX, cellY) {
      // A checkerboard corner may contain the same label in two diagonal
      // quadrants. Give those sectors distinct vertices so the extruded shells
      // do not touch along a four-face, non-manifold depth edge.
      const key = `${component}|${gx}|${side}|${gz}${sectorAtVertex(gx,gz,cellX,cellY)}`;
      let id = vertexIds.get(key);
      if (id !== undefined)
        return id;
      id = vertices.length;
      vertexIds.set(key, id);
      vertices.push([round3(gx * sx), side ? thickness : 0, round3(gz * sz)]);
      return id;
    }
    function quad(a, b, c, d) {
      triangles.push([a, b, c], [a, c, d]);
    }

    function neighbourCuts(neighbourY, x0, x1) {
      const cuts = [x0];
      if (neighbourY >= 0 && neighbourY < h) {
        const base = neighbourY * w;
        for (let x = x0 + 1; x < x1; x++) {
          if (labels[base + x - 1] !== labels[base + x])
            cuts.push(x);
        }
      }
      cuts.push(x1);
      return cuts;
    }

    // 溝槽列判定（含鄰列一致性：本列與該列自己的 cuts 條件完全同式，供鄰列互查）
    const runX0 = (yy, xe) => { let x = xe - 1; while (x > 0 && labels[yy * w + x - 1] === k) x--; return x; };
    function rowNotched(yy, xe) {
      if (!notchOn || !boundaryAt(yy, xe) || !boundaryAt(yy - 1, xe) || !boundaryAt(yy + 1, xe))
        return false;
      const rx0 = runX0(yy, xe);
      if ((xe - rx0) * sx < nD + 1.6) return false;
      const apex = xe - nD / sx;
      const bc = neighbourCuts(yy + 1, rx0, xe);
      const tc = neighbourCuts(yy - 1, rx0, xe);
      const bXa = bc[bc.length - 2], tXa = tc[tc.length - 2];
      const bExp = !(yy + 1 < h && labels[(yy + 1) * w + bXa] === k);
      const tExp = !(yy > 0 && labels[(yy - 1) * w + tXa] === k);
      return (!bExp || bXa <= apex) && (!tExp || tXa <= apex);
    }

    for (const [y, x0, x1, component] of runs) {
      componentIds.add(component);
      const z0 = h - y - 1;
      const z1 = h - y;
      const v = (gx, side, gz) => {
        const cellX = Math.max(x0, Math.min(x1 - 1, Math.floor(gx)));
        return vertex(component, gx, side, gz, cellX, y);
      };
      const vn = (gx, ymm, gz) => {
        const key = `${component}|${round3(gx)}|n${ymm}|${gz}`;
        let id = vertexIds.get(key);
        if (id === undefined) {
          id = vertices.length; vertexIds.set(key, id);
          vertices.push([round3(gx * sx), ymm, round3(gz * sz)]);
        }
        return id;
      };
      const notched = rowNotched(y, x1);
      const apexGx = x1 - nD / sx;
      const bottomCuts = neighbourCuts(y + 1, x0, x1);
      const topCuts = neighbourCuts(y - 1, x0, x1);
      if (notched) {
        // apex 只在「該側末段外露（會出挖口蓋片）」時才進 cuts——內部過渡線兩側扇形
        // 頂點集必須一致，多插會造成 T 接點開放邊
        const bXa0 = bottomCuts[bottomCuts.length - 2];
        if (!(y + 1 < h && labels[(y + 1) * w + Math.floor(bXa0)] === k))
          bottomCuts.splice(bottomCuts.length - 1, 0, apexGx);
        const tXa0 = topCuts[topCuts.length - 2];
        if (!(y > 0 && labels[(y - 1) * w + Math.floor(tXa0)] === k))
          topCuts.splice(topCuts.length - 1, 0, apexGx);
      }
      const boundary = [];
      for (const x of bottomCuts)
        boundary.push([x, z0]);
      for (let i = topCuts.length - 1; i >= 0; i--)
        boundary.push([topCuts[i], z1]);

      // Each scanline run is a rectangle, but its top and bottom can have
      // different split points. A centre fan preserves every shared boundary
      // vertex without propagating cuts through the whole component.
      const cx = (x0 + x1) / 2;
      const cz = (z0 + z1) / 2;
      const frontCenter = v(cx, 0, cz);
      const backCenter = v(cx, 1, cz);
      for (let i = 0; i < boundary.length; i++) {
        const p = boundary[i];
        const q = boundary[(i + 1) % boundary.length];
        triangles.push([
          frontCenter,
          v(p[0], 0, p[1]),
          v(q[0], 0, q[1])
        ]);
        triangles.push([
          backCenter,
          v(q[0], 1, q[1]),
          v(p[0], 1, p[1])
        ]);
      }
      tiles++;

      quad(
        v(x0, 0, z0),
        v(x0, 0, z1),
        v(x0, 1, z1),
        v(x0, 1, z0)
      );
      if (notched) {
        // 右端＝V 溝面：上下平帶＋兩斜面（每層迴路多一個尖銳凹角＝縫的磁鐵）
        const pt = (gx, ym, gz) => ym === 0 ? v(x1, 0, gz) : (ym === thickness ? v(x1, 1, gz) : vn(gx, ym, gz));
        const PL = [[x1, 0], [x1, nYA], [apexGx, nYM], [x1, nYB], [x1, thickness]];
        for (let i = 0; i + 1 < PL.length; i++)
          quad(pt(PL[i][0], PL[i][1], z0), pt(PL[i + 1][0], PL[i + 1][1], z0),
               pt(PL[i + 1][0], PL[i + 1][1], z1), pt(PL[i][0], PL[i][1], z1));
        if (boundaryAt(y - 1, x1) && !rowNotched(y - 1, x1))   // 上鄰平端 → 空腔天花板（-z）
          triangles.push([vn(x1, nYA, z1), vn(apexGx, nYM, z1), vn(x1, nYB, z1)]);
        if (boundaryAt(y + 1, x1) && !rowNotched(y + 1, x1))   // 下鄰平端 → 空腔地板（+z）
          triangles.push([vn(x1, nYA, z0), vn(x1, nYB, z0), vn(apexGx, nYM, z0)]);
      } else {
        // 平端；鄰列若是溝槽列，該側 z 邊在 nYA/nYB 細分（與溝槽帶逐邊配對，保水密）
        const splitB = notchOn && rowNotched(y + 1, x1);
        const splitT = notchOn && rowNotched(y - 1, x1);
        if (!splitB && !splitT) {
          quad(v(x1, 0, z0), v(x1, 1, z0), v(x1, 1, z1), v(x1, 0, z1));
        } else {
          const eB = splitB ? [0, nYA, nYB, thickness] : [0, thickness];
          const eT = splitT ? [0, nYA, nYB, thickness] : [0, thickness];
          const poly = [];
          for (const ym of eB)
            poly.push(ym === 0 ? v(x1, 0, z0) : (ym === thickness ? v(x1, 1, z0) : vn(x1, ym, z0)));
          for (let i = eT.length - 1; i >= 0; i--) {
            const ym = eT[i];
            poly.push(ym === 0 ? v(x1, 0, z1) : (ym === thickness ? v(x1, 1, z1) : vn(x1, ym, z1)));
          }
          for (let i = 1; i + 1 < poly.length; i++)
            triangles.push([poly[0], poly[i], poly[i + 1]]);
        }
      }

      for (let i = 0; i + 1 < bottomCuts.length; i++) {
        const xa = bottomCuts[i];
        const xb = bottomCuts[i + 1];
        if (y + 1 < h && labels[(y + 1) * w + Math.floor(xa)] === k)
          continue;
        if (notched && xb === x1 && xa === apexGx) {
          // 溝槽列末段底面（-z）：兩帶＋兩三角（挖掉 V 缺口）
          quad(v(apexGx, 0, z0), vn(apexGx, nYA, z0), vn(x1, nYA, z0), v(x1, 0, z0));
          quad(vn(apexGx, nYB, z0), v(apexGx, 1, z0), v(x1, 1, z0), vn(x1, nYB, z0));
          triangles.push([vn(apexGx, nYA, z0), vn(apexGx, nYM, z0), vn(x1, nYA, z0)]);
          triangles.push([vn(apexGx, nYB, z0), vn(x1, nYB, z0), vn(apexGx, nYM, z0)]);
          continue;
        }
        quad(
          v(xa, 0, z0),
          v(xa, 1, z0),
          v(xb, 1, z0),
          v(xb, 0, z0)
        );
      }
      for (let i = 0; i + 1 < topCuts.length; i++) {
        const xa = topCuts[i];
        const xb = topCuts[i + 1];
        if (y > 0 && labels[(y - 1) * w + Math.floor(xa)] === k)
          continue;
        if (notched && xb === x1 && xa === apexGx) {
          // 溝槽列末段頂面（+z）：兩帶＋兩三角
          quad(v(apexGx, 0, z1), v(x1, 0, z1), vn(x1, nYA, z1), vn(apexGx, nYA, z1));
          quad(vn(apexGx, nYB, z1), vn(x1, nYB, z1), v(x1, 1, z1), v(apexGx, 1, z1));
          triangles.push([vn(apexGx, nYA, z1), vn(x1, nYA, z1), vn(apexGx, nYM, z1)]);
          triangles.push([vn(apexGx, nYB, z1), vn(apexGx, nYM, z1), vn(x1, nYB, z1)]);
          continue;
        }
        quad(
          v(xa, 0, z1),
          v(xb, 0, z1),
          v(xb, 1, z1),
          v(xa, 1, z1)
        );
      }
    }

    return {
      vertices,
      triangles,
      tiles,
      components: componentIds.size
    };
  }

  function auditMesh(mesh) {
    const edgeCounts = new Map();
    const parent = new Int32Array(mesh.vertices.length);
    for (let i = 0; i < parent.length; i++)
      parent[i] = i;
    function find(value) {
      while (parent[value] !== value) {
        parent[value] = parent[parent[value]];
        value = parent[value];
      }
      return value;
    }
    function unite(a, b) {
      const ra = find(a);
      const rb = find(b);
      if (ra !== rb)
        parent[rb] = ra;
    }
    function edge(a, b) {
      if (a > b)
        [a, b] = [b, a];
      const key = `${a}|${b}`;
      edgeCounts.set(key, (edgeCounts.get(key) || 0) + 1);
    }

    let signedVolume = 0;
    for (const [a, b, c] of mesh.triangles) {
      edge(a, b);
      edge(b, c);
      edge(c, a);
      unite(a, b);
      unite(b, c);
      const A = mesh.vertices[a];
      const B = mesh.vertices[b];
      const C = mesh.vertices[c];
      signedVolume += (
        A[0] * (B[1] * C[2] - B[2] * C[1]) -
        A[1] * (B[0] * C[2] - B[2] * C[0]) +
        A[2] * (B[0] * C[1] - B[1] * C[0])
      ) / 6;
    }

    let boundaryEdges = 0;
    let nonManifoldEdges = 0;
    for (const count of edgeCounts.values()) {
      if (count === 1)
        boundaryEdges++;
      else if (count !== 2)
        nonManifoldEdges++;
    }
    const roots = new Set();
    for (let i = 0; i < parent.length; i++)
      roots.add(find(i));
    return {
      vertices: mesh.vertices.length,
      triangles: mesh.triangles.length,
      boundaryEdges,
      nonManifoldEdges,
      components: roots.size,
      signedVolume
    };
  }

  // 水平最小可印寬（Eric 2026-07-19 定，縫隙根因）：零件的水平剖面窄於噴頭「進得去、
  // 繞得出來」的寬度（≈2×口徑）就印不出來＝白縫。垂直方向靠層高離散、不受此限，
  // 所以這裡只逐列處理水平向：短於 minCells 的水平連續段併入左右鄰段。
  // 連結串列合併、合併後回頭重驗，O(n) 攤還；同標籤相鄰段自動收斂。
  //
  // 併哪一邊（#50 甲，Eric 2026-10-01 裁）：兩邊都有鄰段時併進「顏色最近」的那邊，一樣近才比長度
  // （＝舊判準：併進較長的、一樣長併左）；只有一邊就併那邊。舊判準只比長度——8 階時主體被切成
  // 好幾條各窄於門檻的細帶，每條都輸給更長的背景、背景越吃越長 ⇒ 整截主體不見
  // （#45 實測：杜賓 60×90 8 階缺塊高 15.2 mm，改判準後 1.3 mm）。
  // tone（選填）＝標籤距離表，tone[a][b]＝標籤 a 與 b 有多不像（只比大小）。
  // 沒給＝序號差 |a−b|：雙料的標籤是明度色階序號，序號差就是明暗差。四料的標籤是調色盤索引
  // （類別、不是順序：序號相鄰的兩色可能是不同色相）⇒ 由引擎給調色盤色差表（engine.js filterLabels）。
  // toneFlips＝新判準跟「只比長度」選了不同邊的次數（進 3MF 統計＝分得出新舊判準）。
  // AIP 刀 1（2026-10-03，第二輪 Q10）：同一支跑兩個方向——一條「線」＝左右的一列或上下的一欄，
  // 第 x 格在 base＋x×step（左右：base＝y×w、step＝1；上下：base＝x、step＝w）。左右的行為與抽出前逐格相同
  // （phototile_filter_test.js 第 4 節拿 #50 前的逐字副本比 2,000 列）。
  function enforceMinRuns(labels, lines, count, lineStride, step, minCells, tone) {
    const out = new Uint8Array(labels);
    let changed = 0, mergedRuns = 0, toneFlips = 0;
    if (!(minCells > 1)) return { labels: out, changedPixels: 0, mergedRuns: 0, toneFlips: 0 };
    const start = new Int32Array(count), len = new Int32Array(count), lab = new Int32Array(count);
    const prev = new Int32Array(count), next = new Int32Array(count);
    for (let line = 0; line < lines; line++) {
      const row = line * lineStride;
      let n = 0;
      for (let x = 0; x < count;) {
        const v = out[row + x * step];
        let x2 = x + 1;
        while (x2 < count && out[row + x2 * step] === v) x2++;
        start[n] = x; len[n] = x2 - x; lab[n] = v;
        prev[n] = n - 1; next[n] = (x2 < count) ? n + 1 : -1;
        n++; x = x2;
      }
      if (n <= 1) continue;
      let head = 0, i = 0;
      while (i !== -1) {
        const p = prev[i], q = next[i];
        if (len[i] >= minCells || (p === -1 && q === -1)) { i = q; continue; }
        const pl = (p === -1) ? -1 : len[p];
        const ql = (q === -1) ? -1 : len[q];
        mergedRuns++;
        let goLeft = pl >= ql;                 // 只有一邊、或兩邊一樣近：比長度（一樣長併左）
        if (p !== -1 && q !== -1) {
          const a = lab[i];
          const dp = tone ? tone[a][lab[p]] : Math.abs(lab[p] - a);
          const dq = tone ? tone[a][lab[q]] : Math.abs(lab[q] - a);
          if (dp !== dq) {
            if ((dp < dq) !== goLeft) toneFlips++;
            goLeft = dp < dq;
          }
        }
        if (goLeft) {                          // 併入左段（含只剩左段）
          len[p] += len[i];
          next[p] = q; if (q !== -1) prev[q] = p;
          if (q !== -1 && lab[q] === lab[p]) { // 左右同標籤 → 一起收斂
            len[p] += len[q];
            next[p] = next[q]; if (next[q] !== -1) prev[next[q]] = p;
          }
          i = p;                               // 回頭重驗合併後的段
        } else {                               // 併入右段
          start[q] = start[i]; len[q] += len[i];
          prev[q] = p; if (p !== -1) next[p] = q;
          if (p !== -1 && lab[p] === lab[q]) {
            start[q] = start[p]; len[q] += len[p];
            prev[q] = prev[p]; if (prev[p] !== -1) next[prev[p]] = q;
          }
          if (prev[q] === -1) head = q;        // 頭段被併走 → 新頭
          i = (prev[q] !== -1) ? prev[q] : q;
        }
      }
      // 依存活串列重寫該列
      for (let r = head; r !== -1; r = next[r]) {
        const v = lab[r], x0 = start[r], x1 = start[r] + len[r];
        for (let x = x0; x < x1; x++) {
          const at = row + x * step;
          if (out[at] !== v) { out[at] = v; changed++; }
        }
      }
    }
    return { labels: out, changedPixels: changed, mergedRuns, toneFlips };
  }
  function enforceMinHorizontalWidth(labels, w, h, minCells, tone) {
    return enforceMinRuns(labels, h, w, w, 1, minCells, tone);
  }
  // 上下（Z＝層疊方向）：格點的「列」由上往下排 ⇒「併左」＝併進上面那段。判準同左右（顏色最近，平手比長度）。
  function enforceMinVerticalHeight(labels, w, h, minCells, tone) {
    return enforceMinRuns(labels, w, h, 1, w, minCells, tone);
  }

  /* ── 2-D 最小特徵開運算（Eric 2026-08-22 裁「換」；原型出自開發線 engine.js 2026-08-15）──
     `enforceMinHorizontalWidth` 只守水平、`filterSmallComponents` 只殺孤島，兩者都處理不了
     「附著在大塊上的細長突起」（垂直與斜向）。這支用矩形結構元素做開運算：
     先找出「以自己為中心、wx×wy 視窗內全同標籤」的核心格，再從核心 BFS 回填其餘格。

     ⚠ **這一步會丟掉物理上印得出來的垂直細節**（垂直方向靠層高離散、不受口徑限制）。
       它是**風格取捨**不是可印性修復——Eric 2026-08-22 看過 A/B 數據後裁定要換：
       咬痕 22→3、邊界總長 −13.1%；代價就是垂直細節。只約束水平的版本實測等於什麼都沒做。

     🔴 **呼叫端必須在本步之後再跑一次 `enforceMinHorizontalWidth`**：BFS 回填會重新製造
       短於最小寬的水平段。實測（Codex 反審指出、本專案親驗）：開運算前違規 0 段，
       開運算後 57 段、最短 1 格（門檻 16 格）。不補修復＝白縫回來。

     🔴 **偶數 kernel 取上界**：視窗恆為 2r+1（奇數），r 取 ceil((w-1)/2) ⇒ 實際視窗 ≥ 要求值。
       原版用 `(w-1)>>1` 會少一格（wx=16 實際只有 15；3×3 與 4×4 結果完全相同）。
       最小寬是**硬約束**，寧可多開一格也不能少。 */
  function openLabelsMinWidth(labels, w, h, wx, wy) {
    const n = w * h;
    const rx = Math.ceil((Math.max(1, wx) - 1) / 2);
    const ry = Math.ceil((Math.max(1, wy) - 1) / 2);
    if (rx < 1 && ry < 1)
      return { labels: new labels.constructor(labels), openedAway: 0, degenerate: false };
    const W1 = w + 1, IH = new Int32Array(W1 * (h + 1)), IV = new Int32Array(W1 * (h + 1));
    for (let y = 0; y < h; y++) {
      const r = y * w, o = (y + 1) * W1, o0 = y * W1;
      let rh = 0, rv = 0;
      for (let x = 0; x < w; x++) {
        const p = r + x;
        if (x < w - 1 && labels[p] !== labels[p + 1]) rh++;
        if (y < h - 1 && labels[p] !== labels[p + w]) rv++;
        IH[o + x + 1] = IH[o0 + x + 1] + rh; IV[o + x + 1] = IV[o0 + x + 1] + rv;
      }
    }
    const q = (I, x0, y0, x1, y1) => (x1 < x0 || y1 < y0) ? 0 :
      I[(y1 + 1) * W1 + x1 + 1] - I[y0 * W1 + x1 + 1] - I[(y1 + 1) * W1 + x0] + I[y0 * W1 + x0];
    const uni = new Uint8Array(n);
    for (let y = 0; y < h; y++) {
      const y0 = Math.max(0, y - ry), y1 = Math.min(h - 1, y + ry), r = y * w;
      for (let x = 0; x < w; x++) {
        const x0 = Math.max(0, x - rx), x1 = Math.min(w - 1, x + rx);
        if (q(IH, x0, y0, x1 - 1, y1) === 0 && q(IV, x0, y0, x1, y1 - 1) === 0) uni[r + x] = 1;
      }
    }
    IH.fill(0);
    for (let y = 0; y < h; y++) {
      const r = y * w, o = (y + 1) * W1, o0 = y * W1;
      let row = 0;
      for (let x = 0; x < w; x++) { row += uni[r + x]; IH[o + x + 1] = IH[o0 + x + 1] + row; }
    }
    const out = new labels.constructor(labels), assigned = new Uint8Array(n);
    const queue = new Int32Array(n);
    let qt = 0;
    for (let y = 0; y < h; y++) {
      const y0 = Math.max(0, y - ry), y1 = Math.min(h - 1, y + ry), r = y * w;
      for (let x = 0; x < w; x++) {
        const x0 = Math.max(0, x - rx), x1 = Math.min(w - 1, x + rx);
        if (q(IH, x0, y0, x1, y1) > 0) { assigned[r + x] = 1; queue[qt++] = r + x; }
      }
    }
    /* 🔴 一格核心都沒有＝門檻對這張圖不合理。原版在這裡「原樣退回且把 openedAway 報成 0」
       ⇒ 診斷看起來像「沒問題」，其實整步失效。改成顯性回報 degenerate，呼叫端才判得出來。 */
    if (qt === 0)
      return { labels: out, openedAway: 0, degenerate: true };
    /* 🔴 種子數要在 BFS **之前**取——迴圈裡 qt 會一路長到全部格子都被指派，
       事後再算 n-qt 恆為 0（2026-08-22 移植時我自己踩到，實測 9,410 格被改卻報 openedAway=0）。 */
    const seeded = qt;
    let qh = 0;
    while (qh < qt) {
      const p = queue[qh++], c = p % w;
      if (c > 0     && !assigned[p - 1]) { assigned[p - 1] = 1; out[p - 1] = out[p]; queue[qt++] = p - 1; }
      if (c < w - 1 && !assigned[p + 1]) { assigned[p + 1] = 1; out[p + 1] = out[p]; queue[qt++] = p + 1; }
      if (p >= w    && !assigned[p - w]) { assigned[p - w] = 1; out[p - w] = out[p]; queue[qt++] = p - w; }
      if (p + w < n && !assigned[p + w]) { assigned[p + w] = 1; out[p + w] = out[p]; queue[qt++] = p + w; }
    }
    return { labels: out, openedAway: n - seeded, degenerate: false };
  }

  /* 可印性斷言：數出「違反最小水平寬」的段數。貼邊段不計（它們被影像邊界截斷，不是真的細條）。
     用途＝濾除鏈跑完後自我檢查；非 0 就是有人把保證弄壞了，要吵不要靜默。 */
  /* 把上下方向量化到「層」（AIP 刀 1 甲案的最後一步）：從底面（最後一列＝熱床，z＝0）往上每 bandRows 列一條帶，
     帶內每一欄取出現最多的標籤（平手取靠下面的），整條帶修一次左右最小寬，再寫回帶內每一列。
     ⇒ 每一欄的上下段都是整條帶的倍數（≥ 一層）、每一列的左右段都 ≥ minCells，兩個方向同時成立。
     為什麼不用「左右、上下輪流修」：實測不收斂——一列高的細舌頭夾在兩色之間，修上下把它併掉、修左右又把它長回來，
     中年男 8 階輪流 8 輪還剩 540 段。帶寬＝一層，等於照機台真正的層去量化（印出來本來就只有整層）。
     最上面那條帶不滿 bandRows 時照樣處理；它貼頂，不算違規（同貼邊段）。 */
  function snapRowsToBands(labels, w, h, bandRows, minCells, tone) {
    const out = new Uint8Array(labels);
    const b = Math.max(1, Math.floor(bandRows) || 1);
    const row = new Uint8Array(w), counts = new Uint32Array(256);
    let changed = 0, bands = 0;
    for (let y1 = h; y1 > 0; y1 -= b) {               // 這條帶＝列 [y0, y1)
      const y0 = Math.max(0, y1 - b);
      bands++;
      for (let x = 0; x < w; x++) {
        let best = labels[(y1 - 1) * w + x], bestCount = 0;
        for (let y = y1 - 1; y >= y0; y--) counts[labels[y * w + x]]++;
        for (let y = y1 - 1; y >= y0; y--) {
          const v = labels[y * w + x];
          if (counts[v] > bestCount) { best = v; bestCount = counts[v]; }
        }
        for (let y = y0; y < y1; y++) counts[labels[y * w + x]] = 0;
        row[x] = best;
      }
      const fixed = enforceMinRuns(row, 1, w, w, 1, minCells, tone).labels;
      for (let y = y0; y < y1; y++) {
        const o = y * w;
        for (let x = 0; x < w; x++) if (out[o + x] !== fixed[x]) { out[o + x] = fixed[x]; changed++; }
      }
    }
    return { labels: out, changedPixels: changed, bands, bandRows: b };
  }

  function countMinRunViolations(labels, lines, count, lineStride, step, minCells) {
    if (!(minCells > 1)) return 0;
    let bad = 0;
    for (let line = 0; line < lines; line++) {
      const o = line * lineStride;
      let x = 0;
      while (x < count) {
        const v = labels[o + x * step];
        let x2 = x + 1;
        while (x2 < count && labels[o + x2 * step] === v) x2++;
        if (x2 - x < minCells && x !== 0 && x2 !== count) bad++;
        x = x2;
      }
    }
    return bad;
  }
  function countMinWidthViolations(labels, w, h, minCells) {
    return countMinRunViolations(labels, h, w, w, 1, minCells);
  }
  // 上下：貼頂、貼底的段不計（同左右的貼邊段）
  function countMinHeightViolations(labels, w, h, minCells) {
    return countMinRunViolations(labels, w, h, 1, w, minCells);
  }

  return { cleanIsolated, smoothLabelNoise, filterSmallComponents, collectParts, buildLabelMesh, auditMesh,
    enforceMinHorizontalWidth, enforceMinVerticalHeight, snapRowsToBands, openLabelsMinWidth, countMinWidthViolations, countMinHeightViolations };
});
