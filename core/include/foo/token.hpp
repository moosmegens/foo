#ifndef FOO_TOKEN_HPP
#define FOO_TOKEN_HPP


#include <cstdint>
#include <string>
#include <variant>

#include "source.hpp"


namespace foo
{
    struct LParen
    {
        static constexpr std::string_view name = "LParen";
        std::string to_string() const { return "("; }
    };

    struct RParen
    {
        static constexpr std::string_view name = "RParen";
        std::string to_string() const { return ")"; }
    };

    struct Doc
    {
        static constexpr std::string_view name = "Doc";
        std::string to_string() const { return "//" + text; }

        std::string text;
    };

    struct EndOfFile
    {
        static constexpr std::string_view name = "EndOfFile";
        static std::string to_string() { return "EOF"; }
    };

    using TokenKind = std::variant<
        LParen, RParen,
        Doc, EndOfFile
    >;

    struct Token
    {
        TokenKind kind;
        SourceSpan source;
    };

    std::string_view name(const TokenKind& kind);
    std::string to_string(const TokenKind& kind);
}


#endif //FOO_TOKEN_HPP