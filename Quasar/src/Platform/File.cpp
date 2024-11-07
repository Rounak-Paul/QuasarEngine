#include "File.h"
#include <iostream>

namespace Quasar {
// Constructor
File::File() {
    
}

bool File::open(const std::string& path, Mode mode, Type type) {
    if(_file.is_open()) {
        LOG_WARN("File %s is already open", path.c_str());
        return false;
    }
    _path = path;
    _mode = mode;
    _type = type;
    open_file(mode, type);
    return true;
}

bool File::open_if_exists(const std::string& path, Mode mode, Type type) {
    if(_file.is_open()) {
        LOG_WARN("File %s is already open", path.c_str());
        return false;
    }
    _path = path;
    _mode = mode;
    _type = type;
    if(exists(path)) {
        open_file(mode, type);
    } else {
        LOG_WARN("Unable to locate file: %s", path.c_str());
        return false;
    }
    return true;
}

// Destructor closes the file if open
File::~File() {
    close();
}

void File::close() {
    if (_file.is_open()) {
        _file.flush();
        _file.close();
    }
}

// Opens the file based on mode and type
void File::open_file(Mode mode, Type type) {
    std::ios_base::openmode open_mode = std::ios::in; // Default to read mode

    switch (mode) {
    case Mode::READ:
        open_mode = std::ios::in;
        break;
    case Mode::WRITE:
        open_mode = std::ios::out | std::ios::trunc;
        break;
    case Mode::APPEND:
        open_mode = std::ios::out | std::ios::app;
        break;
    }

    if (type == Type::BINARY) {
        open_mode |= std::ios::binary;
    }

    _file.open(_path, open_mode);

    if (!_file.is_open()) {
        std::cerr << "Failed to open file: " << _path << std::endl;
    }
}

// Check if the file is open
bool File::is_open() const {
    return _file.is_open();
}

// Read the entire file content (text)
std::string File::read_all() {
    if (!is_open() || _mode != Mode::READ || _type != Type::TEXT) return "";

    std::string content((std::istreambuf_iterator<char>(_file)),
                         std::istreambuf_iterator<char>());
    return content;
}

// Read the file line by line (text)
std::vector<std::string> File::read_lines() {
    std::vector<std::string> lines;
    if (!is_open() || _mode != Mode::READ || _type != Type::TEXT) return lines;

    std::string line;
    while (std::getline(_file, line)) {
        lines.push_back(line);
    }
    return lines;
}

// Write data to the file (text)
void File::write(const std::string& data) {
    if (!is_open() || _mode != Mode::WRITE || _type != Type::TEXT) return;
    _file << data;
}

void File::write_line(const std::string& data) {
    if (!is_open() || _mode != Mode::WRITE || _type != Type::TEXT) return;
    _file << data << '\n';
}

// Append data to the file (text)
void File::append(const std::string& data) {
    if (!is_open() || _mode != Mode::APPEND || _type != Type::TEXT) return;
    _file << data;
}

// Read a single line (text)
std::string File::read_line() {
    std::string line;
    if (!is_open() || _mode != Mode::READ || _type != Type::TEXT) return line;
    // Read the next line
    if (std::getline(_file, line)) {
        return line;
    }
    // If the end of the file or an error occurs, return an empty string
    return {};
}

// Read entire file content (binary)
b8 File::read_all_binary(u8* out_buffer) {
    if (!is_open() || _mode != Mode::READ || _type != Type::BINARY) return false;

    // Get file size
    _file.seekg(0, std::ios::end);
    std::size_t size = _file.tellg();
    _file.seekg(0, std::ios::beg);

    _file.read(reinterpret_cast<char*>(out_buffer), size);
    return true;
}

u64 File::read_binary(void* out_buffer, u64 size) {
    if (!is_open() || _mode != Mode::READ || _type != Type::BINARY) return 0;
    auto start = _file.tellg();   // Get the initial position
    _file.read(reinterpret_cast<char*>(out_buffer), size);
    if (_file.fail() && !_file.eof()) {
        // If reading fails for reasons other than EOF, return 0
        return 0;
    }
    auto end = _file.tellg();   // Get the position after reading
    return static_cast<u64>(end - start);  // Return the actual number of bytes read
}

// Write data to file (binary)
void File::write_binary(const void* data, u32 size) {
    if (!is_open() || _mode != Mode::WRITE || _type != Type::BINARY) return;

    _file.write(reinterpret_cast<const char*>(data), size);
}

// Append data to file (binary)
void File::append_binary(const std::vector<u8>& data) {
    if (!is_open() || _mode != Mode::APPEND || _type != Type::BINARY) return;

    _file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Check if the file exists
bool File::exists(std::string path) {
    return std::filesystem::exists(path);
}

// Get the file size
std::size_t File::get_size() const {
    if (!exists(_path)) return 0;
    return std::filesystem::file_size(_path);
}

// Static method: Check if a file exists
bool File::file_exists(const std::string& path) {
    return std::filesystem::exists(path);
}

// Static method: Get the file size
std::size_t File::file_size(const std::string& path) {
    if (!std::filesystem::exists(path)) return 0;
    return std::filesystem::file_size(path);
}

}