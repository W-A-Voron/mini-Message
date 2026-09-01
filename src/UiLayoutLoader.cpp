#include "UiLayoutLoader.h"
#include <tinyxml2.h>
#include <iostream>

namespace msg {

using json = nlohmann::json;
using namespace tinyxml2;

static json elementToJson(const XMLElement* el) {
    json node;
    node["tag"] = el->Name();

    json attrs = json::object();
    for (const XMLAttribute* a = el->FirstAttribute(); a; a = a->Next()) {
        attrs[a->Name()] = a->Value();
    }
    node["attrs"] = attrs;

    json children = json::array();
    for (const XMLElement* child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        children.push_back(elementToJson(child));
    }
    node["children"] = children;

    if (const char* text = el->GetText()) {
        node["text"] = text;
    }
    return node;
}

json UiLayoutLoader::loadLayout(const std::filesystem::path& xmlPath) {
    XMLDocument doc;
    if (doc.LoadFile(xmlPath.string().c_str()) != XML_SUCCESS) {
        std::cerr << "[UiLayoutLoader] failed to load " << xmlPath << ": " << doc.ErrorStr() << "\n";
        return json{{"error", "load_failed"}, {"path", xmlPath.string()}};
    }
    const XMLElement* root = doc.RootElement();
    if (!root) return json{{"error", "empty_document"}};
    return elementToJson(root);
}

} // namespace msg
