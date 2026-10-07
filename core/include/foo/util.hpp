#ifndef FOO_UTIL_HPP
#define FOO_UTIL_HPP

#include <algorithm>
#include <string_view>

namespace foo
{
    // https://en.cppreference.com/cpp/utility/variant/visit2
    template<typename ... Ts>
    struct overload : Ts ... { using Ts::operator() ...; };

    template<class... Ts>
    overload(Ts...) -> overload<Ts...>;

    inline std::string_view left_trim(std::string_view str, std::string_view pattern = " \t\n\r\f\v")
    {
        const auto pos(str.find_first_not_of(pattern));
        str.remove_prefix(std::min(pos, str.length()));
        return str;
    }

    inline std::string_view right_trim(std::string_view str, std::string_view pattern = " \t\n\r\f\v")
    {
        const auto pos(str.find_last_not_of(pattern));
        str.remove_suffix(std::min(str.length() - pos - 1, str.length()));
        return str;
    }

    inline std::string_view trim(std::string_view str, std::string_view pattern = " \t\n\r\f\v")
    {
        str = left_trim(str, pattern);
        str = right_trim(str, pattern);
        return str;
    }
}

#endif //FOO_UTIL_HPP
