// Walks the {tag, attrs, children, text} tree produced by
// UiLayoutLoader::loadLayout() (see Bridge::handleGetLayout) and builds a
// real DOM subtree. Structure comes from XML, behavior is wired here in JS,
// visuals come entirely from CSS classes - matching the "XML/JS/CSS" split.

const TAG_MAP = {
  screen: "section",
  panel: "div",
  text: "p",
  field: "div",       // expands to label+input below
  button: "button",
  link: "a",
  captcha: "div",     // placeholder widget; see renderCaptcha()
};

function renderCaptcha(node) {
  const wrap = document.createElement("div");
  wrap.className = "captcha-box captcha-loading";
  wrap.dataset.bind = node.attrs.bind || "";
  wrap.innerHTML = `<div class="captcha-visual">Загрузка капчи…</div>`;

  function load() {
    wrap.classList.add("captcha-loading");
    wrap.innerHTML = `<div class="captcha-visual">Загрузка капчи…</div>`;

    window.MessengerBridge.callNative("getCaptcha", {}).then((res) => {
      if (!res.ok) {
        wrap.innerHTML = `
          <div class="captcha-visual captcha-error">Капча недоступна: ${res.error || "нет соединения"}</div>
          <button type="button" class="captcha-retry">Повторить</button>
        `;
        wrap.querySelector(".captcha-retry").addEventListener("click", load);
        return;
      }
      wrap.classList.remove("captcha-loading");
      wrap.dataset.challengeId = res.id;
      wrap.innerHTML = `
        <label class="captcha-question">${res.question}</label>
        <input type="text" class="captcha-answer" inputmode="numeric" placeholder="Ответ" />
      `;
      const input = wrap.querySelector(".captcha-answer");
      const updateToken = () => {
        wrap.dataset.token = input.value ? `${wrap.dataset.challengeId}|${input.value}` : "";
      };
      input.addEventListener("input", updateToken);
    });
  }

  // Real challenge from the server (see CaptchaService on the server side) -
  // not a client-side fake. wrap.dataset.token = "<challengeId>|<answer>",
  // verified server-side and consumed on first use.
  load();
  return wrap;
}

function renderField(node) {
  const wrap = document.createElement("div");
  wrap.className = "field";

  const label = document.createElement("label");
  label.textContent = node.attrs.label || "";
  const input = document.createElement("input");
  input.type = node.attrs.type || "text";
  input.required = node.attrs.required === "true";
  input.dataset.bind = node.attrs.bind || "";
  input.id = node.attrs.id || "";

  wrap.appendChild(label);
  wrap.appendChild(input);
  return wrap;
}

function collectFormState(rootEl) {
  const state = {};
  rootEl.querySelectorAll("[data-bind]").forEach((el) => {
    const key = el.dataset.bind;
    if (!key) return;
    if (el.classList.contains("captcha-box")) {
      state[key] = el.dataset.token || "";
    } else {
      state[key] = el.value;
    }
  });
  return state;
}

function renderNode(node, onAction) {
  if (node.tag === "captcha") return renderCaptcha(node);
  if (node.tag === "field") return renderField(node);

  const htmlTag = TAG_MAP[node.tag] || "div";
  const el = document.createElement(htmlTag);

  if (node.attrs) {
    if (node.attrs.id) el.id = node.attrs.id;
    if (node.attrs.class) el.className = node.attrs.class;
    if (node.attrs.variant) el.classList.add(`variant-${node.attrs.variant}`);
    if (node.attrs.title) el.title = node.attrs.title;
  }
  if (node.text) el.textContent = node.text;
  if (node.tag === "text") el.textContent = node.text || "";
  if (node.tag === "button" || node.tag === "link") {
    el.textContent = node.text || "";
    if (node.attrs && node.attrs.action) {
      el.addEventListener("click", (e) => {
        e.preventDefault();
        onAction(node.attrs.action, el);
      });
    }
  }

  (node.children || []).forEach((child) => {
    el.appendChild(renderNode(child, onAction));
  });

  return el;
}

// Loads screen `name` from C++ (getLayout), renders it into `container`,
// and calls onAction(actionString, formStateAtClickTime, triggerElement)
// whenever a button/link with an `action` attribute is clicked.
async function renderScreen(name, container, onAction) {
  const res = await window.MessengerBridge.callNative("getLayout", { screen: name });
  container.innerHTML = "";
  if (!res.ok) {
    container.textContent = `Layout error: ${res.error || "unknown"}`;
    return null;
  }
  const root = renderNode(res.layout, (action, el) => {
    const state = collectFormState(container);
    onAction(action, state, el);
  });
  container.appendChild(root);
  return root;
}

window.LayoutRenderer = { renderScreen, collectFormState };
