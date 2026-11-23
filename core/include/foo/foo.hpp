#ifndef FOO_FOO_HPP
#define FOO_FOO_HPP


#include <cstdint>
#include <string>


namespace foo
{
    struct version
    {
        unsigned long long major; unsigned long long minor; unsigned long long patch;
    };

    constexpr inline version get_version()
    {
        return version{ 0, 1, 0};
    }

    constexpr inline std::string version_string(version& v)
    {
        return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    }
}


#endif //FOO_FOO_HPP
