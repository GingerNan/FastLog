#pragma once

#include "fastlog/detail/logger.hpp"
#include "fastlog/detail/util.hpp"
#include <filesystem>
#include <unordered_map>
#include <string>

namespace fastlog::detail
{
/**
 * 文件日志管理器，用于创建、删除、获取文件日志器
 * 基于unordered_map实现，key为日志器名称，value为日志器
 */
class FileLoggerManager : util::noncopyable
{
public:
    FileLogger& make_logger(const std::string& logger_name,
        std::filesystem::path file_path)
    {
        _loggers.emplace(logger_name, file_path);
        return _loggers.at(logger_name);
    }

    void delete_logger(const std::string& logger_name)
    {
        _loggers.erase(logger_name);
    }

    FileLogger* get_logger(const std::string& logger_name)
    {
        if (_loggers.find(logger_name) != _loggers.end())
        {
            return std::addressof(_loggers.at(logger_name));
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, FileLogger> _loggers;
};

} // namespace fastlog::detail
