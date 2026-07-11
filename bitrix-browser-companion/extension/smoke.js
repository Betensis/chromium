if (new URL(location.href).searchParams.get("nativeSmoke") === "true") {
  chrome.runtime.sendMessage({type: "nativeSmoke"});
}
