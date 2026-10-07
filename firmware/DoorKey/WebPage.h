#pragma once
#include <Arduino.h>

// 웹 UI (단일 페이지). 수정 후 다시 업로드하면 반영된다.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DoorKey</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2127;--mute:#626a76;--line:#e3e6ea;--field:#858c97;--acc:#2563eb;--on-acc:#fff;--ok:#136f37;--warn:#a14a06;--bad:#b91c1c;--on-bad:#fff;--chip:#eef1f5;--scrim:rgba(0,0,0,.5)}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f25;--fg:#e6e9ee;--mute:#95a0ad;--line:#2a313a;--field:#636d7b;--acc:#60a5fa;--on-acc:#0b1220;--ok:#4ade80;--warn:#fbbf24;--bad:#f87171;--on-bad:#0b1220;--chip:#222a33;--scrim:rgba(0,0,0,.65)}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 -apple-system,system-ui,"Apple SD Gothic Neo","Malgun Gothic",sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--card);border-bottom:1px solid var(--line)}
.top{display:flex;align-items:center;gap:8px;padding:8px 16px;flex-wrap:wrap}
.top h1{font-size:17px;margin:0 8px 0 0}
nav{display:flex;overflow-x:auto;padding:0 8px}
nav button{background:none;border:0;border-bottom:2px solid transparent;color:var(--mute);min-height:48px;padding:0 12px;font-size:14px;white-space:nowrap;cursor:pointer}
nav button.on{color:var(--fg);border-color:var(--acc);font-weight:600}
main{max-width:760px;margin:0 auto;padding:12px 16px 60px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.card h2{font-size:15px;margin:0 0 10px}
.chip{display:inline-flex;align-items:center;gap:4px;background:var(--chip);border-radius:99px;padding:2px 10px;font-size:12px;color:var(--mute)}
.chip.ok{color:var(--ok)}.chip.warn{color:var(--warn)}.chip.bad{color:var(--bad)}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.sp{flex:1}
.mt{margin-top:12px}
.mute{color:var(--mute);font-size:13px}
.small{font-size:12px}
.mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px}
.badge{font-size:12px;font-weight:600;padding:2px 8px;border-radius:6px;background:var(--chip)}
.b-home{color:var(--ok)}.b-away{color:var(--acc)}.b-lost{color:var(--warn)}.b-unknown{color:var(--mute)}
.c-ok{color:var(--ok)}.c-acc{color:var(--acc)}.c-warn{color:var(--warn)}.c-bad{color:var(--bad)}.c-mute{color:var(--mute)}
canvas{width:100%;height:70px;display:block;margin:8px 0 4px;border-radius:6px;background:var(--chip)}
button.b{background:var(--chip);color:var(--fg);border:1px solid var(--line);border-radius:8px;min-height:48px;padding:0 16px;font-size:14px;cursor:pointer}
button.p{background:var(--acc);color:var(--on-acc);border-color:var(--acc)}
button.s{padding:0 12px}
.nw{white-space:nowrap;display:inline-flex;gap:8px}
button.d{color:var(--bad)}
button.dd{background:var(--bad);color:var(--on-bad);border-color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
button.busy{cursor:progress}
button.busy::after{content:"";display:inline-block;width:12px;height:12px;margin-left:8px;vertical-align:-2px;border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:spin .8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
label{display:block;font-size:13px;color:var(--mute);margin:10px 0 3px}
input,select,textarea{width:100%;min-height:48px;padding:0 12px;border:1px solid var(--field);border-radius:8px;background:var(--bg);color:var(--fg);font-size:15px}
textarea{padding:12px}
input[type=file]{padding:8px 12px}
input[type=checkbox]{width:24px;height:24px;min-height:0;padding:0;margin:0;accent-color:var(--acc);flex:none}
label.ck{display:flex;align-items:center;gap:8px;min-height:48px;margin:0;color:var(--fg);font-size:14px;cursor:pointer}
/* 자동 개방 스위치 */
.sw{display:flex;align-items:center;gap:8px;min-height:48px;margin:0;color:var(--fg);font-size:14px;cursor:pointer}
.sw input{appearance:none;-webkit-appearance:none;width:44px;height:24px;min-height:0;padding:0;border:0;border-radius:12px;background:var(--mute);position:relative;margin:0;cursor:pointer;transition:background .15s}
.sw input::after{content:"";position:absolute;top:2px;left:2px;width:20px;height:20px;border-radius:50%;background:var(--card);transition:left .15s}
.sw input:checked{background:var(--acc)}
.sw input:checked::after{left:22px}
.sw input:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
@media (max-width:520px){.grid{grid-template-columns:1fr}}
.help{font-size:12px;color:var(--mute);margin-top:2px}
.banner{border-radius:10px;padding:10px 12px;margin-bottom:12px;font-size:14px;background:var(--chip);border-left:4px solid var(--warn)}
.banner.bad{border-color:var(--bad)}
.banner.info{border-color:var(--acc)}
details summary{cursor:pointer;min-height:48px;display:flex;align-items:center}
.kv{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;font-size:13px}
.kv div:nth-child(odd){color:var(--mute)}
.log{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px;white-space:pre-wrap;word-break:break-all;max-height:65vh;overflow:auto}
.hide{display:none}
ol.steps{padding-left:20px;margin:6px 0}ol.steps li{margin:4px 0}
.dev{padding:8px 0;border-bottom:1px solid var(--line)}
/* 연결 상태·오래된 데이터 */
.age{font-size:12px;color:var(--mute);white-space:nowrap}
.age.bad{color:var(--bad);font-weight:600}
.stalebar{background:var(--bad);color:var(--on-bad);padding:8px 16px;font-size:14px}
body.stale main,body.stale .chips,body.stale .sw{opacity:.45;filter:grayscale(1)}
.dev button.d{margin-left:auto}
/* 확인 대화상자 */
dialog{border:0;border-radius:12px;padding:24px;width:min(420px,calc(100vw - 32px));background:var(--card);color:var(--fg)}
dialog::backdrop{background:var(--scrim)}
dialog h2{font-size:17px;margin:0 0 8px}
dialog p{margin:0 0 16px}
.toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--fg);color:var(--card);padding:8px 16px;border-radius:8px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:calc(100vw - 32px)}
.toast.on{opacity:.95}
</style></head><body>
<header>
 <div class="top"><h1>DoorKey</h1>
  <span class="row chips">
   <span id="cWifi" class="chip"><span class="dot"></span>Wi-Fi</span>
   <span id="cBle" class="chip"><span class="dot"></span>BLE</span>
   <span id="cSt" class="chip"><span class="dot"></span>SmartThings</span>
  </span>
  <span class="sp"></span>
  <span id="age" class="age">연결 중…</span>
  <label class="sw"><input type="checkbox" id="autoSw" role="switch"><span>자동 개방</span></label>
 </div>
 <div id="staleBar" class="stalebar hide"></div>
 <nav id="nav"></nav>
</header>
<main>
 <div id="banners"></div>

 <section id="t-home">
  <div id="devCards"></div>
  <div class="card"><h2>마지막 문 열기</h2><div id="lastCmd" class="mute">없음</div>
   <div class="row mt"><button class="b" onclick="testUnlock(this)">문 열기 테스트</button></div></div>
 </section>

 <section id="t-dev" class="hide">
  <div class="card"><h2>새 폰 등록</h2>
   <div class="row"><input id="enName" placeholder="이름 (예: 아내 아이폰)" maxlength="12" style="flex:1"><button class="b p" id="enBtn" onclick="enroll(this)">등록 시작</button></div>
   <div id="enBox" class="hide mt"><div id="enMsg" class="banner"></div><button class="b" onclick="post('/api/enroll/cancel',{},this)">등록 취소</button></div>
   <ol class="steps mute small">
    <li>등록 시작을 누르면 2분간 <b>DoorKey</b>라는 블루투스 키보드로 보입니다.</li>
    <li><b>아이폰</b>: 설정 › Bluetooth › 기타 기기 › DoorKey › 페어링</li>
    <li><b>갤럭시</b>: 설정 › 연결 › 블루투스 › 스캔 › DoorKey › 등록</li>
    <li>완료되면 폰의 블루투스 목록에서 DoorKey를 <b>삭제</b>해도 됩니다. (신원 키는 이미 저장됨)</li>
    <li>등록된 <b>기기 주소</b>가 폰의 블루투스 주소와 같은지 확인하세요. (아이폰: 설정 › 일반 › 정보 / 갤럭시: 설정 › 휴대전화 정보 › 상태 정보) 다르면 다른 사람이 끼어든 것이니 삭제하세요.</li>
   </ol></div>
  <div class="card"><h2>등록된 기기</h2><div id="devList"></div></div>
  <div class="card"><h2>갤럭시 앱 비컨</h2><div id="appDiag" class="mute">감지 안 됨</div>
   <div class="help">DoorKey 비컨 앱을 켠 갤럭시가 IRK로 해석되면 "해석됨"으로 표시됩니다.</div></div>
  <div class="card"><h2>IRK 직접 추가</h2>
   <div class="grid"><div><label>이름</label><input id="mName" maxlength="12"></div>
   <div><label>IRK (32자 16진수)</label><input id="mIrk" class="mono" placeholder="00112233445566778899aabbccddeeff"></div></div>
   <div class="row mt"><button class="b" onclick="addManual(this)">추가</button></div></div>
 </section>

 <section id="t-set" class="hide">
  <div class="card"><h2>귀가 판정</h2><div class="grid" id="pForm"></div>
   <div class="row mt"><button class="b p" onclick="saveParams(this)">저장</button></div></div>
 </section>

 <section id="t-st" class="hide">
  <div class="card"><h2>상태</h2><div id="stState" class="kv"></div>
   <div class="row mt"><button class="b" onclick="post('/api/st/check',{},this)">연결 확인</button><button class="b" onclick="post('/api/st/refresh',{},this)">토큰 갱신</button><button class="b" onclick="testUnlock(this)">문 열기 테스트</button></div></div>
  <div class="card"><h2>1. OAuth 앱 정보</h2>
   <div class="help">SmartThings CLI로 만든 OAuth-In 앱의 값을 입력하세요. (README 참고)</div>
   <label>Client ID</label><input id="sCid" class="mono">
   <label>Client Secret</label><input id="sCsec" class="mono" placeholder="(저장됨 — 바꿀 때만 입력)">
   <label>Redirect URI</label><input id="sRedir" class="mono">
   <div class="row mt"><button class="b p" onclick="saveSt(this)">저장</button></div></div>
  <div class="card"><h2>2. 인증</h2>
   <div class="row"><a id="authLink" target="_blank" rel="noopener"><button class="b">SmartThings 인증 페이지 열기</button></a></div>
   <label>인증 후 이동한 페이지의 주소(또는 code 값)를 붙여넣기</label>
   <textarea id="sCode" rows="3" class="mono"></textarea>
   <div class="row mt"><button class="b p" onclick="sendCode(this)">토큰 발급</button></div></div>
  <div class="card"><h2>3. 문 열기 명령</h2>
   <div class="grid"><div><label>Device ID</label><input id="sDev" class="mono"></div>
   <div><label>Component</label><input id="sComp" class="mono"></div>
   <div><label>Capability</label><input id="sCap" class="mono"></div>
   <div><label>Command</label><input id="sCmd" class="mono"></div></div>
   <label>Arguments (JSON 배열)</label><input id="sArgs" class="mono">
   <div class="help">직접 호출이 거부되면 가상 스위치로 바꾸세요: Device ID = 가상 스위치, Capability = switch, Command = on</div>
   <div class="row mt"><button class="b p" onclick="saveSt(this)">저장</button></div></div>
  <div class="card"><h2>4. 폰 위치로 외출 확인 (선택)</h2>
   <div class="help">집 안에서 블루투스 신호만 끊겨 '외출'로 잘못 판정돼도 문이 열리지 않게 합니다. 위치 기기 ID를 넣은 사람은 이번 외출 중 SmartThings에서 '외출'이 확인돼야 귀가 시 문을 엽니다. SmartThings 조회가 3분 넘게 안 되면 블루투스만으로 판단합니다.</div>
   <div id="presList" class="mt"></div>
   <div class="row mt"><button class="b p" onclick="savePres(this)">저장</button><button class="b" onclick="post('/api/pres/check',{},this)">지금 조회</button></div>
   <details class="mt"><summary class="mute">위치 기기 ID 찾는 법</summary><ol class="steps mute small">
    <li>폰의 SmartThings 앱에서 이 폰의 위치(재실) 사용을 켜고, 앱 위치 권한을 '항상 허용'으로 둡니다.</li>
    <li>my.smartthings.com/advanced › Devices에서 폰 이름의 기기를 엽니다. (presenceSensor가 있는 기기)</li>
    <li>Device ID(8-4-4-4-12자리)를 복사해 위 칸에 붙여넣고 저장합니다. 칸 아래에 '집' 또는 '외출'이 나오면 정상입니다.</li></ol></details></div>
 </section>

 <section id="t-sys" class="hide">
  <div class="card"><h2>정보</h2><div id="sysInfo" class="kv"></div></div>
  <div class="card"><h2>알림 (ntfy 등)</h2>
   <label>POST URL</label><input id="ntfy" class="mono" placeholder="https://ntfy.sh/내-비밀-토픽">
   <div class="help">문 열림·인증 만료를 폰으로 받습니다. ntfy 앱에서 같은 토픽을 구독하세요. 비우면 끔.</div>
   <div class="row mt"><button class="b p" onclick="saveNtfy(this)">저장</button><button class="b" onclick="post('/api/ntfy/test',{},this)">테스트</button></div></div>
  <div class="card"><h2>Wi-Fi</h2>
   <div class="grid"><div><label>SSID (2.4GHz)</label><input id="wSsid"></div><div><label>비밀번호</label><input id="wPass" type="password"></div></div>
   <div class="row mt"><button class="b p" onclick="saveWifi(this)">저장 후 재부팅</button></div></div>
  <div class="card"><h2>관리자 비밀번호</h2>
   <input id="aPass" type="password" placeholder="새 비밀번호 (6자 이상)">
   <div class="row mt"><button class="b p" onclick="saveAdmin(this)">변경</button></div></div>
  <div class="card"><h2>백업 / 복원</h2>
   <div class="help">기기 IRK·설정·SmartThings 앱 정보 (토큰 제외). IRK가 들어 있으니 안전하게 보관하세요.</div>
   <div class="row mt"><a href="/api/export" download="doorkey-backup.json"><button class="b">내보내기</button></a>
   <input type="file" id="impFile" accept=".json" style="flex:1"><button class="b" onclick="doImport(this)">가져오기</button></div></div>
  <div class="card"><h2>펌웨어 업데이트</h2><div id="updBox" class="kv"></div>
   <div class="row mt"><button class="b" onclick="post('/api/upd/check',{},this)">지금 확인</button><button class="b p hide" id="updBtn" onclick="doUpd(this)">설치</button></div>
   <div class="help">12시간마다 GitHub 릴리스를 확인하고 새 버전이 있으면 알림을 보냅니다. 서명이 맞는 펌웨어만 설치하고, 새 버전이 제대로 동작하지 않으면 이전 버전으로 되돌립니다.</div>
   <details class="mt"><summary class="mute">파일로 직접 올리기</summary>
    <div class="help">릴리스의 DoorKey.bin 또는 Actions 빌드의 _ota.bin (_full.bin은 USB 전용)</div>
    <div class="row mt"><input type="file" id="fwFile" accept=".bin" style="flex:1"><button class="b" onclick="doOta(this)">업로드</button></div>
    <div id="otaMsg" class="mute small"></div></details></div>
  <div class="card"><h2>재부팅</h2>
   <div class="help">약 20초 동안 귀가 감지와 웹 화면이 멈춥니다.</div>
   <div class="row mt"><button class="b" onclick="doReboot(this)">재부팅</button></div></div>
  <div class="card"><h2>설정 초기화</h2>
   <div class="help">등록 기기·SmartThings 인증·판정 설정을 모두 지웁니다. 되돌릴 수 없으니 먼저 백업을 내보내세요.</div>
   <div class="row mt"><button class="b d" onclick="doFactory(this)">설정 초기화…</button></div></div>
 </section>

 <section id="t-log" class="hide">
  <div class="card"><div class="row"><h2 style="margin:0">로그</h2><span class="sp"></span><button class="b" onclick="copyLog()">복사</button></div>
  <div id="log" class="log mt"></div></div>
 </section>
</main>
<dialog id="dlg"><form method="dialog">
 <h2 id="dlgT"></h2><p id="dlgM"></p>
 <div id="dlgTypeBox" class="hide"><label id="dlgTypeL" for="dlgType"></label><input id="dlgType" autocomplete="off"></div>
 <div class="row mt"><button value="cancel" class="b" id="dlgNo">취소</button><span class="sp"></span><button value="ok" class="b" id="dlgYes"></button></div>
</form></dialog>
<div id="toast" class="toast"></div>
<script>
const TABS=[["home","현황"],["dev","기기"],["set","설정"],["st","SmartThings"],["sys","시스템"],["log","로그"]];
const ST={home:"재실",away:"외출",lost:"신호 끊김",unknown:"미확인"};
const STALE_SEC=6;  // 이 시간 넘게 응답이 없으면 화면을 흐리게 (BLE와 무선 공유라 2~3초 지연은 정상)
let tab="home",S=null,CFG=null,logId=0,logLines=[],lastOk=0,inflight=false;
const $=id=>document.getElementById(id);
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
function toast(m){const t=$("toast");t.textContent=m;t.classList.add("on");clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove("on"),3000)}
function ago(s){if(s<0||s==null)return"—";if(s<60)return s+"초 전";if(s<3600)return Math.floor(s/60)+"분 전";if(s<86400)return Math.floor(s/3600)+"시간 전";return Math.floor(s/86400)+"일 전"}
function dur(s){if(s<60)return s+"초";if(s<3600)return Math.floor(s/60)+"분";return Math.floor(s/3600)+"시간 "+Math.floor(s%3600/60)+"분"}
// 요청마다 시간 제한 (ESP가 꺼져 있을 때 요청이 쌓이지 않게)
async function api(url,body,ms=4000){const ac=new AbortController(),tm=setTimeout(()=>ac.abort(),ms);
 const o=body===undefined?{signal:ac.signal}:{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body),signal:ac.signal};
 try{const r=await fetch(url,o);let j={};try{j=await r.json()}catch(e){}if(!r.ok||j.ok===false){throw new Error(j.err||("HTTP "+r.status))}return j}finally{clearTimeout(tm)}}
function errText(e){if(e.name==="AbortError")return"응답 없음 — DoorKey 전원과 Wi-Fi를 확인하세요";if(e instanceof TypeError)return"DoorKey에 연결할 수 없음 — 같은 Wi-Fi인지 확인하세요";return"실패: "+e.message}
// 누르는 즉시 버튼을 잠그고 진행 표시. hold(ms)만큼 완료 후에도 잠가 연타 방지
async function post(url,body,btn,hold){if(btn){if(btn.disabled)return;btn.disabled=true;btn.classList.add("busy")}
 try{const j=await api(url,body||{},8000);toast(j.msg||"완료");refresh();return j}catch(e){toast(errText(e))}
 finally{if(btn){btn.classList.remove("busy");if(hold)setTimeout(()=>btn.disabled=false,hold);else btn.disabled=false}}}
// 확인 대화상자: 동사형 버튼, 기본 포커스는 취소, Esc = 취소. type을 주면 그 단어를 입력해야 실행
function ask({title,msg,ok,danger=true,type}){return new Promise(res=>{const d=$("dlg"),y=$("dlgYes"),ti=$("dlgType");
 $("dlgT").textContent=title;$("dlgM").textContent=msg;y.textContent=ok;y.className="b "+(danger?"dd":"p");
 $("dlgTypeBox").classList.toggle("hide",!type);ti.value="";
 if(type){$("dlgTypeL").textContent=`실행하려면 "${type}"를 입력하세요`;y.disabled=true;ti.oninput=()=>y.disabled=ti.value.trim()!==type}else{y.disabled=false;ti.oninput=null}
 d.returnValue="cancel";d.onclose=()=>res(d.returnValue==="ok");d.showModal();$("dlgNo").focus()})}

function buildNav(){$("nav").innerHTML=TABS.map(([k,v])=>`<button data-k="${k}" class="${k==tab?"on":""}">${v}</button>`).join("");
 $("nav").querySelectorAll("button").forEach(b=>b.onclick=()=>{tab=b.dataset.k;buildNav();TABS.forEach(([k])=>$("t-"+k).classList.toggle("hide",k!=tab));if(tab=="set"||tab=="st"||tab=="sys")loadCfg();render()})}

function chip(el,cls,txt){el.className="chip "+cls;el.innerHTML='<span class="dot"></span>'+txt}

function render(){if(!S)return;
 chip($("cWifi"),S.ap?"warn":(S.rssi?"ok":"bad"),S.ap?"설정 AP":("Wi-Fi "+(S.rssi||"")));
 chip($("cBle"),S.ble.scanning?"ok":(S.enroll.st=="waiting"||S.enroll.st=="connected"?"warn":"bad"),"BLE "+S.ble.rate+"/s");
 const st=S.st;chip($("cSt"),st.broken?"bad":(st.hasToken?"ok":"warn"),st.broken?"ST 재인증":(st.hasToken?"ST 연결":"ST 미인증"));
 const sw=$("autoSw");if(document.activeElement!==sw&&!sw.disabled)sw.checked=S.auto;
 let b="";if(S.defaultPass)b+='<div class="banner bad">관리자 비밀번호가 기본값입니다. 시스템 탭에서 바꾸세요.</div>';
 if(st.broken)b+='<div class="banner bad">SmartThings 인증이 만료됐습니다. SmartThings 탭에서 다시 인증하세요.</div>';
 else if(!st.hasToken)b+='<div class="banner">SmartThings 인증 전입니다. 문을 열 수 없습니다.</div>';
 if(!S.auto)b+='<div class="banner">자동 개방이 꺼져 있습니다.</div>';
 const u=S.upd;if(u){if(u.rolled)b+=`<div class="banner bad">직전 업데이트가 정상 동작하지 않아 이전 버전으로 되돌렸습니다 (${esc(u.rolled)}).</div>`;
  if(u.st=="downloading")b+=`<div class="banner info">펌웨어 v${esc(u.latest)} 받는 중 ${u.progress}% — 끝나면 재부팅합니다.</div>`;
  else if(u.st=="available")b+=`<div class="banner info">새 펌웨어 v${esc(u.latest)}가 있습니다. 시스템 탭에서 설치하세요.</div>`;
  if(u.verifying)b+='<div class="banner info">새 펌웨어 동작 확인 중입니다 (약 1분 30초).</div>'}
 $("banners").innerHTML=b;
 if(tab=="home")renderHome();if(tab=="dev")renderDev();if(tab=="st")renderSt();if(tab=="sys")renderSys()}

function renderHome(){const box=$("devCards");
 if(!S.devices.length){box.innerHTML='<div class="card mute">등록된 기기가 없습니다. 기기 탭에서 폰을 등록하세요.</div>';}
 else{ if(box.children.length!=S.devices.length||box.dataset.k!=S.devices.map(d=>d.slot).join()){box.dataset.k=S.devices.map(d=>d.slot).join();
  box.innerHTML=S.devices.map(d=>`<div class="card" id="dc${d.slot}"><div class="row"><b class="nm"></b><span class="badge st"></span><span class="sp"></span>
  <span class="nw"><button class="b s" onclick="post('/api/device/state',{slot:${d.slot},away:true},this)">외출로</button><button class="b s" onclick="post('/api/device/state',{slot:${d.slot},away:false},this)">재실로</button></span></div>
  <canvas></canvas><div class="mute small l1"></div><div class="small l2"></div><div class="small l3"></div></div>`).join("")}
  S.devices.forEach(d=>{const c=$("dc"+d.slot);c.querySelector(".nm").textContent=d.name+(d.enabled?"":" (비활성)");
   const s=c.querySelector(".st");s.className="badge st b-"+d.st;s.textContent=ST[d.st]+" · "+dur(d.stFor);
   c.querySelector(".l1").textContent=(d.seenAgo<0?"감지 기록 없음":`감지 ${ago(d.seenAgo)} · ${d.rssi} dBm (평균 ${d.ema}) · 간격 ${d.itv}ms · 최대 공백 ${d.maxGap}초`)+(d.exitPeak>-127?` · 이탈 peak ${d.exitPeak}`:"");
   c.querySelector(".l2").textContent=d.event||"";c.querySelector(".l3").innerHTML=presLine(d);spark(c.querySelector("canvas"),d.spark)})}
 const m=S.st.cmd;$("lastCmd").innerHTML=m.code?`<b>${esc(m.who)}</b> · HTTP <b style="color:${m.code>=200&&m.code<300?"var(--ok)":"var(--bad)"}">${m.code}</b> · 요청 ${m.http}ms · 감지 후 ${m.total}ms · ${ago(m.ago)}<div class="mono mute">${esc(m.body)}</div>`:"없음"}

function spark(cv,v){const dpr=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;if(!w)return;cv.width=w*dpr;cv.height=h*dpr;const g=cv.getContext("2d");g.scale(dpr,dpr);
 const lo=-105,hi=-35,y=r=>h-(r-lo)/(hi-lo)*h,x=i=>i/(v.length-1)*(w-8)+4,css=getComputedStyle(document.body);
 g.font="10px sans-serif";
 [[S.params.arriveRssi,"귀가","--ok",0],[S.params.exitRssi,"이탈","--warn",1]].forEach(([r,t,c,right])=>{g.strokeStyle=css.getPropertyValue(c);g.globalAlpha=.6;g.setLineDash([4,4]);g.beginPath();g.moveTo(0,y(r));g.lineTo(w,y(r));g.stroke();g.setLineDash([]);g.fillStyle=g.strokeStyle;const lb=t+" "+r;g.fillText(lb,right?w-g.measureText(lb).width-4:4,y(r)-3);g.globalAlpha=1});
 g.fillStyle=css.getPropertyValue("--acc");v.forEach((r,i)=>{if(r>-127){g.beginPath();g.arc(x(i),y(Math.max(lo,Math.min(hi,r))),2.5,0,7);g.fill()}})}

function renderDev(){const e=S.enroll,busy=e.st=="waiting"||e.st=="connected";if(!$("enBtn").classList.contains("busy"))$("enBtn").disabled=busy;
 $("enBox").classList.toggle("hide",e.st=="idle");$("enMsg").className="banner"+(e.st=="failed"?" bad":"");
 $("enMsg").textContent=(busy?`[${e.name}] 남은 ${e.left}초 — `:`[${e.name}] `)+e.msg;
 const L=$("devList");const key=S.devices.map(d=>d.slot+d.name+d.enabled).join("|");
 // 삭제는 저장과 다른 줄·반대편에 둬서 잘못 누르지 않게
 if(L.dataset.k!==key){L.dataset.k=key;L.innerHTML=S.devices.length?S.devices.map(d=>`<div class="dev">
  <div class="row"><input value="${esc(d.name)}" maxlength="12" style="flex:1;min-width:120px" id="nm${d.slot}">
  <label class="ck"><input type="checkbox" id="en${d.slot}" ${d.enabled?"checked":""}> 사용</label>
  <button class="b" onclick="updDev(${d.slot},this)">저장</button></div>
  <div class="row"><span class="mono mute">${d.idAddr?"기기 주소 "+esc(d.idAddr)+" · ":""}IRK ${esc(d.irk)}</span><span class="sp"></span>
  <button class="b d" onclick="delDev(${d.slot},this)">삭제…</button></div></div>`).join(""):'<div class="mute">없음</div>'}
 const a=S.ble.app;$("appDiag").innerHTML=a.ago<0?"감지 안 됨":`${ago(a.ago)} · ${a.rssi} dBm · `+(a.slot>=0?`<b style="color:var(--ok)">해석됨 (${esc(a.name)})</b>`:'<b style="color:var(--warn)">미해석 — 이 폰을 먼저 등록하세요</b>')}

function renderSt(){const s=S.st;$("stState").innerHTML=[
 ["인증",s.broken?"만료 (재인증 필요)":(s.hasToken?"완료":"안 됨")],
 ["액세스 토큰 만료",s.expIn<0?"—":dur(s.expIn)+" 후"],["마지막 갱신",s.refAgo<0?"이번 부팅엔 없음":ago(s.refAgo)+" (HTTP "+s.refCode+")"],
 ["연결 유지",(s.warm?"연결됨":"끊김")+` · 연결 ${s.warmN}회 · 평균 유지 ${s.warmLife}초 · TLS ${s.tls}ms`],
 ["연결 확인",s.check.code?`HTTP ${s.check.code} · ${ago(s.check.ago)}`:"—"],["",`<span class="mono">${esc(s.check.body)}</span>`],
 ["오류",esc(s.err)||"—"]].map(([k,v])=>`<div>${k}</div><div>${v}</div>`).join("");renderPres()}

// SmartThings 폰 위치
const PRES_STALE=180,UUID=/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const GATE={ok:["c-ok","외출 확인됨 — 귀가하면 문을 엽니다"],wait:["c-warn","SmartThings상 아직 집 — 이대로 들어오면 문을 열지 않습니다"]};
function fmtT(e){if(!e)return"";const t=new Date(e*1000),p=n=>String(n).padStart(2,"0");return`${p(t.getMonth()+1)}-${p(t.getDate())} ${p(t.getHours())}:${p(t.getMinutes())}`}
function presText(d){const p=d.pres;
 if(!d.presId)return["c-mute","위치 확인 안 함 — 블루투스만으로 판단"];
 if(!p||(p.ok<0&&p.try<0))return["c-mute","조회 대기 중…"];
 if(p.ok<0)return["c-bad","조회 실패: "+(p.err||"HTTP "+p.code)+" — 블루투스만으로 판단"];
 let s="SmartThings 위치: "+(p.val==1?"집":"외출")+(p.since?` (${fmtT(p.since)}부터)`:"")+" · "+ago(p.ok)+" 확인";
 if(p.ok>PRES_STALE)return["c-warn",s+" — 오래됨"+(p.err?` (최근 조회 실패: ${p.err})`:"")+", 블루투스만으로 판단"];
 return[p.val==1?"c-ok":"c-acc",s]}
function presLine(d){if(!d.presId)return"";const[c,t]=presText(d);let h=`<span class="${c}">${esc(t)}</span>`;
 const g=d.st=="away"&&d.pres&&d.pres.gate!="stale"&&GATE[d.pres.gate];if(g)h+=`<br><b class="${g[0]}">${esc(g[1])}</b>`;return h}
function renderPres(){const L=$("presList");
 if(!S.devices.length){L.dataset.k="";L.innerHTML='<div class="mute">등록된 기기가 없습니다. 기기 탭에서 먼저 폰을 등록하세요.</div>';return}
 const key=S.devices.map(d=>d.slot+":"+d.name+":"+d.presId).join("|");
 if(L.dataset.k!==key){L.dataset.k=key;L.innerHTML=S.devices.map(d=>`<div class="dev"><label for="pi${d.slot}">${esc(d.name)} — 위치 기기 ID</label>
  <input id="pi${d.slot}" class="mono" value="${esc(d.presId)}" placeholder="비우면 위치 확인 안 함" autocomplete="off" spellcheck="false" autocapitalize="off"><div class="small" id="pr${d.slot}"></div></div>`).join("")}
 S.devices.forEach(d=>{const e=$("pr"+d.slot);if(e){const[c,t]=presText(d);e.className="small "+c;e.textContent=t}})}
function savePres(btn){const items=S.devices.map(d=>{const e=$("pi"+d.slot);return{slot:d.slot,id:e?e.value.trim():""}});
 const bad=items.find(i=>i.id&&!UUID.test(i.id));if(bad){const d=S.devices.find(x=>x.slot==bad.slot);$("pi"+bad.slot).focus();return toast(`${d.name}: ID는 8-4-4-4-12자리 형식이어야 합니다`)}
 post("/api/pres",{items},btn)}

function renderSys(){$("sysInfo").innerHTML=[["IP",S.ip],["Wi-Fi",S.ssid+(S.rssi?` (${S.rssi} dBm)`:"")],["가동",dur(S.uptime)],["힙",`${Math.round(S.heap/1024)}KB (최소 ${Math.round(S.minHeap/1024)}KB)`],["PSRAM",Math.round(S.psram/1024)+"KB"],
 ["BLE 광고",`${S.ble.adv} (RPA ${S.ble.rpa}, 캐시 ${S.ble.hit}, 누락 ${S.ble.drop})`],["펌웨어",S.fw]].map(([k,v])=>`<div>${k}</div><div>${esc(v)}</div>`).join("");
 const u=S.upd||{},UST={idle:"확인 전",checking:"확인 중…",available:"새 버전 있음",latest:"최신 버전입니다",downloading:`받는 중 ${u.progress}%`,error:"오류"};
 $("updBox").innerHTML=[["현재 버전",S.fw],["최신 릴리스",u.latest||"—"],["마지막 확인",u.checkedAgo<0?"—":ago(u.checkedAgo)],["상태",(UST[u.st]||u.st)+(u.st=="error"&&u.err?" — "+u.err:"")],...(u.notes&&u.st=="available"?[["변경 내용",u.notes]]:[])].map(([k,v])=>`<div>${k}</div><div>${esc(v)}</div>`).join("");
 const ub=$("updBtn");ub.classList.toggle("hide",u.st!="available");if(u.st=="available"&&!ub.classList.contains("busy"))ub.textContent=`v${u.latest} 설치`}

const PF=[["arriveRssi","귀가 RSSI (dBm)","외출 상태에서 이 값 이상으로 잡히면 귀가로 판단. 엘리베이터 앞 실측값보다 조금 낮게."],
 ["confirmCount","귀가 확인 횟수","5초 안에 위 조건을 N번 만족해야 판단. 1이 가장 빠름."],
 ["exitRssi","이탈 RSSI (dBm)","사라지기 직전 최대 RSSI가 이 값 이상이면 '현관으로 나감'(외출). 낮으면 '집 안에서 신호만 끊김'."],
 ["exitWindowSec","이탈 판정 구간 (초)","마지막 감지 이전 N초 동안의 최대 RSSI를 봅니다. 최대 300."],
 ["absentSec","부재 판정 (초)","이 시간 동안 안 잡히면 사라진 것으로 봅니다. 폰의 '최대 공백'보다 길게."],
 ["minAwaySec","최소 외출 (초)","이보다 짧은 외출은 자동으로 열지 않습니다."],
 ["cooldownSec","쿨다운 (초)","문 열기 요청 사이 최소 간격 (둘이 같이 올 때 중복 방지)."],
 ["activeFrom","허용 시작 (시)","0~23"],["activeTo","허용 종료 (시)","1~24. 0~24면 항상 허용"],
 ["requireNewAddr","재생 공격 방지","귀가 시 외출 전과 다른 BLE 주소를 요구합니다. 폰 주소는 약 15분마다 바뀌므로 짧은 외출은 안 열릴 수 있음.","b"],
 ["keepWarm","연결 미리 유지","외출 중인 사람이 있으면 SmartThings 연결을 열어 둬 응답을 빠르게 합니다.","b"],
 ["notifyUnlock","문 열림 알림","문을 열 때마다 알림을 보냅니다 (알림 URL 설정 시).","b"]];
function renderParams(){$("pForm").innerHTML=PF.map(([k,t,h,ty])=>ty=="b"?`<div><label class="ck"><input type="checkbox" id="p_${k}" ${CFG.params[k]?"checked":""}> ${t}</label><div class="help">${h}</div></div>`
 :`<div><label>${t}</label><input id="p_${k}" type="number" inputmode="numeric" value="${CFG.params[k]}"><div class="help">${h}</div></div>`).join("")}
async function loadCfg(){try{CFG=await api("/api/config");renderParams();const s=CFG.st;$("sCid").value=s.clientId;$("sCsec").value="";$("sCsec").placeholder=s.hasSecret?"(저장됨 — 바꿀 때만 입력)":"";$("sRedir").value=s.redirect;
 $("sDev").value=s.deviceId;$("sComp").value=s.component;$("sCap").value=s.capability;$("sCmd").value=s.command;$("sArgs").value=s.args;$("ntfy").value=CFG.ntfy;$("wSsid").value=CFG.wifiSsid;authLink()}catch(e){toast("설정 읽기 실패 — "+errText(e))}}
function authLink(){const cid=$("sCid").value.trim(),r=$("sRedir").value.trim();$("authLink").href=`https://api.smartthings.com/oauth/authorize?client_id=${encodeURIComponent(cid)}&response_type=code&redirect_uri=${encodeURIComponent(r)}&scope=${encodeURIComponent("r:devices:* x:devices:*")}`}
["sCid","sRedir"].forEach(i=>$(i).addEventListener("input",authLink));
function saveParams(btn){const o={};PF.forEach(([k,,,ty])=>{const e=$("p_"+k);o[k]=ty=="b"?e.checked:parseInt(e.value,10)});post("/api/params",o,btn)}
function saveSt(btn){post("/api/st",{clientId:$("sCid").value.trim(),clientSecret:$("sCsec").value.trim(),redirect:$("sRedir").value.trim(),deviceId:$("sDev").value.trim(),component:$("sComp").value.trim(),capability:$("sCap").value.trim(),command:$("sCmd").value.trim(),args:$("sArgs").value.trim()||"[]"},btn)}
function sendCode(btn){const c=$("sCode").value.trim();if(!c)return toast("인증 후 이동한 페이지 주소를 붙여넣으세요");post("/api/st/code",{code:c},btn).then(j=>{if(j)$("sCode").value=""})}
async function testUnlock(btn){if(await ask({title:"현관문 열기",msg:"지금 현관문이 실제로 열립니다. 도어락 자동 잠금이 켜져 있는지 확인하세요.",ok:"현관문 열기"}))post("/api/st/test",{},btn,5000)}
function enroll(btn){post("/api/enroll",{name:$("enName").value.trim()},btn)}
function updDev(s,btn){post("/api/device/update",{slot:s,name:$("nm"+s).value.trim(),enabled:$("en"+s).checked},btn)}
async function delDev(s,btn){const d=S.devices.find(x=>x.slot==s),n=d?d.name:"기기";
 if(await ask({title:n+" 삭제",msg:"이 폰의 신원 키(IRK)가 지워져 더 이상 귀가를 감지하지 못합니다. 다시 쓰려면 폰 블루투스 페어링부터 다시 등록해야 합니다.",ok:n+" 삭제"}))post("/api/device/delete",{slot:s},btn)}
function addManual(btn){post("/api/device/add",{name:$("mName").value.trim(),irk:$("mIrk").value.trim()},btn)}
function saveNtfy(btn){post("/api/ntfy",{url:$("ntfy").value.trim()},btn)}
async function saveWifi(btn){if(await ask({title:"Wi-Fi 변경",msg:"저장 후 재부팅합니다. SSID나 비밀번호가 틀리면 DoorKey-Setup AP에 접속해 다시 설정해야 합니다.",ok:"저장 후 재부팅",danger:false}))post("/api/wifi",{ssid:$("wSsid").value,pass:$("wPass").value},btn)}
function saveAdmin(btn){const p=$("aPass").value;if(p.length<6)return toast("비밀번호는 6자 이상이어야 합니다");post("/api/admin",{pass:p},btn).then(j=>{if(j){$("aPass").value="";toast("변경됨 — 새 비밀번호로 다시 로그인")}})}
async function doImport(btn){const f=$("impFile").files[0];if(!f)return toast("백업 파일(.json)을 먼저 고르세요");let o;try{o=JSON.parse(await f.text())}catch(e){return toast("백업 파일 형식이 아닙니다 (JSON 오류)")}
 if(await ask({title:"백업 가져오기",msg:"등록된 기기 목록과 판정 설정, SmartThings 앱 정보가 백업 파일 내용으로 바뀝니다. 지금 등록된 기기는 지워집니다.",ok:"덮어쓰기"}))post("/api/import",o,btn)}
function doOta(btn){const f=$("fwFile").files[0];if(!f)return toast("_ota.bin 파일을 먼저 고르세요");btn.disabled=true;btn.classList.add("busy");const fd=new FormData();fd.append("fw",f);const x=new XMLHttpRequest();
 const done=m=>{$("otaMsg").textContent=m;btn.disabled=false;btn.classList.remove("busy")};
 x.upload.onprogress=e=>{$("otaMsg").textContent="업로드 "+Math.round(e.loaded/e.total*100)+"%"};
 x.onload=()=>done(x.status==200&&x.responseText.includes("true")?"완료 — 재부팅합니다":"실패: "+x.responseText);x.onerror=()=>done("업로드 실패 — 연결이 끊겼습니다");x.open("POST","/update");x.send(fd)}
async function doUpd(btn){const u=S.upd;if(await ask({title:`v${u.latest} 설치`,msg:`다운로드와 설치에 1분 정도 걸리고 끝나면 재부팅합니다. 그동안 귀가 감지와 문 열기가 멈춥니다. 새 버전이 정상 동작하지 않으면 자동으로 이전 버전(v${S.fw})으로 돌아갑니다.`+(u.notes?` 변경 내용: ${u.notes}`:""),ok:`v${u.latest} 설치`,danger:false}))post("/api/upd/install",{},btn)}
async function doReboot(btn){if(await ask({title:"재부팅",msg:"약 20초 동안 귀가 감지와 웹 화면이 멈춥니다.",ok:"재부팅",danger:false}))post("/api/reboot",{},btn)}
async function doFactory(btn){if(await ask({title:"설정 초기화",msg:"등록 기기, SmartThings 인증·앱 정보, 판정 설정, 알림 URL, 관리자 비밀번호가 모두 지워지고 재부팅합니다. Wi-Fi는 펌웨어 기본값으로 돌아갑니다. 되돌릴 수 없습니다.",ok:"모두 지우고 초기화",type:"초기화"}))post("/api/factory",{},btn)}
function copyLog(){navigator.clipboard.writeText(logLines.join("\n")).then(()=>toast("복사됨"))}
$("autoSw").onchange=e=>post("/api/params",{autoEnabled:e.target.checked},e.target);

// 마지막 갱신 표시. 응답이 끊기면 본문을 흐리게 하고 경고
function age(){if(document.hidden)return;const el=$("age"),sb=$("staleBar");
 if(!lastOk){el.textContent="연결 중…";return}
 const s=Math.floor((Date.now()-lastOk)/1000),stale=s>=STALE_SEC;
 el.textContent=stale?"⚠ 연결 끊김":`갱신 ${s}초 전`;el.classList.toggle("bad",stale);
 document.body.classList.toggle("stale",stale);sb.classList.toggle("hide",!stale);
 if(stale)sb.textContent=`DoorKey 응답 없음 — 아래는 ${ago(s)} 데이터입니다. 전원과 Wi-Fi를 확인하세요. 자동으로 다시 연결합니다.`}
async function refresh(){if(inflight)return;inflight=true;
 try{S=await api("/api/status",undefined,5000);lastOk=Date.now();render();if(S.logId>logId)pullLog()}catch(e){}finally{inflight=false;age()}}
async function pullLog(){try{const r=await api("/api/log?since="+logId);r.forEach(e=>{logLines.push(e.t+"  "+e.m);logId=e.id});if(logLines.length>400)logLines=logLines.slice(-400);
 const L=$("log"),bottom=L.scrollHeight-L.scrollTop-L.clientHeight<30;L.textContent=logLines.join("\n");if(bottom)L.scrollTop=L.scrollHeight}catch(e){}}
document.addEventListener("visibilitychange",()=>{if(!document.hidden)refresh()});
buildNav();refresh();setInterval(()=>{if(!document.hidden)refresh()},2000);setInterval(age,1000);
</script></body></html>)HTML";
