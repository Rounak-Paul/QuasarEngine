#pragma once
#include <iostream>
#include <string>
#include <cstring>  // For strlen and memcpy

#include <Memory/Memory.h>

namespace Quasar {
using String = std::string;
}

// namespace Quasar {
// class QS_API String {
// private:
//     char* _data;
//     size_t _length;

//     void allocate_and_copy(const char* src, size_t len) {
//         _data = static_cast<char*>(QSMEM.allocate(len + 1));
//         if (!_data) {
//             throw std::bad_alloc();
//         }
//         memcpy(_data, src, len);
//         _data[len] = '\0';
//         _length = len;
//     }

// public:
//     // Default constructor
//     String() : _data(nullptr), _length(0) {}

//     // Constructor with C-string
//     String(const char* str) {
//         if (str) {
//             size_t len = strlen(str);
//             allocate_and_copy(str, len);
//         } else {
//             _data = nullptr;
//             _length = 0;
//         }
//     }

//     // Constructor with std::string
//     String(const std::string& str) {
//         size_t len = str.size();
//         allocate_and_copy(str.c_str(), len);
//     }

//     // Copy constructor
//     String(const String& other) {
//         if (other._data) {
//             allocate_and_copy(other._data, other._length);
//         } else {
//             _data = nullptr;
//             _length = 0;
//         }
//     }

//     // Move constructor
//     String(String&& other) noexcept : _data(other._data), _length(other._length) {
//         other._data = nullptr;
//         other._length = 0;
//     }

//     // Destructor
//     ~String() {
//         clear();
//     }

//     // Copy assignment operator
//     String& operator=(const String& other) {
//         if (this != &other) {
//             if (_data) {
//                 QSMEM.free(_data);
//             }
//             if (other._data) {
//                 allocate_and_copy(other._data, other._length);
//             } else {
//                 _data = nullptr;
//                 _length = 0;
//             }
//         }
//         return *this;
//     }

//     // Move assignment operator
//     String& operator=(String&& other) noexcept {
//         if (this != &other) {
//             if (_data) {
//                 QSMEM.free(_data);
//             }
//             _data = other._data;
//             _length = other._length;
//             other._data = nullptr;
//             other._length = 0;
//         }
//         return *this;
//     }

//     // Constructor with repeated characters
//     String(size_t count, char ch) {
//         _data = static_cast<char*>(QSMEM.allocate(count + 1));
//         if (!_data) {
//             throw std::bad_alloc();
//         }
//         memset(_data, ch, count);
//         _data[count] = '\0';
//         _length = count;
//     }

//     // Replace content
//     void replace(const char* str) {
//         if (_data) {
//             QSMEM.free(_data);
//         }
//         if (str) {
//             size_t len = strlen(str);
//             allocate_and_copy(str, len);
//         } else {
//             _data = nullptr;
//             _length = 0;
//         }
//     }

//     // Get _length of the string
//     size_t length() const {
//         return _length;
//     }

//     // Check if the string is empty
//     bool empty() const {
//         return _length == 0;
//     }

//     // Get C-string representation
//     const char* c_str() const {
//         return _data;
//     }

//     // Append another string
//     void append(const char* str) {
//         if (str) {
//             size_t new_len = _length + strlen(str);
//             char* new_data = nullptr;
//             try {
//                 new_data = static_cast<char*>(QSMEM.allocate(new_len + 1));
//                 if (!new_data) {
//                     throw std::bad_alloc();
//                 }
//                 if (_data) {
//                     memcpy(new_data, _data, _length);
//                 }
//                 memcpy(new_data + _length, str, strlen(str));
//                 new_data[new_len] = '\0';
//                 if (_data) {
//                     QSMEM.free(_data);
//                 }
//                 _data = new_data;
//                 _length = new_len;
//             } catch (...) {
//                 if (new_data) {
//                     QSMEM.free(new_data);
//                 }
//                 throw;
//             }
//         }
//     }

//     // Append another String
//     String& operator+=(const String& other) {
//         append(other._data);
//         return *this;
//     }

//     // Concatenate another String
//     String operator+(const String& other) const {
//         String result;
//         result._length = _length + other._length;
//         result._data = static_cast<char*>(QSMEM.allocate(result._length + 1));
//         if (!result._data) {
//             throw std::bad_alloc();
//         }
//         try {
//             if (_data) {
//                 memcpy(result._data, _data, _length);
//             }
//             if (other._data) {
//                 memcpy(result._data + _length, other._data, other._length);
//             }
//             result._data[result._length] = '\0';
//         } catch (...) {
//             QSMEM.free(result._data);
//             throw;
//         }
//         return result;
//     }

//     // Concatenate with C-string on the right-hand side
//     String operator+(const char* str) const {
//         if (!str) {
//             return *this;
//         }
//         size_t str_len = strlen(str);
//         String result;
//         result._length = _length + str_len;
//         result._data = static_cast<char*>(QSMEM.allocate(result._length + 1));
//         if (!result._data) {
//             throw std::bad_alloc();
//         }
//         try {
//             if (_data) {
//                 memcpy(result._data, _data, _length);
//             }
//             memcpy(result._data + _length, str, str_len);
//             result._data[result._length] = '\0';
//         } catch (...) {
//             QSMEM.free(result._data);
//             throw;
//         }
//         return result;
//     }

//     // Concatenate with C-string on the left-hand side
//     friend String operator+(const char* str, const String& other) {
//         String result;
//         size_t str_len = strlen(str);
//         result._length = str_len + other._length;
//         result._data = static_cast<char*>(QSMEM.allocate(result._length + 1));
//         if (!result._data) {
//             throw std::bad_alloc();
//         }
//         try {
//             memcpy(result._data, str, str_len);
//             if (other._data) {
//                 memcpy(result._data + str_len, other._data, other._length);
//             }
//             result._data[result._length] = '\0';
//         } catch (...) {
//             QSMEM.free(result._data);
//             throw;
//         }
//         return result;
//     }

//     // Append C-string
//     String& operator+=(const char* str) {
//         append(str);
//         return *this;
//     }

//     // Equality operator
//     bool operator==(const String& other) const {
//         if (_length != other._length) {
//             return false;
//         }
//         return strcmp(_data, other._data) == 0;
//     }

//     // Inequality operator
//     bool operator!=(const String& other) const {
//         return !(*this == other);
//     }

//     // Less than operator
//     bool operator<(const String& other) const {
//         return strcmp(_data, other._data) < 0;
//     }

//     // Greater than operator
//     bool operator>(const String& other) const {
//         return strcmp(_data, other._data) > 0;
//     }

//     // Index operator
//     char& operator[](size_t index) {
//         if (index >= _length) {
//             throw std::out_of_range("Index out of range");
//         }
//         return _data[index];
//     }

//     const char& operator[](size_t index) const {
//         if (index >= _length) {
//             throw std::out_of_range("Index out of range");
//         }
//         return _data[index];
//     }

//     // Output stream operator
//     friend std::ostream& operator<<(std::ostream& os, const String& str) {
//         if (str._data) {
//             os << str._data;
//         }
//         return os;
//     }

//     // Input stream operator
//     friend std::istream& operator>>(std::istream& is, String& str) {
//         char buffer[1024];
//         is >> buffer;
//         str.replace(buffer);
//         return is;
//     }

//     // Find a substring
//     size_t find(const char* str) const {
//         if (!_data || !str) return std::string::npos;
//         char* pos = strstr(_data, str);
//         if (pos) {
//             return pos - _data;
//         }
//         return std::string::npos;
//     }

//     //Find a character
//     size_t find(char ch, size_t start = 0) const {
//         if (!_data || start >= _length) return std::string::npos;
//         for (size_t i = start; i < _length; ++i) {
//         if (_data[i] == ch) {
//         return i;
//         }
//         }
//         return std::string::npos;
//     }
//     // Substring
//     String substr(size_t pos, size_t len) const {
//         if (pos > _length) {
//             throw std::out_of_range("Position out of range");
//         }
//         size_t actual_len = std::min(len, _length - pos);
//         String result;
//         result.allocate_and_copy(_data + pos, actual_len);
//         return result;
//     }

//     // Clear the string (QSMEM.free memory and reset _length)
//     void clear() {
//         if (_data) {
//             QSMEM.free(_data);
//         }
//         _data = nullptr;
//         _length = 0;
//     }
// };
// }