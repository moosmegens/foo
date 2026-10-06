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
}


#endif //FOO_ERROR_HPP
