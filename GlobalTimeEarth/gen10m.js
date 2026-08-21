const fs = require('fs');
const data = JSON.parse(fs.readFileSync('ne_10m.geojson', 'utf8'));
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

function buildGlobal(step, max) {
  const set = new Set();
  for (const f of feats) for (const ring of ringsOf(f.geometry)) {
    const k = Math.max(1, Math.floor(ring.length / 300));
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
  return arr;
}

function buildCountry(match, max) {
  const m = match.toLowerCase();
  const picked = feats.filter(f => nameOf(f.properties).toLowerCase().includes(m));
  // 全环均匀采样（step=N/max）：所有环点拼接后等距取，疏密一致（修复逐环 ceil 不均）
  const allPts = picked.flatMap(f => ringsOf(f.geometry)).flat().map(c => [c[1], c[0]]);
  const total = allPts.length;
  const step = Math.max(1, Math.floor(total / max));
  const out = [], seen = new Set();
  for (let i = 0; i < total; i += step) {
    const lat = Math.round(allPts[i][0] * 100) / 100, lon = Math.round(allPts[i][1] * 100) / 100;
    if (!seen.has(lat + ',' + lon)) { seen.add(lat + ',' + lon); out.push([lat, lon]); }
  }
  return out.slice(0, max);
}

function flat(pairs) { return pairs.flatMap(p => p); }

const GLOBAL = buildGlobal(2, 400); // 全球加密到点池上限
const HK = buildCountry('Hong Kong', 400); // 香港特写拉满
const CN = buildCountry('China', 300);
const JP = buildCountry('Japan', 300);
const FR = buildCountry('France', 300);
// 北美合并：US（含阿拉斯加）+ 加拿大 → 特写完整北美，阿拉斯加/加美边界可见
const US = [...buildCountry('United States', 200), ...buildCountry('Canada', 100)];

console.log(`点数统计 -> GLOBAL:${GLOBAL.length} HK:${HK.length} CN:${CN.length} JP:${JP.length} FR:${FR.length} US:${US.length}`);

const js = `
const DOTS_GLOBAL = [${flat(GLOBAL).join(',')}];
const DOTS_HK = [${flat(HK).join(',')}];
const DOTS_CN = [${flat(CN).join(',')}];
const DOTS_JP = [${flat(JP).join(',')}];
const DOTS_FR = [${flat(FR).join(',')}];
const DOTS_US = [${flat(US).join(',')}];
`;
fs.writeFileSync('dots_all_10m.js', js);
console.log('✅ 已生成 dots_all_10m.js');