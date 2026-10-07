#ifndef FOO_ERROR_HPP
#define FOO_ERROR_HPP


#include <cstddef>
#include <expected>
#include <format>
#include <string>

#include <foo/source.hpp>


namespace foo
{
    struct SimpleError
    {
        std::string msg;
    };

    template<class T, class E = SimpleError>
    using Result = std::expected<T, E>;
}


#endif //FOO_ERROR_HPP
