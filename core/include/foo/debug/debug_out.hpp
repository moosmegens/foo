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
        std::cout << detail::join(args...);
    }

    template <typename... Args>
    inline void println([[maybe_unused]] const Args&... args)
    {
        std::cout << detail::join(args...) << '\n';
    }
}

#endif //FOO_DEBUG_OUT_HPP
