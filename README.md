# Messenger — Client (Phase 1 scaffold)

Это фундамент клиента: рабочая структура проекта, CMake-сборка, C++
бизнес-логика, мост C++⇄JS и UI, чья **структура** описана в XML,
**поведение** — в JS, **внешний вид** — в CSS. Реализованы регистрация,
вход (двойная капча по спеке), переключение Standard/Pro интерфейса,
и заготовка Pro-режима с CLI-панелью и флагом `--cli` у самого бинарника.

## Что уже работает
- CMake-проект (`cmake -B build && cmake --build build`), тянет
  nlohmann/json и tinyxml2 через FetchContent.
- `Config` — читает/пишет `config/client.config.json`; адрес сервера
  (`127.0.0.1:8443` по умолчанию) меняется без пересборки — файлом,
  флагом `--server=host:port` или (позже) экраном настроек.
- `NetworkClient` — фреймированный JSON-протокол поверх TCP (задел под
  TLS явно помечен `TODO` в коде — **не для продакшена без него**).
- `Crypto` — обёртка над **libsodium** (X25519 / XChaCha20-Poly1305 /
  BLAKE2b) со схемой в духе X3DH + Double Ratchet. Никакого
  самописного шифра — см. `SECURITY.md` ниже.
- `Bridge` — единственная точка входа для UI: и Standard, и Pro
  интерфейс дергают одни и те же обработчики, поведение не расходится.
- `WebViewHost` — на Windows реально создаёт окно + WebView2
  (`MSG_USE_WEBVIEW2=1`); на других платформах — понятный стаб с логом,
  чтобы остальной клиент всё равно собирался.
- `UiLayoutLoader` — парсит `ui/shared/*.layout.xml` в JSON, который
  `layout-renderer.js` превращает в DOM.
- Экраны логина/регистрации: логин + пароль + капча → капча ещё раз
  после регистрации, как в требовании №5.

## Чего here ещё нет (следующие фазы)
1. **Экраны чатов/каналов/групп/подарков/премиума** — макеты только
   auth. Нужно добавить `chatlist.layout.xml`, `chat.layout.xml`,
   `settings.layout.xml`, `premium.layout.xml` (с недоступной кнопкой
   «Оформить» согласно спеке) и т.д.
2. **Локальное хранилище сообщений** (`LocalVault`) — шифрованная БД на
   диске клиента (SQLite + ключ, производный от пароля пользователя).
   Сервер сообщений не видит вообще.
3. **Полный Double Ratchet стейт-машина** (сейчас есть только строительные
   блоки — деривация ключей и AEAD; сессии/раунды не реализованы).
4. **TLS** в `NetworkClient` (сейчас — предупреждение в логе, если
   `use_tls: true`, но соединение фактически plaintext).
5. **Сервер** — отдельный проект (обсуждали PostgreSQL для хранения
   логинов/паролей/премиума/звёзд/подарков + админ-консоль). Это
   следующий этап по вашему выбору.
6. **CEF/иной бэкенд** для Linux/macOS, если понадобится
   кроссплатформенность — сейчас реализован только WebView2 (Windows).

## Сборка (Windows, WebView2)
```powershell
git clone <repo>
cd messenger-client
cmake -B build -DMSG_USE_WEBVIEW2=ON
cmake --build build --config Release
```
Перед сборкой нужно положить WebView2 SDK (NuGet `Microsoft.Web.WebView2`)
в `third_party/webview2/` — CMake ссылается на
`third_party/webview2/build/native/...`. Это не входит в этот скаффолд,
т.к. NuGet-пакеты сюда не докладываются.

## Запуск CLI-режима (без окна)
```powershell
messenger_client.exe --cli login --login alice --password ***
messenger_client.exe --cli whoami
```

## Переключение интерфейса
```powershell
messenger_client.exe --interface=pro
```
или через `config/client.config.json → interface.mode`.
