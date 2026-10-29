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

#include "foo/collector.hpp"
#include "foo/util/str.hpp"

#include <foo/path.hpp>

#include "foo/runtime.hpp"
#include "foo/debug/debug_out.hpp"

#define CLR_BLACK   "\033[1;30m"
#define CLR_RED     "\033[1;31m"
#define CLR_GREEN   "\033[1;32m"
#define CLR_YELLOW  "\033[1;33m"
#define CLR_BLUE    "\033[1;34m"
#define CLR_MAGENTA "\033[1;35m"
#define CLR_CYAN    "\033[1;36m"
#define CLR_WHITE   "\033[1;37m"
#define CLR_RESET   "\033[0m"

#define CLR_BRIGHT_BLACK   "\033[1;90m"
#define CLR_BRIGHT_RED     "\033[1;91m"
#define CLR_BRIGHT_GREEN   "\033[1;92m"
#define CLR_BRIGHT_YELLOW  "\033[1;93m"
#define CLR_BRIGHT_BLUE    "\033[1;94m"
#define CLR_BRIGHT_MAGENTA "\033[1;95m"
#define CLR_BRIGHT_CYAN    "\033[1;96m"
#define CLR_BRIGHT_WHITE   "\033[1;97m"

void print_diagnostics(const std::string& error, std::string_view src, foo::SourceSpan span, bool is_error = true, const std::string file = "")
{
    auto lines = foo::get_diagnostic_lines(src, span, is_error ? 2 : 1);

    std::string_view col = is_error ? CLR_RED : CLR_YELLOW;

    if (is_error)
        std::cout << col << "error" << CLR_RESET << ": " << error << "\n";
    else
        std::cout << col << "warning" << CLR_RESET << ": " << error << "\n";

    if (!file.empty())
    {
        std::cout << file << "\n";
    }

    if (lines.empty())
    {
        std::cout << "NO LINES FOUND\n";
        return;
    }

    int max_line = lines.back().line_number;
    int pad_len = std::to_string(max_line).length();

    std::string pad(pad_len, ' ');

    for (const auto& dl : lines)
    {
        std::string num_str = std::to_string(dl.line_number);
        std::string num_pad(pad_len - num_str.length(), ' ');

        std::cout << CLR_CYAN << num_str << num_pad
                  << " |" << CLR_BRIGHT_WHITE << " "
                  << dl.text << CLR_RESET << '\n';

        if (dl.underline_length > 0)
        {
            std::cout << pad << " " << CLR_CYAN << "|" << CLR_RESET << " ";
            std::cout << std::string(dl.underline_start, ' ');
            std::cout << col << std::string(dl.underline_length, '^');
            std::cout << " " << error << CLR_RESET << '\n';
        }
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


void main2()
{
    foo::scoped_timer t("timer");
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

    for (const auto& file : r.resolver_input.files)
    {
        std::cout << CLR_YELLOW << "[" << file.path << "]" << CLR_RESET <<'\n';

        std::cout << CLR_GREEN;
        for (const auto& decl : file.decls)
        {
            std::cout << foo::to_string(decl, 1) << '\n';
        }
        std::cout << CLR_RESET;

        std::cout << std::endl;
    }

    foo::NativeRegistry natives;
    natives.type(r.resolver_input.types, "io::file");

    natives.add_method("to_string", [](foo::TypeTable& types, foo::TypeId receiver) -> std::optional<foo::FunctionSig>
    {
        foo::FunctionSig sig;
        sig.ret = foo::BaseTypes::String;
        return sig;
    });

    r.resolver_input.natives = std::move(natives);

    foo::Runtime::inject(r.resolver_input.types, r.resolver_input.natives);

    auto o = foo::resolve(r.resolver_input);
    if (o.errors.empty())
    {

    }
    else
    {
        for (const auto& err : o.errors)
        {
            print_diagnostics(err.message, r.resolver_input.sources[err.file_index].text, err.span, true,
                r.resolver_input.sources[err.file_index].path.string());
        }
    }

    for (const auto& err : o.warnings)
    {
        print_diagnostics(err.message, r.resolver_input.sources[err.file_index].text, err.span, false,
            r.resolver_input.sources[err.file_index].path.string());
    }

}

#define FASSERT(cond) if (!(cond)) { std::cout << #cond << std::endl; }

int main()
{
    try
    {
        main2();


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