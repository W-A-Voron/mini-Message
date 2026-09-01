const app = document.getElementById("app");
const cliOutput = document.getElementById("cliOutput");
const cliInput = document.getElementById("cliInput");
const cliShellBadge = document.getElementById("cliShellBadge");

// Detect which shell likely launched us, purely cosmetic for the badge.
// Real shell-specific piping (stdin/stdout for cmd.exe / PowerShell / ISI
// Engine's console) happens through the --cli flag on the native binary,
// documented in main.cpp; this in-window panel is a convenience mirror.
(function detectShellHint() {
  const ua = navigator.userAgent || "";
  cliShellBadge.textContent = ua.includes("Windows") ? "cmd.exe / PowerShell" : "shell";
})();

function cliPrint(text, cls = "") {
  const line = document.createElement("div");
  if (cls) line.className = cls;
  line.textContent = text;
  cliOutput.appendChild(line);
  cliOutput.scrollTop = cliOutput.scrollHeight;
}

// Parses a single CLI-style line: "command --flag value --flag2 value2"
function parseCliLine(line) {
  const parts = line.trim().split(/\s+/);
  const command = parts.shift();
  const args = {};
  for (let i = 0; i < parts.length; i += 2) {
    const key = (parts[i] || "").replace(/^--/, "");
    if (!key) continue;
    args[key] = parts[i + 1] ?? "";
  }
  return { command, args };
}

const CLI_COMMANDS = {
  async captcha() {
    const res = await MessengerBridge.callNative("getCaptcha", {});
    if (!res.ok) return res;
    return { ok: true, question: res.question, hint: `use: --captcha ${res.id}|<answer>` };
  },
  async login(args) {
    if (!args.captcha) return { ok: false, error: "missing_captcha: run 'captcha' first, then pass --captcha <id>|<answer>" };
    return MessengerBridge.callNative("login", {
      login: args.login, password: args.password, captchaToken: args.captcha,
    });
  },
  async register(args) {
    if (!args.captcha) return { ok: false, error: "missing_captcha: run 'captcha' first, then pass --captcha <id>|<answer>" };
    return MessengerBridge.callNative("register", {
      login: args.login, displayName: args.name, password: args.password,
      captchaToken: args.captcha,
    });
  },
  async whoami() { return MessengerBridge.callNative("getSession", {}); },
  async logout() { return MessengerBridge.callNative("logout", {}); },
  async help() {
    return { ok: true, commands: Object.keys(CLI_COMMANDS) };
  },
};

cliInput.addEventListener("keydown", async (e) => {
  if (e.key !== "Enter" || !cliInput.value.trim()) return;
  const raw = cliInput.value;
  cliInput.value = "";
  cliPrint("> " + raw, "cli-line-cmd");

  const { command, args } = parseCliLine(raw);
  const handler = CLI_COMMANDS[command];
  if (!handler) {
    cliPrint(`Неизвестная команда: ${command}. Доступно: ${Object.keys(CLI_COMMANDS).join(", ")}`, "cli-line-err");
    return;
  }
  try {
    const res = await handler(args);
    cliPrint(JSON.stringify(res, null, 2), res.ok ? "" : "cli-line-err");
  } catch (err) {
    cliPrint(String(err), "cli-line-err");
  }
});

cliPrint("Pro-интерфейс готов. Введите 'help' для списка команд.");

// --- Same auth screen flow as Standard interface ------------------------
// (kept in sync deliberately - both interfaces call the exact same Bridge
// handlers, so there is exactly one source of truth for auth logic.)

async function showLogin() {
  await LayoutRenderer.renderScreen("login", app, async (action, state) => {
    if (action === "navigate:register") return showRegister();
    if (action.startsWith("switchInterface:")) {
      const mode = action.split(":")[1];
      MessengerBridge.callNative("setInterfaceMode", { mode });
      return;
    }
    if (action === "login") {
      if (!state.login || !state.password) return showError("Заполните логин и пароль.");
      if (!state.captchaToken) return showError("Пройдите капчу.");
      const res = await MessengerBridge.callNative("login", state);
      if (!res.ok) return showError(`Ошибка: ${res.error}`);
      cliPrint(`[auth] вход выполнен: ${res.displayName || state.login}`);
      ChatShell.renderChatShell(app, { login: state.login, displayName: res.displayName || state.login, starsBalance: res.starsBalance || 0 });
    }
  });
}

async function showRegister() {
  await LayoutRenderer.renderScreen("register", app, async (action, state) => {
    if (action === "navigate:login") return showLogin();
    if (action === "register") {
      if (state.password !== state.passwordRepeat) return showError("Пароли не совпадают.");
      if (!state.captchaToken1) return showError("Пройдите капчу.");
      const res = await MessengerBridge.callNative("register", {
        login: state.login, displayName: state.displayName,
        password: state.password, captchaToken: state.captchaToken1,
      });
      if (!res.ok) return showError(`Ошибка: ${res.error}`);
      document.getElementById("postCaptchaPanel").classList.remove("hidden");
    }
    if (action === "finishRegister") {
      if (!state.captchaToken2) return showError("Пройдите капчу ещё раз.");
      showLogin();
    }
  });
}

function showError(text) {
  cliPrint(text, "cli-line-err");
  const card = document.querySelector(".auth-card");
  if (!card) return;
  const old = card.querySelector(".toast-error");
  if (old) old.remove();
  const div = document.createElement("div");
  div.className = "toast-error";
  div.textContent = text;
  card.appendChild(div);
}

showLogin();
