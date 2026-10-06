#ifndef FOO_TOKEN_HPP
#define FOO_TOKEN_HPP


#include <cstdint>
#include <format>
#include <string>
#include <variant>

#include "source.hpp"

#define FOO_TOKEN(NAME, TO_STRING, ...)                 \
    struct NAME {                                  \
        static constexpr std::string_view name = #NAME; \
        __VA_ARGS__                                \
        std::string to_string() const { return TO_STRING; } \
        bool operator==(const NAME&) const = default; \
    }

namespace foo
{
    FOO_TOKEN(LParen, "(");
    FOO_TOKEN(RParen, ")");

    FOO_TOKEN(LBracket, "[");
    FOO_TOKEN(RBracket, "]");

    FOO_TOKEN(LBrace, "{");
    FOO_TOKEN(RBrace, "}");

    FOO_TOKEN(Comma, ",");
    FOO_TOKEN(Semicolon, ";");
    FOO_TOKEN(Colon, ":");
    FOO_TOKEN(Dot, ".");

    FOO_TOKEN(PlusOp, "+");
    FOO_TOKEN(MinusOp, "-");
    FOO_TOKEN(MultiplyOp, "*");
    FOO_TOKEN(DivideOp, "/");
    FOO_TOKEN(ModuloOp, "%");

    FOO_TOKEN(EqualOp, "==");
    FOO_TOKEN(NotEqualOp, "!=");
    FOO_TOKEN(LessOp, "<");
    FOO_TOKEN(LessEqualOp, "<=");
    FOO_TOKEN(GreaterOp, ">");
    FOO_TOKEN(GreaterEqualOp, ">=");

    FOO_TOKEN(AndOp, "&&");
    FOO_TOKEN(OrOp, "||");
    FOO_TOKEN(NotOp, "!");

    FOO_TOKEN(AssignOp, "=");

    FOO_TOKEN(Var, "var");
    FOO_TOKEN(Function, "fn");
    FOO_TOKEN(Struct, "struct");
    FOO_TOKEN(Ref, "ref");

    FOO_TOKEN(If, "if");
    FOO_TOKEN(Else, "else");
    FOO_TOKEN(While, "while");
    FOO_TOKEN(For, "for");
    FOO_TOKEN(In, "in");

    FOO_TOKEN(Return, "return");

    FOO_TOKEN(String, "\"" + value + "\"",
        std::string value;
    );

    FOO_TOKEN(Integer, "int(" + std::to_string(value) + ")",
        std::int64_t value;
    );

    FOO_TOKEN(Float, std::format("float({})", value),
        double value;
    );

    FOO_TOKEN(Boolean, value ? "true" : "false",
        bool value;
    );

    FOO_TOKEN(Identifier, "ident(" + value + ")",
        std::string value;
    );

    FOO_TOKEN(Import, "import");

    FOO_TOKEN(Doc, "doc(" + text + ")",
        std::string text;
    );

    FOO_TOKEN(EndOfFile, "EOF");


    using TokenKind = std::variant<
        LParen, RParen,
        LBracket, RBracket,
        LBrace, RBrace,

        Comma, Semicolon, Colon, Dot,

        Identifier, String, Integer, Float, Boolean,

        PlusOp, MinusOp, MultiplyOp, ModuloOp, DivideOp,

        EqualOp, NotEqualOp,
        LessOp, LessEqualOp,
        GreaterOp, GreaterEqualOp,

        AndOp, OrOp, NotOp,

        AssignOp,

        Var, Function, Struct, Ref,
        If, Else, While, For, In,
        Return,

        Import,

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


#undef FOO_TOKEN


#endif //FOO_TOKEN_HPP