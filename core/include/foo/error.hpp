#ifndef FOO_ERROR_HPP
#define FOO_ERROR_HPP


#include <cstddef>
#include <expected>
#include <format>
#include <string>

#include <foo/source.hpp>


namespace foo
{
    struct Error
    {
        SourceSpan location;
        std::string text;
    };

    template<class T>
    using Result = std::expected<T, Error>;

    template<class... Args>
    std::unexpected<Error> fail(SourceSpan location, std::format_string<Args...> fmt, Args&&... args)
    {
        return std::unexpected(Error{location, std::format(fmt, std::forward<Args>(args)...)});
    }
}


#endif //FOO_ERROR_HPP
