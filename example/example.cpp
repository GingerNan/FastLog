#include <vector>
#include "fastlog/detail/loglevel.hpp"
#include "fastlog/fastlog.hpp"

std::vector<int> vec = {1,2,3,4,5,6,7,8,9,10};
void file_log_func1()
{
    long long count = 0;
    while (true)
    {
        fastlog::file::get_logger("file_log1")
            ->info("hello world log1, count:{}, vec:{}", count++, vec);

        if (count > 1000000)
            break;
    }
}

void file_log_func2()
{
    long long count = 0;
    while (true)
    {
        fastlog::file::get_logger("file_log2")
            ->info("hello world log2, count:{}, vec:{}", count++, vec);

        if (count > 1000000)
            break;
    }
}

void console_log_test()
 {
    fastlog::set_consolelog_level(fastlog::LogLevel::Trace);
    fastlog::console.trace("hello world");
    fastlog::console.debug("hello world");
    fastlog::console.info("hello world");
    fastlog::console.warn("hello world");
    fastlog::console.error("hello world");
    fastlog::console.fatal("hello world");
}

void file_log_test()
 {
    // 注册文件日志器
    auto &file_logger1 = fastlog::file::make_logger("file_log1", FILE_LOG1_PATH);
    auto &file_logger2 = fastlog::file::make_logger("file_log2", FILE_LOG2_PATH);
    std::thread t1(file_log_func1);
    std::thread t2(file_log_func2);
    t1.join();
    t2.join();
}

void test2()
 {
    console_log_test();
    fastlog::console.info("file log write start .............\n");
    file_log_test();
    fastlog::console.info("file log write finish .............\n");
}

void test1()
{
    // 设置控制台日志最低级别
    fastlog::set_consolelog_level(fastlog::LogLevel::Trace);

    // 控制台日志
    fastlog::console.info("Hello, FastLog! Value: {}", 18);
    fastlog::console.warn("This is a warning");
    fastlog::console.error("This is an error");

    // 文件日志
    // 注册文件日志器
    auto& logger = fastlog::file::make_logger("app_log");
    logger.info("Application started, user_id: {}", 12345);

    // 获取指定文件日志器
    fastlog::file::get_logger("app_log")->info("hello world");
}

int main()
{
    test1();
    return 0;
}