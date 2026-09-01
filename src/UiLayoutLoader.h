#pragma once
#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

namespace msg {

// The spec asks for UI "in XML, JS and CSS". We take that literally for the
// structural layer: each screen (login, register, chat-list, chat-window,
// settings, ...) is described declaratively in ui/shared/*.layout.xml.
// C++ parses that XML into JSON and hands it to app.js, which walks the
// tree and builds real DOM nodes + wires up CSS classes. This keeps screen
// *structure* out of C++/JS and in a designer-editable XML file, while JS
// keeps behavior and CSS keeps styling - a clean separation of concerns.
class UiLayoutLoader {
public:
    // Parses a single .layout.xml file into a JSON tree:
    // { "tag": "screen", "attrs": {...}, "children": [ ... ] }
    static nlohmann::json loadLayout(const std::filesystem::path& xmlPath);
};

} // namespace msg
