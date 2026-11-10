// map call return address -> decoded string, from skeet-debug ctx.h + menu_fonts.h
const fs=require('fs');const D=(process.env.SKEET_BINARY||'skeet-debug-main/skeet/Binary')+'/';  // dir holding ctx.h + menu_fonts.h
const mf=Buffer.from(fs.readFileSync(D+'menu_fonts.h','utf8').split('{')[1].match(/0x[0-9A-Fa-f]{2}/g).map(x=>parseInt(x,16)));
const ctx=fs.readFileSync(D+'ctx.h','utf8');const re=/\{\s*(0x[0-9A-F]+),\s*(0x[0-9A-F]+),(?:\s*0x[0-9A-F]+,){6}\s*(0x[0-9A-F]+),/g;
const out={};let m;
function dec(o,wide){let s='';if(wide){for(let i=o;i<mf.length-1;i+=2){const c=mf.readUInt16LE(i);if(!c)break;s+=String.fromCharCode(c);}}else{for(let i=o;i<mf.length;i++){if(!mf[i])break;s+=String.fromCharCode(mf[i]);}}return s;}
while((m=re.exec(ctx))){const cr=parseInt(m[1],16);if(cr!==0x43493900&&cr!==0x434938FC)continue;const o=parseInt(m[2],16)-0x940000;const rip=parseInt(m[3],16).toString(16);
 out[rip]={stub:cr===0x43493900?'ff':'fb',off:o,w:dec(o,true),a:dec(o,false)};}
fs.writeFileSync(process.argv[2],JSON.stringify(out,null,0));
const ks=Object.keys(out);console.log(ks.length);for(const k of ks.slice(0,30))console.log(k,out[k].stub,out[k].off.toString(16),JSON.stringify(out[k].w).slice(0,40),JSON.stringify(out[k].a).slice(0,40));
