#ifndef FOO_TOKEN_HPP
#define FOO_TOKEN_HPP


#include <cstdint>
#include <string>
#include <variant>


namespace foo
{
    struct LParen {};
    struct RParen {};
    struct LBracket {};
    struct RBracket {};
    struct LBrace {};
    struct RBrace {};

    struct Comma {};
    struct Semicolon {};
    struct Colon {};
    struct Dot {};

    struct Identifier { std::string name; };
    struct String { std::string value; };
    struct Integer { std::int64_t value; };
    struct Float { double value; };
    struct Boolean { bool value; };

    struct PlusOp {};
    struct MinusOp {};
    struct MultiplyOp {};
    struct DivideOp {};
    struct ModuloOp {};

    struct EqualOp {};
    struct NotEqualOp {};
    struct LessOp {};
    struct LessEqualOp {};
    struct GreaterOp {};
    struct GreaterEqualOp {};

    struct AndOp {};
    struct OrOp {};
    struct NotOp {};

    struct AssignOp {};

    struct Var {};
    struct Function {};
    struct Struct {};
    struct Ref {};
    struct If {};
    struct Else {};
    struct While {};
    struct For {};
    struct In {};
    struct Return {};

    struct EndOfFile {};

    using Token = std::variant<
        LParen, RParen, LBracket, RBracket, LBrace, RBrace,
        Comma, Semicolon, Colon, Dot,
        Identifier, String, Integer, Float, Boolean,
        PlusOp, MinusOp, MultiplyOp, DivideOp, ModuloOp,
        EqualOp, NotEqualOp, LessOp, LessEqualOp, GreaterOp, GreaterEqualOp,
        AndOp, OrOp, NotOp,
        AssignOp,
        Var, Function, Struct, Ref, If, Else, While, For, In, Return,
        EndOfFile
    >;
}


#endif //FOO_TOKEN_HPP