// #include "Json.h"
// #include <stdexcept>
// #include <cctype>

// namespace Quasar {

// // Constructors
// JsonValue::JsonValue() : value(nullptr) {}
// JsonValue::JsonValue(bool b) : value(b) {}
// JsonValue::JsonValue(double d) : value(d) {}
// JsonValue::JsonValue(const std::string& s) : value(s) {}
// JsonValue::JsonValue(const char* s) : value(std::string(s)) {}
// JsonValue::JsonValue(const JsonArray& arr) : value(arr) {}
// JsonValue::JsonValue(const JsonObject& obj) : value(obj) {}

// // Type checking
// bool JsonValue::isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
// bool JsonValue::isBool() const { return std::holds_alternative<bool>(value); }
// bool JsonValue::isNumber() const { return std::holds_alternative<double>(value); }
// bool JsonValue::isString() const { return std::holds_alternative<std::string>(value); }
// bool JsonValue::isArray() const { return std::holds_alternative<JsonArray>(value); }
// bool JsonValue::isObject() const { return std::holds_alternative<JsonObject>(value); }

// // Type conversion
// bool JsonValue::asBool() const { return std::get<bool>(value); }
// double JsonValue::asNumber() const { return std::get<double>(value); }
// const std::string& JsonValue::asString() const { return std::get<std::string>(value); }
// const JsonArray& JsonValue::asArray() const { return std::get<JsonArray>(value); }
// const JsonObject& JsonValue::asObject() const { return std::get<JsonObject>(value); }

// // Parse JSON from string
// JsonValue JsonValue::parse(const std::string& jsonStr) {
//     size_t pos = 0;
//     return parseValue(jsonStr, pos);
// }

// JsonValue JsonValue::parseValue(const std::string& str, size_t& pos) {
//     skipWhitespace(str, pos);
//     if (str[pos] == 'n') return parseNull(str, pos);
//     if (str[pos] == 't' || str[pos] == 'f') return parseBool(str, pos);
//     if (str[pos] == '-' || std::isdigit(str[pos])) return parseNumber(str, pos);
//     if (str[pos] == '"') return parseString(str, pos);
//     if (str[pos] == '[') return parseArray(str, pos);
//     if (str[pos] == '{') return parseObject(str, pos);
//     throw std::runtime_error("Invalid JSON");
// }

// void JsonValue::skipWhitespace(const std::string& str, size_t& pos) {
//     while (pos < str.size() && std::isspace(str[pos])) pos++;
// }

// JsonValue JsonValue::parseNull(const std::string& str, size_t& pos) {
//     if (str.substr(pos, 4) == "null") {
//         pos += 4;
//         return JsonValue();
//     }
//     throw std::runtime_error("Invalid null value");
// }

// JsonValue JsonValue::parseBool(const std::string& str, size_t& pos) {
//     if (str.substr(pos, 4) == "true") {
//         pos += 4;
//         return JsonValue(true);
//     }
//     if (str.substr(pos, 5) == "false") {
//         pos += 5;
//         return JsonValue(false);
//     }
//     throw std::runtime_error("Invalid boolean value");
// }

// JsonValue JsonValue::parseNumber(const std::string& str, size_t& pos) {
//     size_t start = pos;
//     while (pos < str.size() && (std::isdigit(str[pos]) || str[pos] == '.' || str[pos] == '-' || str[pos] == 'e' || str[pos] == 'E' || str[pos] == '+'))
//         pos++;
//     return JsonValue(std::stod(str.substr(start, pos - start)));
// }

// JsonValue JsonValue::parseString(const std::string& str, size_t& pos) {
//     pos++; // Skip opening quote
//     size_t start = pos;
//     while (pos < str.size() && str[pos] != '"') pos++;
//     std::string result = str.substr(start, pos - start);
//     pos++; // Skip closing quote
//     return JsonValue(result);
// }

// JsonValue JsonValue::parseArray(const std::string& str, size_t& pos) {
//     pos++; // Skip '['
//     JsonArray arr;
//     while (pos < str.size()) {
//         skipWhitespace(str, pos);
//         if (str[pos] == ']') { pos++; break; }
//         arr.push_back(parseValue(str, pos));
//         skipWhitespace(str, pos);
//         if (str[pos] == ']') { pos++; break; }
//         if (str[pos] != ',') throw std::runtime_error("Expected ',' in array");
//         pos++;
//     }
//     return JsonValue(arr);
// }

// JsonValue JsonValue::parseObject(const std::string& str, size_t& pos) {
//     pos++; // Skip '{'
//     JsonObject obj;
//     while (pos < str.size()) {
//         skipWhitespace(str, pos);
//         if (str[pos] == '}') { pos++; break; }
//         JsonValue key = parseString(str, pos);
//         skipWhitespace(str, pos);
//         if (str[pos] != ':') throw std::runtime_error("Expected ':' in object");
//         pos++;
//         JsonValue value = parseValue(str, pos);
//         obj[key.asString()] = value;
//         skipWhitespace(str, pos);
//         if (str[pos] == '}') { pos++; break; }
//         if (str[pos] != ',') throw std::runtime_error("Expected ',' in object");
//         pos++;
//     }
//     return JsonValue(obj);
// }

// // Convert JSON object to string
// std::string JsonValue::stringify(int indent) const {
//     std::ostringstream oss;
//     writeJson(oss, value, indent, 0);
//     return oss.str();
// }

// } // namespace Quasar