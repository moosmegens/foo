#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

#include <foo/util/scoped_timer.hpp>

#include <foo/parser.hpp>
#include <foo/source.hpp>

void print_diagnostics(const std::string& error, std::string_view src, foo::SourceSpan span)
{
    #define CLR_RED "\033[1;31m"
    #define CLR_CYAN "\033[1;36m"
    #define CLR_BLUE "\033[1;34m"
    #define CLR_RESET "\033[0m"

    auto lines = foo::get_diagnostic_lines(src, span);

    std::cout << CLR_RED << "error" << CLR_RESET << ": " << error << "\n";


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

void rng_lex()
{
    std::string chars = "<+->!=a:";
    std::string str;

    std::srand(std::time(nullptr));

    for (int i = 0; i < 20000; i++)
    {
        str += chars[std::rand() % chars.length()];
    }

    foo::LexOutput tokens;
    {
        foo::scoped_timer timer{"tokenize", true};
        tokens = foo::tokenize(str);
    }

    for (const auto& error : tokens.errors)
    {
        std::cout << "error: " << error.error << std::endl;
    }
}

void parse()
{
    std::string str = "[error, ]";


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
}

int main()
{
    //rng_lex();
    parse();

    return 0;
}