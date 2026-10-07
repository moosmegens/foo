#include <unordered_map>
#include <foo/lexer.hpp>
#include <utility>

namespace foo
{
    namespace
    {
        struct LexToken
        {
            Token token;
            SourceSpan source;
        };
    }


    static bool is_ident_start(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    static bool is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    static bool is_ident_part(char c)
    {
        return is_ident_start(c) || is_digit(c);
    }

    static LexToken make(Token token, size_t start, size_t end)
    {
        return LexToken{std::move(token), SourceSpan::between(start, end)};
    }

    class Lexer
    {
    public:
        Lexer() = default;

        LexOutput tokenize(std::string_view source = "")
        {
            _source = source;
            _offset = 0;


            LexOutput out{};

            while (true)
            {
                auto result = lex_token();
                if (!result)
                {
                    out.errors.push_back(std::move(result.error()));
                    continue;
                }

                const bool done = holds_alternative<End>(result.value().token);
                out.tokens.push_back(std::move(result.value().token));

                if (done)
                    break;
            }

            return out;
        }

    private:
        std::string_view _source;
        size_t _offset{0};

        [[nodiscard]] char peek(size_t ahead = 0) const
        {
            return (_offset + ahead < _source.size()) ?
                _source[_offset + ahead] :
                '\0';
        }

        void skip()
        {
            while (_offset < _source.size() && std::isspace(static_cast<unsigned char>(_source[_offset])))
            {
                _offset++;
            }
        }

        bool match(char c)
        {
            if (peek() == c)
            {
                _offset++;
                return true;
            }

            return false;
        }

        Result<LexToken, LexError> lex_word(size_t start)
        {
            while (is_ident_part(peek()))
                _offset++;

            const std::string_view text = _source.substr(start, _offset - start);

            static const std::unordered_map<std::string_view, Token> keywords = {
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
                return make(it->second, start, _offset);

            return make(Identifier{std::string(text)}, start, _offset);
        }

        Result<LexToken, LexError> lex_string(size_t start)
        {
            std::string value;
            while (true)
            {
                if (_offset >= _source.size() || _source[_offset] == '\n')
                    return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unterminated string"}};

                const char c = _source[_offset++];
                if (c == '"')
                    return make(String{std::move(value)}, start, _offset);

                if (c != '\\')
                {
                    value += c;
                    continue;
                }

                if (_offset >= _source.size())
                    return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unterminated string"}};

                //const size_t esc = _offset - 1;
                switch (const char e = _source[_offset++])
                {
                    case 'n':  value += '\n'; break;
                    case 't':  value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '"':  value += '"';  break;
                    default:
                        return std::unexpected<LexError>{{SourceSpan::between(start, _offset), std::format("invalid escape: '{}'", e)}};
                }
            }
        }

        Result<LexToken, LexError> lex_number(size_t start)
        {
            bool is_float = false;

            while (is_digit(peek()))
                ++_offset;

            if (peek() == '.' && is_digit(peek(1)))
            {
                is_float = true;
                _offset++;

                while (is_digit(peek()))
                    _offset++;
            }

            const std::string_view text = _source.substr(start, _offset - start);

            try
            {
                if (is_float)
                    return make(Float{std::stod(std::string(text))}, start, _offset);

                return make(Integer{std::stoll(std::string(text))}, start, _offset);
            }
            catch (const std::exception& e)
            {
                return std::unexpected<LexError>{{
                    SourceSpan::between(start, _offset),
                    std::format("invalid numeric literal '{}'", text)
                }};
            }
        }

        Result<LexToken, LexError> lex_token()
        {
            skip();
            const size_t start = _offset;

            if (_offset >= _source.size())
                return make(End{}, start, _offset);

            const char c = _source[_offset++];
            switch (c)
            {
                case '(': return make(LParen{},   start, _offset);
                case ')': return make(RParen{},   start, _offset);
                case '[': return make(LBracket{}, start, _offset);
                case ']': return make(RBracket{}, start, _offset);
                case '{': return make(LBrace{},   start, _offset);
                case '}': return make(RBrace{},   start, _offset);

                case ',': return make(Comma{},     start, _offset);
                case ';': return make(Semicolon{}, start, _offset);
                case ':': return make(Colon{},     start, _offset);
                case '.': return make(Dot{},       start, _offset);

                case '+': return make(PlusOp{},     start, _offset);
                case '-': return make(MinusOp{},    start, _offset);
                case '*': return make(MultiplyOp{}, start, _offset);
                case '%': return make(ModuloOp{},   start, _offset);

                case '=':
                {
                    if (match('='))
                        return make(EqualOp{}, start, _offset);
                    else
                        return make(AssignOp{}, start, _offset);
                }

                case '!':
                {
                    if (match('='))
                        return make(NotEqualOp{}, start, _offset);
                    else
                        return make(NotOp{}, start, _offset);
                }

                case '<':
                {
                    if (match('='))
                        return make(LessEqualOp{}, start, _offset);
                    else
                        return make(LessOp{}, start, _offset);
                }

                case '>':
                {
                    if (match('='))
                        return make(GreaterEqualOp{}, start, _offset);
                    else
                        return make(GreaterOp{}, start, _offset);
                }

                case '&':
                    if (match('&')) return make(AndOp{}, start, _offset);
                    return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unexpected '&', did you mean '&&'?"}};
                case '|':
                    if (match('|')) return make(OrOp{}, start, _offset);
                    return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unexpected '|', did you mean '||'?"}};

                case '"': return lex_string(start);

                case '/':
                    if (peek() == '/')
                    {
                        const size_t text_start = ++_offset;

                        while (_offset < _source.size() && _source[_offset] != '\n')
                            _offset++;

                        return make(Doc{std::string(_source.substr(text_start, _offset - text_start))}, start, _offset);
                    }

                    return make(DivideOp{}, start, _offset);
            }


            if (is_ident_start(c))
            {
                while (is_ident_part(peek()))
                    _offset++;

                return lex_word(start);
            }

            if (is_digit(c))
                return lex_number(start);

            return std::unexpected<LexError>{{SourceSpan::between(start, _offset),
                std::format("unexpected character '{}'", c)}};
        }
    };

    LexOutput tokenize(std::string_view src)
    {
        Lexer lexer{};
        return lexer.tokenize(src);
    }
}