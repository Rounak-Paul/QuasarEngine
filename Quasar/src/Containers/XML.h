#pragma once

#include "String.h"
#include <unordered_map>
#include <vector>
#include <sstream>
#include <memory>
#include <stack>

namespace Quasar {

class XMLElement {
public:
    using XMLElementPtr = std::shared_ptr<XMLElement>;

    String name;
    std::unordered_map<String, String> attributes;
    std::vector<XMLElementPtr> children;
    String content;

    static XMLElementPtr create(const String& name);

    XMLElement();
    explicit XMLElement(const String& n);

    String get_attribute(const String& key, const String& default_value = "") const;
    void set_attribute(const String& key, const String& value);

    XMLElementPtr get_child(const String& child_name) const;
    std::vector<XMLElementPtr> get_children(const String& child_name) const;
    void add_child(const XMLElementPtr& child);

    String to_string(int depth = 0) const;
};

class XMLParser {
public:
    String save(XMLElement::XMLElementPtr elim);
    XMLElement::XMLElementPtr parse(const String& xml);
};

}  // namespace Quasar