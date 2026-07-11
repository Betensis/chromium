const HOST = "org.bitrix.marta_browser";
let port;
let reconnectAttempt = 0;
const BACKOFF_MS = [1000, 2000, 5000, 10000];
const MAX_NATIVE_CAPTURE_BYTES = 700 * 1024;
const pendingWorkspaceRequests = new Map();

function nativeError(code, message) {
  return {code, message};
}

function rejectPendingNativeRequests(reason) {
  for (const {reject} of pendingWorkspaceRequests.values()) reject(reason);
  pendingWorkspaceRequests.clear();
}

function connect() {
  try {
    port = chrome.runtime.connectNative(HOST);
    reconnectAttempt = 0;
    port.onMessage.addListener(onNativeMessage);
    port.onDisconnect.addListener(() => {
      const detail = chrome.runtime.lastError?.message || "Native host disconnected";
      port = undefined;
      rejectPendingNativeRequests(nativeError("NATIVE_HOST_DISCONNECTED", detail));
      const delay = BACKOFF_MS[Math.min(reconnectAttempt++, BACKOFF_MS.length - 1)];
      setTimeout(connect, delay);
    });
  } catch (_) {
    const delay = BACKOFF_MS[Math.min(reconnectAttempt++, BACKOFF_MS.length - 1)];
    setTimeout(connect, delay);
  }
}

function send(message) {
  if (!port) connect();
  if (port) port.postMessage(message);
}

function chooseWorkspace() {
  if (!port) {
    connect();
    if (!port) return Promise.reject(nativeError("NATIVE_HOST_DISCONNECTED", "Native host is not connected"));
  }
  const id = crypto.randomUUID();
  return new Promise((resolve, reject) => {
    pendingWorkspaceRequests.set(id, {resolve, reject});
    try {
      port.postMessage({id, method: "workspace.choose", params: {}});
    } catch (_) {
      pendingWorkspaceRequests.delete(id);
      reject(nativeError("NATIVE_HOST_DISCONNECTED", "Native host is not connected"));
    }
  });
}

function tabShape(tab) {
  return {id: tab.id, windowId: tab.windowId, index: tab.index, active: tab.active, pinned: tab.pinned, groupId: tab.groupId, title: tab.title || "", url: tab.url || ""};
}
function error(code, message, detail) {
  const result = {code, message};
  if (detail) result.detail = detail;
  throw result;
}
function requirePositiveIds(ids) {
  if (!Array.isArray(ids) || !ids.length || ids.some(id => !Number.isInteger(id) || id <= 0) || new Set(ids).size !== ids.length) error("INVALID_ARGUMENT", "tabIds must be unique positive integers");
}
function allowedUrl(url) { return /^https?:\/\//.test(url); }
async function getTab(id) {
  try { return await chrome.tabs.get(id); } catch (_) { error("NOT_FOUND", "tab not found", "TAB_NOT_FOUND"); }
}
async function checkedActive(tab) {
  await chrome.windows.update(tab.windowId, {focused: true});
  await chrome.tabs.update(tab.id, {active: true});
  const [active] = await chrome.tabs.query({active: true, windowId: tab.windowId});
  if (!active || active.id !== tab.id || active.url !== tab.url) error("STALE_PAGE", "selected tab changed before capture");
  return active;
}
async function jpegDataUrl(dataUrl) {
  const blob = await (await fetch(dataUrl)).blob();
  const bitmap = await createImageBitmap(blob);
  const scale = Math.min(1, 1920 / Math.max(bitmap.width, bitmap.height));
  const canvas = new OffscreenCanvas(Math.max(1, Math.round(bitmap.width * scale)), Math.max(1, Math.round(bitmap.height * scale)));
  canvas.getContext("2d").drawImage(bitmap, 0, 0, canvas.width, canvas.height);
  const jpeg = await canvas.convertToBlob({type: "image/jpeg", quality: 0.82});
  if (jpeg.size > MAX_NATIVE_CAPTURE_BYTES) error("CAPTURE_TOO_LARGE", "JPEG exceeds Native Messaging frame limit");
  return await new Promise((resolve, reject) => { const reader = new FileReader(); reader.onload = () => resolve(reader.result); reader.onerror = reject; reader.readAsDataURL(jpeg); });
}
async function snapshot(tab) {
  if (!allowedUrl(tab.url)) error("CAPTURE_FORBIDDEN", "only http and https pages may be read");
  const before = await checkedActive(tab);
  const [{result}] = await chrome.scripting.executeScript({target: {tabId: tab.id}, func: () => {
    const cap = value => String(value ?? "").slice(0, 4096);
    const active = document.activeElement;
    const fields = [...document.querySelectorAll("input,textarea,select,[contenteditable=true]")].filter(el => !(el instanceof HTMLInputElement && (el.type === "password" || el.type === "hidden"))).map(el => ({tag: el.tagName.toLowerCase(), name: el.name || el.id || "", type: el.type || "", value: cap(el instanceof HTMLInputElement && (el.type === "checkbox" || el.type === "radio") ? "" : (el.value ?? el.textContent)), checked: el instanceof HTMLInputElement && (el.type === "checkbox" || el.type === "radio") ? el.checked : null}));
    return {title: document.title, url: location.href, viewport: {width: innerWidth, height: innerHeight}, activeElement: active ? {tag: active.tagName.toLowerCase(), name: active.name || active.id || ""} : null, fields};
  }});
  const [after] = await chrome.tabs.query({active: true, windowId: before.windowId});
  if (!after || after.id !== before.id || after.url !== before.url || result.url !== before.url) error("STALE_PAGE", "selected tab changed during snapshot");
  const output = {tabId: before.id, url: before.url, capturedAt: new Date().toISOString(), ...result};
  let encoded = new TextEncoder().encode(JSON.stringify(output));
  while (encoded.length > 256 * 1024 && output.fields.length) { output.fields.pop(); encoded = new TextEncoder().encode(JSON.stringify(output)); }
  return output;
}
async function execute(command) {
  const {method, params = {}} = command;
  switch (method) {
    case "tabs.list": return {tabs: (await chrome.tabs.query({})).map(tabShape)};
    case "tabs.open": {
      if (!allowedUrl(params.url)) error("INVALID_ARGUMENT", "url must use http or https");
      return {tab: tabShape(await chrome.tabs.create({url: params.url, active: params.active !== false, windowId: params.windowId}))};
    }
    case "tabs.close": requirePositiveIds(params.tabIds); await chrome.tabs.remove(params.tabIds); return {closedTabIds: params.tabIds};
    case "tabs.group": {
      requirePositiveIds(params.tabIds); const groupId = await chrome.tabs.group(params.groupId == null ? {tabIds: params.tabIds} : {tabIds: params.tabIds, groupId: params.groupId});
      const update = {}; for (const key of ["title", "color", "collapsed"]) if (params[key] !== undefined) update[key] = params[key];
      if (Object.keys(update).length) await chrome.tabGroups.update(groupId, update); return {groupId};
    }
    case "page.screenshot": {
      const tab = await getTab(params.tabId); if (!allowedUrl(tab.url)) error("CAPTURE_FORBIDDEN", "only http and https pages may be captured");
      const before = await checkedActive(tab); const raw = await chrome.tabs.captureVisibleTab(before.windowId, {format: "png"}); const after = await chrome.tabs.query({active: true, windowId: before.windowId});
      if (!after[0] || after[0].id !== before.id || after[0].url !== before.url) error("STALE_PAGE", "selected tab changed during capture");
      return {tabId: before.id, url: before.url, capturedAt: new Date().toISOString(), mimeType: "image/jpeg", imageDataUrl: await jpegDataUrl(raw)};
    }
    case "page.snapshot": return snapshot(await getTab(params.tabId));
    default: error("INVALID_ARGUMENT", "unknown extension method");
  }
}
async function onNativeMessage(command) {
  if (command && typeof command.ok === "boolean") {
    const workspacePending = pendingWorkspaceRequests.get(command.id);
    if (workspacePending) {
      pendingWorkspaceRequests.delete(command.id);
      if (command.ok) workspacePending.resolve(command.result);
      else workspacePending.reject(command.error || nativeError("NATIVE_HOST_ERROR", "Native host rejected the request"));
      return;
    }
    const {nativeSmokeRequestId} = await chrome.storage.session.get("nativeSmokeRequestId");
    if (nativeSmokeRequestId === command.id) await chrome.storage.session.set({nativeSmokeResult: command});
    return;
  }
  try { send({id: command.id, ok: true, result: await execute(command)}); }
  catch (caught) { send({id: command.id, ok: false, error: typeof caught === "object" && caught.code ? caught : {code: "INTERNAL_ERROR", message: "extension command failed"}}); }
}

function configuredPortalOrigin(tab) {
  try { const url = new URL(tab?.url); return allowedUrl(url.href) ? url.origin : null; }
  catch (_) { return null; }
}

async function portalOrigin() {
  const [tab] = await chrome.tabs.query({active: true, lastFocusedWindow: true});
  const origin = configuredPortalOrigin(tab);
  if (origin) await chrome.storage.local.set({martaPortalOrigin: origin});
  if (origin) return origin;
  const stored = await chrome.storage.local.get("martaPortalOrigin");
  return stored.martaPortalOrigin || null;
}

async function currentPageContext() {
  const [tab] = await chrome.tabs.query({active: true, lastFocusedWindow: true});
  if (!tab?.id || !allowedUrl(tab.url)) throw nativeError("PAGE_CONTEXT_UNAVAILABLE", "Откройте http(s)-страницу для снимка");
  const screenshot = await execute({method: "page.screenshot", params: {tabId: tab.id}});
  const page = await execute({method: "page.snapshot", params: {tabId: tab.id}});
  return {page: {url: page.url, title: page.title, snapshot: page, screenshot: {mimeType: screenshot.mimeType, imageDataUrl: screenshot.imageDataUrl, capturedAt: screenshot.capturedAt}}};
}

chrome.runtime.onMessage.addListener((message, _, respond) => {
  if (message.type === "nativeSmoke") { const id = crypto.randomUUID(); send({id, method: "smoke.echo", params: {value: id}}); chrome.storage.session.set({nativeSmokeRequestId: id}); respond({id}); }
  if (message.type === "portal.getOrigin") {
    portalOrigin().then(origin => {
      if (!origin) throw nativeError("PORTAL_NOT_FOUND", "Откройте Side Panel на локальном портале");
      respond({ok: true, origin});
    }).catch(error => respond({ok: false, error: typeof error === "object" ? error : nativeError("PORTAL_NOT_FOUND", "Локальный портал недоступен")}));
    return true;
  }
  if (message.type === "page.context") {
    currentPageContext().then(result => respond({ok: true, result}))
      .catch(error => respond({ok: false, error: typeof error === "object" ? error : nativeError("PAGE_CONTEXT_UNAVAILABLE", "Не удалось получить снимок страницы")}));
    return true;
  }
  if (message.type === "workspace.choose") {
    chooseWorkspace()
      .then(result => {
        if (!result || typeof result.workspace !== "string" || !result.workspace) throw nativeError("NATIVE_HOST_ERROR", "Native host returned an invalid workspace");
        respond({ok: true, result});
      })
      .catch(error => respond({ok: false, error: typeof error === "object" ? error : nativeError("NATIVE_HOST_ERROR", "Workspace selection failed")}));
    return true;
  }
});
function enableActionSidePanel() {
  return chrome.sidePanel.setPanelBehavior({openPanelOnActionClick: true});
}

chrome.runtime.onInstalled.addListener(enableActionSidePanel);
chrome.runtime.onStartup.addListener(enableActionSidePanel);
enableActionSidePanel();
connect();
