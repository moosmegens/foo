#ifndef FOO_STR_UTIL_HPP
#define FOO_STR_UTIL_HPP


#include <algorithm>
#include <string_view>
#include <span>


namespace foo
{
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

    inline std::string str_repeat(std::string_view str, int count)
    {
        std::string out;

        if (count <= 0 || str.empty())
            return out;

        out.reserve(str.size() * static_cast<std::size_t>(count));

        for (int i = 0; i < count; i++)
            out += str;

        return out;
    }

    inline std::string join(std::span<const std::string> strs, std::string_view separator = ", ")
    {
        std::string result;

        for (const auto& s : strs)
        {
            if (!result.empty())
                result += separator;

            result += s;
        }

        return result;
    }
}

#endif //FOO_STR_UTIL_HPP
