#include "foo/dynamic_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

#include <foo/util/scoped_timer.hpp>

#include <foo/parser.hpp>
#include <foo/source.hpp>

#include <foo/ast_printer.hpp>

#include <foo/debug/log.hpp>

#include "foo/util/str.hpp"

void print_diagnostics(const std::string& error, std::string_view src, foo::SourceSpan span)
{
    #define CLR_RED "\033[1;31m"
    #define CLR_CYAN "\033[1;36m"
    #define CLR_BLUE "\033[1;34m"
    #define CLR_RESET "\033[0m"

    auto lines = foo::get_diagnostic_lines(src, span);

    std::cout << CLR_RED << "error" << CLR_RESET << ": " << error << "\n";

    if (lines.empty())
    {
        std::cout << "NO LINES FOUND\n";
        return;
    }

    int max_line = lines.back().line_number;
    int pad_len = std::to_string(max_line).length();

    std::string pad(pad_len, ' ');

    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto& dl = lines[i];
        std::string num_str = std::to_string(dl.line_number);
        std::string num_pad(pad_len - num_str.length(), ' ');

        std::cout << CLR_CYAN << num_str << num_pad << " |" << CLR_RESET << " " << dl.text << "\n";

        std::cout << pad << " " << CLR_CYAN << "|" << CLR_RESET << " ";
        std::cout << std::string(dl.underline_start, ' ');
        std::cout << CLR_RED << std::string(dl.underline_length, '^');

        if (i == 0)
            std::cout << " " << error;

        std::cout << CLR_RESET << "\n";
    }
}

std::filesystem::path foo_root()
{
    const char* root = std::getenv("FOO_ROOT");

    if (!root)
        throw std::runtime_error("FOO_ROOT is not set");

    return root;
}

std::string load_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
        throw std::runtime_error("Failed to open " + path.string());


    return {std::istreambuf_iterator<char>(file), {}};
}


void run()
{
    std::string str = load_file(foo_root() / "data" / "main.foo");

    auto lex_result = foo::tokenize(str);

    for (const auto& error : lex_result.errors)
    {
        print_diagnostics(error.error, str, error.source);
    }

    auto ast_result = foo::parse(lex_result.tokens);

    for (const auto& error : ast_result.errors)
    {
        print_diagnostics(error.error, str, error.source);
    }

    if (ast_result.errors.empty())
    {

        std::vector<std::string> imports;

        for (const auto& item : ast_result.imports)
        {
            imports.push_back(item.name);
        }

        std::cout << "imports: " << foo::join(imports) << std::endl;

        for (const auto& d : ast_result.decls)
        {
            std::cout << foo::to_string(d) << std::endl;
        }

        auto error = foo::run(ast_result.decls);
        if (error)
        {
            print_diagnostics(error->message, str, error->span);
        }
    }
}

int main()
{
    try
    {

        run();

    } catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
    } catch (...)
    {
        std::cerr << "exception" << std::endl;
    }

    foo::trace("test");
    foo::info("idk bro", "heeeeelp");
    foo::error("idk bro", "heeeeelp\nerror in blah blah blah\nQIUIHOIQUWDH");
    foo::trace();

    return 0;
}