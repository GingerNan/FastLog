#pragma once

#include "util.hpp"
#include <array>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>

namespace fastlog::detail
{
class Logfstream
{
    static inline constexpr std::size_t kBufferSize = 1024;
public:
    Logfstream(std::filesystem::path file_path)
        : _file_path(file_path)
    {
        // 如果文件路径有父目录
        auto log_dir = _file_path.parent_path();
        // 如果日志目录不存在，创建目录
        if (!std::filesystem::exists(log_dir))
        {
            std::filesystem::create_directories(log_dir);
        }

        create();
        // 设置文件缓冲区
        _file_stream.rdbuf()->pubsetbuf(_buffer.data(), _buffer.size());
    }

    ~Logfstream()
    {
        _file_stream.close();
    }

    // 刷新文件缓冲区
    void flush()
    {
        _file_stream.flush();
    }

    // 设置单个文件的最大大小
    void set_maxsize(std::size_t maxsize)
    {
        _file_maxsize = maxsize;
    }

    [[nodiscard]]
    auto maxsize() const noexcept -> std::size_t
    {
        return _file_maxsize;
    }

    // 写入日志数据
    void write(const char* data, std::size_t size)
    {
        _file_stream.write(data, size);
        _file_size += size;
        
        // 检查文件大小是否超过最大限制
        if (_file_size > _file_maxsize)
        {
            create();
        }
    }

private:
    // 创建新文件
    void create()
    {
        auto time_str = util::get_current_time_tostring();
        if (time_str.has_value())
        {
            std::filesystem::path log_path = std::format("{}-{}", _file_path.string(), time_str.value());

            _file_size = 0;
            if (_file_stream.is_open())
            {
                _file_stream.close();
            }
            _file_stream.open(log_path, std::ios::out);

            if (!_file_stream.is_open())
            {
                throw std::runtime_error("create log file failed");
            }
        }
    }

private:
    std::ofstream _file_stream{};                   // 文件输出流
    std::filesystem::path _file_path;               // 文件路径
    std::size_t _file_maxsize{100 * 1024 * 1024};   // 单个文件最大大小
    std::array<char, kBufferSize> _buffer;          // 文件输出流缓冲区
    std::size_t _file_size{0};                      // 当前文件大小
};
} // namespace fastlog::detail
