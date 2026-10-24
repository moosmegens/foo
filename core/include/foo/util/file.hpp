#ifndef FOO_FILE_UTIL_HPP
#define FOO_FILE_UTIL_HPP

#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace foo
{
    // inline std::optional<std::string> read_entire_file(const std::filesystem::path& path)
    // {
    //     std::ifstream in(path, std::ios::binary);
    //     if (!in)
    //         return std::nullopt;
    //
    //     std::ostringstream ss;
    //     ss << in.rdbuf();
    //     return ss.str();
    // }

    inline std::expected<std::monostate, std::string> create_file(const std::filesystem::path& path)
    {
        std::error_code ec;

        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), ec);

            if (ec)
                return std::unexpected{
                    "Failed to create directory '" + path.parent_path().string() + "': " + ec.message()
                };
        }

        std::ofstream out(path, std::ios::binary);

        if (!out)
            return std::unexpected{
                "Failed to create file '" + path.string() + "'"
            };

        return {};
    }

    inline std::expected<std::monostate, std::string> write_to_file(const std::filesystem::path& path, std::string_view data, bool append = true)
    {
        std::error_code ec;

        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), ec);

            if (ec)
                return std::unexpected{
                    "Failed to create directory '" +
                    path.parent_path().string() + "': " + ec.message()
                };
        }

        std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));

        if (!out)
            return std::unexpected{
                "Failed to open file '" + path.string() + "' for writing"
            };

        out.write(data.data(), static_cast<std::streamsize>(data.size()));

        if (!out)
            return std::unexpected{
                "Failed to write to file '" + path.string() + "'"
            };

        out.close();

        if (out.fail())
            return std::unexpected{
                "Failed to close file '" + path.string() + "'"
            };

        return {};
    }

    inline std::expected<std::monostate, std::string> create_dir(const std::filesystem::path& path)
    {
        std::error_code ec;

        std::filesystem::create_directories(path, ec);

        if (ec)
            return std::unexpected{
                "Failed to create directory '" + path.string() +
                "': " + ec.message()
            };

        return {};
    }

    inline std::expected<std::string, std::string> read_entire_file(const std::filesystem::path& path)
    {
        errno = 0;
        std::ifstream in(path, std::ios::binary);

        if (!in)
        {
            const auto error = errno;
            return std::unexpected{
                std::format("Failed to open '{}': {}",
                            path.string(),
                            std::strerror(error))
            };
        }

        std::ostringstream ss;
        ss << in.rdbuf();

        if (in.bad())
        {
            const auto error = errno;
            return std::unexpected{
                std::format("Failed to read '{}': {}",
                            path.string(),
                            error ? std::strerror(error) : "I/O error")
            };
        }

        return ss.str();
    }
}


#endif //FOO_FILE_UTIL_HPP
