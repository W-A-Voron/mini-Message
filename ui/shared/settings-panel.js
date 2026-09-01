// Settings screen - reached via the gear icon in the chat shell sidebar.
// Per spec: premium/stars/gifts purchase buttons are BUILT but disabled
// ("Оформить" greyed out) so a real payment provider can be wired in later
// without redoing this screen - only the button's disabled state and its
// click handler change, not the layout.
//
// The legal notice text next to every purchase button is intentionally
// generic/informational, not a rendering of any specific statute's exact
// wording - Claude can't respond confidently for the user's real, current
// legal exposure across five different jurisdictions, and a wrong "quote"
// of a law is worse than a plain factual note. If real compliance copy is
// needed, it needs a lawyer's sign-off, not scraped legal text.

const PAYMENT_LEGAL_NOTICE = `Оплата подразумевает обработку данных платёжной системой. По законодательству РФ (Федеральный закон №115-ФЗ «О противодействии легализации доходов, полученных преступным путём», Федеральный закон №35-ФЗ «О противодействии терроризму»), а также аналогичным нормам США (Bank Secrecy Act, USA PATRIOT Act), Республики Беларусь, Украины и Республики Казахстан, операторы платёжных систем обязаны предоставлять данные о транзакциях и их участниках уполномоченным государственным органам по официальному запросу. Это означает, что оплата любых платных функций раскрывает часть вашей идентификации соответствующим платёжным провайдером и снижает уровень анонимности использования мессенджера.`;

async function renderSettings(container, session, onBack) {
  container.innerHTML = `
    <div class="settings-shell">
      <div class="settings-header">
        <button type="button" class="icon-btn" id="settingsBack" title="Назад">←</button>
        <span class="settings-title">Настройки</span>
      </div>
      <div class="settings-body">

        <section class="settings-section">
          <h3>Аккаунт</h3>
          <div class="settings-row">
            <span class="settings-row-label">Логин</span>
            <span class="settings-row-value">${escapeHtmlS(session.login)}</span>
          </div>
          <div class="settings-row">
            <span class="settings-row-label">Отображаемое имя</span>
            <span class="settings-row-value">${escapeHtmlS(session.displayName || session.login)}</span>
          </div>
        </section>

        <section class="settings-section">
          <h3>Интерфейс</h3>
          <div class="settings-row">
            <span class="settings-row-label">Режим</span>
            <div class="settings-interface-buttons">
              <button type="button" class="variant-secondary" id="btnIfaceStandard">Обычный</button>
              <button type="button" class="variant-secondary" id="btnIfacePro">Профессиональный</button>
            </div>
          </div>
        </section>

        <section class="settings-section">
          <h3>Локальное хранилище</h3>
          <div class="settings-row">
            <span class="settings-row-label">Шифрование переписок на диске</span>
            <span class="settings-row-value" id="vaultStatus">проверка…</span>
          </div>
          <p class="settings-desc">
            Ключ выводится из вашего пароля (Argon2id) и нигде не сохраняется — только
            производная соль. Без пароля файл с историей чатов на диске нечитаем.
          </p>
        </section>

        <section class="settings-section">
          <h3>Telegram Premium</h3>
          <p class="settings-desc">
            Безлимитная загрузка файлов (без ограничения в 30 ГБ), приоритетная поддержка,
            расширенные реакции, эксклюзивные стикеры и эмодзи, увеличенные лимиты каналов,
            папок и закреплённых чатов, значок Premium в профиле.
          </p>
          <div class="settings-purchase-row">
            <button type="button" class="variant-primary" disabled title="Оплата пока не подключена">
              Оформить — недоступно
            </button>
          </div>
          <p class="legal-notice">${PAYMENT_LEGAL_NOTICE}</p>
        </section>

        <section class="settings-section">
          <h3>Звёзды</h3>
          <p class="settings-desc">
            Внутренняя валюта для отправки подарков, оплаты реакций и функций в мини-приложениях.
            Текущий баланс: <strong>${session.starsBalance ?? 0}</strong> ★
          </p>
          <div class="settings-purchase-row">
            <button type="button" class="variant-primary" disabled title="Оплата пока не подключена">
              Пополнить — недоступно
            </button>
          </div>
          <p class="legal-notice">${PAYMENT_LEGAL_NOTICE}</p>
        </section>

        <section class="settings-section">
          <h3>Подарки</h3>
          <p class="settings-desc">
            Отправляйте друзьям коллекционные подарки. Каталог откроется, когда будет
            подключена оплата — сама отправка/получение подарков администратором сервера
            уже работает (см. админ-консоль сервера).
          </p>
          <div class="settings-purchase-row">
            <button type="button" class="variant-primary" disabled title="Оплата пока не подключена">
              Магазин подарков — недоступно
            </button>
          </div>
          <p class="legal-notice">${PAYMENT_LEGAL_NOTICE}</p>
        </section>

        <section class="settings-section">
          <h3>Сессия</h3>
          <button type="button" class="variant-secondary" id="btnLogout">Выйти из аккаунта</button>
        </section>

      </div>
    </div>
  `;

  container.querySelector("#settingsBack").addEventListener("click", onBack);

  MessengerBridge.callNative("getVaultStatus", {}).then((res) => {
    const el = container.querySelector("#vaultStatus");
    if (!el) return;
    if (res.ok && res.encrypted) {
      el.textContent = "Включено (AEAD)";
      el.style.color = "#3fbf6b";
    } else {
      el.textContent = "Отключено — работает без шифрования";
      el.style.color = "var(--danger)";
    }
  });

  container.querySelector("#btnIfaceStandard").addEventListener("click", () => {
    MessengerBridge.callNative("setInterfaceMode", { mode: "standard" });
  });
  container.querySelector("#btnIfacePro").addEventListener("click", () => {
    MessengerBridge.callNative("setInterfaceMode", { mode: "pro" });
  });

  container.querySelector("#btnLogout").addEventListener("click", async () => {
    await MessengerBridge.callNative("logout", {});
    location.reload();
  });
}

function escapeHtmlS(s) {
  const div = document.createElement("div");
  div.textContent = s ?? "";
  return div.innerHTML;
}

window.SettingsPanel = { renderSettings };
