#ifndef FOO_DEBUG_OUT_HPP
#define FOO_DEBUG_OUT_HPP

#include <iostream>
#include <format>
#include <source_location>
#include <string>
#include <cstdlib>

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

    [[noreturn]]
    inline void todo(
        std::string_view message = {},
        std::source_location location = std::source_location::current())
    {
        foo::print("TODO:");

        if (!message.empty())
            foo::print(" ", message);

        foo::println(
            "\n  at ",
            std::format("{}:{}:{}",
                location.file_name(),
                location.line(),
                location.column()
            )
        );

        std::cout.flush();

        std::exit( EXIT_FAILURE );
    }
}

#endif //FOO_DEBUG_OUT_HPP
