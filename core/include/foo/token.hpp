#ifndef FOO_TOKEN_HPP
#define FOO_TOKEN_HPP


#include <cstdint>
#include <format>
#include <string>
#include <variant>

#include "source.hpp"


namespace foo
{
    struct LParen { static constexpr std::string_view name = "LParen";
        std::string to_string() const { return "("; }
    };

    struct RParen { static constexpr std::string_view name = "RParen";
        std::string to_string() const { return ")"; }
    };

    struct LBracket { static constexpr std::string_view name = "LBracket";
        std::string to_string() const { return "["; }
    };

    struct RBracket { static constexpr std::string_view name = "RBracket";
        std::string to_string() const { return "]"; }
    };

    struct LBrace { static constexpr std::string_view name = "LBrace";
        std::string to_string() const { return "{"; }
    };

    struct RBrace { static constexpr std::string_view name = "RBrace";
        std::string to_string() const { return "}"; }
    };


    struct Comma { static constexpr std::string_view name = "Comma";
        std::string to_string() const { return ","; }
    };

    struct Semicolon { static constexpr std::string_view name = "Semicolon";
        std::string to_string() const { return ";"; }
    };

    struct Colon { static constexpr std::string_view name = "Colon";
        std::string to_string() const { return ":"; }
    };

    struct Dot { static constexpr std::string_view name = "Dot";
        std::string to_string() const { return "."; }
    };


    struct PlusOp { static constexpr std::string_view name = "PlusOp";
        std::string to_string() const { return "+"; }
    };

    struct MinusOp { static constexpr std::string_view name = "MinusOp";
        std::string to_string() const { return "-"; }
    };

    struct MultiplyOp { static constexpr std::string_view name = "MultiplyOp";
        std::string to_string() const { return "*"; }
    };

    struct DivideOp { static constexpr std::string_view name = "DivideOp";
        std::string to_string() const { return "/"; }
    };

    struct ModuloOp { static constexpr std::string_view name = "ModuloOp";
        std::string to_string() const { return "%"; }
    };


    struct EqualOp { static constexpr std::string_view name = "EqualOp";
        std::string to_string() const { return "=="; }
    };

    struct NotEqualOp { static constexpr std::string_view name = "NotEqualOp";
        std::string to_string() const { return "!="; }
    };

    struct LessOp { static constexpr std::string_view name = "LessOp";
        std::string to_string() const { return "<"; }
    };

    struct LessEqualOp { static constexpr std::string_view name = "LessEqualOp";
        std::string to_string() const { return "<="; }
    };

    struct GreaterOp { static constexpr std::string_view name = "GreaterOp";
        std::string to_string() const { return ">"; }
    };

    struct GreaterEqualOp { static constexpr std::string_view name = "GreaterEqualOp";
        std::string to_string() const { return ">="; }
    };

    struct AndOp { static constexpr std::string_view name = "AndOp";
        std::string to_string() const { return "&&"; }
    };
    struct OrOp { static constexpr std::string_view name = "OrOp";
        std::string to_string() const { return "||"; }
    };
    struct NotOp { static constexpr std::string_view name = "NotOp";
        std::string to_string() const { return "!"; }
    };

    struct AssignOp { static constexpr std::string_view name = "AssignOp";
        std::string to_string() const { return "="; }
    };


    struct Var { static constexpr std::string_view name = "Var";
        std::string to_string() const { return "var"; }
    };

    struct Function { static constexpr std::string_view name = "Function";
        std::string to_string() const { return "fn"; }
    };

    struct Struct   { static constexpr std::string_view name = "Struct";
        std::string to_string() const { return "struct"; }
    };

    struct Ref { static constexpr std::string_view name = "Ref";
        std::string to_string() const { return "ref"; }
    };

    struct If { static constexpr std::string_view name = "If";
        std::string to_string() const { return "if"; }
    };

    struct Else { static constexpr std::string_view name = "Else";
        std::string to_string() const { return "else"; }
    };

    struct While { static constexpr std::string_view name = "While";
        std::string to_string() const { return "while"; }
    };

    struct For { static constexpr std::string_view name = "For";
        std::string to_string() const { return "for"; }
    };

    struct In { static constexpr std::string_view name = "In";
        std::string to_string() const { return "in"; }
    };

    struct Return { static constexpr std::string_view name = "Return";
        std::string to_string() const { return "return"; }
    };

    struct String
    {
        static constexpr std::string_view name = "String";
        std::string to_string() const { return "\"" + value + "\""; }

        std::string value;
    };

    struct Integer
    {
        static constexpr std::string_view name = "Integer";
        std::string to_string() const { return "int(" + std::to_string(value) + ")"; }

        std::int64_t value;
    };

    struct Float
    {
        static constexpr std::string_view name = "Float";
        std::string to_string() const { return std::format("float({})", value); }

        double value;
    };

    struct Boolean
    {
        static constexpr std::string_view name = "Boolean";
        std::string to_string() const { return value ? "true" : "false"; }

        bool value;
    };

    struct Identifier
    {
        static constexpr std::string_view name = "Identifier";
        std::string to_string() const { return "ident(" + value + ")"; }

        std::string value;
    };

    struct Import { static constexpr std::string_view name = "Import";
        std::string to_string() const { return "import"; }
    };


    struct Doc
    {
        static constexpr std::string_view name = "Doc";
        std::string to_string() const { return "doc(" + text + ")"; }

        std::string text;
    };

    struct EndOfFile
    {
        static constexpr std::string_view name = "EndOfFile";
        static std::string to_string() { return "EOF"; }
    };

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


#endif //FOO_TOKEN_HPP