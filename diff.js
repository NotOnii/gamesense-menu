// node diff.js ref.png-as-bgra? no: node diff.js out.bmp ref.bgra [region x y w h]
// Compares capture BMP (32bpp top-down) with reference raw BGRA (669x564). Prints mismatch stats + per-area summary, writes diff.bmp.
const fs=require('fs');const W=669,H=564;
const out=fs.readFileSync(process.argv[2]),ref=fs.readFileSync(process.argv[3]);const o=54;
let bad=0,sum=0,mx=0;const img=Buffer.alloc(54+W*H*4);out.copy(img,0,0,54);const cells={};
for(let y=0;y<H;y++)for(let x=0;x<W;x++){const i=(y*W+x)*4;let d=0;for(let c=0;c<3;c++)d=Math.max(d,Math.abs(out[o+i+c]-ref[i+c]));
 const j=o+i;if(d){bad++;sum+=d;mx=Math.max(mx,d);const k=`${x>>5},${y>>5}`;cells[k]=(cells[k]||0)+1;img[j]=0;img[j+1]=d>8?0:255;img[j+2]=255;img[j+3]=255}else{const g=ref[i+1]>>2;img[j]=img[j+1]=img[j+2]=g;img[j+3]=255}}
fs.writeFileSync(process.argv[4]||'diff.bmp',img);
console.log(`mismatched ${bad}/${W*H} (${(100*bad/W/H).toFixed(2)}%) maxdelta ${mx} meandelta ${(sum/Math.max(1,bad)).toFixed(1)}`);
console.log('worst 32px cells:',Object.entries(cells).sort((a,b)=>b[1]-a[1]).slice(0,12).map(([k,v])=>`(${k.split(',').map(n=>n*32).join(',')}):${v}`).join(' '));
