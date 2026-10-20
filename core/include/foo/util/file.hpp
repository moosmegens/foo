#ifndef FOO_FILE_UTIL_HPP
#define FOO_FILE_UTIL_HPP


#include <sstream>
#include <fstream>
#include <string>
#include <optional>
#include <filesystem>


namespace foo
{
    inline std::optional<std::string> read_entire_file(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::nullopt;

        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
}


#endif //FOO_FILE_UTIL_HPP
