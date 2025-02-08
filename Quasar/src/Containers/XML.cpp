#include "xml.h"
#include <Core/Log.h>

namespace Quasar {

XMLElement::XMLElementPtr XMLElement::create(const String& name) {
    return std::make_shared<XMLElement>(name);
}

XMLElement::XMLElement() = default;

XMLElement::XMLElement(const String& n) : name(n) {}

String XMLElement::get_attribute(const String& key, const String& default_value) const {
    auto it = attributes.find(key);
    if (it != attributes.end()) return it->second;
    LOG_WARN("XML -- key {%s} not found, returning default", key.c_str());
    return default_value;
}

void XMLElement::set_attribute(const String& key, const String& value) {
    attributes[key] = value;
}

XMLElement::XMLElementPtr XMLElement::get_child(const String& child_name) const {
    for (const auto& child : children) {
        if (child->name == child_name) {
            return child;
        }
    }
    LOG_WARN("XML -- Parent {%s} has no child with name {%s}", child_name.c_str()); 
    return nullptr;
}

std::vector<XMLElement::XMLElementPtr> XMLElement::get_children(const String& child_name) const {
    std::vector<XMLElementPtr> matching_children;
    for (const auto& child : children) {
        if (child->name == child_name) {
            matching_children.push_back(child);
        }
    }

    if (matching_children.empty()) {
        LOG_WARN("XML -- Parent {%s} has no children with name {%s} found", name.c_str(), child_name.c_str());
    }

    return matching_children;
}

void XMLElement::add_child(const XMLElementPtr& child) {
    children.push_back(child);
}

String XMLElement::to_string(int depth) const {
    std::ostringstream oss;
    String indent(depth * 2, ' ');

    oss << indent << "<" << name;
    for (const auto& [key, value] : attributes) {
        oss << " " << key << "=\"" << value << "\"";
    }

    if (content.empty() && children.empty()) {
        oss << "/>\n";
    } else {
        oss << ">\n";
        if (!content.empty()) {
            oss << indent << "  " << content << "\n";
        }
        for (const auto& child : children) {
            oss << child->to_string(depth + 1);
        }
        oss << indent << "</" << name << ">\n";
    }
    return oss.str();
}

String XMLParser::save(XMLElement::XMLElementPtr elim) {
    return elim->to_string();
}

XMLElement::XMLElementPtr XMLParser::parse(const String& xml) {
    std::istringstream stream(xml);
    std::stack<XMLElement::XMLElementPtr> elementStack;
    XMLElement::XMLElementPtr root = nullptr;
    String line;
    bool inComment = false;  // Track whether inside a C-style comment

    while (std::getline(stream, line)) {
        size_t pos = 0;
        while (pos < line.length()) {
            if (inComment) {
                // Check for the end of a comment
                size_t commentEnd = line.find("*/", pos);
                if (commentEnd != String::npos) {
                    inComment = false;
                    pos = commentEnd + 2;  // Move past the end of the comment
                } else {
                    break;  // Continue skipping lines inside the comment
                }
            }

            // Skip whitespace
            while (pos < line.length() && std::isspace(line[pos])) {
                ++pos;
            }
            if (pos >= line.length()) break;

            // Check for start of a C-style comment
            if (line[pos] == '/' && line[pos + 1] == '*') {
                inComment = true;
                pos += 2;  // Move past the start of the comment
                continue;
            }

            // Check for start or self-closing tag
            if (line[pos] == '<' && line[pos + 1] != '/') {
                size_t endPos = line.find('>', pos);
                if (endPos == String::npos) {
                    throw std::runtime_error("Malformed XML: missing '>'");
                }

                bool selfClosing = (line[endPos - 1] == '/');
                String tagContent = line.substr(pos + 1, endPos - pos - 1 - (selfClosing ? 1 : 0));
                size_t spacePos = tagContent.find(' ');
                String tagName = (spacePos != String::npos) ? tagContent.substr(0, spacePos) : tagContent;

                auto element = XMLElement::create(tagName);

                // Parse attributes
                size_t attrStart = spacePos;
                while (attrStart != String::npos) {
                    size_t equalsPos = tagContent.find('=', attrStart);
                    if (equalsPos == String::npos) break;

                    String attrName = tagContent.substr(attrStart + 1, equalsPos - attrStart - 1);
                    size_t valueStart = tagContent.find('"', equalsPos + 1);
                    size_t valueEnd = tagContent.find('"', valueStart + 1);
                    String attrValue = tagContent.substr(valueStart + 1, valueEnd - valueStart - 1);

                    element->set_attribute(attrName, attrValue);
                    attrStart = tagContent.find(' ', valueEnd);
                }

                if (!elementStack.empty()) {
                    elementStack.top()->add_child(element);
                } else {
                    root = element;
                }

                if (!selfClosing) {
                    elementStack.push(element);
                }
                pos = endPos + 1;
            }
            // Check for end tag
            else if (line[pos] == '<' && line[pos + 1] == '/') {
                size_t endPos = line.find('>', pos);
                if (endPos == String::npos) {
                    throw std::runtime_error("Malformed XML: missing '>'");
                }
                String tagName = line.substr(pos + 2, endPos - pos - 2);
                if (elementStack.empty() || elementStack.top()->name != tagName) {
                    throw std::runtime_error("Malformed XML: mismatched end tag " + tagName);
                }
                elementStack.pop();
                pos = endPos + 1;
            }
            // Handle text content
            else {
                size_t endContent = line.find('<', pos);
                if (endContent != String::npos) {
                    String content = line.substr(pos, endContent - pos);
                    if (!elementStack.empty()) {
                        elementStack.top()->content += content;
                    }
                    pos = endContent;
                } else {
                    if (!elementStack.empty()) {
                        elementStack.top()->content += line.substr(pos);
                    }
                    break;
                }
            }
        }
    }

    if (!elementStack.empty()) {
        throw std::runtime_error("Malformed XML: unclosed tags remaining");
    }

    return root;
}

}  // namespace Quasar