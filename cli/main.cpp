#include "vendor/argparse.hpp"

#include <foo/dynamic_runner.hpp>

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

#include <foo/collector.hpp>

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


int main(int argc, char** argv)
{
    argparse::ArgumentParser program("foo");

    program.add_argument("script")
        .default_value("main")
        .help("Script file to execute");

    program.add_argument("--ast")
        .default_value(false)
        .implicit_value(true)
        .help("Print the AST");

    program.add_argument("--setup")
        .default_value(false)
        .implicit_value(true)
        .help("Sets up single file project");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << '\n';
        return 2;
    }

    const auto script = program.get<std::string>("script");
    const bool ast = program.get<bool>("--ast");
    const bool setup = program.get<bool>("--setup");

    if (setup)
    {
        std::ofstream file(script + ".foo");

        file << "fn main() -> void" << std::endl;
        file << "{" << std::endl;
        file << "    io::println(\"Hello, World!\");" << std::endl;
        file << "}" << std::endl;

        file.close();

        return 0;
    }

    std::filesystem::path path = script;

    auto r = foo::collect(path.root_directory(), path.filename().string());
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
        if (ast)
        {
            for (auto& decl : decls)
            {
                std::cout << foo::to_string(decl) << std::endl;
            }
        }
        else
        {
            auto err = foo::run(decls);
            if (err)
            {
                print_diagnostics(err->message, r.resolver_input.sources[0].text, err->span);
            }
        }
    }
}