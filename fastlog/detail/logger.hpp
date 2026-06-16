#pragma once

#include "fastlog/detail/logbuffers.hpp"
#include "fastlog/detail/loglevel.hpp"
#include "logfstream.hpp"
#include "fastlog/detail/util.hpp"
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <print>
#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <atomic>

namespace fastlog::detail
{
// 日志记录结构体
struct logrecord_t
{
    const char* data_time;      // 日志记录时间
    uint64_t thread_id;         // 线程ID
    const char* file_name;      // 文件名
    size_t line;                // 行号
    std::string content;        // 日志内容
};

// 日志格式化参数类，封装日志格式化参数
template <typename... Args>
struct basic_format_string_wrapper
{
    template <typename T>
    requires std::convertible_to<T, std::string_view> 
        consteval basic_format_string_wrapper(
            const T& s, std::source_location loc = std::source_location::current()
        ) :fmt(s), loc(loc) {}

    std::format_string<Args...> fmt;
    std::source_location loc;
};

// 格式化字符串包装器，使用std::type_identity_t避免自动类型推导
template <typename... Args>
using format_string_wrapper = basic_format_string_wrapper<std::type_identity_t<Args>...>;

// 日志器基类，基于crtp实现日志器的多态
template <typename DerviceLogger>
class BaseLogger : util::noncopyable
{
public:
    void set_level(LogLevel level)
    {
        _level = level;
    }

    [[nodiscard]]
    LogLevel level() const
    {
        return _level;
    }

    template <typename... Args>
    void trace(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Trace>(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Debug, Args...>(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Info>(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Warn, Args...>(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Error, Args...>(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void fatal(format_string_wrapper<Args...> fmt, Args &&...args) {
        format<LogLevel::Fatal>(fmt, std::forward<Args>(args)...);
    }

private:
    // 格式化日志记录，基于crtp
    template <LogLevel LEVEL, typename... Args>
    void format(format_string_wrapper<Args...> fmt_w, Args&&... args)
    {
        if (LEVEL < _level)
            return;

        std::string time_str;
        auto res = util::get_current_time_tostring();
        if (res.has_value())
        {
            time_str = res.value();
        }

        // 调用派生类的log方法记录日志
        static_cast<DerviceLogger*>(this)->template log<LEVEL>(logrecord_t{
            .data_time = time_str.c_str(),
            .thread_id = util::get_current_pid(),
            .file_name = fmt_w.loc.file_name(),
            .line = fmt_w.loc.line(),
            .content = std::format(fmt_w.fmt, std::forward<Args>(args)...)
        });
    }
private:
    LogLevel _level{LogLevel::Debug};
};


// 控制台日志器
class ConsoleLogger : public BaseLogger<ConsoleLogger>
{
public:
    template <LogLevel level>
    void log(const logrecord_t& record)
    {
        LogLevelWrapper level_wrapper(level);
        std::print("{} [{}{}{}] {} {}:{} {}\n",
            record.data_time,
            level_wrapper.to_color(), level_wrapper.to_string(), reset_format(),
            record.thread_id, record.file_name,
            record.line, record.content    
        );
    }
};

// 文件日志器
class FileLogger: public BaseLogger<FileLogger>
{
    using FileLogBuf = FileLogBuffer<4 * 1024 * 1024>;
    using Logbuf_ptr = std::unique_ptr<FileLogBuf>;
public:
    FileLogger(std::filesystem::path file_path)
        : _logfs(file_path), _current_buffer(std::make_unique<FileLogBuf>())
        , _work_thread(&FileLogger::work, this)
    {
        for (int i = 0; i < 2; ++i)
        {
            _empty_buffers.push_back(std::make_unique<FileLogBuf>());
        }
    }

    ~FileLogger()
    {
        _running = false;
        _cv.notify_one();
        if (_work_thread.joinable())
        {
            _work_thread.join();
        }
    }

    /**
     * 日志记录方法，基于crtp，基于缓冲区机制，三缓冲：
     * 1. 当前缓冲区：当前正在写入日志的换成工区
     * 2. 满缓冲区列表：满了的缓冲区，通知等待被消费的线程
     * 3. 空缓冲区列表：空的缓冲区，等待被填充
     * 
     * 总体思路：
     * 1. 写入当前缓冲区，直到当前缓冲区被写满
     * 2. 将当前缓冲区移动到满缓冲区列表
     * 3. 创建新的当前缓冲区
     * 4. 通知工作线程消费
     * 
     * 当前缓冲区选择：
     * 1. 如果当前缓冲区能够容纳msg，就写入当前缓冲区
     * 2. 如果当前缓冲区不能容纳msg，就将当前缓冲区移动到满缓冲区列表
     * 3. 如果空缓冲区列表不为空，就从空缓冲区列表中取出一个缓冲区，赋值给当前缓冲区
     * 4. 如果空缓冲区列表为空，就创建一个新的缓冲区，赋值给当前缓冲区
     */
    template <LogLevel leve>
    void log(const logrecord_t& record)
    {
        if (!_running)
        {
            return;
        }

        LogLevelWrapper level_wrapper(leve);
        std::string msg{std::format("{} {} {} {}:{} {}\n",
            record.data_time, level_wrapper.to_string(),
            record.thread_id, record.file_name, record.line, record.content
        )};
        
        // 如果当前缓冲区能够容纳msg，就写入当前缓冲区
        if (msg.size() < _current_buffer->writeable())
        {
            _current_buffer->write(msg);
        }
        else
        {
            // 如果当前缓冲区不能容纳msg，就将当前缓冲区移动到满缓冲区列表
            _full_buffers.push_back(std::move(_current_buffer));
            
            // 如果当前空缓冲区列表不为空，就从空缓冲区列表中取出一个缓冲区，赋值给当前缓冲区
            if (!_empty_buffers.empty())
            {
                _current_buffer = std::move(_empty_buffers.front());
                _empty_buffers.pop_front();
            }
            else
            {
                // 如果空缓冲区列表为空，就创建一个新的缓冲区，赋值给当前缓冲区
                _current_buffer = std::make_unique<FileLogBuf>();
            }

            // 写入当前缓冲区
            _current_buffer->write(msg);
            _cv.notify_one();
        }
    }

private:
    /**
     * 工作线程，消费满缓冲区列表中的缓冲区
     * 1. 等待满缓冲区列表不为空
     * 2. 如果满缓冲区过多，只保留两个
     * 3. 消费满缓冲区列表中的缓冲区，将数据写入文件缓冲区
     * 4. 如果满缓冲区列表的缓冲区数量超过2个，只保留2个
     * 5. 如果运行标志为false，且当亲啊缓冲区部位空，则处理关闭前剩余的日志记录，将当前
     * 缓冲区数据写入文件
     * 6. 刷新文件缓冲区，写入文件
     * 7. 将满缓冲区列表中的缓冲区移动到空缓冲区列表
     */
    void work()
    {
        constexpr std::size_t max_buffer_list_size = 15;
        while (_running)
        {
            std::unique_lock lock(_mtx);
            _cv.wait_for(lock, std::chrono::seconds(3),[this](){
                return !_full_buffers.empty();
            });

            // 如果缓冲区链表的缓冲区数量过多，只剩2个，其余丢弃掉
            if (_full_buffers.size() > max_buffer_list_size)
            {
                std::cerr << std::format("Dropped log messages {} larger buffers\n", _full_buffers.size() - 2);
                _full_buffers.resize(2);
            }

            // 消费满缓冲区列表中缓冲区，将数据写入文件缓冲区
            for (auto& buffer : _full_buffers)
            {
                _logfs.write(buffer->data(), buffer->size());
                buffer->reset();
            }

            // 如果满缓冲区列表的缓冲区数量超过2个，只保留2个
            if (_full_buffers.size() > 2)
            {
                _full_buffers.resize(2);
            }

            // 如果运行标志为false，且当前缓冲区不为空，则处理关闭前剩余的日志记录，将当前缓冲区数据写入文件
            if (!_running && !_current_buffer->empty())
            {
                _logfs.write(_current_buffer->data(), _current_buffer->size());
            }

            // 刷新文件缓冲区，写入文件
            _logfs.flush();
            // 将满缓冲区列表中的缓冲区移动到空缓冲区列表
            _empty_buffers.splice(_empty_buffers.end(), _full_buffers);
        }
    }

private:
    Logfstream _logfs;                              // 文件流
    Logbuf_ptr _current_buffer;                     // 当前日志缓冲区
    std::list<Logbuf_ptr> _empty_buffers{};         // 空缓冲区列表
    std::list<Logbuf_ptr> _full_buffers{};          // 满缓冲区列表
    std::mutex _mtx{};                              // 互斥锁
    std::condition_variable _cv{};                  // 条件变量
    std::thread _work_thread{};                     // 工作线程
    std::atomic<bool> _running{true};            // 运行标志
};

} // namespace fastlog::detail
