// Chat list + message pane, rendered inside `container` after a successful
// login. Deliberately NOT going through UiLayoutLoader/XML like the auth
// screens: this content is data-driven (a variable-length list from
// LocalStore, re-rendered whenever a message is sent), not a fixed form
// structure - XML makes sense for "these exact fields, always", not for
// "however many chats this account happens to have".
//
// IMPORTANT, and worth remembering while reading this file: sending a
// message here only writes to this machine's local store (see
// Bridge::handleSendMessage in the C++ side). There is no delivery to
// another device yet - that's a still-open design question (relay through
// the server vs. direct P2P), not a bug in this UI.

async function renderChatShell(container, session) {
  container.innerHTML = `
    <div class="chat-shell">
      <aside class="chat-sidebar">
        <div class="chat-sidebar-header">
          <span class="chat-sidebar-title">${escapeHtml(session.displayName || session.login)}</span>
          <div class="chat-sidebar-actions">
            <button type="button" class="icon-btn" id="settingsBtn" title="Настройки">⚙</button>
            <button type="button" class="icon-btn" id="newChatBtn" title="Новый чат/канал/группа">+</button>
          </div>
        </div>
        <div class="chat-list" id="chatList"></div>
      </aside>
      <main class="chat-main" id="chatMain">
        <div class="chat-empty">Выберите чат слева или создайте новый — кнопка "+"</div>
      </main>
    </div>
  `;

  const chatListEl = container.querySelector("#chatList");
  const chatMainEl = container.querySelector("#chatMain");
  const newChatBtn = container.querySelector("#newChatBtn");
  const settingsBtn = container.querySelector("#settingsBtn");

  settingsBtn.addEventListener("click", () => {
    SettingsPanel.renderSettings(container, session, () => renderChatShell(container, session));
  });

  let activeChatId = null;

  async function refreshChatList() {
    const res = await MessengerBridge.callNative("listChats", {});
    chatListEl.innerHTML = "";
    if (!res.ok) {
      chatListEl.innerHTML = `<div class="chat-list-error">Не удалось загрузить список: ${res.error}</div>`;
      return;
    }
    if (res.chats.length === 0) {
      chatListEl.innerHTML = `<div class="chat-list-empty">Пока пусто. Нажмите "+", чтобы создать чат, группу или канал.</div>`;
      return;
    }
    // Most recently active first.
    const sorted = [...res.chats].sort((a, b) => b.lastTimestampMs - a.lastTimestampMs);
    for (const chat of sorted) {
      const item = document.createElement("div");
      item.className = "chat-item" + (chat.id === activeChatId ? " active" : "");
      item.dataset.chatId = chat.id;
      item.innerHTML = `
        <div class="chat-item-icon">${chatTypeIcon(chat.type)}</div>
        <div class="chat-item-body">
          <div class="chat-item-name">${escapeHtml(chat.name)}</div>
          <div class="chat-item-preview">${escapeHtml(chat.lastMessage || "Нет сообщений")}</div>
        </div>
      `;
      item.addEventListener("click", () => openChat(chat.id, chat.name, chat.type));
      chatListEl.appendChild(item);
    }
  }

  async function openChat(chatId, name, type) {
    activeChatId = chatId;
    chatListEl.querySelectorAll(".chat-item").forEach((el) => {
      el.classList.toggle("active", el.dataset.chatId === chatId);
    });

    chatMainEl.innerHTML = `
      <div class="chat-header">
        <span class="chat-header-icon">${chatTypeIcon(type)}</span>
        <span class="chat-header-name">${escapeHtml(name)}</span>
      </div>
      <div class="chat-messages" id="chatMessages"></div>
      <form class="chat-input-row" id="chatInputForm">
        <input type="text" id="chatInputText" placeholder="Написать сообщение…" autocomplete="off" />
        <button type="submit" class="variant-primary">Отправить</button>
      </form>
    `;

    await refreshMessages();

    chatMainEl.querySelector("#chatInputForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const input = chatMainEl.querySelector("#chatInputText");
      const text = input.value.trim();
      if (!text) return;
      input.value = "";
      const res = await MessengerBridge.callNative("sendMessage", { chatId, text });
      if (!res.ok) {
        alert("Не удалось отправить: " + res.error);
        return;
      }
      await refreshMessages();
      await refreshChatList();
    });
  }

  async function refreshMessages() {
    const messagesEl = chatMainEl.querySelector("#chatMessages");
    if (!messagesEl) return;
    const res = await MessengerBridge.callNative("getMessages", { chatId: activeChatId });
    messagesEl.innerHTML = "";
    if (!res.ok) {
      messagesEl.innerHTML = `<div class="chat-list-error">${res.error}</div>`;
      return;
    }
    for (const m of res.messages) {
      const bubble = document.createElement("div");
      const mine = m.from === session.login;
      bubble.className = "chat-bubble" + (mine ? " mine" : "");
      bubble.innerHTML = `
        <div class="chat-bubble-author">${escapeHtml(m.from)}</div>
        <div class="chat-bubble-text">${escapeHtml(m.text)}</div>
        <div class="chat-bubble-time">${formatTime(m.ts)}</div>
      `;
      messagesEl.appendChild(bubble);
    }
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }

  newChatBtn.addEventListener("click", async () => {
    const type = prompt("Тип: dm / group / channel", "dm");
    if (!type || !["dm", "group", "channel"].includes(type)) return;

    let name, peerLogin = "";
    if (type === "dm") {
      peerLogin = prompt("Логин собеседника (его реальный логин в мессенджере)");
      if (!peerLogin) return;
      name = peerLogin; // dm's display name is just the peer's login for now
    } else {
      name = prompt("Название " + (type === "group" ? "группы" : "канала"));
      if (!name) return;
    }

    const res = await MessengerBridge.callNative("createChat", { type, name, peerLogin });
    if (!res.ok) { alert("Ошибка: " + res.error); return; }
    await refreshChatList();
    openChat(res.id, name, type);
  });

  await refreshChatList();

  // Poll the server for anything sent to this account while we weren't
  // looking. Deliberately a poll, not a push connection - see
  // NetworkServer.h for why that's the pragmatic first version. 4s keeps
  // it feeling reasonably live without hammering the server.
  const syncTimer = setInterval(async () => {
    const res = await MessengerBridge.callNative("syncMessages", {});
    if (!res.ok || !res.newCount) return;
    await refreshChatList();
    if (activeChatId) await refreshMessages();
  }, 4000);

  // Stop the timer once this specific chat-shell DOM is torn down (e.g.
  // logout re-renders #app with the login screen). Checking `container`
  // itself would never trigger - #app persists across screens, only its
  // children get swapped - so we track the shell's own root element.
  const shellRoot = container.querySelector(".chat-shell");
  const observer = new MutationObserver(() => {
    if (!document.body.contains(shellRoot)) {
      clearInterval(syncTimer);
      observer.disconnect();
    }
  });
  observer.observe(document.body, { childList: true, subtree: true });
}

function chatTypeIcon(type) {
  if (type === "channel") return "📢";
  if (type === "group") return "👥";
  return "💬";
}

function escapeHtml(s) {
  const div = document.createElement("div");
  div.textContent = s ?? "";
  return div.innerHTML;
}

function formatTime(ts) {
  if (!ts) return "";
  const d = new Date(ts);
  return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

window.ChatShell = { renderChatShell };
