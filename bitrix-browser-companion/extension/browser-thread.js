const AJAX_PATH = "/bitrix/services/main/ajax.php";
const ACTION_PREFIX = "aiassistant.api.BrowserThread.";

function asObject(value) {
  return value && typeof value === "object" ? value : {};
}

function errorMessage(body, status) {
  const error = body?.error ?? body?.errors?.[0];
  if (typeof error === "string") return error;
  if (typeof error?.message === "string") return error.message;
  return `Ошибка портала (${status})`;
}

function formData(value, prefix = "", output = new URLSearchParams()) {
  for (const [key, item] of Object.entries(asObject(value))) {
    const name = prefix ? `${prefix}[${key}]` : key;
    if (Array.isArray(item)) output.set(name, JSON.stringify(item));
    else if (item && typeof item === "object") formData(item, name, output);
    else if (item != null) output.set(name, String(item));
  }
  return output;
}

function assistantMessage(payload) {
  const messages = payload?.data?.messages ?? payload?.messages ?? [];
  if (!Array.isArray(messages)) return null;
  for (const message of [...messages].reverse()) {
    const role = String(message?.role ?? message?.authorType ?? "").toLowerCase();
    const text = message?.text ?? message?.content;
    if ((role === "assistant" || role === "marta") && typeof text === "string" && text.trim()) {
      return {id: message.id ?? message.messageId, text: text.trim()};
    }
  }
  return null;
}

function delay(ms) {
  return ms > 0 ? new Promise(resolve => setTimeout(resolve, ms)) : Promise.resolve();
}

function splitPageContext(context) {
  const page = asObject(context?.page);
  const screenshot = asObject(page.screenshot);
  if (!Object.keys(screenshot).length) return {context};
  const cleanPage = {...page};
  delete cleanPage.screenshot;
  return {context: {...asObject(context), page: cleanPage}, screenshot};
}

export class BrowserThreadTransport {
  constructor({portalOrigin, fetch = globalThis.fetch, pollIntervalMs = 1500, responseTimeoutMs = 120000} = {}) {
    if (!portalOrigin || !/^https?:\/\//.test(portalOrigin)) throw new Error("Откройте Side Panel на портале");
    this.portalOrigin = portalOrigin.replace(/\/$/, "");
    this.fetch = (...args) => fetch(...args);
    this.pollIntervalMs = pollIntervalMs;
    this.responseTimeoutMs = responseTimeoutMs;
    this.threadId = null;
    this.pendingTurn = null;
  }

  async request(action, payload = {}) {
    const headers = {"Content-Type": "application/x-www-form-urlencoded;charset=UTF-8", Accept: "application/json"};
    const requestBody = formData(payload);
    const response = await this.fetch(`${this.portalOrigin}${AJAX_PATH}?action=${encodeURIComponent(ACTION_PREFIX + action)}`, {
      method: "POST", credentials: "include", headers, body: requestBody.toString(),
    });
    const raw = await response.text();
    let body;
    try { body = raw ? JSON.parse(raw) : {}; } catch (_) { throw new Error(`${ACTION_PREFIX + action}: портал вернул некорректный AJAX-ответ`); }
    if (!response.ok || body.error || body.errors?.length) throw new Error(`${ACTION_PREFIX + action}: ${errorMessage(body, response.status)}`);
    return body;
  }

  async createThread() {
    if (this.threadId) return this.threadId;
    const body = await this.request("create");
    const threadId = body?.data?.threadId ?? body?.data?.thread_id ?? body?.threadId;
    if (typeof threadId !== "string" && !Number.isInteger(threadId)) throw new Error(`${ACTION_PREFIX}create: портал не вернул threadId`);
    this.threadId = String(threadId);
    return this.threadId;
  }

  async sendTurn(message, context) {
    if (this.pendingTurn) throw new Error("Предыдущий запрос ещё обрабатывается");
    this.pendingTurn = this.sendTurnInternal(message, context).finally(() => { this.pendingTurn = null; });
    return this.pendingTurn;
  }

  async sendTurnInternal(message, context) {
    const threadId = await this.createThread();
    const sent = await this.request("send", {threadId, message, ...splitPageContext(context)});
    const afterId = sent?.data?.messageId ?? sent?.data?.message_id ?? sent?.messageId ?? null;
    const deadline = Date.now() + this.responseTimeoutMs;
    while (Date.now() <= deadline) {
      const messages = await this.request("messages", {threadId, ...(afterId != null ? {afterId} : {})});
      const answer = assistantMessage(messages);
      if (answer) return answer;
      await delay(this.pollIntervalMs);
    }
    throw new Error("BrowserThread.messages: Марта не ответила за отведённое время");
  }
}
