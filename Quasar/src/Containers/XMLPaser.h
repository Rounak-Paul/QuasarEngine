#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>  // For std::shared_ptr
#include <stack>

namespace Quasar {

// Represents an XML element with attributes, children, and text content.
class XMLElement {
public:
    using XMLElementPtr = std::shared_ptr<XMLElement>;
    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<XMLElementPtr> children;
    std::string content;

    // Factory method to create shared pointer to XMLElement
    static XMLElementPtr create(const std::string& name) {
        return std::make_shared<XMLElement>(name);
    }

    XMLElement() = default;
    explicit XMLElement(const std::string& n) : name(n) {}

    // Get attribute by key, return default if not found
    std::string get_attribute(const std::string& key, const std::string& default_value = "") const {
        auto it = attributes.find(key);
        if (it != attributes.end()) return it->second;
        LOG_WARN("key {%s} not found, returning default", key.c_str());
        return default_value;
    }

    // Add or set attribute
    void set_attribute(const std::string& key, const std::string& value) {
        attributes[key] = value;
    }

    // Add child element by shared pointer
    void add_child(const XMLElementPtr& child) {
        children.push_back(child);
    }

    // Serialize this element to XML format
    std::string to_string(int depth = 0) const {
        std::ostringstream oss;
        std::string indent(depth * 2, ' ');  // Indentation for pretty printing

        oss << indent << "<" << name;
        for (const auto& [key, value] : attributes) {
            oss << " " << key << "=\"" << value << "\"";
        }

        if (content.empty() && children.empty()) {
            oss << "/>\n";  // Self-closing tag if no content or children
        } else {
            oss << ">\n";  // New line after opening tag
            if (!content.empty()) {
                oss << indent << "  " << content << "\n";  // Indented content
            }
            if (!children.empty()) {
                for (const auto& child : children) {
                    oss << child->to_string(depth + 1);  // Recursively serialize children
                }
            }
            oss << indent << "</" << name << ">\n";  // Properly indented closing tag
        }
        return oss.str();
    }

};

class XMLParser {
public:
    std::string save(XMLElement::XMLElementPtr elim) {
        return elim->to_string();
    }
    
    XMLElement::XMLElementPtr parse(const std::string& xml) {
        std::istringstream stream(xml);
        std::stack<XMLElement::XMLElementPtr> elementStack;
        XMLElement::XMLElementPtr root = nullptr;
        std::string line;

        while (std::getline(stream, line)) {
            size_t pos = 0;
            while (pos < line.length()) {
                // Skip whitespace
                while (pos < line.length() && std::isspace(line[pos])) {
                    ++pos;
                }
                if (pos >= line.length()) break;

                // Check for start or self-closing tag
                if (line[pos] == '<' && line[pos + 1] != '/') {
                    size_t endPos = line.find('>', pos);
                    if (endPos == std::string::npos) {
                        throw std::runtime_error("Malformed XML: missing '>'");
                    }

                    bool selfClosing = (line[endPos - 1] == '/');
                    std::string tagContent = line.substr(pos + 1, endPos - pos - 1 - (selfClosing ? 1 : 0));
                    size_t spacePos = tagContent.find(' ');
                    std::string tagName = (spacePos != std::string::npos) ? tagContent.substr(0, spacePos) : tagContent;

                    auto element = XMLElement::create(tagName);

                    // Parse attributes
                    size_t attrStart = spacePos;
                    while (attrStart != std::string::npos) {
                        size_t equalsPos = tagContent.find('=', attrStart);
                        if (equalsPos == std::string::npos) break;

                        std::string attrName = tagContent.substr(attrStart + 1, equalsPos - attrStart - 1);
                        size_t valueStart = tagContent.find('"', equalsPos + 1);
                        size_t valueEnd = tagContent.find('"', valueStart + 1);
                        std::string attrValue = tagContent.substr(valueStart + 1, valueEnd - valueStart - 1);

                        element->set_attribute(attrName, attrValue);
                        attrStart = tagContent.find(' ', valueEnd);
                    }

                    if (!elementStack.empty()) {
                        elementStack.top()->add_child(element);
                    } else {
                        root = element;  // Set root element
                    }

                    if (!selfClosing) {
                        elementStack.push(element);  // Only push to stack if it's not self-closing
                    }
                    pos = endPos + 1;  // Move past the '>'
                }
                // Check for end tag
                else if (line[pos] == '<' && line[pos + 1] == '/') {
                    size_t endPos = line.find('>', pos);
                    if (endPos == std::string::npos) {
                        throw std::runtime_error("Malformed XML: missing '>'");
                    }
                    std::string tagName = line.substr(pos + 2, endPos - pos - 2);
                    if (elementStack.empty() || elementStack.top()->name != tagName) {
                        throw std::runtime_error("Malformed XML: mismatched end tag " + tagName);
                    }
                    elementStack.pop();  // End current element
                    pos = endPos + 1;    // Move past the '>'
                }
                // Handle text content
                else {
                    size_t endContent = line.find('<', pos);
                    if (endContent != std::string::npos) {
                        std::string content = line.substr(pos, endContent - pos);
                        if (!elementStack.empty()) {
                            elementStack.top()->content += content;  // Append to current element's content
                        }
                        pos = endContent;  // Move to the next '<'
                    } else {
                        if (!elementStack.empty()) {
                            elementStack.top()->content += line.substr(pos);  // Append remaining content
                        }
                        break;  // End of line
                    }
                }
            }
        }

        if (!elementStack.empty()) {
            throw std::runtime_error("Malformed XML: unclosed tags remaining");
        }

        return root;
    }
};

}  // namespace xml
