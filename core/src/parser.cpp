#include <expected>
#include <iostream>
#include <ostream>
#include <optional>

#include <foo/parser.hpp>

#include <foo/debug/debug_out.hpp>

#include <foo/ast.hpp>
#include <foo/ast_printer.hpp>
#include <source_location>



namespace foo
{

    namespace
    {


        class Parser
        {
        public:
            Parser() = default;

            ParseOutput parse(std::string_view source)
            {
                init(source);
                ParseOutput result;

                result.errors = std::move(_errors);
                result.imports = std::move(_imports);
                return result;
            }

            IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::string_view source)
            {
                init(source);

                return {};
            }

            IntermediateParseOutput<ast::ExprPtr> parse_expr(std::string_view source)
            {
                init(source);

                return {};
            }

            IntermediateParseOutput<ast::StmtPtr> parse_statement(std::string_view source)
            {
                init(source);

                return {};
            }

            IntermediateParseOutput<ast::DeclPtr> parse_decl(std::string_view source)
            {
                init(source);

                return {};
            }

        private:
            std::string_view _src;
            size_t _pos = 0;
            size_t _prev_start = 0;
            size_t _prev_end = 0;
            bool _newline_before = false;

            std::vector<ast::ImportDecl> _imports;
            std::vector<ParseError> _errors;

            void init(std::string_view source)
            {
                _src = source;
                _pos = 0;
                _prev_start = 0;
                _prev_end = 0;
                _newline_before = false;
                _imports = {};
                _errors = {};
            }

            static bool is_digit(char c)
            {
                return c >= '0' && c <= '9';
            }

            static bool is_ident_start(char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            }

            static bool is_ident_part(char c)
            {
                return is_ident_start(c) || is_digit(c);
            }

            static int hex_value(char c)
            {
                if (c >= '0' && c <= '9')
                    return c - '0';

                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;

                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;

                return -1;
            }

            char peek(size_t ahead = 0) const
            {
                return (_pos + ahead < _src.size()) ? _src[_pos + ahead] : '\0';
            }

            bool at_end() const
            {
                return _pos >= _src.size();
            }

            std::string_view peek_word() const
            {
                size_t p = _pos;

                if (p < _src.size() && is_ident_start(_src[p]))
                    while (p < _src.size() && is_ident_part(_src[p]))
                        p++;

                return _src.substr(_pos, p - _pos);
            }

            std::string_view peek_op() const
            {
                static constexpr std::string_view ops[] = {
                    "..=",
                    "..", "::", "->", "=>", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=",
                    "+", "-", "*", "/", "%", "=", "<", ">", "!", "|", "?", ".", ",", ";", ":",
                    "(", ")", "[", "]", "{", "}", "^", "#", "@", "~", "`"
                };

                for (std::string_view op : ops)
                    if (_src.substr(_pos, op.size()) == op)
                        return op;

                return {};
            }

            static bool is_reserved(std::string_view w)
            {
                static constexpr std::string_view words[] = {
                    "var", "fn", "struct", "enum", "ref", "if", "else", "while", "for", "in", "return",
                    "true", "false", "import", "check", "as", "none", "priv", "break", "continue"
                };

                return std::find(std::begin(words), std::end(words), w) != std::end(words);
            }

            size_t string_end(size_t p) const
            {
                const char quote = _src[p];
                size_t i = p + 1;

                while (i < _src.size() && _src[i] != quote && _src[i] != '\n')
                    i += (_src[i] == '\\') ? 2 : 1;

                if (i < _src.size() && _src[i] == quote)
                    ++i;

                return std::min(i, _src.size());
            }

            size_t token_len() const
            {
                if (at_end())
                    return 0;

                const char c = peek();

                if (c == '"' || c == '\'')
                    return string_end(_pos) - _pos;

                if (const auto word = peek_word(); !word.empty())
                    return word.size();

                if (is_digit(c))
                {
                    size_t p = _pos;

                    while (p < _src.size() && is_digit(_src[p]))
                        p++;

                    if (p + 1 < _src.size() && _src[p] == '.' && is_digit(_src[p + 1]))
                        for (p++; p < _src.size() && is_digit(_src[p]); p++) {}

                    return p - _pos;
                }

                if (const auto op = peek_op(); !op.empty())
                    return op.size();

                return 1;
            }

            void skip_trivia()
            {
                _newline_before = false;

                while (!at_end())
                {
                    const char c = peek();

                    if (c == '\n')
                    {
                        _newline_before = true;
                        _pos++;
                    }
                    else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
                    {
                        _pos++;
                    }
                    else if (c == '/' && peek(1) == '/')
                    {
                        while (!at_end() && peek() != '\n')
                            _pos++;
                    }
                    else if (c == '/' && peek(1) == '*')
                    {
                        const size_t start = _pos;
                        _pos += 2;

                        while (!at_end() && !(peek() == '*' && peek(1) == '/'))
                        {
                            if (peek() == '\n')
                                _newline_before = true;

                            _pos++;
                        }

                        if (at_end())
                            error_at(SourceSpan::between(start, start + 2), "unterminated comment");
                        else
                            _pos += 2;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            bool at(std::string_view op) const      { return peek_op() == op; }
            bool at_kw(std::string_view kw) const   { return peek_word() == kw; }

            bool at_ident() const
            {
                const auto word = peek_word();
                return !word.empty() && !is_reserved(word);
            }

            void finish_token(size_t start)
            {
                _prev_start = start;
                _prev_end = _pos;
                skip_trivia();
            }

            void bump(size_t n)
            {
                const size_t start = _pos;
                _pos = std::min(_pos + n, _src.size());
                finish_token(start);
            }

            void skip_token()
            {
                bump(std::max<size_t>(token_len(), 1));
            }

            bool accept(std::string_view op)
            {
                if (!at(op))
                    return false;

                bump(op.size());
                return true;
            }

            bool accept_kw(std::string_view kw)
            {
                if (!at_kw(kw))
                    return false;

                bump(kw.size());
                return true;
            }

            bool accept_raw(char c)
            {
                if (peek() != c)
                    return false;

                bump(1);
                return true;
            }

            std::optional<std::string> ident()
            {
                if (!at_ident())
                    return std::nullopt;

                std::string name{peek_word()};
                bump(name.size());
                return name;
            }

            bool expect(std::string_view op)
            {
                if (accept(op))
                    return true;

                error_after_prev(std::format("expected '{}'", op));
                return false;
            }

            bool expect_kw(std::string_view kw)
            {
                if (accept_kw(kw))
                    return true;

                error_after_prev(std::format("expected '{}'", kw));
                return false;
            }


            struct Mark
            {
                size_t pos;
                size_t prev_start;
                size_t prev_end;
                size_t errors;
                bool newline;
            };

            Mark mark() const
            {
                return {_pos, _prev_start, _prev_end, _errors.size(), _newline_before};
            }

            void reset(const Mark& m)
            {
                _pos = m.pos;
                _prev_start = m.prev_start;
                _prev_end = m.prev_end;
                _newline_before = m.newline;

                _errors.erase(_errors.begin() + static_cast<std::ptrdiff_t>(m.errors), _errors.end());
            }

            bool at_ident_then(std::string_view op)
            {
                if (!at_ident())
                    return false;

                const Mark m = mark();
                bump(peek_word().size());
                const bool result = at(op);
                reset(m);

                return result;
            }

            bool at_alias_decl()
            {
                if (!at_kw("type"))
                    return false;

                const Mark m = mark();
                bump(4);

                bool result = false;
                if (at_ident())
                {
                    bump(peek_word().size());
                    result = at("=");
                }

                reset(m);
                return result;
            }


            SourceSpan span_from(size_t start) const
            {
                return SourceSpan::between(start, _prev_end);
            }

            SourceSpan here_span() const
            {
                if (at_end())
                    return _src.empty() ? SourceSpan{} : SourceSpan{_src.size() - 1, 1};

                return SourceSpan{_pos, token_len()};
            }

            void error_at(SourceSpan span, std::string message)
            {
                _errors.push_back({span, std::move(message)});
            }

            void error_here(std::string message)
            {
                error_at(here_span(), std::move(message));
            }

            std::string describe_prev() const
            {
                const std::string_view text = _src.substr(_prev_start, _prev_end - _prev_start);

                if (text.empty())
                    return {};

                const char c = text.front();

                if (c == '"')
                    return "string literal";

                if (c == '\'')
                    return "format string";

                if (is_digit(c))
                    return text.find('.') != std::string_view::npos ? "float literal" : "integer literal";

                if (is_ident_start(c))
                {
                    if (text == "true" || text == "false")
                        return "boolean literal";

                    return is_reserved(text) ? std::string(text) : "identifier";
                }

                return std::string(text);
            }

            void error_after_prev(std::string message, bool with_after = true)
            {
                if (_prev_end == _prev_start)
                {
                    error_here(std::move(message));
                    return;
                }

                if (with_after)
                    message += std::format(" after '{}'", describe_prev());

                error_at(SourceSpan{_prev_end, 1}, std::move(message));
            }
        }; // parser

    } // namespace {


    ParseOutput parse(std::string_view source)
    {
        Parser parser;
        return parser.parse(source);
    }

    IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::string_view source)
    {
        Parser parser;
        return parser.parse_type(source);
    }

    IntermediateParseOutput<ast::ExprPtr> parse_expr(std::string_view source)
    {
        Parser parser;
        return parser.parse_expr(source);
    }

    IntermediateParseOutput<ast::StmtPtr> parse_statement(std::string_view source)
    {
        Parser parser;
        return parser.parse_statement(source);
    }

    IntermediateParseOutput<ast::DeclPtr> parse_decl(std::string_view source)
    {
        Parser parser;
        return parser.parse_decl(source);
    }
}