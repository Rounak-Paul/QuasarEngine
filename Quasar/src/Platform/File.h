#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#include <qspch.h>

namespace Quasar {
class QS_API File {
public:
    enum class Mode { READ, WRITE, APPEND };
    enum class Type { TEXT, BINARY };

    // Constructor
    File();
    ~File();

    // Basic operations
    bool open(const std::string& path, Mode mode, Type type = Type::TEXT);
    bool open_if_exists(const std::string& path, Mode mode, Type type = Type::TEXT);
    void close();
    bool is_open() const;
    std::string read_all();
    std::vector<std::string> read_lines();
    void write(const std::string& data);
    void write_line(const std::string& data);
    void append(const std::string& data);
    std::string read_line();
    bool eof() {return _file.eof();};
    
    // Binary operations

    /**
     * @brief seek to the start of the file and read till the end of the file, buffer must be allocated by user 
     * 
     * @param out_buffer buffer to store the data
     * @return b8 return true if file read was success, file pointer is at the end of file after this operation
     */
    b8 read_all_binary(u8* out_buffer);

    /// @brief read binary data to the provided buffer, buffer must be allocated by user
    /// @param out_buffer buffer to store the data
    /// @param size size of the desired read operation
    /// @return file pointer index after the read operation
    u64 read_binary(void* out_buffer, u64 size);
    void write_binary(const void* data, u32 size);
    void append_binary(const std::vector<u8>& data);

    static bool exists(std::string path);
    std::size_t get_size() const;

    // Utility functions
    static bool file_exists(const std::string& path);
    static std::size_t file_size(const std::string& path);

private:
    std::string _path;
    Mode _mode;
    Type _type;
    std::fstream _file;

    void open_file(Mode mode, Type type);
};
}