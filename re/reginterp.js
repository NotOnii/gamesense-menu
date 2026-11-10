// Config-var registration interpreter: linear sweep of regions from LinDis, records node hash/type/size/default.
const fs = require("fs");
const [disFile, binFile, outFile] = process.argv.slice(2);
const bin = fs.readFileSync(binFile); const BASE = 0x43310000;
const rd32 = a => bin.readUInt32LE(a - BASE);
const regions = []; let cur = null;
for (const line of fs.readFileSync(disFile, "utf8").split(/\r?\n/)) {
  if (line.startsWith("==== region")) { cur = []; regions.push(cur); continue; }
  const m = line.match(/^([0-9a-f]+)\s+(.*)$/); if (!m || !cur) continue;
  cur.push({ a: parseInt(m[1], 16), t: m[2] });
}
const N = o => ({ b: null, o: o >>> 0 });
const V = (b, o) => ({ b, o: o >>> 0 });
const add = (v, d) => ({ ...v, o: (v.o + d) >>> 0 });
let objN = 0; const objs = {};  // id -> object record
function mkobj(kind, props) { const id = "o" + (++objN); objs[id] = { id, kind, ...props, fields: {}, events: [] }; return V(id, 0); }
const mem = new Map(); const key = v => (v.b || "") + ":" + v.o;
const R = { EAX: N(0), EBX: N(0), ECX: N(0), EDX: N(0), ESI: N(0), EDI: N(0), EBP: N(0x7f00000), ESP: V("stk", 0x100000) };
const log = []; const items = [];
function rd(v) { if (mem.has(key(v))) return mem.get(key(v)); if (v.b === null && v.o >= BASE && v.o < BASE + bin.length) return N(rd32(v.o)); return V("?" + key(v), 0); }
function wr(v, val, size) {
  if (v.b && objs[v.b]) objs[v.b].fields[v.o.toString(16)] = { size, val: show(val) };
  mem.set(key(v), val);
}
function show(v) { if (!v) return "undef"; if (v.b === null) return "0x" + v.o.toString(16); if (v.b.startsWith("str")) return JSON.stringify(strOf(v)); return v.b + (v.o ? "+0x" + v.o.toString(16) : ""); }
const strTab = {}; let strN = 0;
function mkstr(s) { const id = "str" + (++strN); strTab[id] = s; return V(id, 0); }
const strOf = v => v && v.b && strTab[v.b] !== undefined ? strTab[v.b] : (v && v.b && v.b.startsWith("wstr:") ? wstrs[v.b] : undefined);
const wstrs = {};
const regs8 = { AL: ["EAX", 0], CL: ["ECX", 0], DL: ["EDX", 0], BL: ["EBX", 0], AH: ["EAX", 8], CH: ["ECX", 8], DH: ["EDX", 8], BH: ["EBX", 8] };
const regs16 = { AX: "EAX", BX: "EBX", CX: "ECX", DX: "EDX", SI: "ESI", DI: "EDI", BP: "EBP", SP: "ESP" };
function addr(e) {  // "[ESP + 0x1c]" / "[EAX + ECX*0x4 + 0x10]"
  e = e.replace(/^.*\[/, "").replace(/\]$/, "");
  let v = N(0);
  for (let t of e.split(/\s*(?=[+-])\s*/)) {
    let sgn = 1; t = t.trim(); if (t[0] === "+") t = t.slice(1).trim(); else if (t[0] === "-") { sgn = -1; t = t.slice(1).trim(); }
    if (/^0x[0-9a-f]+$/.test(t) || /^\d+$/.test(t)) v = add(v, sgn * parseInt(t));
    else if (/^[A-Z]+[*]0x1$/.test(t)) { const r = R[t.split("*")[0]]; v = v.b ? add(v, r.o) : { b: r.b, o: (v.o + r.o) >>> 0 }; }
    else if (/^[A-Z]+[*]0x[1248]$/.test(t)) { const [r, k] = t.split("*"); const rv = R[r]; if (rv.b) throw new Error("scaled sym " + e); v = add(v, sgn * rv.o * parseInt(k)); }
    else if (R[t]) { const r = R[t]; v = v.b ? add(v, r.o) : { b: r.b, o: (v.o + r.o) >>> 0 }; }
    else throw new Error("addr " + e);
  }
  return v;
}
function get(op) {
  op = op.trim();
  if (R[op]) return R[op];
  if (regs8[op]) { const [r, s] = regs8[op]; return R[r].b ? V("?8", 0) : N((R[r].o >> s) & 0xff); }
  if (regs16[op]) { const r = R[regs16[op]]; return r.b ? V("?16", 0) : N(r.o & 0xffff); }
  if (op.includes("[")) { const v = rd(addr(op)); if (/^byte/.test(op) && !v.b) return N(v.o & 0xff); if (/^word/.test(op) && !v.b) return N(v.o & 0xffff); return v; }
  if (/^-?0x[0-9a-f]+$/.test(op)) return N(parseInt(op));
  if (/^-?\d+$/.test(op)) return N(parseInt(op));
  throw new Error("get " + op);
}
function set(op, v) {
  op = op.trim();
  if (R[op]) { R[op] = v; return; }
  if (regs8[op]) { const [r, s] = regs8[op]; const old = R[r].b ? 0 : R[r].o; R[r] = N((old & ~(0xff << s)) | ((v.o & 0xff) << s)); return; }
  if (regs16[op]) { const r = regs16[op]; const old = R[r].b ? 0 : R[r].o; R[r] = N((old & 0xffff0000) | (v.o & 0xffff)); return; }
  const size = /^byte/.test(op) ? 1 : /^word/.test(op) ? 2 : 4;
  wr(addr(op), size === 4 || v.b ? v : N(v.o & (size === 1 ? 0xff : 0xffff)), size);
}
function push(v) { R.ESP = add(R.ESP, -4); mem.set(key(R.ESP), v); }
function pop() { const v = rd(R.ESP); R.ESP = add(R.ESP, 4); return v; }
function args(n) { const a = []; for (let i = 0; i < n; i++) a.push(rd(add(R.ESP, 4 * i))); return a; }
let flags = { z: false };
const xmm = {}; const vecs = {};
const nodes = {};  // key(node) -> {hash, type, size, at, val?}
const HELP = { 0x434823c3: 8, 0x434903d1: 12, 0x434820cf: 8, 0x43482286: 4, 0x434825fb: 0, 0x43482470: 8, 0x43480690: 8, 0x43484fff: 4, 0x43385ae4: 0 };
const done = [];
function fin(k) { const n = nodes[k]; if (!n) return; if (n.val === undefined) { const off = +k.slice(k.lastIndexOf(":") + 1); n.val = mem.get(k.slice(0, k.lastIndexOf(":")) + ":" + (off + 0x10)); } done.push(n); delete nodes[k]; }
function reg(node, hash, type, size, at, val) { fin(key(node)); mem.delete(key(add(node, 0x10))); nodes[key(node)] = { node: key(node), hash: hash >>> 0, type, size, at: at.toString(16), val, step: STEP }; }
let STEP = 0;
const ops = s => { const i = s.indexOf(" "); if (i < 0) return [s, []]; const rest = s.slice(i + 1); const parts = []; let depth = 0, c = ""; for (const ch of rest) { if (ch === "[") depth++; if (ch === "]") depth--; if (ch === "," && !depth) { parts.push(c); c = ""; } else c += ch; } parts.push(c); return [s.slice(0, i), parts.map(x => x.trim())]; };
let ri = 0;
for (const region of regions) {
  ri++;
  for (const r of ["EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"]) R[r] = V(r + "@" + ri, 0);
  R.ESP = V("stk" + ri, 0x100000);
  for (const I of region) {
    STEP++; for (const k of Object.keys(nodes)) if (STEP - nodes[k].step > 16) fin(k);
    if (/^RET/.test(I.t)) for (const k of Object.keys(nodes)) fin(k);
    const [m, o] = ops(I.t);
    try {
      switch (m) {
        case "MOV": case "MOVZX": case "MOVSX": set(o[0], get(o[1])); break;
        case "LEA": set(o[0], addr(o[1])); break;
        case "PUSH": push(get(o[0])); break;
        case "POP": set(o[0], pop()); break;
        case "ADD": case "SUB": case "XOR": case "AND": case "OR": {
          const a = get(o[0]), b = get(o[1]); let r;
          if (m === "XOR" && o[0] === o[1]) r = N(0); else if (m === "AND" && !b.b && b.o === 0) r = N(0); else if (m === "OR" && !b.b && b.o === 0xffffffff) r = N(-1);
          else if (a.b || b.b) r = m === "ADD" && !b.b ? add(a, b.o) : m === "SUB" && !b.b ? add(a, -b.o) : V("?", 0);
          else r = N(m === "ADD" ? a.o + b.o : m === "SUB" ? a.o - b.o : m === "XOR" ? a.o ^ b.o : m === "AND" ? a.o & b.o : a.o | b.o);
          set(o[0], r); break; }
        case "INC": { const v = get(o[0]); set(o[0], v.b ? V("?", 0) : N(v.o + 1)); break; }
        case "DEC": { const v = get(o[0]); set(o[0], v.b ? V("?", 0) : N(v.o - 1)); break; }
        case "CALL": {
          const t = parseInt(o[0]); const A = args(4); const n = HELP[t];
          if (t === 0x43385ae4) { const h = rd(add(R.ECX, 8)), ty = rd(add(R.ECX, 0xe)), sz = rd(add(R.ECX, 0xc)); reg(R.ECX, h.b ? -1 : h.o, ty.b ? -1 : ty.o & 0xff, sz.b ? -1 : sz.o & 0xffff, I.a, undefined); }
          else if (t === 0x434823c3) reg(R.ECX, A[0].o, 4, 4, I.a, A[1]);
          else if (t === 0x43482470) reg(R.ECX, A[0].o, 3, 4, I.a, A[1]);
          else if (t === 0x434903d1) reg(R.ECX, A[0].o, 7, 8, I.a, { key: A[1], mode: A[2] });
          else if (t === 0x434825fb) reg(R.ECX, 0, 7, 8, I.a, { key: N(0x1b), mode: N(1) });
          else if (t === 0x434820cf) reg(R.ECX, A[0].o, 6, 9, I.a, N(0x01010101));
          else if (t === 0x43484fff) reg(R.ECX, A[0].o, 6, 9, I.a, N(0));
          else if (t === 0x43480690) reg(R.ECX, A[0].o, 6, 0x24, I.a, A[1]);
          else if (t === 0x43482286) reg(R.ECX, A[0].o, 12, 16, I.a, N(0));
          if (n !== undefined) R.ESP = add(R.ESP, n);
          R.EAX = n !== undefined ? R.ECX : V("ret" + I.a, 0); R.ECX = V("clob", 0); R.EDX = V("clob", 0);
          break; }
      }
    } catch (e) { if (process.env.DBG) console.log("ERR", I.a.toString(16), I.t, e.message); }
  }
}
// late writes to node+0x10 after inline links
const out = {}; const absW = {}; for (const [k, v] of mem) if (k.startsWith(":") && !v.b) absW[(+k.slice(1)).toString(16)] = v.o;
for (const k of Object.keys(nodes)) fin(k);
for (const n of done) {
  let v = n.val;
  const sv = v === undefined ? null : v.key ? { key: v.key.b ? null : v.key.o, mode: v.mode.b ? null : v.mode.o } : (v.b ? "sym:" + show(v) : v.o);
  if (n.hash === -1 || n.hash === 0 && n.type !== 7) continue;
  (out[n.hash.toString(16)] = out[n.hash.toString(16)] || []).push({ type: n.type, size: n.size, val: sv, at: n.at });
}
// static .data nodes: hash at +8, flags at +0xc..0xf, value at +0x10
for (let i = 0x43466000 - BASE; i < 0x4347d000 - BASE; i += 4) {
  const ty = bin[i + 0xe], sz = bin.readUInt16LE(i + 0xc), h = bin.readUInt32LE(i + 8);
  if (ty >= 1 && ty <= 12 && sz >= 1 && sz <= 0x40 && bin.readUInt32LE(i) === 0 && bin.readUInt32LE(i + 4) === 0 && h > 0x10000) {
    const k = h.toString(16); if (out[k]) continue;
    const w = mem.get(":" + (i + BASE + 0x10)); const ab = w && !w.b ? w.o : null;
    out[k] = [{ type: ty, size: sz, val: ty === 7 ? { key: bin.readUInt32LE(i + 0x10), mode: bin.readUInt32LE(i + 0x14) } : (ab !== null ? ab : bin.readUInt32LE(i + 0x10)), at: "data:" + (i + BASE).toString(16) }];
  }
}
out.__abs = absW; fs.writeFileSync(outFile, JSON.stringify(out, null, 0));
console.log("nodes", Object.keys(out).length);
