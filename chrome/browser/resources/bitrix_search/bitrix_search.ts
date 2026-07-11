// @ts-nocheck
import {addWebUiListener} from 'chrome://resources/js/cr.js';
const $=id=>document.getElementById(id);let currentQuery='';
const escapeHtml=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function renderAnswer(text,sources=[]){const valid=new Set(sources.map(s=>Number(s.id)));let html=escapeHtml(text).replace(/^### (.*)$/gm,'<h3>$1</h3>').replace(/^## (.*)$/gm,'<h2>$1</h2>').replace(/\*\*(.*?)\*\*/g,'<strong>$1</strong>').replace(/\n\n/g,'</p><p>').replace(/\n/g,'<br>');html=html.replace(/\[sourceId:(\d+)\]/g,(m,id)=>valid.has(Number(id))?`<a class="cite" href="#source-${id}">${id}</a>`:'');$('answer').classList.remove('skeleton');$('answer').innerHTML=`<p>${html}</p>`}
function addStep(text,active=true){document.querySelectorAll('#steps li.active').forEach(x=>x.classList.remove('active'));const li=document.createElement('li');li.textContent=text;li.className=active?'active':'';$('steps').append(li)}
function start(q){q=q.trim();if(!q)return;currentQuery=q;$('query').value=q;$('hero').hidden=true;$('layout').hidden=false;$('error').hidden=true;$('answer').className='answer skeleton';$('answer').innerHTML='<i></i><i></i><i></i>';$('steps').textContent='';$('sources').textContent='';$('sourcesWrap').hidden=true;$('title').textContent=q;addStep('Подключаюсь к поисковому агенту');chrome.send('startSearch',[q])}
function renderSources(items){if(!items?.length)return;$('sourcesWrap').hidden=false;for(const s of items){try{const u=new URL(s.url);if(!['http:','https:'].includes(u.protocol))continue;const a=document.createElement('a');a.className='source';a.id=`source-${s.id}`;a.href=u.href;a.target='_blank';a.rel='noopener';a.innerHTML=`<div class="domain">${escapeHtml(u.hostname)}</div><b>${escapeHtml(s.title||u.hostname)}</b><p>${escapeHtml(s.description||'Открыть источник')}</p>`;$('sources').append(a)}catch(e){}}}
addWebUiListener('bitrix-search-started',()=>addStep('Ищу релевантные источники'));
addWebUiListener('bitrix-search-progress',e=>addStep(e.message||'Анализирую найденные материалы'));
addWebUiListener('bitrix-search-answer',e=>renderAnswer(e.answer||'',e.sources||[]));
addWebUiListener('bitrix-search-completed',e=>{addStep('Ответ готов',false);renderAnswer(e.answer||'',e.sources||[]);renderSources(e.sources||[])});
addWebUiListener('bitrix-search-failed',e=>{$('layout').hidden=true;$('error').hidden=false;$('errorText').textContent=e.message||'Проверьте подключение и повторите попытку.'});
$('searchForm').addEventListener('submit',e=>{e.preventDefault();start($('query').value)});$('retry').onclick=()=>start(currentQuery);window.addEventListener('beforeunload',()=>chrome.send('cancelSearch'));const q=new URLSearchParams(location.search).get('q');if(q)start(q);
