import assert from "node:assert/strict";
import test from "node:test";

import {BrowserThreadTransport} from "../extension/browser-thread.js";

function jsonResponse(body, {status = 200} = {}) {
  return new Response(JSON.stringify(body), {status, headers: {"content-type": "application/json"}});
}

test("creates a BrowserThread, sends page context, then reads the assistant reply by polling", async () => {
  const calls = [];
  let messagesCalls = 0;
  const fetch = async (url, init) => {
    calls.push({url, init});
    if (url.includes("BrowserThread.create")) return jsonResponse({data: {threadId: "thread-1"}});
    if (url.includes("BrowserThread.send")) return jsonResponse({data: {messageId: "user-1"}});
    if (url.includes("BrowserThread.messages")) {
      messagesCalls += 1;
      return jsonResponse({data: {messages: messagesCalls === 1 ? [] : [{id: "assistant-2", role: "assistant", text: "Готово"}]}});
    }
    throw new Error(`unexpected action: ${url}`);
  };
  const transport = new BrowserThreadTransport({portalOrigin: "https://example.test", fetch, pollIntervalMs: 0, responseTimeoutMs: 100});

  const result = await transport.sendTurn("привет", {page: {url: "https://example.test/task", title: "Задача", snapshot: {fields: []}, screenshot: {mimeType: "image/jpeg", imageDataUrl: "data:image/jpeg;base64,AA=="}}});

  assert.equal(result.text, "Готово");
  assert.equal(calls[0].url, "https://example.test/bitrix/services/main/ajax.php?action=aiassistant.api.BrowserThread.create");
  assert.equal(calls[1].url, "https://example.test/bitrix/services/main/ajax.php?action=aiassistant.api.BrowserThread.send");
  assert.deepEqual(Object.fromEntries(new URLSearchParams(calls[1].init.body)), {
    threadId: "thread-1", message: "привет", "context[page][url]": "https://example.test/task", "context[page][title]": "Задача", "context[page][snapshot][fields]": "[]", "screenshot[mimeType]": "image/jpeg", "screenshot[imageDataUrl]": "data:image/jpeg;base64,AA==",
  });
  assert.equal(calls[1].init.headers["X-Bitrix-Csrf-Token"], undefined);
  assert.equal(new URLSearchParams(calls[1].init.body).has("sessid"), false);
  assert.equal(calls.every(call => call.init.credentials === "include"), true);
});

test("turns an undocumented BrowserThread error into a clear contract error", async () => {
  const transport = new BrowserThreadTransport({portalOrigin: "https://example.test", fetch: async () => jsonResponse({data: {} })});
  await assert.rejects(transport.sendTurn("привет", {}), /BrowserThread\.create: портал не вернул threadId/);
});
