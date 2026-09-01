// Thin wrapper around the native <-> web bridge.
// On Windows/WebView2 this is window.chrome.webview.postMessage(...) with a
// matching response event; here we wrap it as a Promise-based `callNative()`
// so screen code doesn't need to know the transport.

let _requestId = 0;
const _pending = new Map();

function _hasNativeHost() {
  return typeof window.chrome !== "undefined" && window.chrome.webview;
}

if (_hasNativeHost()) {
  window.chrome.webview.addEventListener("message", (event) => {
    const msg = event.data;
    const resolver = _pending.get(msg.requestId);
    if (!resolver) return;
    _pending.delete(msg.requestId);
    resolver(msg.result);
  });
}

// Safety net: if the C++ side ever again fails to answer a call (missing
// handler, crash mid-dispatch, etc), the screen must show a visible error
// instead of hanging forever on a blank background - that silent hang is
// exactly what caused the "blue screen" bug. 10s is generous for a local
// IPC round trip; real network calls (login/register) can be slower, so
// give those a bit more room via the `timeoutMs` param.
function callNative(call, args = {}, timeoutMs = 10000) {
  return new Promise((resolve) => {
    if (!_hasNativeHost()) {
      console.warn(`[bridge] no native host attached; stubbing call "${call}"`, args);
      resolve({ ok: false, error: "no_native_host" });
      return;
    }
    const requestId = ++_requestId;

    const timer = setTimeout(() => {
      if (!_pending.has(requestId)) return; // already resolved
      _pending.delete(requestId);
      console.error(`[bridge] call "${call}" timed out after ${timeoutMs}ms`);
      resolve({ ok: false, error: "native_timeout" });
    }, timeoutMs);

    _pending.set(requestId, (result) => {
      clearTimeout(timer);
      resolve(result);
    });
    window.chrome.webview.postMessage({ call, args, requestId });
  });
}

window.MessengerBridge = { callNative };
