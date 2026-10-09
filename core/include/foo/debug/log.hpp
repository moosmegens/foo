#ifndef FOO_LOG_HPP
#define FOO_LOG_HPP


#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>


namespace foo
{
    namespace logger
    {
        enum class LogLevel { Trace, Info, Error, Off };

        struct LogState
        {
            std::mutex mtx;
            std::ofstream file{"foo.log", std::ios::trunc};
            std::atomic<LogLevel> min{LogLevel::Trace};
        };

        inline LogState& log_state()
        {
            static LogState s{};
            return s;
        }

        inline void set_log_level(LogLevel level)
        {
            log_state().min.store(level);
        }

        inline bool is_log_enabled(LogLevel level)
        {
            return level >= log_state().min.load();
        }

        inline bool set_file(
            const std::filesystem::path& path,
            const bool append = true)
        {
            auto& s = log_state();
            std::lock_guard lock(s.mtx);

            if (s.file.is_open())
                s.file.close();

            s.file.open(path, append ? std::ios::app : std::ios::trunc);
            return s.file.is_open();
        }

        inline std::string_view get_level_name(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace: return "TRACE";
                case LogLevel::Info: return "INFO";
                case LogLevel::Error: return "ERROR";

                default:
                    return "?";
            }
        }

        inline void write(
            LogLevel level,
            std::string_view component = "",
            std::string_view msg = "",
            std::source_location loc = std::source_location::current())
        {
            std::string_view level_name = get_level_name(level);

            const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());

            std::string possible_trace;
            if (level == LogLevel::Trace)
            {
                possible_trace = std::format("{}:{}", std::filesystem::path(loc.file_name()).filename().string(), loc.line());
                component = possible_trace;
            }

            const std::string header = std::format(
                "{:%F %T}|{:<5}|{:<14} : ",
                now,
                level_name,
                component);


            std::string indent(header.size(), ' ');

            std::string body;
            body.reserve(msg.size());
            for (const char c : msg)
            {
                body += c;
                if (c == '\n')
                    body += indent;
            }

            const std::string text = header + body + '\n';

            auto& s = log_state();
            std::lock_guard lock(s.mtx);

            if (s.file.is_open())
            {
                s.file << text;
                if (level >= LogLevel::Error)
                    s.file.flush();
            }
        }
    }

    inline void trace(std::source_location loc = std::source_location::current())
    {
        logger::write(logger::LogLevel::Trace, "", "", loc);
    }

    inline void trace(std::string_view msg,
                  std::source_location loc = std::source_location::current())
    {
        logger::write(logger::LogLevel::Trace, "", msg, loc);
    }

    inline void info(std::string_view component, std::string_view msg,
                 std::source_location loc = std::source_location::current())
    {
        logger::write(logger::LogLevel::Info, component, msg, loc);
    }

    inline void error(std::string_view component, std::string_view msg,
                  std::source_location loc = std::source_location::current())
    {
        logger::write(logger::LogLevel::Error, component, msg, loc);
    }
}



#endif /* FOO_LOG_HPP */