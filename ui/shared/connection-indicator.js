// Small "нет соединения" corner indicator, shared by both interfaces.
// Per spec: when offline, the app must still load all locally cached data
// (chats/channels) - this indicator only informs, it never blocks the UI.

(function () {
  const el = document.createElement("div");
  el.id = "connectionIndicator";
  el.className = "connection-indicator hidden";
  el.textContent = "Нет соединения с сервером";
  document.body.appendChild(el);

  async function poll() {
    try {
      const res = await window.MessengerBridge.callNative("getConnectionStatus", {});
      const online = res && res.ok ? res.online : false;
      el.classList.toggle("hidden", !!online);
    } catch (e) {
      el.classList.remove("hidden");
    }
  }

  poll();
  setInterval(poll, 4000);
})();
