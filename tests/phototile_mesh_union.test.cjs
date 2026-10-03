"use strict";

const assert = require("node:assert/strict");
const {
  cleanIsolated,
  smoothLabelNoise,
  collectParts,
  buildLabelMesh,
  auditMesh
} = require("../resources/web/phototile/mesh_union.js");

function flatten(rows) {
  return new Uint8Array(rows.flat());
}

function verifyMask(name, rows, paletteSize, expectedComponents) {
  const h = rows.length;
  const w = rows[0].length;
  const labels = flatten(rows);
  const collected = collectParts(labels, w, h, paletteSize);
  assert.equal(collected.components, expectedComponents.reduce((a, b) => a + b, 0), `${name}: total components`);

  for (let k = 0; k < paletteSize; k++) {
    const part = collected.parts.find(item => item.k === k);
    const cells = Array.from(labels).filter(value => value === k).length;
    if (!cells) {
      assert.equal(part, undefined, `${name}: unused label ${k}`);
      continue;
    }
    assert.ok(part, `${name}: missing label ${k}`);
    const mesh = buildLabelMesh(labels, w, h, k, part.runs, 0.1, 0.1, 6);
    const audit = auditMesh(mesh);
    assert.equal(audit.boundaryEdges, 0, `${name}: label ${k} has open edges`);
    assert.equal(audit.nonManifoldEdges, 0, `${name}: label ${k} has non-manifold edges`);
    assert.equal(audit.components, expectedComponents[k], `${name}: label ${k} shell count`);
    assert.ok(audit.signedVolume > 0, `${name}: label ${k} winding`);
    assert.ok(Math.abs(audit.signedVolume - cells * 0.1 * 0.1 * 6) < 1e-8, `${name}: label ${k} volume`);
  }
}

verifyMask("solid", [
  [0, 0],
  [0, 0]
], 1, [1]);

verifyMask("L shape", [
  [0, 1],
  [0, 0]
], 2, [1, 1]);

verifyMask("hole", [
  [0, 0, 0],
  [0, 1, 0],
  [0, 0, 0]
], 2, [1, 1]);

verifyMask("diagonal islands", [
  [0, 1],
  [1, 0]
], 2, [2, 2]);

verifyMask("three labels", [
  [0, 0, 1, 1],
  [0, 1, 1, 1],
  [2, 2, 1, 0]
], 3, [2, 1, 1]);

const isolated = flatten([
  [0, 0, 0],
  [0, 1, 0],
  [0, 0, 0]
]);
assert.deepEqual(Array.from(cleanIsolated(isolated, 3, 3)), new Array(9).fill(0), "isolated pixel cleanup");

const bridge = flatten([
  [0, 0, 0, 0, 0],
  [1, 1, 1, 1, 1],
  [0, 0, 0, 0, 0]
]);
assert.deepEqual(Array.from(cleanIsolated(bridge, 5, 3)), Array.from(bridge), "one-pixel bridge must be preserved");

const bridgeNoiseRows = Array.from({ length: 21 }, () => new Array(21).fill(0));
for (let y = 6; y <= 14; y++) for (let x = 0; x <= 6; x++) bridgeNoiseRows[y][x] = 1;
for (let x = 7; x <= 17; x++) bridgeNoiseRows[10][x] = 1; // thin bridge makes this one large connected component
const bridgeNoise = smoothLabelNoise(flatten(bridgeNoiseRows), 21, 21, 2, 0.1, 0.1, 0.5, "median");
assert.equal(bridgeNoise.labels[10 * 21 + 17], 0, "median smoothing removes a speck joined to a major region by a thin bridge");
assert.equal(bridgeNoise.labels[10 * 21 + 3], 1, "median smoothing preserves the major region");

const pinholeRows = Array.from({ length: 9 }, () => new Array(9).fill(1));
pinholeRows[4][4] = 0;
const pinhole = smoothLabelNoise(flatten(pinholeRows), 9, 9, 2, 0.1, 0.1, 0.5, "median");
assert.equal(pinhole.labels[4 * 9 + 4], 1, "median smoothing fills a small pinhole");

const smoothingDisabledSource = flatten([
  [0, 0, 0],
  [0, 1, 0],
  [0, 0, 0]
]);
const smoothingDisabled = smoothLabelNoise(smoothingDisabledSource, 3, 3, 2, 0.1, 0.1, 0, "median");
assert.deepEqual(Array.from(smoothingDisabled.labels), Array.from(smoothingDisabledSource), "zero disables label smoothing");

let seed = 0x5eed1234;
function random() {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  return seed / 0x100000000;
}
for (let sample = 0; sample < 40; sample++) {
  const w = 3 + (sample % 6);
  const h = 3 + (sample % 5);
  const labels = new Uint8Array(w * h);
  for (let i = 0; i < labels.length; i++)
    labels[i] = Math.floor(random() * 3);
  const collected = collectParts(labels, w, h, 3);
  for (const part of collected.parts) {
    const mesh = buildLabelMesh(labels, w, h, part.k, part.runs, 0.1, 0.1, 6);
    const audit = auditMesh(mesh);
    const cells = Array.from(labels).filter(value => value === part.k).length;
    assert.equal(audit.boundaryEdges, 0, `random ${sample}: label ${part.k} open edges`);
    assert.equal(audit.nonManifoldEdges, 0, `random ${sample}: label ${part.k} non-manifold edges`);
    assert.equal(audit.components, part.components, `random ${sample}: label ${part.k} components`);
    assert.ok(Math.abs(audit.signedVolume - cells * 0.1 * 0.1 * 6) < 1e-8, `random ${sample}: label ${part.k} volume`);
  }
}

console.log("phototile mesh union tests: PASS");
