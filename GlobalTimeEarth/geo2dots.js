#!/usr/bin/env node
// geo2dots.js —— PC 端 geojson 降采样工具（不在手表上跑）
// 用法：
//   node geo2dots.js countries.geojson all                      ← 一次出全部 6 份（推荐）
//   node geo2dots.js countries.geojson global [step=2] [max=250]
//   node geo2dots.js countries.geojson country <NAME或中文> [max=120]
// 输出：dots_all.js（或单份 <CONST>.js），const 为 flat 数组 [lat,lon,lat,lon,...]
const fs = require('fs');
const [,, file, mode, ...rest] = process.argv;
if (!file || !mode) { console.log('用法见注释'); process.exit(1); }
const data = JSON.parse(fs.readFileSync(file, 'utf8'));
const feats = data.features || [];

function ringsOf(g) {
  if (!g) return [];
  if (g.type === 'Polygon') return g.coordinates || [];
  if (g.type === 'MultiPolygon') return (g.coordinates || []).flat();
  if (g.type === 'GeometryCollection') return (g.geometries || []).flatMap(ringsOf);
  return [];
}
function nameOf(p) {
  return ['NAME','NAME_ZH','ADMIN','name','name_zh','ADM0_EN','BRK_NAME','SUBUNIT','NAME_LONG']
    .map(k => p && p[k]).filter(v => typeof v === 'string').join('|');
}
// 全球粗点：量化 step° 去重，再按空间排序均匀抽稀到 max
function buildGlobal(step, max) {
  const set = new Set();
  for (const f of feats) for (const ring of ringsOf(f.geometry)) {
    const k = Math.max(1, Math.floor(ring.length / 200));
    for (let i = 0; i < ring.length; i += k) {
      set.add(Math.round(ring[i][1] / step) * step + ',' + Math.round(ring[i][0] / step) * step);
    }
  }
  let arr = [...set].map(s => s.split(',').map(Number));
  arr.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  if (arr.length > max) {
    const out = [], n = arr.length / max;
    for (let i = 0; i < max; i++) out.push(arr[Math.floor(i * n)]);
    arr = out;
  }
  return { out: arr, matched: feats.length, total: set.size };
}
// 国家细点：全环均匀采样（等距 step=N/max，去重 round 2 位）→ 海岸线连贯
function buildCountry(match, max) {
  const m = match.toLowerCase();
  const picked = feats.filter(f => nameOf(f.properties).toLowerCase().includes(m));
  const allRings = picked.flatMap(f => ringsOf(f.geometry));
  const allPts = allRings.flat().map(c => [c[1], c[0]]);          // [lat, lon]
  const total = allPts.length;
  const step = Math.max(1, Math.floor(total / max));              // 全环均匀 step
  const out = [], seen = new Set();
  for (let i = 0; i < total; i += step) {
    const lat = Math.round(allPts[i][0] * 100) / 100, lon = Math.round(allPts[i][1] * 100) / 100;
    if (!seen.has(lat + ',' + lon)) { seen.add(lat + ',' + lon); out.push([lat, lon]); }
  }
  return { out: out.slice(0, max), matched: picked.length, total };
}
function flat(pairs) { return pairs.flatMap(p => p); }
function emit(name, r) {
  console.log(`${name}: matched=${r.matched} raw=${r.total} out=${r.out.length}`);
  if (!r.out.length) console.log('  ⚠ 0 点！可用 NAME 示例：',
    feats.slice(0, 8).map(f => nameOf(f.properties).split('|')[0]).join(' / '));
  return `const ${name}=[${flat(r.out).join(',')}];`;
}

const ALL = [
  ['DOTS_GLOBAL', () => buildGlobal(+rest[0] || 2, +rest[1] || 250)],
  ['DOTS_HK', () => buildCountry(rest[0] || 'Hong Kong', +rest[1] || 120)],
];
let lines = [];
if (mode === 'all') {
  lines.push(emit('DOTS_GLOBAL', buildGlobal(2, 250)));
  for (const [n, m] of [['DOTS_HK','Hong Kong'],['DOTS_CN','China'],['DOTS_JP','Japan'],
                        ['DOTS_FR','France'],['DOTS_US','United States']])
    lines.push(emit(n, buildCountry(m, 120)));
  fs.writeFileSync('dots_all.js', lines.join('\n') + '\n');
  console.log('✅ 已写 dots_all.js');
} else if (mode === 'global') {
  fs.writeFileSync((rest[2] || 'DOTS_GLOBAL') + '.js', emit(rest[2] || 'DOTS_GLOBAL', buildGlobal(+rest[0] || 2, +rest[1] || 250)));
} else if (mode === 'country') {
  const nm = rest[1] || ('DOTS_' + rest[0].replace(/\W+/g, '').toUpperCase());
  fs.writeFileSync(nm + '.js', emit(nm, buildCountry(rest[0], +rest[1] || 120)));
}