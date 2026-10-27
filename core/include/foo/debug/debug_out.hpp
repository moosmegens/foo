#ifndef FOO_DEBUG_OUT_HPP
#define FOO_DEBUG_OUT_HPP

#include <iostream>
#include <format>
#include <source_location>
#include <string>
#include <print>

namespace foo
{
    namespace detail
    {
        template <typename... Args>
        inline std::string join(const Args&... args)
        {
            std::string out;
            bool first = true;

            auto append = [&](const auto& arg)
            {
                if (!first)
                    out += ' ';
                first = false;
                std::format_to(std::back_inserter(out), "{}", arg);
            };

            (append(args), ...);
            return out;
        }
    }

    template <typename... Args>
    inline void print([[maybe_unused]] const Args&... args)
    {
#ifndef NDEBUG
        std::cerr << detail::join(args...);
#endif
    }

    template <typename... Args>
    inline void println([[maybe_unused]] const Args&... args)
    {
#ifndef NDEBUG
        std::cerr << detail::join(args...) << '\n';
#endif
    }

    template <typename... Args>
    struct print_trace
    {
        print_trace(
            [[maybe_unused]] const Args&... args,
            [[maybe_unused]] std::source_location loc = std::source_location::current())
        {
#ifndef NDEBUG
            std::println(stderr, "{}: {}", loc.function_name(), detail::join(args...));
#endif
        }
    };

    template <typename... Args>
    print_trace(const Args&...) -> print_trace<Args...>;
}

#endif //FOO_DEBUG_OUT_HPP
