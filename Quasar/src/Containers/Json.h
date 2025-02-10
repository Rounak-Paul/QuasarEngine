// #pragma once

// #include <iostream>
// #include <unordered_map>
// #include <vector>
// #include <string>
// #include <variant>
// #include <sstream>

// namespace Quasar {

// // Forward declaration
// class JsonValue;

// // Type aliases for JSON objects and arrays
// using JsonObject = std::unordered_map<std::string, JsonValue>;
// using JsonArray = std::vector<JsonValue>;

// // Variant to hold different JSON value types
// using JsonVariant = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

// class JsonValue {
// public:
//     JsonVariant value;

//     // Constructors
//     JsonValue();
//     JsonValue(bool b);
//     JsonValue(double d);
//     JsonValue(const std::string& s);
//     JsonValue(const char* s);
//     JsonValue(const JsonArray& arr);
//     JsonValue(const JsonObject& obj);

//     // Type checking
//     bool isNull() const;
//     bool isBool() const;
//     bool isNumber() const;
//     bool isString() const;
//     bool isArray() const;
//     bool isObject() const;

//     // Type conversion
//     bool asBool() const;
//     double asNumber() const;
//     const std::string& asString() const;
//     const JsonArray& asArray() const;
//     const JsonObject& asObject() const;

//     // Convert JSON to string
//     std::string stringify(int indent = 0) const;

//     // Parse JSON from string
//     static JsonValue parse(const std::string& jsonStr);

// private:
//     static JsonValue parseValue(const std::string& str, size_t& pos);
//     static void skipWhitespace(const std::string& str, size_t& pos);
//     static JsonValue parseNull(const std::string& str, size_t& pos);
//     static JsonValue parseBool(const std::string& str, size_t& pos);
//     static JsonValue parseNumber(const std::string& str, size_t& pos);
//     static JsonValue parseString(const std::string& str, size_t& pos);
//     static JsonValue parseArray(const std::string& str, size_t& pos);
//     static JsonValue parseObject(const std::string& str, size_t& pos);
//     static void writeJson(std::ostringstream& oss, const JsonVariant& value, int indent, int depth);
// };

// } // namespace Quasar