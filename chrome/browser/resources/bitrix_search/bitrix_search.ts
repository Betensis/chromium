// @ts-nocheck
import {addWebUiListener} from 'chrome://resources/js/cr.js';

// ---------------------------------------------------------------------------
// Small DOM helpers (no innerHTML/insertAdjacentHTML/document.write anywhere).
// ---------------------------------------------------------------------------

const SVG_NS = 'http://www.w3.org/2000/svg';

function $(id) {
  return document.getElementById(id);
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined && text !== null) node.textContent = text;
  return node;
}

function svg(attrs, children) {
  const node = document.createElementNS(SVG_NS, 'svg');
  node.setAttribute('viewBox', '0 0 24 24');
  node.setAttribute('width', '16');
  node.setAttribute('height', '16');
  node.setAttribute('fill', 'none');
  node.setAttribute('stroke', 'currentColor');
  node.setAttribute('stroke-width', '1.8');
  node.setAttribute('stroke-linecap', 'round');
  node.setAttribute('stroke-linejoin', 'round');
  for (const k in attrs || {}) node.setAttribute(k, attrs[k]);
  for (const c of children || []) node.append(c);
  return node;
}

function path(d, attrs) {
  const node = document.createElementNS(SVG_NS, 'path');
  node.setAttribute('d', d);
  for (const k in attrs || {}) node.setAttribute(k, attrs[k]);
  return node;
}

function circle(cx, cy, r, attrs) {
  const node = document.createElementNS(SVG_NS, 'circle');
  node.setAttribute('cx', String(cx));
  node.setAttribute('cy', String(cy));
  node.setAttribute('r', String(r));
  for (const k in attrs || {}) node.setAttribute(k, attrs[k]);
  return node;
}

function line(x1, y1, x2, y2) {
  const node = document.createElementNS(SVG_NS, 'line');
  node.setAttribute('x1', String(x1));
  node.setAttribute('y1', String(y1));
  node.setAttribute('x2', String(x2));
  node.setAttribute('y2', String(y2));
  return node;
}

function rect(x, y, w, h, rx) {
  const node = document.createElementNS(SVG_NS, 'rect');
  node.setAttribute('x', String(x));
  node.setAttribute('y', String(y));
  node.setAttribute('width', String(w));
  node.setAttribute('height', String(h));
  if (rx) node.setAttribute('rx', String(rx));
  return node;
}

// ---------------------------------------------------------------------------
// Icon library — hand-built inline SVGs (no external assets).
// ---------------------------------------------------------------------------

function icon(name) {
  switch (name) {
    case 'search':
      return svg({}, [circle(10, 10, 6.2), line(14.6, 14.6, 20, 20)]);
    case 'globe':
    case 'link':
      return svg({}, [
        path('M9 15l-2.5 2.5a4 4 0 01-5.7-5.7L4 9.5'),
        path('M15 9l2.5-2.5a4 4 0 015.7 5.7L21 14.5'),
        line(9, 15, 15, 9),
      ]);
    case 'doc':
      return svg({}, [
        path('M6 3h8l5 5v13a1 1 0 01-1 1H6a1 1 0 01-1-1V4a1 1 0 011-1z'),
        path('M14 3v5h5'),
        line(8.5, 13, 15.5, 13),
        line(8.5, 16.5, 13, 16.5),
      ]);
    case 'brain':
      return svg({}, [circle(6, 12, 1.6, {fill: 'currentColor', stroke: 'none'}),
        circle(12, 12, 1.6, {fill: 'currentColor', stroke: 'none'}),
        circle(18, 12, 1.6, {fill: 'currentColor', stroke: 'none'})]);
    case 'spark':
      return svg({}, [path('M12 2l2.1 7.9L22 12l-7.9 2.1L12 22l-2.1-7.9L2 12l7.9-2.1z',
        {fill: 'currentColor', stroke: 'none'})]);
    case 'check':
      return svg({}, [path('M4 12.5l5.2 5.2L20 6.3')]);
    case 'alert':
      return svg({}, [
        path('M12 2.5L22.5 21H1.5z'),
        line(12, 10, 12, 14.5),
        circle(12, 17.5, 0.15, {fill: 'currentColor'}),
      ]);
    case 'zap':
      return svg({}, [path('M13 2L4.5 14H11l-1.5 8L20.5 9H13.8z',
        {fill: 'currentColor', stroke: 'none'})]);
    case 'lock':
      return svg({}, [
        rect(5, 10.5, 14, 10, 3),
        path('M7.5 10.5V7.2a4.5 4.5 0 019 0v3.3'),
      ]);
    case 'x':
      return svg({}, [line(6, 6, 18, 18), line(18, 6, 6, 18)]);
    case 'gear': {
      const children = [circle(12, 12, 3.4)];
      for (let i = 0; i < 8; i++) {
        const r = rect(10.6, 1.6, 2.8, 4.2, 1.2);
        r.setAttribute('transform', `rotate(${i * 45} 12 12)`);
        children.push(r);
      }
      return svg({}, children);
    }
    default:
      return svg({}, []);
  }
}

function mountIcon(hostId, name) {
  const host = $(hostId);
  if (host) host.append(icon(name));
}

function mountIconsByAttr() {
  const nodes = document.querySelectorAll('[data-icon]');
  nodes.forEach((node) => {
    node.append(icon(node.getAttribute('data-icon')));
  });
}

// ---------------------------------------------------------------------------
// Domain / avatar helpers.
// ---------------------------------------------------------------------------

const AVATAR_COLORS = ['#2fc6f6', '#6c7cf0', '#a78bfa', '#34d8b9', '#f5b75b', '#f5789b'];

// ---------------------------------------------------------------------------
// Search modes.
// ---------------------------------------------------------------------------

const VALID_MODES = new Set(['simple', 'deep']);
const MODE_LABELS = {simple: 'Быстрый', deep: 'Deep Research'};

function parseMode(raw) {
  return VALID_MODES.has(raw) ? raw : 'deep';
}

function colorForDomain(domain) {
  let h = 0;
  for (let i = 0; i < domain.length; i++) {
    h = (h * 31 + domain.charCodeAt(i)) >>> 0;
  }
  return AVATAR_COLORS[h % AVATAR_COLORS.length];
}

function hostnameOf(url) {
  try {
    const u = new URL(url);
    if (u.protocol === 'http:' || u.protocol === 'https:') return u.hostname;
  } catch (e) {
    // ignore malformed urls
  }
  return null;
}

function uniqueDomains(urls) {
  const seen = new Set();
  const out = [];
  for (const u of urls || []) {
    const h = hostnameOf(u);
    if (h && !seen.has(h)) {
      seen.add(h);
      out.push(h);
    }
  }
  return out;
}

function domainChip(domain) {
  const wrap = el('span', 'domainChip');
  const avatar = el('span', 'domainChip__avatar', (domain[0] || '?').toUpperCase());
  avatar.style.background = colorForDomain(domain);
  const label = el('span', 'domainChip__label', domain);
  wrap.append(avatar, label);
  return wrap;
}

function domainChipsRow(domains) {
  const wrap = el('div', 'domainChips');
  domains.forEach((d) => wrap.append(domainChip(d)));
  return wrap;
}

// ---------------------------------------------------------------------------
// Safe markdown renderer (DOM nodes only — Trusted Types safe).
// ---------------------------------------------------------------------------

const INLINE_RE =
    /\[sourceId:(?<cite>\d+)\]|\[(?<linkText>[^\]]+)\]\((?<linkUrl>https?:\/\/[^\s)]+)\)|\*\*(?<bold>[^*]+)\*\*|`(?<code>[^`]+)`|\*(?<italic>[^*]+)\*/g;

function appendInline(container, text, validSourceIds) {
  const re = new RegExp(INLINE_RE.source, 'g');
  let lastIndex = 0;
  let m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > lastIndex) {
      container.append(document.createTextNode(text.slice(lastIndex, m.index)));
    }
    const g = m.groups || {};
    if (g.cite !== undefined) {
      const num = g.cite;
      const ok = validSourceIds === null || validSourceIds.has(Number(num));
      if (ok) {
        const a = el('a', 'cite', num);
        a.setAttribute('href', `#source-${num}`);
        container.append(a);
      }
    } else if (g.linkText !== undefined) {
      let safeUrl = null;
      try {
        const u = new URL(g.linkUrl);
        if (u.protocol === 'http:' || u.protocol === 'https:') safeUrl = u.href;
      } catch (e) {
        safeUrl = null;
      }
      if (safeUrl) {
        const a = document.createElement('a');
        a.href = safeUrl;
        a.target = '_blank';
        a.rel = 'noopener';
        appendInline(a, g.linkText, validSourceIds);
        container.append(a);
      } else {
        container.append(document.createTextNode(m[0]));
      }
    } else if (g.bold !== undefined) {
      const strong = document.createElement('strong');
      appendInline(strong, g.bold, validSourceIds);
      container.append(strong);
    } else if (g.code !== undefined) {
      container.append(el('code', undefined, g.code));
    } else if (g.italic !== undefined) {
      const em = document.createElement('em');
      appendInline(em, g.italic, validSourceIds);
      container.append(em);
    }
    lastIndex = re.lastIndex;
  }
  if (lastIndex < text.length) {
    container.append(document.createTextNode(text.slice(lastIndex)));
  }
}

function renderMarkdown(text, validSourceIds) {
  const root = document.createDocumentFragment();
  const src = String(text ?? '').replace(/\r\n/g, '\n');
  const lines = src.split('\n');

  let currentList = null; // {type: 'ul'|'ol', el}
  let paraLines = [];

  function flushList() {
    if (currentList) {
      root.append(currentList.el);
      currentList = null;
    }
  }

  function flushPara() {
    if (paraLines.length) {
      const p = document.createElement('p');
      paraLines.forEach((ln, idx) => {
        if (idx > 0) p.append(document.createElement('br'));
        appendInline(p, ln, validSourceIds);
      });
      root.append(p);
      paraLines = [];
    }
  }

  for (const rawLine of lines) {
    if (rawLine.trim() === '') {
      flushPara();
      flushList();
      continue;
    }

    const headingMatch = /^(#{2,3})\s+(.*)$/.exec(rawLine);
    if (headingMatch) {
      flushPara();
      flushList();
      const level = headingMatch[1].length === 2 ? 'h2' : 'h3';
      const h = document.createElement(level);
      appendInline(h, headingMatch[2], validSourceIds);
      root.append(h);
      continue;
    }

    const ulMatch = /^[-*]\s+(.*)$/.exec(rawLine);
    if (ulMatch) {
      flushPara();
      if (!currentList || currentList.type !== 'ul') {
        flushList();
        currentList = {type: 'ul', el: document.createElement('ul')};
      }
      const li = document.createElement('li');
      appendInline(li, ulMatch[1], validSourceIds);
      currentList.el.append(li);
      continue;
    }

    const olMatch = /^\d+\.\s+(.*)$/.exec(rawLine);
    if (olMatch) {
      flushPara();
      if (!currentList || currentList.type !== 'ol') {
        flushList();
        currentList = {type: 'ol', el: document.createElement('ol')};
      }
      const li = document.createElement('li');
      appendInline(li, olMatch[1], validSourceIds);
      currentList.el.append(li);
      continue;
    }

    flushList();
    paraLines.push(rawLine);
  }
  flushPara();
  flushList();
  return root;
}

function caretSpan() {
  const s = el('span', 'caret');
  s.setAttribute('aria-hidden', 'true');
  return s;
}

function appendCaret(container) {
  let target = container.lastElementChild;
  if (!target) {
    container.append(caretSpan());
    return;
  }
  if (target.tagName === 'UL' || target.tagName === 'OL') {
    target = target.lastElementChild || target;
  }
  target.append(caretSpan());
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------

let currentQuery = '';
let currentMode = 'deep'; // 'simple' | 'deep'
let currentState = 'empty'; // 'empty' | 'onboarding' | 'active'
let tokenConfigured = false;
let tokenSubmittedThisSession = false;

let latestAnswerText = '';
let rafScheduled = false;
let doneFinalized = false;
let lastProgressKind = null;

function setState(name) {
  currentState = name;
  $('heroSection').hidden = name !== 'empty';
  $('onboardingSection').hidden = name !== 'onboarding';
  $('resultsSection').hidden = name !== 'active';
  document.body.dataset.state = name;
  updateModeSwitches();
}

function start(query) {
  const q = (query || '').trim();
  if (!q) return;
  location.href =
      `chrome://bitrix-search/?q=${encodeURIComponent(q)}&mode=${currentMode}`;
}

// ---------------------------------------------------------------------------
// Mode switch (segmented control).
// ---------------------------------------------------------------------------

function renderModeBadge(mode) {
  $('modeBadge').textContent = MODE_LABELS[mode] || MODE_LABELS.deep;
}

function updateModeSwitches() {
  document.querySelectorAll('.modeSwitch').forEach((wrap) => {
    const buttons = wrap.querySelectorAll('.modeSwitch__btn');
    let activeBtn = null;
    buttons.forEach((btn) => {
      const isActive = btn.getAttribute('data-mode') === currentMode;
      btn.setAttribute('aria-pressed', isActive ? 'true' : 'false');
      if (isActive) activeBtn = btn;
    });
    const thumb = wrap.querySelector('.modeSwitch__thumb');
    if (thumb && activeBtn) {
      thumb.style.width = `${activeBtn.offsetWidth}px`;
      thumb.style.transform = `translateX(${activeBtn.offsetLeft}px)`;
    }
  });
}

function setMode(mode) {
  currentMode = mode;
  updateModeSwitches();
}

function handleModeClick(mode) {
  if (!VALID_MODES.has(mode) || mode === currentMode) return;
  if (currentQuery) {
    location.href = `chrome://bitrix-search/?q=${
        encodeURIComponent(currentQuery)}&mode=${mode}`;
    return;
  }
  setMode(mode);
}

function wireModeSwitches() {
  document.querySelectorAll('.modeSwitch').forEach((wrap) => {
    wrap.querySelectorAll('.modeSwitch__btn').forEach((btn) => {
      btn.addEventListener(
          'click', () => handleModeClick(btn.getAttribute('data-mode')));
    });
  });
  window.addEventListener('resize', () => updateModeSwitches());
}

// ---------------------------------------------------------------------------
// Answer rendering.
// ---------------------------------------------------------------------------

function resetAnswerToSkeleton() {
  latestAnswerText = '';
  doneFinalized = false;
  const content = $('answerContent');
  content.replaceChildren();
  content.classList.add('skeleton');
  const widths = [undefined, 'skeletonLine--w2', 'skeletonLine--w3'];
  widths.forEach((w) => {
    const line = el('div', w ? `skeletonLine ${w}` : 'skeletonLine');
    content.append(line);
  });
}

function setAnswerContent(fragment, streaming) {
  const content = $('answerContent');
  content.classList.remove('skeleton');
  content.replaceChildren(fragment);
  if (streaming) appendCaret(content);
}

function scheduleAnswerRender() {
  if (rafScheduled) return;
  rafScheduled = true;
  requestAnimationFrame(() => {
    rafScheduled = false;
    if (doneFinalized) return;
    if (!latestAnswerText || !latestAnswerText.trim()) return;
    const frag = renderMarkdown(latestAnswerText, null);
    setAnswerContent(frag, true);
  });
}

function finalizeAnswer(answerText, sources) {
  doneFinalized = true;
  const idSet = new Set((sources || []).map((s) => s.id));
  const frag = renderMarkdown(answerText || '', idSet);
  setAnswerContent(frag, false);
}

// ---------------------------------------------------------------------------
// Sources.
// ---------------------------------------------------------------------------

function renderSources(sources) {
  const grid = $('sourcesGrid');
  grid.replaceChildren();
  let count = 0;
  for (const s of sources || []) {
    let u;
    try {
      u = new URL(s.url);
    } catch (e) {
      continue;
    }
    if (u.protocol !== 'http:' && u.protocol !== 'https:') continue;

    const a = document.createElement('a');
    a.className = 'sourceCard';
    a.id = `source-${s.id}`;
    a.href = u.href;
    a.target = '_blank';
    a.rel = 'noopener';

    const top = el('div', 'sourceCard__top');
    const badge = el('span', 'sourceCard__badge', String(s.id));
    const domainWrap = el('span', 'sourceCard__domain');
    const avatar = el('span', 'sourceCard__avatar', (u.hostname[0] || '?').toUpperCase());
    avatar.style.background = colorForDomain(u.hostname);
    const domainLabel = el('span', undefined, u.hostname);
    domainWrap.append(avatar, domainLabel);
    top.append(badge, domainWrap);

    const title = el('h3', 'sourceCard__title', s.title || u.hostname);
    const desc = el('p', 'sourceCard__desc', s.description || '');

    a.append(top, title, desc);
    grid.append(a);
    count++;
  }
  $('sourcesWrap').hidden = count === 0;
}

// ---------------------------------------------------------------------------
// Timeline.
// ---------------------------------------------------------------------------

const TIMELINE_NODE_CAP = 100;

function resetTimeline() {
  const list = $('timelineList');
  list.replaceChildren();
  lastProgressKind = null;
  addTimelineNode({iconName: 'zap', text: 'Подключаюсь к поисковому агенту'});
}

function trimTimeline() {
  const list = $('timelineList');
  while (list.children.length > TIMELINE_NODE_CAP) {
    const victim =
        list.querySelector('.timelineNode:not([data-final])') || list.firstElementChild;
    if (!victim) break;
    victim.remove();
  }
}

function addTimelineNode(opts) {
  const list = $('timelineList');
  const prevActive = list.querySelector('.timelineNode--active');
  if (prevActive) prevActive.classList.remove('timelineNode--active');

  const li = document.createElement('li');
  li.className = 'timelineNode';
  if (opts.tone) li.classList.add(`timelineNode--${opts.tone}`);
  if (opts.final) {
    li.dataset.final = '';
  } else {
    li.classList.add('timelineNode--active');
  }

  const dot = el('span', 'timelineNode__dot');
  dot.append(icon(opts.iconName));

  const body = el('div', 'timelineNode__body');
  body.append(el('div', 'timelineNode__label', opts.text));
  if (opts.chip) body.append(opts.chip);

  li.append(dot, body);
  list.append(li);
  trimTimeline();
  list.scrollTop = list.scrollHeight;
}

function handleProgress(e) {
  const kind = e.kind;
  if (kind === 'thinking') {
    if (lastProgressKind === 'thinking') return;
    addTimelineNode({iconName: 'brain', text: 'Анализирую материалы'});
    lastProgressKind = 'thinking';
    return;
  }
  if (kind === 'search') {
    const chip = el('span', 'queryChip', `«${e.query || ''}»`);
    addTimelineNode({iconName: 'search', text: 'Ищу в интернете', chip});
    lastProgressKind = 'search';
    return;
  }
  if (kind === 'found') {
    const count = typeof e.count === 'number' ? e.count : (e.items ? e.items.length : 0);
    const domains = uniqueDomains((e.items || []).map((it) => it.url)).slice(0, 3);
    const chip = domains.length ? domainChipsRow(domains) : undefined;
    addTimelineNode({iconName: 'doc', text: `Нашёл ${count} страниц`, chip});
    lastProgressKind = 'found';
    return;
  }
  if (kind === 'open') {
    const domains = uniqueDomains(e.urls || []).slice(0, 5);
    const chip = domains.length ? domainChipsRow(domains) : undefined;
    addTimelineNode({iconName: 'link', text: 'Открываю источники', chip});
    lastProgressKind = 'open';
    return;
  }
  if (kind === 'read') {
    const count = typeof e.count === 'number' ? e.count : 0;
    addTimelineNode({iconName: 'doc', text: `Изучил ${count} страниц`});
    lastProgressKind = 'read';
    return;
  }
  if (kind === 'synthesizing') {
    addTimelineNode({iconName: 'spark', text: 'Готовлю ответ'});
    lastProgressKind = 'synthesizing';
    return;
  }
}

// ---------------------------------------------------------------------------
// Error block.
// ---------------------------------------------------------------------------

function showErrorBlock(kind, message) {
  $('answerBlock').hidden = true;
  $('sourcesWrap').hidden = true;
  $('errorBlock').hidden = false;
  $('errorTitle').textContent = kind === 'network' ? 'Сервис недоступен' : 'Поиск не удался';
  $('errorMessage').textContent = message || 'Проверьте подключение и повторите попытку.';
}

function hideErrorBlock() {
  $('errorBlock').hidden = true;
  $('answerBlock').hidden = false;
}

// ---------------------------------------------------------------------------
// Onboarding.
// ---------------------------------------------------------------------------

function showOnboardingError(message) {
  const node = $('onboardingError');
  node.textContent = message;
  node.hidden = false;
}

function hideOnboardingError() {
  $('onboardingError').hidden = true;
}

// ---------------------------------------------------------------------------
// Settings dialog.
// ---------------------------------------------------------------------------

function updateSettingsStatus() {
  const status = $('settingsStatus');
  status.textContent = tokenConfigured ? 'Токен подключён ✓' : 'Токен ещё не настроен';
  status.classList.toggle('settingsDialog__status--ok', tokenConfigured);
}

function showClearConfirm() {
  $('settingsClearConfirm').hidden = false;
  $('settingsClearBtn').hidden = true;
}

function hideClearConfirm() {
  $('settingsClearConfirm').hidden = true;
  $('settingsClearBtn').hidden = false;
}

function showSettingsError(message) {
  const node = $('settingsError');
  node.textContent = message;
  node.hidden = false;
}

// ---------------------------------------------------------------------------
// Wiring.
// ---------------------------------------------------------------------------

function wireEvents() {
  $('heroForm').addEventListener('submit', (e) => {
    e.preventDefault();
    start($('heroInput').value);
  });

  $('compactForm').addEventListener('submit', (e) => {
    e.preventDefault();
    start($('compactInput').value);
  });

  $('exampleChips').addEventListener('click', (e) => {
    const btn = e.target.closest ? e.target.closest('.chip') : null;
    if (!btn) return;
    start(btn.getAttribute('data-query'));
  });

  $('onboardingForm').addEventListener('submit', (e) => {
    e.preventDefault();
    const input = $('onboardingInput');
    const val = input.value.trim();
    input.value = '';
    if (!val) return;
    tokenSubmittedThisSession = true;
    hideOnboardingError();
    chrome.send('setToken', [val]);
  });

  $('retryBtn').addEventListener('click', () => {
    hideErrorBlock();
    resetAnswerToSkeleton();
    resetTimeline();
    $('sourcesWrap').hidden = true;
    $('sourcesGrid').replaceChildren();
    chrome.send('startSearch', [currentQuery, currentMode]);
  });

  $('answerContent').addEventListener('click', (e) => {
    const a = e.target.closest ? e.target.closest('a.cite') : null;
    if (!a) return;
    e.preventDefault();
    const href = a.getAttribute('href') || '';
    const targetId = href.replace('#', '');
    const card = document.getElementById(targetId);
    if (!card) return;
    card.scrollIntoView({behavior: 'smooth', block: 'center'});
    card.classList.add('sourceCard--pulse');
    setTimeout(() => card.classList.remove('sourceCard--pulse'), 1200);
  });

  $('gearBtn').addEventListener('click', () => {
    updateSettingsStatus();
    hideClearConfirm();
    $('settingsError').hidden = true;
    $('settingsTokenInput').value = '';
    $('settingsDialog').showModal();
  });

  $('settingsCloseBtn').addEventListener('click', () => {
    $('settingsDialog').close();
  });

  $('settingsForm').addEventListener('submit', (e) => {
    e.preventDefault();
    const input = $('settingsTokenInput');
    const val = input.value.trim();
    input.value = '';
    if (!val) {
      showSettingsError('Введите токен.');
      return;
    }
    $('settingsError').hidden = true;
    chrome.send('setToken', [val]);
  });

  $('settingsClearBtn').addEventListener('click', () => showClearConfirm());
  $('settingsClearCancelBtn').addEventListener('click', () => hideClearConfirm());
  $('settingsClearConfirmBtn').addEventListener('click', () => {
    chrome.send('clearToken', []);
    hideClearConfirm();
  });
}

function subscribeWebUiListeners() {
  addWebUiListener('bitrix-search-status', (e) => {
    tokenConfigured = !!e.tokenConfigured;
    updateSettingsStatus();
    if (currentQuery && !tokenConfigured) {
      setState('onboarding');
    }
  });

  addWebUiListener('bitrix-search-started', (e) => {
    currentQuery = e.query || currentQuery;
    if (e.mode) currentMode = parseMode(e.mode);
    $('queryTitle').textContent = currentQuery;
    $('heroInput').value = currentQuery;
    $('compactInput').value = currentQuery;
    renderModeBadge(currentMode);
    hideErrorBlock();
    resetAnswerToSkeleton();
    resetTimeline();
    $('sourcesWrap').hidden = true;
    $('sourcesGrid').replaceChildren();
    setState('active');
  });

  addWebUiListener('bitrix-search-progress', (e) => handleProgress(e));

  addWebUiListener('bitrix-search-answer', (e) => {
    latestAnswerText = e.answer || '';
    scheduleAnswerRender();
  });

  addWebUiListener('bitrix-search-completed', (e) => {
    hideErrorBlock();
    finalizeAnswer(e.answer || '', e.sources || []);
    renderSources(e.sources || []);
    addTimelineNode({iconName: 'check', text: 'Ответ готов', final: true, tone: 'success'});
  });

  addWebUiListener('bitrix-search-failed', (e) => {
    if (e.kind === 'token') {
      hideErrorBlock();
      setState('onboarding');
      if (tokenSubmittedThisSession) {
        showOnboardingError('Токен не подошёл, проверьте и попробуйте ещё раз.');
      }
      return;
    }
    setState('active');
    showErrorBlock(e.kind, e.message);
    const list = $('timelineList');
    const last = list.lastElementChild;
    if (!last || !last.classList.contains('timelineNode--danger')) {
      addTimelineNode({iconName: 'alert', text: 'Ошибка', final: true, tone: 'danger'});
    }
  });
}

function init() {
  mountIconsByAttr();
  mountIcon('gearBtn', 'gear');

  const params = new URLSearchParams(location.search);
  const q = params.get('q') || '';
  currentQuery = q;
  currentMode = parseMode(params.get('mode'));

  wireEvents();
  wireModeSwitches();
  subscribeWebUiListeners();
  updateModeSwitches();

  if (q) {
    $('heroInput').value = q;
    $('compactInput').value = q;
    $('queryTitle').textContent = q;
    renderModeBadge(currentMode);
    resetAnswerToSkeleton();
    resetTimeline();
    setState('active');
  } else {
    setState('empty');
  }

  chrome.send('pageReady', []);
}

init();
