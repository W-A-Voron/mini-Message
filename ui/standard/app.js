const app = document.getElementById("app");

async function showLogin() {
  await LayoutRenderer.renderScreen("login", app, async (action, state) => {
    if (action === "navigate:register") return showRegister();

    if (action.startsWith("switchInterface:")) {
      const mode = action.split(":")[1];
      // Fire-and-forget: the native side navigates the whole window to
      // the other interface's index.html, which tears down this JS
      // context entirely - there's nothing meaningful to await here.
      MessengerBridge.callNative("setInterfaceMode", { mode });
      return;
    }

    if (action === "login") {
      const err = validateLogin(state);
      if (err) return showError(err);

      const res = await MessengerBridge.callNative("login", state);
      if (!res.ok) return showError(mapAuthError(res.error));
      showChatShell({ login: state.login, displayName: res.displayName || state.login, starsBalance: res.starsBalance || 0 });
    }
  });
}

async function showChatShell(session) {
  await ChatShell.renderChatShell(app, session);
}

async function showRegister() {
  await LayoutRenderer.renderScreen("register", app, async (action, state, el) => {
    if (action === "navigate:login") return showLogin();

    if (action === "register") {
      const err = validateRegister(state);
      if (err) return showError(err);

      const res = await MessengerBridge.callNative("register", {
        login: state.login,
        displayName: state.displayName,
        password: state.password,
        captchaToken: state.captchaToken1,
      });
      if (!res.ok) return showError(mapAuthError(res.error));

      // Reveal the second, post-registration captcha per spec step 5.
      document.getElementById("postCaptchaPanel").classList.remove("hidden");
    }

    if (action === "finishRegister") {
      if (!state.captchaToken2) return showError("Пройдите капчу ещё раз.");
      showLogin();
    }
  });
}

function validateLogin(state) {
  if (!state.login || !state.password) return "Заполните логин и пароль.";
  if (!state.captchaToken) return "Пройдите капчу.";
  return null;
}

function validateRegister(state) {
  if (!state.login || !state.displayName || !state.password || !state.passwordRepeat)
    return "Заполните все поля.";
  if (state.password !== state.passwordRepeat) return "Пароли не совпадают.";
  if (!state.captchaToken1) return "Пройдите капчу.";
  return null;
}

function mapAuthError(code) {
  const map = {
    no_native_host: "Нет соединения с клиентским ядром (запущено вне WebView2).",
    not_connected: "Нет соединения с сервером.",
    login_taken: "Такой логин уже занят.",
    invalid_credentials: "Неверный логин или пароль.",
  };
  return map[code] || `Ошибка: ${code}`;
}

function showError(text) {
  const card = document.querySelector(".auth-card");
  const old = card.querySelector(".toast-error");
  if (old) old.remove();
  const div = document.createElement("div");
  div.className = "toast-error";
  div.textContent = text;
  card.appendChild(div);
}

showLogin();
