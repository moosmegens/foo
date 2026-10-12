#include <unordered_map>
#include <utility>

#include <foo/lexer.hpp>
#include <foo/util/str.hpp>

namespace foo
{
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

    static Token make(TokenVariant token, size_t start, size_t end)
    {
        return Token{std::move(token), SourceSpan::between(start, end)};
    }

    static std::string doc_string(std::string_view text)
    {
        return std::string(trim(text));
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

                const bool done = holds_alternative<tok::End>(result.value().variant);
                out.tokens.push_back(std::move(result.value()));

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

        Result<Token, LexError> lex_word(size_t start)
        {
            while (is_ident_part(peek()))
                _offset++;

            const std::string_view text = _source.substr(start, _offset - start);

            static const std::unordered_map<std::string_view, TokenVariant> keywords = {
                {"var", tok::Var{}},
                {"fn", tok::Function{}},
                {"struct", tok::Struct{}},
                {"ref", tok::Ref{}},
                {"if", tok::If{}},
                {"else", tok::Else{}},
                {"while", tok::While{}},
                {"for", tok::For{}},
                {"in", tok::In{}},
                {"return", tok::Return{}},
                {"true", tok::Boolean{true}},
                {"false", tok::Boolean{false}},
                {"import", tok::Import{}},
            };

            if (auto it = keywords.find(text); it != keywords.end())
                return make(it->second, start, _offset);

            return make(tok::Identifier{std::string(text)}, start, _offset);
        }

        Result<Token, LexError> lex_string(size_t start)
        {
            std::string value;
            while (true)
            {
                if (_offset >= _source.size() || _source[_offset] == '\n')
                    return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unterminated string"}};

                const char c = _source[_offset++];
                if (c == '"')
                    return make(tok::String{std::move(value)}, start, _offset);

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

        Result<Token, LexError> lex_number(size_t start)
        {
            bool is_float = false;

            while (is_digit(peek()))
                _offset++;

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
                    return make(tok::Float{std::stod(std::string(text))}, start, _offset);

                return make(tok::Integer{std::stoll(std::string(text))}, start, _offset);
            }
            catch (const std::exception& e)
            {
                return std::unexpected<LexError>{{
                    SourceSpan::between(start, _offset),
                    std::format("invalid numeric literal '{}'", text)
                }};
            }
        }

        Result<Token, LexError> lex_token()
        {
            while (true)
            {
                skip();
                const size_t start = _offset;

                if (_offset >= _source.size())
                    return make(tok::End{}, _source.size()-1, _source.size());

                const char c = _source[_offset++];
                switch (c)
                {
                    case '(': return make(tok::LParen{},   start, _offset);
                    case ')': return make(tok::RParen{},   start, _offset);
                    case '[': return make(tok::LBracket{}, start, _offset);
                    case ']': return make(tok::RBracket{}, start, _offset);
                    case '{': return make(tok::LBrace{},   start, _offset);
                    case '}': return make(tok::RBrace{},   start, _offset);

                    case '`': return make(tok::BackTick{},   start, _offset);
                    case '~': return make(tok::CurlyLine{},   start, _offset);
                    case '@': return make(tok::At{}, start, _offset);
                    case '#': return make(tok::Hash{}, start, _offset);

                    case '?': return make(tok::QuestionMark{},   start, _offset);

                    case ',': return make(tok::Comma{},     start, _offset);
                    case ';': return make(tok::Semicolon{}, start, _offset);

                    case ':':
                    {
                        if (match(':'))
                            return make(tok::ColonColon{}, start, _offset);
                        else
                            return make(tok::Colon{}, start, _offset);
                    }

                    case '.':
                    {
                        if (match('.'))
                        {
                            if (match('='))
                                return make(tok::DotDotEq{}, start, _offset);

                            return make(tok::DotDot{}, start, _offset);
                        } else
                            return make(tok::Dot{}, start, _offset);
                    }

                    case '+': return make(tok::PlusOp{},     start, _offset);
                    case '-': return make(tok::MinusOp{},    start, _offset);
                    case '*': return make(tok::MultiplyOp{}, start, _offset);
                    case '%': return make(tok::ModuloOp{},   start, _offset);

                    case '=':
                    {
                        if (match('='))
                            return make(tok::EqualOp{}, start, _offset);
                        else
                            return make(tok::AssignOp{}, start, _offset);
                    }

                    case '!':
                    {
                        if (match('='))
                            return make(tok::NotEqualOp{}, start, _offset);
                        else
                            return make(tok::NotOp{}, start, _offset);
                    }

                    case '<':
                    {
                        if (match('='))
                            return make(tok::LessEqualOp{}, start, _offset);
                        else
                            return make(tok::LessOp{}, start, _offset);
                    }

                    case '>':
                    {
                        if (match('='))
                            return make(tok::GreaterEqualOp{}, start, _offset);
                        else
                            return make(tok::GreaterOp{}, start, _offset);
                    }

                    case '&':
                        if (match('&')) return make(tok::AndOp{}, start, _offset);
                        return std::unexpected<LexError>{{SourceSpan::between(start, _offset), "unexpected '&', did you mean '&&'?"}};

                    case '|':
                        if (match('|'))
                            return make(tok::OrOp{}, start, _offset);
                        else
                            return make(tok::Pipe{},   start, _offset);

                    case '"': return lex_string(start);

                    case '/':
                        if (peek() == '/')
                        {
                            _offset++;

                            if (peek() == '!')
                            {
                                _offset++;
                                const size_t text_start = _offset;

                                while (peek() != '\n' && _offset < _source.size())
                                    _offset++;

                                return make(
                                    tok::Doc{std::string(
                                        doc_string(_source.substr(text_start, _offset - text_start)))},
                                    start,
                                    _offset
                                );
                            }

                            while (peek() != '\n' && _offset < _source.size())
                                _offset++;

                            continue;
                        }

                        if (peek() == '*')
                        {
                            _offset++;

                            if (peek() == '!')
                            {
                                _offset++;
                                const size_t text_start = _offset;

                                while (_offset < _source.size() && !(peek() == '*' && peek(1) == '/'))
                                    _offset++;

                                _offset++;
                                _offset++;

                                return make(
                                    tok::Doc{std::string(
                                        doc_string(_source.substr(text_start, _offset - text_start - 2)))},
                                    start,
                                    _offset
                                );
                            }

                            while (_offset < _source.size() && !(peek() == '*' && peek(1) == '/'))
                                _offset++;

                            _offset++;
                            _offset++;

                            continue;
                        }

                        return make(tok::DivideOp{}, start, _offset);
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
        }
    };

    LexOutput tokenize(std::string_view src)
    {
        Lexer lexer{};
        return lexer.tokenize(src);
    }
}
