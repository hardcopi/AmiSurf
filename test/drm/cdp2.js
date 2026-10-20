// node cdp2.js PORT URL OUTPREFIX : navigate, wait, report, screenshot + screencast frame
const [port,url,out]=process.argv.slice(2); const fs=require('fs');
(async()=>{
 const l=await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
 const t=l.find(x=>x.type==='page'); const ws=new WebSocket(t.webSocketDebuggerUrl); let id=1; const pend={}; let frames=[];
 ws.onmessage=e=>{const m=JSON.parse(e.data); if(m.id&&pend[m.id]){pend[m.id](m);delete pend[m.id]} else if(m.method==='Page.screencastFrame'){frames.push(m.params.data); ws.send(JSON.stringify({id:id++,method:'Page.screencastFrameAck',params:{sessionId:m.params.sessionId}}))}};
 await new Promise(r=>ws.onopen=r);
 const call=(method,params={})=>new Promise(r=>{const i=id++;pend[i]=r;ws.send(JSON.stringify({id:i,method,params}))});
 await call('Page.enable'); await call('Emulation.setDeviceMetricsOverride',{width:1000,height:700,deviceScaleFactor:1,mobile:false});
 await call('Page.navigate',{url}); await new Promise(r=>setTimeout(r,20000));
 const ev=await call('Runtime.evaluate',{expression:`JSON.stringify({st,log:document.getElementById('log').textContent,t:document.querySelector('video').currentTime,rs:document.querySelector('video').readyState,w:document.querySelector('video').videoWidth})`,returnByValue:true});
 console.log('STATE',ev.result?.result?.value);
 const s=await call('Page.captureScreenshot',{format:'png'}); fs.writeFileSync(out+'-shot.png',Buffer.from(s.result.data,'base64'));
 await call('Page.startScreencast',{format:'png',everyNthFrame:1}); await new Promise(r=>setTimeout(r,3000)); await call('Page.stopScreencast');
 console.log('frames',frames.length); if(frames.length) fs.writeFileSync(out+'-cast.png',Buffer.from(frames[frames.length-1],'base64'));
 process.exit(0);
})().catch(e=>{console.log('ERR',e);process.exit(1)});
