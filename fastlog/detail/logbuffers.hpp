#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <string>

namespace fastlog::detail
{
// 基于std::array封装文件日志器的缓冲区
template<std::size_t SIZE>
class FileLogBuffer
{
public:
    FileLogBuffer() noexcept
        : _cur(_data.begin())
        {}

    void write(const std::string& str) noexcept
    {
        assert(writeable() > str.size());
        std::copy(str.begin(), str.end(), _cur);
        _cur += str.size();
    }

    [[nodiscard]]
    constexpr auto capacity() noexcept -> std::size_t
    {
        return SIZE;
    }

    [[nodiscard]]
    auto size() noexcept -> std::size_t
    {
        return std::distance(_data.begin(), _cur);
    }

    [[nodiscard]]
    auto writeable() noexcept -> std::size_t
    {
        return capacity() - size();
    }

    [[nodiscard]]
    auto data() const noexcept -> const char*
    {
        return _data.data();
    }

    [[nodiscard]]
    auto empty() const noexcept -> bool
    {
        return _cur == _data.begin();
    }

    void reset() noexcept
    {
        _cur = _data.begin();
    }

private:
    std::array<char, SIZE> _data{};             // 缓冲区数据
    std::array<char, SIZE>::iterator _cur{};    // 当前缓冲区的位置指针
};
} // namespace fastlog::detail
