#include <foo/collector.hpp>
#include <optional>
#include <string>
#include <filesystem>
#include <format>


namespace foo
{
    namespace
    {
        namespace fs = std::filesystem;

        std::optional<std::string> check_import_path(std::string_view path)
        {
            if (path.empty())
                return "import path is empty";

            bool segment_empty = true;

            for (char c : path)
            {
                if (c == '/')
                {
                    if (segment_empty)
                        return "import path has an empty segment (leading or doubled '/')";

                    segment_empty = true;
                }
                else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                {
                    segment_empty = false;
                }
                else if (c == '.')
                {
                    return "import path cannot contain '.' (no '..', and no file extension)";
                }
                else if (c == '\\')
                {
                    return "use '/' as the separator in import paths";
                }
                else
                {
                    return std::format("invalid character '{}' in import path", c);
                }
            }

            if (segment_empty)
                return "import path cannot end with '/'";

            return std::nullopt;
        }
    }
}
