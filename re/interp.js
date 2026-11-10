// Symbolic interpreter for the gamesense menu builder (0x4348704a) over Ghidra's flow disassembly.
// Values: {b: base|null, o: uint32}. Memory cells are dwords keyed "base:offset".
const fs = require("fs");
const [disFile, strFile, binFile, outFile] = process.argv.slice(2);
const strs = JSON.parse(fs.readFileSync(strFile, "utf8"));
const bin = fs.readFileSync(binFile); const BASE = 0x43310000;
const rd32 = a => bin.readUInt32LE(a - BASE);
const ins = new Map(); const order = [];
for (const line of fs.readFileSync(disFile, "utf8").split(/\r?\n/)) {
  const m = line.match(/^([0-9a-f]+)\s+(.*?)(\s+; opaque)?$/); if (!m) continue;
  const a = parseInt(m[1], 16); ins.set(a, { a, t: m[2], opaque: !!m[3] }); order.push(a);
}
order.sort((x, y) => x - y);
const nextOf = new Map(); for (let i = 0; i < order.length - 1; i++) nextOf.set(order[i], order[i + 1]);

const N = o => ({ b: null, o: o >>> 0 });
const V = (b, o) => ({ b, o: o >>> 0 });
const add = (v, d) => ({ ...v, o: (v.o + d) >>> 0 });
let objN = 0; const objs = {};  // id -> object record
function mkobj(kind, props) { const id = "o" + (++objN); objs[id] = { id, kind, ...props, fields: {}, events: [] }; return V(id, 0); }
const mem = new Map(); const key = v => (v.b || "") + ":" + v.o;
const R = { EAX: N(0), EBX: N(0), ECX: N(0), EDX: N(0), ESI: N(0), EDI: N(0), EBP: N(0x7f00000), ESP: V("stk", 0x100000) };
R.ECX = mkobj("Menu", {});
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
const RET = JSON.parse(fs.readFileSync(__dirname + "/rets.json", "utf8"));
let flags = { z: false };
const xmm = {}; const vecs = {};
// element creation helper
function elem(kind, parent, p) { const v = mkobj(kind, { parent: parent.b, ...p }); items.push(v.b); if (parent.b && objs[parent.b]) (objs[parent.b].children = objs[parent.b].children || []).push(v.b); return v; }
const varOf = v => v && v.b && v.b.startsWith("var") ? v.b.slice(3) + "+" + v.o.toString(16) : (v && v.b === null && v.o === 0 ? null : show(v));
function decodeW(p){let s="";for(let i=0;i<64;i++){const v=rd(add(p,4*i));if(v.b)return s+"<?>";const a=v.o&0xffff,b=v.o>>>16;if(!a)break;s+=String.fromCharCode(a);if(!b)break;s+=String.fromCharCode(b);}return s;}
function call(t, at) {
  const n = (RET[t.toString(16)] ?? 0) / 4; const A = args(n); const ecx = R.ECX, edx = R.EDX;
  const S = (x) => strOf(x) ?? show(x);
  let ret = V("ret_" + t.toString(16) + "_" + at.toString(16), 0);
  const ra = (at + 5).toString(16);
  switch (t) {
    case 0x434938ff: case 0x434938fb: { const s = strs[ra]; ret = mkstr(s ? (t === 0x434938ff ? s.w : s.a) : "<?" + ra + ">"); break; }
    case 0x43387448: ret = mkobj("raw", { size: ecx.o }); break;
    case 0x4348e41d: { objs[ecx.b].kind = "Tab"; objs[ecx.b].name = S(A[1]); objs[ecx.b].at = at; items.push(ecx.b); ret = ecx; break; }
    case 0x4348210c: { const o = objs[ecx.b]; Object.assign(o, { kind: "Child", tab: A[0].b, name: S(A[1]), rect: show(A[2]), a4: show(A[3]), a5: show(A[4]), at }); items.push(ecx.b); ret = ecx; break; }
    case 0x4347de9b: ret = A[0]; if (A[0].b && objs[A[0].b]) objs[A[0].b].tab = ecx.b; break;
    case 0x4348206a: ret = A[0]; if (A[0].b && objs[A[0].b] && !objs[A[0].b].parent) objs[A[0].b].parent = ecx.b; break;
    case 0x433e7f74: ret = V("var" + ecx.o.toString(16), 0); break;
    case 0x43486bb2: ret = elem("Checkbox", ecx, { name: S(A[0]), v: varOf(A[1]), at }); break;
    case 0x4347e7a8: ret = elem("Checkbox", ecx, { name: S(A[0]), v: null, at }); break;
    case 0x4347e591: ret = elem("Hotkey", ecx, { v: varOf(A[0]), at }); break;
    case 0x43485705: ret = elem("Combo", ecx, { name: S(A[0]), v: varOf(A[1]), items: [], at }); break;
    case 0x434909b1: ret = elem("Combo", ecx, { name: S(A[0]), v: null, items: [], at }); break;
    case 0x4347ea53: ret = elem("Combo", ecx, { name: "", v: varOf(A[1]), items: [], at }); break;
    case 0x433f8d73: { const o = objs[ecx.b]; if (o) (o.items = o.items || []).push([S(A[0]), A[1].o]); break; }
    case 0x4347d886: ret = elem("Multi", ecx, { name: S(A[0]), v: varOf(A[1]), items: [], at }); break;
    case 0x43490270: ret = elem("Multi", ecx, { name: S(A[0]), v: varOf(A[1]), a3: show(A[2]), a4: show(A[3]), items: [], at }); break;
    case 0x4337cb10: { const o = objs[ecx.b]; if (o) (o.items = o.items || []).push([S(A[0]), A[1].o]); break; }
    case 0x433b4e48: { const o = objs[ecx.b], src = objs[A[0].b]; if (o && src) o.items = (src.items || []).slice(), o.itemsFrom = src.id; break; }
    case 0x433c081c: { const pair = [rd(A[0]), rd(add(A[0], 4))]; const owner = ecx.b; if (objs[owner]) (objs[owner].callbacks = objs[owner].callbacks || []).push({ ctx: show(pair[0]), fn: show(pair[1]) }); else log.push(["cb?", show(ecx), show(pair[0]), show(pair[1])]); break; }
    case 0x433e000b: { const o = objs[ecx.b]; if (o) o.visible = A[0].o & 0xff; else log.push(["setvis?", show(ecx), show(A[0]), at.toString(16)]); break; }
    case 0x43484e7d: ret = elem("Slider", ecx, { name: S(A[0]), min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), a5: A[4].o, unit: A[5].o, step: 1, at }); break;
    case 0x4347dbaa: ret = elem("Slider", ecx, { name: "", min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), a5: A[4].o, unit: A[5].o, step: 1, at }); break;
    case 0x43484978: ret = elem("Slider", ecx, { name: S(A[0]), min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), unit: 0, step: 1, at }); break;
    case 0x43482223: ret = elem("Slider", ecx, { name: "", min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), unit: A[5].o, stepRaw: A[6].o, at }); break;
    case 0x43486463: ret = elem("Slider", ecx, { name: S(A[0]), min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), unit: A[5].o, stepRaw: A[6].o, at }); break;
    case 0x43485039: ret = elem("Slider", ecx, { name: S(A[0]), min: A[1].o | 0, max: A[2].o | 0, v: varOf(A[3]), unit: 0xb0, step: 1, at }); break;
    case 0x43402af1: { const o = objs[ecx.b]; const w = rd(add(R.ESP, 4)); if (o) (o.tips = o.tips || []).push([A[0].o | 0, strOf(w) ?? show(w)]); break; }
    case 0x433d2786: { wr(ecx, A[0].b && strTab[A[0].b] !== undefined ? A[0] : A[0]); ret = ecx; break; }
    case 0x4347eb5a: ret = elem("Color", ecx, { v: varOf(A[0]), at }); break;
    case 0x4348dc73: ret = elem("Label", ecx, { name: S(A[0]), at }); break;
    case 0x4347e840: ret = elem("Button", ecx, { name: S(A[0]), cb: show(A[1]), a3: show(A[2]), at }); break;
    case 0x43485cfb: ret = elem("Listbox", ecx, { name: S(A[0]), a: A.slice(1).map(show), at }); break;
    case 0x43365b5e: ret = elem("Slider?43480054", ecx, { name: decodeW(edx), v: varOf(A[0]), at }); break;
    case 0x433f1694: { (vecs[key(ecx)] = vecs[key(ecx)] || []).push([strOf(rd(A[0])) ?? show(rd(A[0])), rd(A[1]).o]); break; }
    case 0x43331f24: { vecs[key(ecx)] = (vecs[key(A[0])] || ["<?vec " + key(A[0]) + ">"]).slice(); ret = ecx; break; }
    case 0x433ee01c: { const o = objs[ecx.b]; const vk = key(R.ESP); if (o) { o.items = (vecs[vk] || ["<?vec>"]).slice(); } break; }
    case 0x43322ae6: { let b = rd(ecx); if (!b.b || !b.b.startsWith("vb")) { b = V("vb" + (++objN), 0); mem.set(key(ecx), b); mem.set(key(add(ecx, 4)), b); mem.set(key(add(ecx, 8)), add(b, 0x1000)); }
      const e = rd(add(ecx, 4)); mem.set(key(e), rd(A[0])); mem.set(key(add(ecx, 4)), add(e, 4)); (vecs[key(ecx)] = vecs[key(ecx)] || []).push(show(rd(A[0]))); break; }
    case 0x433ccb69: case 0x43384a6b: break;
    case 0x43484fc2: ret = elem("Textbox", ecx, { at }); break;
    case 0x43342275: ret = A[0]; if (objs[A[0].b]) objs[A[0].b].addedAt = at; break;
    case 0x43486ccf: ret = elem("Multi", ecx, { name: "", v: varOf(A[1]), items: [], at }); break;
    case 0x4348e65f: ret = elem("Multi", ecx, { name: "", v: varOf(A[1]), items: [], at }); break;
    case 0x43486f12: ret = elem("Slider", ecx, { name: S(A[0]), min: 1, max: 1001, v: varOf(A[3]), unit: 0x73006d, step: 1, at, a: A.map(show) }); break;
    case 0x4347feeb: ret = elem("Hotkey", ecx, { v: show(A[0]), a: A.map(show), at }); break;
    case 0x43485fc1: ret = elem("Hotkey", ecx, { v: null, at }); break;
    case 0x4348017f: ret = elem("Combo", ecx, { name: "", v: varOf(A[1]), items: [], at }); break;
    case 0x43486ff9: case 0x434859b4: ret = elem("Listbox", ecx, { name: S(A[0]), a: A.slice(1).map(show), at }); break;
    case 0x43481d7d: ret = elem("Checkbox", ecx, { name: S(A[0]), v: null, at }); break;
    case 0x43485bad: { const o = objs[ecx.b]; Object.assign(o, { kind: "Child", tab: A[0].b, name: S(A[1]), rect: show(A[2]), a4: show(A[3]), at }); items.push(ecx.b); ret = ecx; break; }
    case 0x4336d9ef: case 0x433231ca: ret = elem("Custom_" + t.toString(16), ecx, { name: S(edx), a: A.map(show), at }); break;
    case 0x43407c86: case 0x433fb6b1: case 0x4337cacf: case 0x433ab16b: case 0x433ce8ba: case 0x433e520a: case 0x433f9c51: case 0x433a121b: case 0x4334d372: case 0x433cf58b: case 0x4335c747: case 0x433cd70e:
      { const o = objs[ecx.b]; if (o) (o.directCalls = o.directCalls || []).push([t.toString(16), at.toString(16), A.map(show)]); else log.push(["dcall", t.toString(16), show(ecx)]); break; }
    default: log.push(["call", t.toString(16), at.toString(16), "ecx=" + show(ecx), "edx=" + show(edx), ...A.map(show)]);
  }
  R.ESP = add(R.ESP, 4 * n);
  R.EAX = ret; R.ECX = V("clob", 0); R.EDX = V("clob", 0);
}
function arith(m, d, s) {
  const a = get(d), b = get(s);
  let r;
  if (m === "ADD") r = a.b && b.b ? V("?add", 0) : a.b ? add(a, b.o) : b.b ? add(b, a.o) : N(a.o + b.o);
  else if (m === "SUB") r = a.b && b.b ? (a.b === b.b ? N(a.o - b.o) : V("?sub", 0)) : a.b ? add(a, -b.o) : N(a.o - b.o);
  else if (m === "XOR") r = d.trim() === s.trim() ? N(0) : (a.b || b.b) ? V("?xor", 0) : N(a.o ^ b.o);
  else if (m === "AND" && a.b === "stk" && !b.b) r = V("stk", a.o & b.o);
  else if (m === "AND") r = (a.b || b.b) ? (b.b === null && b.o === 0 ? N(0) : V("?and", 0)) : N(a.o & b.o);
  else if (m === "OR") r = (a.b || b.b) ? (b.b === null && b.o === 0xffffffff ? N(-1) : V("?or", 0)) : N(a.o | b.o);
  else if (m === "SHR") r = a.b ? V("?shr", 0) : N(a.o >>> b.o);
  else if (m === "SAR") r = a.b ? V("?sar", 0) : N((a.o | 0) >> b.o);
  set(d, r); flags.z = !r.b && r.o === 0;
}
let pc = 0x4348704a, steps = 0;
const ops = s => { const i = s.indexOf(" "); if (i < 0) return [s, []]; const rest = s.slice(i + 1); const parts = []; let depth = 0, cur = ""; for (const c of rest) { if (c === "[") depth++; if (c === "]") depth--; if (c === "," && !depth) { parts.push(cur); cur = ""; } else cur += c; } parts.push(cur); return [s.slice(0, i), parts.map(x => x.trim())]; };
while (pc && steps++ < 200000) {
  const I = ins.get(pc); if (!I) { log.push(["nopc", pc.toString(16)]); break; }
  let [m, o] = ops(I.t); let next = nextOf.get(pc);
  try {
    switch (m) {
      case "MOV": case "MOVZX": case "MOVSX": set(o[0], get(o[1])); break;
      case "LEA": set(o[0], addr(o[1])); break;
      case "PUSH": push(get(o[0])); break;
      case "POP": set(o[0], pop()); break;
      case "ADD": case "SUB": case "XOR": case "AND": case "OR": case "SHR": case "SAR": arith(m, o[0], o[1]); break;
      case "INC": { const v = get(o[0]); set(o[0], add(v, 1)); flags.z = !v.b && ((v.o + 1) >>> 0) === 0; break; }
      case "DEC": { const v = get(o[0]); set(o[0], add(v, -1)); flags.z = !v.b && v.o === 1; break; }
      case "TEST": { const a = get(o[0]), b = get(o[1]); flags.z = a.b || b.b ? false : (a.o & b.o) === 0; break; }
      case "CMP": { const a = get(o[0]), b = get(o[1]); flags.z = a.b || b.b ? (a.b === b.b && a.o === b.o) : a.o === b.o; flags.lt = !a.b && !b.b && (a.o | 0) < (b.o | 0); break; }
      case "CALL": call(parseInt(o[0]), pc); break;
      case "JMP": next = parseInt(o[0]); break;
      case "JZ": if (flags.z) next = parseInt(o[0]); break;
      case "JNZ": if (I.opaque || !flags.z) next = parseInt(o[0]); break;
      case "RET": next = 0; break;
      case "MOVD": { if (o[0].startsWith("XMM")) xmm[o[0]] = [get(o[1])]; else set(o[0], xmm[o[1]][0]); break; }
      case "PSHUFD": { const x = xmm[o[1]][0]; xmm[o[0]] = [x, x, x, x]; break; }
      case "MOVUPS": case "MOVQ": { const n = m === "MOVQ" ? 2 : 4; if (o[0].startsWith("XMM")) { const b = addr(o[1]); xmm[o[0]] = [...Array(n)].map((_, i) => rd(add(b, 4 * i))); } else { const b = addr(o[0]); xmm[o[1]].slice(0, n).forEach((v, i) => wr(add(b, 4 * i), v, 4)); } break; }
      case "PXOR": { xmm[o[0]] = xmm[o[0]].map((v, i) => N(v.o ^ xmm[o[1]][i].o)); break; }
      default: log.push(["op?", pc.toString(16), I.t]);
    }
  } catch (e) { log.push(["err", pc.toString(16), I.t, e.message]); }
  pc = next;
}
// stack-built wide strings: decode dword cells as UTF-16 pairs where referenced
fs.writeFileSync(outFile, JSON.stringify({ items: items.map(id => objs[id]), log }, null, 1));
console.log("steps", steps, "items", items.length, "log", log.length);
