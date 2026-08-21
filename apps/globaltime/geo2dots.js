// geo2dots.js — geojson 离线降采样 → 点阵常量（环球时间 app 用）
// 用法（在装有 node 的 PC 上跑，输出 JS 数组贴回 main.js）：
//   node geo2dots.js <geojson> global 2 250
//       → 全球点阵：量化 2° 去重，≤250 点 → DOTS_GLOBAL
//   node geo2dots.js <geojson> country "Hong Kong" 0.01 120
//       → 国家/地区细节：properties.NAME / NAME_ZH 包含匹配，步长抽样，坐标 round 2 位 → DOTS_HK 等
// 输出：stdout 打印 count 与 JS 数组（[lat, lon] 对）。
'use strict';

const fs = require('fs');

const args = process.argv.slice(2);
if (args.length < 3) {
    console.error('usage: node geo2dots.js <geojson> global <stepDeg> <limit> | country "<name>" <stepDeg> <limit>');
    process.exit(1);
}
const [geojsonPath, mode] = args;

const geo = JSON.parse(fs.readFileSync(geojsonPath, 'utf8'));
const features = geo.features || [];

// 提取一个 feature 的所有环点 [lat, lon]
function ringPoints(f) {
    const out = [];
    const g = f.geometry;
    if (!g) return out;
    if (g.type === 'Polygon') {
        for (const ring of g.coordinates)
            for (const c of ring) out.push([c[1], c[0]]);
    } else if (g.type === 'MultiPolygon') {
        for (const poly of g.coordinates)
            for (const ring of poly)
                for (const c of ring) out.push([c[1], c[0]]);
    }
    return out;
}

let dots = [];
if (mode === 'global') {
    const stepDeg = parseFloat(args[2]);
    const limit = parseInt(args[3], 10);
    const seen = new Set();
    for (const f of features) {
        for (const [lat, lon] of ringPoints(f)) {
            if (seen.size >= limit) break;
            const qLat = Math.round(lat / stepDeg) * stepDeg;
            const qLon = ((Math.round(lon / stepDeg) * stepDeg) % 360 + 360) % 360;
            const key = qLat + ',' + qLon;
            if (!seen.has(key)) { seen.add(key); dots.push([qLat, qLon]); }
        }
        if (seen.size >= limit) break;
    }
} else if (mode === 'country') {
    const name = args[2];
    const stepDeg = parseFloat(args[3]);
    const limit = parseInt(args[4], 10);
    for (const f of features) {
        const props = f.properties || {};
        const nm = (props.NAME || props.NAME_ZH || props.name || '') + '|' + (props.NAME_ZH || '');
        if (nm.indexOf(name) < 0) continue;           // properties 包含匹配
        const pts = ringPoints(f);
        const step = Math.max(1, Math.floor(pts.length / limit));
        let n = 0;
        for (let i = 0; i < pts.length && n < limit; i += step) {
            const [lat, lon] = pts[i];
            const rLat = Math.round(lat / stepDeg) * stepDeg;
            const rLon = Math.round(lon / stepDeg) * stepDeg;
            dots.push([Math.round(rLat * 100) / 100, Math.round(rLon * 100) / 100]);
            n++;
        }
    }
} else {
    console.error('mode must be global | country');
    process.exit(1);
}

console.log('// count=' + dots.length);
console.log('// 贴回 main.js：swapDots(dots) 中填入该数组');
console.log(JSON.stringify(dots));
