#include "foo/dynamic_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <ranges>

#include <foo/lexer.hpp>

#include <foo/util/scoped_timer.hpp>

#include <foo/parser.hpp>
#include <foo/source.hpp>

#include <foo/ast_printer.hpp>

#include <foo/debug/log.hpp>

#include "foo/collector.hpp"
#include "foo/util/str.hpp"

#include <foo/path.hpp>

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
    // std::string str = load_file(foo_root() / "data" / "main.foo");
    //
    // auto lex_result = foo::tokenize(str);
    //
    // for (const auto& error : lex_result.errors)
    // {
    //     print_diagnostics(error.error, str, error.source);
    // }
    //
    // auto ast_result = foo::parse(lex_result.tokens);
    //
    // for (const auto& error : ast_result.errors)
    // {
    //     print_diagnostics(error.error, str, error.source);
    // }
    //
    // if (ast_result.errors.empty())
    // {
    //
    //     std::vector<std::string> imports;
    //
    //     for (const auto& item : ast_result.imports)
    //     {
    //         imports.push_back(item.name);
    //     }
    //
    //     std::cout << "imports: " << foo::join(imports) << std::endl;
    //
    //     for (const auto& d : ast_result.decls)
    //     {
    //         std::cout << foo::to_string(d) << std::endl;
    //     }
    //
    //     auto error = foo::run(ast_result.decls);
    //     if (error)
    //     {
    //         print_diagnostics(error->message, str, error->span);
    //     }
    // }

    auto r = foo::collect(foo_root() / "data", "main");
    for (const auto& err : r.errors)
    {
        if (err.file_id < 0)
        {
            std::cerr << "error: " << err.message << '\n';
            continue;
        }

        const auto& file = r.resolver_input.sources[static_cast<size_t>(err.file_id)];

        print_diagnostics(
            std::format("{}: {}", file.path.string(), err.message),
            file.text,
            err.span);
    }

    std::vector<foo::ast::DeclPtr> decls;
    for (auto& file : r.resolver_input.files)
    {
        decls.append_range(file.decls | std::views::as_rvalue);
    }

    if (r.errors.empty())
    {
        auto err = foo::run(decls);
        if (err)
        {
            print_diagnostics(err->message, r.resolver_input.sources[0].text, err->span);
        }
    }
}

#define FASSERT(cond) if (!(cond)) { std::cout << #cond << std::endl; }

int main()
{
    try
    {

        foo::Path p = {"io", "println"};

        std::cout << foo::to_string(p) << std::endl;


        //run();

    } catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << std::endl;
    } catch (...)
    {
        std::cerr << "exception" << std::endl;
    }

    return 0;
}