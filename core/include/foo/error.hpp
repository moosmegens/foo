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
    struct Result : std::expected<T, E>
    {
        using std::expected<T, E>::expected;
        bool is_ok()  const { return this->has_value(); }
        bool is_err() const { return !this->has_value(); }
    };

    template<class E = SimpleError>
    using PossibleError = Result<void, E>;
}


#endif //FOO_ERROR_HPP
