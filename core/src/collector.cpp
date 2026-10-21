#include <foo/collector.hpp>

#include <optional>
#include <string>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "foo/lexer.hpp"
#include "foo/parser.hpp"
#include "foo/util/file.hpp"


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

        fs::path to_os_path(const fs::path& root, std::string_view logical)
        {
            fs::path p = root;

            size_t start = 0;
            while (start <= logical.size())
            {
                size_t slash = logical.find('/', start);
                if (slash == std::string_view::npos)
                    slash = logical.size();

                p /= std::string(logical.substr(start, slash - start));
                start = slash + 1;
            }

            p += ".foo";
            return p;
        }

        class Collector
        {
        public:
            Collector() = default;

            CollectorOutput collect(const fs::path& root, const std::string& entry)
            {
                _root = root;

                load(entry, -1, SourceSpan{});

                for (size_t i = 0; i < _out.resolver_input.files.size(); i++)
                {
                    for (size_t k = 0; k < _out.resolver_input.files[i].imports.size(); k++)
                    {
                        const int importer = _out.resolver_input.files[i].source_id;
                        const std::string name = _out.resolver_input.files[i].imports[k].name;
                        const SourceSpan  span = _out.resolver_input.files[i].imports[k].span;

                        load(name, importer, span);
                    }
                }

                return std::move(_out);
            }


        private:
            fs::path _root;
            std::unordered_map<std::string, fs::path> _files;
            CollectorOutput _out;


            void error(int file_id, SourceSpan span, const std::string& message)
            {
                _out.errors.push_back({file_id, span, message});
            }

            void load(const std::string& logical, int importer, SourceSpan span)
            {
                if (auto err = check_import_path(logical))
                {
                    error(importer, span, std::format("invalid import '{}': {}", logical, *err));
                    return;
                }

                const fs::path file_path = to_os_path(_root, logical);

                if (!_files.try_emplace(logical, file_path).second)
                    return;

                auto possible_text = read_entire_file(file_path);
                std::string text;
                if (!possible_text.has_value())
                {
                    error(importer, span, std::format("cannot open '{}': {}", file_path.string(), possible_text.error()));
                    return;
                } else
                {
                    text = possible_text.value();
                }

                for (const auto& [other_name, other_path] : _files)
                {
                    if (other_name == logical)
                        continue;

                    std::error_code ec;
                    const bool same_file = fs::equivalent(file_path, other_path, ec);

                    if (ec)
                    {
                        error(importer, span, std::format("internal error: ec={}", ec.message()));
                        return;
                    }

                    if (same_file)
                    {
                        error(importer, span, std::format(
                            "'{}' and '{}' are the same file.",
                            logical, other_name));

                        return;
                    }
                }

                const int id = static_cast<int>(_out.resolver_input.sources.size());
                _out.resolver_input.sources.push_back({logical, file_path, std::move(text)});
                std::string_view source = _out.resolver_input.sources[static_cast<size_t>(id)].text;

                LexOutput lexed = tokenize(source);
                if (!lexed.errors.empty())
                {
                    for (const auto& e : lexed.errors)
                        error(id, e.source, e.error);

                    return;
                }


                ParseOutput parsed = parse(lexed.tokens);
                for (const auto& e : parsed.errors)
                    error(id, e.source, e.error);

                ResolverFile file;
                file.path = logical;
                file.source_id = id;
                file.decls = std::move(parsed.decls);
                file.imports = std::move(parsed.imports);
                _out.resolver_input.files.push_back(std::move(file));
            }
        };

    }

    CollectorOutput collect(const std::filesystem::path& root, const std::string& entry)
    {
        Collector collector{};
        return collector.collect(root, entry);
    }
}
