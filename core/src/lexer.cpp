#include <unordered_map>
#include <foo/lexer.hpp>

namespace foo
{
    static char peek(std::string_view src, size_t offset, size_t ahead = 0)
    {
        return offset + ahead < src.size() ? src[offset + ahead] : '\0';
    }

    static void skip(std::string_view src, size_t& offset)
    {
        while (offset < src.size() && std::isspace(static_cast<unsigned char>(src[offset])))
            ++offset;
    }

    static bool match(std::string_view src, size_t& offset, char c)
    {
        if (peek(src, offset) == c)
        {
            ++offset;
            return true;
        }

        return false;
    }

    static bool is_ident_start(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    static bool is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    static bool is_ident_part(char c) noexcept
    {
        return is_ident_start(c) || is_digit(c);
    }

    static Token make(TokenKind kind, size_t start, size_t end)
    {
        return Token{std::move(kind), SourceSpan::between(start, end)};
    }

    static Result<Token> lex_word(std::string_view src, size_t& offset, size_t start)
    {
        while (is_ident_part(peek(src, offset))) ++offset;
        const std::string_view text = src.substr(start, offset - start);

        static const std::unordered_map<std::string_view, TokenKind> keywords = {
            {"var", Var{}},
            {"fn", Function{}},
            {"struct", Struct{}},
            {"ref", Ref{}},
            {"if", If{}},
            {"else", Else{}},
            {"while", While{}},
            {"for", For{}},
            {"in", In{}},
            {"return", Return{}},
            {"true", Boolean{true}},
            {"false", Boolean{false}},
            {"import", Import{}},
        };

        if (auto it = keywords.find(text); it != keywords.end())
            return make(it->second, start, offset);

        return make(Identifier{std::string(text)}, start, offset);
    }

    static Result<Token> lex_string(std::string_view src, size_t& pos, size_t start)
    {
        std::string value;
        while (true)
        {
            if (pos >= src.size() || src[pos] == '\n')
                return fail(SourceSpan::between(start, pos), "unterminated string");

            const char c = src[pos++];
            if (c == '"')
                return make(String{std::move(value)}, start, pos);

            if (c != '\\')
            {
                value += c;
                continue;
            }

            if (pos >= src.size())
                return fail(SourceSpan::between(start, pos), "unterminated string");

            const size_t esc = pos - 1;
            switch (const char e = src[pos++])
            {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                default:
                    return fail(SourceSpan::between(esc, pos), "invalid escape sequence '\\{}'", e);
            }
        }
    }

    static Result<Token> lex_number(std::string_view src, size_t& offset, size_t start)
    {
        bool is_float = false;

        while (is_digit(peek(src, offset)))
            ++offset;

        if (peek(src, offset) == '.' && is_digit(peek(src, offset, 1)))
        {
            is_float = true;
            ++offset;

            while (is_digit(peek(src, offset)))
                ++offset;
        }

        const std::string_view text = src.substr(start, offset - start);

        try
        {
            if (is_float)
                return make(Float{std::stod(std::string(text))}, start, offset);

            return make(Integer{std::stoll(std::string(text))}, start, offset);
        }
        catch (const std::exception& e)
        {
            return fail(
                SourceSpan::between(start, offset),
                "invalid numeric literal '{}': {}",
                text, e.what()
            );
        }
    }


    static Result<Token> lex_token(std::string_view src, size_t& offset)
    {
        skip(src, offset);
        const size_t start = offset;

        if (offset >= src.size())
            return make(EndOfFile{}, start, offset);

        const char c = src[offset++];
        switch (c)
        {
            case '(': return make(LParen{},   start, offset);
            case ')': return make(RParen{},   start, offset);
            case '[': return make(LBracket{}, start, offset);
            case ']': return make(RBracket{}, start, offset);
            case '{': return make(LBrace{},   start, offset);
            case '}': return make(RBrace{},   start, offset);

            case ',': return make(Comma{},     start, offset);
            case ';': return make(Semicolon{}, start, offset);
            case ':': return make(Colon{},     start, offset);
            case '.': return make(Dot{},       start, offset);

            case '+': return make(PlusOp{},     start, offset);
            case '-': return make(MinusOp{},    start, offset);
            case '*': return make(MultiplyOp{}, start, offset);
            case '%': return make(ModuloOp{},   start, offset);

            case '=':
            {
                if (match(src, offset, '='))
                    return make(EqualOp{}, start, offset);
                else
                    return make(AssignOp{}, start, offset);
            }

            case '!':
            {
                if (match(src, offset, '='))
                    return make(NotEqualOp{}, start, offset);
                else
                    return make(NotOp{}, start, offset);
            }

            case '<':
            {
                if (match(src, offset, '='))
                    return make(LessEqualOp{}, start, offset);
                else
                    return make(LessOp{}, start, offset);
            }

            case '>':
            {
                if (match(src, offset, '='))
                    return make(GreaterEqualOp{}, start, offset);
                else
                    return make(GreaterOp{}, start, offset);
            }

            case '&':
                if (match(src, offset, '&')) return make(AndOp{}, start, offset);
                return fail(SourceSpan::between(start, offset), "unexpected '&', did you mean '&&'?");
            case '|':
                if (match(src, offset, '|')) return make(OrOp{}, start, offset);
                return fail(SourceSpan::between(start, offset), "unexpected '|', did you mean '||'?");

            case '"': return lex_string(src, offset, start);

            case '/':
                if (peek(src, offset) == '/')
                {
                    const size_t text_start = ++offset;
                    while (offset < src.size() && src[offset] != '\n')
                        ++offset;

                    return make(Doc{std::string(src.substr(text_start, offset - text_start))}, start, offset);
                }
                return make(DivideOp{}, start, offset);
        }


        if (is_ident_start(c))
        {
            while (is_ident_part(peek(src, offset)))
                ++offset;

            return lex_word(src, offset, start);
        }

        if (is_digit(c))
            return lex_number(src, offset, start);

        return fail(SourceSpan{start, offset}, "unexpected character '{}'", c);
    }


    LexOutput tokenize(std::string_view src)
    {
        LexOutput out{};
        size_t offset = 0;

        while (true)
        {
            auto result = lex_token(src, offset);
            if (!result)
            {
                out.errors.push_back(std::move(result.error()));
                continue;
            }

            const bool done = holds_alternative<EndOfFile>(result->kind);
            out.tokens.push_back(std::move(*result));

            if (done)
                break;
        }

        return out;
    }
}