const j=require("./menu.json");const by={};for(const o of j.items)by[o.id]=o;
const U=u=>{if(!u)return"";let s="";for(const c of [u&0xffff,u>>>16])if(c)s+=String.fromCharCode(c);return s;};
for(const o of j.items){
 let d="";
 if(o.kind==="Tab")d=`\n=== TAB ${o.id} "${o.name}"`;
 else if(o.kind==="Child")d=`\n  -- CHILD ${o.id} "${o.name}" rect=${o.rect} a4=${o.a4} a5=${o.a5||""}`;
 else{d=`    ${o.kind} ${o.id} "${o.name??""}" v=${o.v}`;
  if(o.kind==="Slider")d+=` [${o.min},${o.max}] unit="${U(o.unit)}" ${o.stepRaw?"step="+o.stepRaw:""}${o.a5!==undefined?" a5="+o.a5:""}`;
  if(o.items&&o.items.length)d+=` items=${JSON.stringify(o.items)}`;if(o.itemsFrom)d+=` from=${o.itemsFrom}`;
  if(o.tips)d+=` tips=${JSON.stringify(o.tips)}`;}
 if(o.visible!==undefined)d+=` VIS=${o.visible}`;
 if(o.callbacks)d+=` CB=${o.callbacks.map(c=>c.fn+"("+c.ctx+")").join(",")}`;
 if(o.directCalls)d+=` DC=${JSON.stringify(o.directCalls)}`;
 if(o.parent&&by[o.parent]&&by[o.parent].kind!=="Child"&&o.kind!=="Child"&&o.kind!=="Tab")d+=` PARENT=${o.parent}`;
 console.log(d);}
console.log(j.log.map(l=>JSON.stringify(l)).join("\n"));
