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

                while (!at_end())
                {

                    const size_t before = _pos;
                    auto decl = parse_decl();

                    if (decl)
                    {
                        result.decls.push_back(std::move(decl));
                        continue;
                    }

                    if (_pos == before && !at_end())
                        skip_token();

                    synchronize_declaration(before);
                }

                result.errors = std::move(_errors);
                result.imports = std::move(_imports);
                return result;
            }

            IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::string_view source)
            {
                init(source);
                auto value = parse_type();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::ExprPtr> parse_expr(std::string_view source)
            {
                init(source);
                auto value = parse_expr();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::StmtPtr> parse_statement(std::string_view source)
            {
                init(source);
                auto value = parse_stmt();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::DeclPtr> parse_decl(std::string_view source)
            {
                init(source);
                auto value = parse_decl();

                return {std::move(_errors), std::move(value)};
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

                skip_trivia();
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
                    "var", "fn", "struct", "enum", "interface", "ref",
                    "if", "else", "while", "for", "in", "return",
                    "true", "false", "import", "check", "as", "none",
                    "priv", "break", "continue", "code"
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
                    i++;

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

            bool at_ref_lambda()
            {
                if (!at_kw("ref"))
                    return false;

                const Mark m = mark();
                bump(3);
                const bool result = peek() == '|';
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

            void synchronize_declaration(size_t start_pos)
            {
                while (!at_end())
                {
                    if (_pos > start_pos && is_declaration_start())
                        return;

                    skip_token();
                }
            }

            void synchronize_statement(size_t start_pos)
            {
                while (!at_end() && !at("}"))
                {
                    if (accept(";"))
                        return;

                    if (_pos > start_pos && is_statement_start())
                        return;

                    skip_token();
                }
            }

            bool is_struct_member_start()
            {
                return at_kw("fn")
                    || at_kw("priv")
                    || at_ident_then(":");
            }

            void synchronize_struct_member(size_t start_pos)
            {
                while (!at_end() && !at("}"))
                {
                    if (_pos > start_pos && is_struct_member_start())
                        return;

                    if (accept(";"))
                        return;

                    skip_token();
                }
            }

            void synchronize_interface_member(size_t start_pos)
            {
                while (!at_end() && !at("}"))
                {
                    if (_pos > start_pos && _newline_before && at_ident())
                        return;

                    if (accept(";"))
                        return;

                    skip_token();
                }
            }

            bool is_statement_start() const
            {
                const auto word = peek_word();

                return word == "var"
                    || word == "return"
                    || word == "if"
                    || word == "while"
                    || word == "for"
                    || word == "check"
                    || word == "continue"
                    || word == "break"
                    || at("{");
            }

            bool is_declaration_start()
            {
                return at_kw("fn")
                    || at_kw("struct")
                    || at_kw("enum")
                    || at_kw("interface")
                    || at_kw("import")
                    || at_kw("code")
                    || at_alias_decl();
            }

            bool is_expr_boundary() const
            {
                return at(",")
                    || at(";")
                    || at(")")
                    || at("]")
                    || at("}")
                    || at("=>")
                    || at_end();
            }

            bool is_closing_delimiter() const
            {
                return at(")")
                    || at("]")
                    || at("}");
            }

            bool is_recovery_boundary() const
            {
                return is_closing_delimiter()
                    || at(";")
                    || at("=>")
                    || at_end();
            }

            struct Depth
            {
                int parens = 0;
                int brackets = 0;
                int braces = 0;

                bool zero() const { return parens == 0 && brackets == 0 && braces == 0; }

                void account(char c)
                {
                    switch (c)
                    {
                        case '(': parens++; break;
                        case ')': if (parens > 0) parens--; break;
                        case '[': brackets++; break;
                        case ']': if (brackets > 0) brackets--; break;
                        case '{': braces++; break;
                        case '}': if (braces > 0) braces--; break;
                        default: break;
                    }
                }
            };

            Depth depth_between(size_t from, size_t to) const
            {
                Depth depth;
                size_t i = from;
                to = std::min(to, _src.size());

                while (i < to)
                {
                    const char c = _src[i];

                    if (c == '"' || c == '\'')
                    {
                        i = string_end(i);
                    }
                    else if (c == '/' && i + 1 < _src.size() && _src[i + 1] == '/')
                    {
                        while (i < _src.size() && _src[i] != '\n')
                            i++;
                    }
                    else if (c == '/' && i + 1 < _src.size() && _src[i + 1] == '*')
                    {
                        i += 2;

                        while (i + 1 < _src.size() && !(_src[i] == '*' && _src[i + 1] == '/'))
                            i++;

                        i += 2;
                    }
                    else
                    {
                        depth.account(c);
                        i++;
                    }
                }

                return depth;
            }

            void synchronize_delimited(std::string_view closing, size_t start_pos)
            {
                Depth depth = depth_between(start_pos, _pos);

                while (!at_end())
                {
                    if (depth.zero())
                    {
                        if (at(closing))
                            return;

                        if (accept(","))
                            return;

                        if (is_closing_delimiter() || at(";") || at("=>"))
                            return;
                    }

                    depth.account(peek());
                    skip_token();
                }
            }



            struct BindingPower { int left; int right; };

            static constexpr int as_bp = 18;
            static constexpr int unary_bp = 19;
            static constexpr int postfix_bp = 21;
            static constexpr int assign_l_bp = 2;

            static std::optional<BindingPower> binary_bp(std::string_view op)
            {
                if (op == "=")
                    return BindingPower{assign_l_bp, 1};

                if (op == ".." || op == "..=")
                    return BindingPower{4, 5};

                if (op == "||")
                    return BindingPower{6, 7};

                if (op == "&&")
                    return BindingPower{8, 9};

                if (op == "==" || op == "!=")
                    return BindingPower{10, 11};

                if (op == "<" || op == "<=" || op == ">" || op == ">=")
                    return BindingPower{12, 13};

                if (op == "+" || op == "-")
                    return BindingPower{14, 15};

                if (op == "*" || op == "/" || op == "%")
                    return BindingPower{16, 17};

                return std::nullopt;
            }

            static std::optional<ast::BinaryOp> to_binary_op(std::string_view op)
            {
                using Op = ast::BinaryOp;

                if (op == "+")      return Op::Add;
                if (op == "-")      return Op::Sub;
                if (op == "*")      return Op::Mul;
                if (op == "/")      return Op::Div;
                if (op == "%")      return Op::Mod;
                if (op == "==")     return Op::Eq;
                if (op == "!=")     return Op::NotEq;
                if (op == "<")      return Op::Less;
                if (op == "<=")     return Op::LessEq;
                if (op == ">")      return Op::Greater;
                if (op == ">=")     return Op::GreaterEq;
                if (op == "&&")     return Op::And;
                if (op == "||")     return Op::Or;
                if (op == "=")      return Op::Assign;
                if (op == "..")     return Op::Range;
                if (op == "..=")    return Op::RangeInclusive;

                return std::nullopt;
            }

            void read_escape(std::string& out)
            {
                const size_t backslash = _pos - 1;

                if (at_end())
                    return;

                switch (const char e = _src[_pos++])
                {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"':  out += '"';  break;
                    case '\'': out += '\''; break;
                    case '{':  out += '{';  break;
                    case 'x':
                    {
                        const int hi = hex_value(peek());
                        const int lo = hex_value(peek(1));

                        if (hi < 0 || lo < 0)
                        {
                            error_at(SourceSpan::between(backslash, _pos), "'\\x' must be followed by two hex digits");
                            break;
                        }

                        _pos += 2;
                        out += static_cast<char>(hi * 16 + lo);
                        break;
                    }
                    default:
                        error_at(SourceSpan::between(backslash, _pos), std::format("invalid escape: '{}'", e));
                        break;
                }
            }

            void read_string(std::string& out)
            {
                const size_t start = _pos;
                _pos++;

                while (true)
                {
                    if (at_end() || peek() == '\n')
                    {
                        error_at(SourceSpan::between(start, _pos), "unterminated string");
                        break;
                    }

                    const char c = _src[_pos++];

                    if (c == '"')
                        break;

                    if (c == '\\')
                        read_escape(out);
                    else
                        out += c;
                }

                finish_token(start);
            }

            ast::ExprPtr parse_string()
            {
                const size_t start = _pos;
                std::string value;

                read_string(value);
                return std::make_unique<ast::Expr>(ast::StringLit{std::move(value)}, span_from(start));
            }

            ast::ExprPtr parse_format_string()
            {
                const size_t start = _pos;
                _pos++; // the opening ', raw

                ast::ExprPtr result;
                std::string text;
                size_t text_start = _pos;

                auto append = [&](ast::ExprPtr piece)
                {
                    if (!result)
                    {
                        result = std::move(piece);
                        return;
                    }

                    const SourceSpan joined = SourceSpan::combine(result->span, piece->span);
                    result = std::make_unique<ast::Expr>(
                        ast::Binary{ast::BinaryOp::Add, std::move(result), std::move(piece)}, joined);
                };

                auto flush = [&]()
                {
                    if (!text.empty())
                        append(std::make_unique<ast::Expr>(
                            ast::StringLit{std::move(text)}, SourceSpan::between(text_start, _pos)));

                    text.clear();
                };

                auto fail = [&](std::string message) -> ast::ExprPtr
                {
                    error_at(SourceSpan::between(start, _pos), std::move(message));

                    while (!at_end() && peek() != '\'' && peek() != '\n')
                        _pos++;

                    if (peek() == '\'')
                        _pos++;

                    finish_token(start);
                    return nullptr;
                };

                while (peek() != '\'')
                {
                    if (at_end() || peek() == '\n')
                        return fail("unterminated format string");

                    if (peek() == '{')
                    {
                        flush();
                        _pos++;
                        skip_trivia();

                        auto expr = parse_expr();

                        if (!expr || peek() != '}')
                            return fail("expected '}' to end the interpolation");

                        _pos++; // the closing '}', raw: the text after it keeps its spaces
                        text_start = _pos;

                        const SourceSpan span = expr->span;
                        auto method = std::make_unique<ast::Expr>(ast::Member{std::move(expr), "to_string"}, span);
                        append(std::make_unique<ast::Expr>(ast::Call{std::move(method), {}}, span));
                        continue;
                    }

                    if (peek() == '\\')
                    {
                        _pos++;
                        read_escape(text);
                        continue;
                    }

                    text += _src[_pos++];
                }

                flush();
                _pos++; // the closing '
                finish_token(start);

                if (!result)
                    result = std::make_unique<ast::Expr>(ast::StringLit{""}, span_from(start));

                result->span = span_from(start);
                return result;
            }

            ast::ExprPtr parse_number()
            {
                const size_t start = _pos;
                const size_t length = token_len();
                const std::string text(_src.substr(start, length));

                bump(length);

                try
                {
                    if (text.find('.') != std::string::npos)
                        return std::make_unique<ast::Expr>(ast::FloatLit{std::stod(text)}, span_from(start));

                    return std::make_unique<ast::Expr>(ast::IntLit{std::stoll(text)}, span_from(start));
                }
                catch (const std::exception&)
                {
                    error_at(span_from(start), std::format("invalid numeric literal '{}'", text));
                    return nullptr;
                }
            }


            // types

            ast::TypeExprPtr parse_type()
            {
                const size_t start = _pos;
                ast::TypeExprPtr base = nullptr;



                // map or set
                if (at("["))
                {
                    bump(1);

                    auto first_type = parse_type();
                    if (!first_type)
                        return nullptr;

                    ast::TypeExprPtr second_type;

                    if (accept(","))
                    {
                        second_type = parse_type();
                        if (!second_type)
                            return nullptr;
                    }

                    if (!at("]"))
                    {
                        error_here(second_type ? "expected ']' after map value type"
                                               : "expected ',' or ']'");
                        return nullptr;
                    }

                    bump(1);

                    base = std::make_unique<ast::TypeExpr>();
                    if (second_type)
                        base->type = ast::MapType{std::move(first_type), std::move(second_type)};
                    else
                        base->type = ast::SetType{std::move(first_type)};
                    base->span = span_from(start);
                }
                // tuple, variant or for forcing primary
                else if (at("("))
                {
                    bump(1);

                    std::vector<ast::TupleElem> items;
                    bool is_variant = false;

                    while (true)
                    {
                        ast::TupleElem item;

                        // an optional name, only documentation
                        if (at_ident_then(":"))
                        {
                            item.name = *ident();
                            bump(1); // ':'
                        }

                        item.type = parse_type();
                        if (!item.type)
                            return nullptr;

                        items.push_back(std::move(item));

                        if (at(",") || at("|"))
                        {
                            const bool pipe = at("|");

                            if (items.size() == 1)
                            {
                                is_variant = pipe;
                            }
                            else if (pipe != is_variant)
                            {
                                error_here("cannot mix ',' and '|', wrap one of them in parentheses");
                                return nullptr;
                            }

                            bump(1);
                            continue;
                        }

                        break;
                    }

                    if (!expect(")"))
                        return nullptr;

                    const SourceSpan span = span_from(start);

                    if (items.size() == 1 && items[0].name.empty())
                    {
                        // (T) is T, this is what makes (fn(int) -> int)? an optional function
                        base = std::move(items[0].type);
                    }
                    else if (items.size() < 2)
                    {
                        error_at(span, "a tuple or variant needs at least 2 types");
                        return nullptr;
                    }
                    else
                    {
                        base = std::make_unique<ast::TypeExpr>();

                        if (is_variant)
                        {
                            ast::VariantType variant;
                            for (auto& item : items)
                                variant.alternatives.push_back(std::move(item.type));

                            base->type = std::move(variant);
                        }
                        else
                            base->type = ast::TupleType{std::move(items)};

                        base->span = span;
                    }
                }
                // fn(int, string) -> bool
                else if (at_kw("fn"))
                {
                    bump(2);

                    if (!expect("("))
                        return nullptr;

                    ast::FnType fn;

                    while (!at(")") && !at_end())
                    {
                        auto param = parse_type();
                        if (!param)
                            return nullptr;

                        fn.params.push_back(std::move(param));

                        if (!accept(","))
                            break;
                    }

                    if (!expect(")"))
                        return nullptr;

                    // `fn(int) -> int?` is a function that returns an int?
                    if (accept("->"))
                    {
                        fn.ret = parse_type();
                        if (!fn.ret)
                            return nullptr;
                    }

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = std::move(fn);
                    base->span = span_from(start);
                }
                // named
                else if (at_ident())
                {
                    std::vector<std::string> parts;
                    parts.push_back(*ident());

                    while (accept("::"))
                    {
                        if (!at_ident())
                        {
                            error_here("expected identifier after '::'");
                            return nullptr;
                        }

                        parts.push_back(*ident());
                    }

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = ast::NamedType{std::move(parts)};
                    base->span = span_from(start);
                }
                else
                {
                    error_after_prev("expected type expression");
                    return nullptr;
                }


                // 2 modifiers, optional and array

                bool last_was_optional = false;
                while (true)
                {
                    if (at("[")) // array
                    {
                        bump(1);

                        if (!expect("]"))
                            break;

                        auto arr = std::make_unique<ast::TypeExpr>();
                        arr->type = ast::ArrayType{std::move(base)};
                        arr->span = span_from(start);
                        base = std::move(arr);

                        last_was_optional = false;
                    }
                    else
                    if (accept_raw('?')) // optional
                    {
                        if (last_was_optional)
                        {
                            error_at(SourceSpan::between(_prev_start, _prev_end), "you can't have an optional of an optional");
                            continue;
                        }

                        auto optional = std::make_unique<ast::TypeExpr>();
                        optional->type = ast::OptionalType{std::move(base)};
                        optional->span = span_from(start);
                        base = std::move(optional);

                        last_was_optional = true;
                    }
                    else
                    {
                        break;
                    }
                }


                return base;
            }


            // decls

            void parse_import()
            {
                do
                {
                    if (peek() != '"')
                    {
                        error_after_prev("expected import path string");
                        break;
                    }

                    const size_t start = _pos;
                    std::string path;

                    read_string(path);
                    _imports.push_back({std::move(path), span_from(start)});
                }
                while (accept(","));

                accept(";");
            }

            ast::DeclPtr parse_decl()
            {
                const size_t start = _pos;

                bool pub = true;
                if (at_kw("priv"))
                {
                    bump(4);
                    pub = false;

                    if (!at_kw("fn"))
                    {
                        error_here("expected 'fn' after 'priv'");
                        return nullptr;
                    }
                }

                if (accept_kw("interface"))
                {
                    auto interface = parse_interface_decl();
                    if (!interface)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*interface), span_from(start));
                }


                if (accept_kw("enum"))
                {
                    auto enumeration = parse_enum_decl();
                    if (!enumeration)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*enumeration), span_from(start));
                }

                if (accept_kw("code"))
                {
                    auto code = parse_code_decl();
                    if (!code)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*code), span_from(start));
                }

                if (at_alias_decl())
                {
                    bump(4); // 'type'

                    auto alias = parse_alias_decl();
                    if (!alias)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*alias), span_from(start));
                }

                if (accept_kw("struct"))
                {
                    auto strukt = parse_struct_decl();
                    if (!strukt)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*strukt), span_from(start));
                }

                if (accept_kw("fn"))
                {
                    auto fn = parse_fn_decl(pub);
                    if (!fn)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*fn), span_from(start));
                }


                error_here("expected declaration");
                return nullptr;
            }

            std::optional<ast::InterfaceDecl> parse_interface_decl()
            {
                auto name = ident();

                if (!name)
                {
                    error_here("expected interface name");
                    return std::nullopt;
                }

                if (!expect("{"))
                    return std::nullopt;

                std::vector<ast::FnInterface> functions;

                while (!at("}") && !at_end())
                {
                    const size_t member_start = _pos;

                    auto fn_name = ident();

                    if (!fn_name)
                    {
                        error_here("expected function name in interface");

                        if (_pos == member_start)
                            skip_token();

                        synchronize_interface_member(member_start);
                        continue;
                    }

                    auto signature = parse_function_signature();

                    if (!signature)
                    {
                        synchronize_interface_member(member_start);
                        continue;
                    }

                    functions.push_back({
                        std::move(*fn_name),
                        std::move(*signature)
                    });

                    accept(";");
                }

                if (!expect("}"))
                    return std::nullopt;

                return ast::InterfaceDecl{
                    std::move(*name),
                    std::move(functions)
                };
            }

            std::optional<ast::EnumDecl> parse_enum_decl()
            {
                auto name = ident();
                if (!name)
                {
                    error_here("expected enum name");
                    return std::nullopt;
                }

                if (!expect("{"))
                    return std::nullopt;

                std::vector<ast::EnumVariant> variants;

                while (!at("}") && !at_end())
                {
                    if (!at_ident())
                    {
                        error_here("expected variant name");

                        while (!at_end() && !at(",") && !at("}"))
                            skip_token();

                        accept(",");
                        continue;
                    }

                    const SourceSpan span = here_span();
                    variants.push_back({*ident(), span});

                    if (accept(","))
                        continue;

                    if (!at("}"))
                        error_here("expected ',' or '}' after enum variant");
                }

                if (!expect("}"))
                    return std::nullopt;

                return ast::EnumDecl{std::move(*name), std::move(variants)};
            }

            std::optional<ast::CodeDecl> parse_code_decl()
            {
                auto target = parse_type();
                if (!target)
                    return std::nullopt;

                ast::TypeExprPtr interface;
                if (accept_kw("as"))
                {
                    interface = parse_type();
                    if (!interface)
                        return std::nullopt;
                }

                if (!expect("{"))
                    return std::nullopt;

                std::vector<ast::FnDecl> fns;

                while (!at("}") && !at_end())
                {
                    const size_t member_start = _pos;

                    const bool pub = !accept_kw("priv");

                    if (accept_kw("fn"))
                    {
                        if (auto fn = parse_fn_decl(pub))
                            fns.push_back(std::move(*fn));
                        else
                            synchronize_struct_member(member_start);

                        continue;
                    }

                    error_here("expected 'fn' inside a code block");

                    if (_pos == member_start)
                        skip_token();

                    synchronize_struct_member(member_start);
                }

                if (!expect("}"))
                    return std::nullopt;

                return ast::CodeDecl{std::move(target), std::move(interface), std::move(fns)};
            }

            std::optional<ast::AliasDecl> parse_alias_decl()
            {
                auto name = ident();
                if (!name)
                {
                    error_here("expected alias name");
                    return std::nullopt;
                }

                if (!expect("="))
                    return std::nullopt;

                auto type = parse_type();
                if (!type)
                    return std::nullopt;

                accept(";");
                return ast::AliasDecl{std::move(*name), std::move(type)};
            }

            std::optional<ast::StructDecl> parse_struct_decl()
            {
                auto name = ident();
                if (!name)
                {
                    error_here("expected struct name");
                    return std::nullopt;
                }

                if (!at("{"))
                {
                    error_here("expected open brace");
                    return std::nullopt;
                }
                bump(1);

                std::vector<ast::StructField> fields;
                std::vector<ast::FnDecl> fns;

                while (!at("}") && !at_end())
                {
                    const size_t member_start = _pos;

                    bool pub = true;
                    if (accept_kw("priv"))
                        pub = false;

                    if (at_ident())
                    {
                        const size_t field_start = _pos;
                        std::string field_name = *ident();

                        if (!expect(":"))
                        {
                            synchronize_struct_member(member_start);
                            continue;
                        }

                        auto type = parse_type();

                        if (!type)
                        {
                            synchronize_struct_member(member_start);
                            continue;
                        }

                        fields.push_back({
                            std::move(field_name),
                            std::move(type),
                            pub,
                            span_from(field_start)
                        });

                        accept(";");
                        continue;
                    }

                    if (accept_kw("fn"))
                    {
                        error_at(SourceSpan::between(_prev_start, _prev_end),
                            "methods are declared in a 'code' block, not inside a struct");

                        if (!parse_fn_decl(pub))
                            synchronize_struct_member(member_start);

                        continue;
                    }

                    error_here("expected a field");

                    if (_pos == member_start)
                        skip_token();

                    synchronize_struct_member(member_start);
                }

                expect("}");

                return ast::StructDecl{
                    std::move(*name), std::move(fields)
                };
            }

            std::optional<ast::FnDecl> parse_fn_decl(bool pub = true)
            {
                const size_t decl_start = _pos;

                if (accept_kw("priv"))
                    pub = false;

                auto name = ident();

                if (!name)
                {
                    error_here("expected function name");
                    return std::nullopt;
                }

                auto signature = parse_function_signature();

                if (!signature)
                    return std::nullopt;

                const SourceSpan decl_span = span_from(decl_start);

                auto body = parse_block_body();

                if (!body)
                    return std::nullopt;

                return ast::FnDecl{
                    std::move(*name),
                    std::move(*signature),
                    std::move(*body),
                    pub,
                    decl_span
                };
            }

            std::optional<std::vector<ast::Param>> parse_params()
            {
                std::vector<ast::Param> params;

                while (!at(")") && !at_end())
                {
                    const size_t before = _pos;

                    auto param = parse_param();

                    if (param)
                    {
                        params.push_back(std::move(*param));
                    }
                    else
                    {
                        synchronize_delimited(")", before);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    if (accept(","))
                    {
                        if (at(")"))
                        {
                            error_here("expected parameter after ','");
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }

                if (!expect(")"))
                    return std::nullopt;

                return params;
            }

            std::optional<ast::FunctionSignature> parse_function_signature()
            {
                if (!expect("("))
                    return std::nullopt;

                auto params = parse_params();
                if (!params)
                    return std::nullopt;

                ast::TypeExprPtr return_type;

                if (accept("->"))
                {
                    return_type = parse_type();

                    if (!return_type)
                        return std::nullopt;
                }

                return ast::FunctionSignature{
                    std::move(*params),
                    std::move(return_type)
                };
            }

            std::optional<ast::Param> parse_param()
            {
                const bool is_ref = accept_kw("ref");

                if (!at_ident())
                {
                    error_here("expected parameter name");
                    return std::nullopt;
                }

                const size_t start = _pos;
                std::string name = *ident();

                if (!accept(":"))
                {
                    if (name == "self")
                        return ast::Param{std::move(name), nullptr, is_ref, span_from(start)};

                    error_after_prev("expected ':', parameter requires a type");
                    return std::nullopt;
                }

                auto type = parse_type();
                if (!type)
                    return std::nullopt;

                return ast::Param{
                    std::move(name), std::move(type), is_ref, span_from(start)
                };
            }


            // stmts

            std::optional<ast::Block> parse_block_body()
            {
                if (!expect("{"))
                    return std::nullopt;

                std::vector<ast::StmtPtr> stmts;

                while (!at("}") && !at_end())
                {
                    const size_t before = _pos;

                    if (auto stmt = parse_stmt())
                    {
                        if (_pos == before)
                        {
                            error_here("internal parser error: statement consumed no tokens");
                            skip_token();
                        }

                        stmts.push_back(std::move(stmt));
                        continue;
                    }

                    if (_pos == before && !at_end() && !at("}"))
                        skip_token();

                    synchronize_statement(before);
                }

                expect("}");
                return ast::Block{std::move(stmts)};
            }

            ast::StmtPtr parse_stmt()
            {
                const size_t start = _pos;

                if (accept_kw("var"))       return parse_var_stmt(start);
                if (accept_kw("return"))    return parse_return_stmt(start);
                if (accept_kw("if"))        return parse_if_stmt(start);
                if (accept_kw("while"))     return parse_while_stmt(start);
                if (accept_kw("for"))       return parse_for_stmt(start);
                if (accept_kw("check"))     return parse_check_stmt(start);

                if (at("{"))                return parse_block_stmt(start);

                if (accept_kw("continue"))
                {
                    accept(";");
                    return std::make_unique<ast::Stmt>(ast::Continue{}, span_from(start));
                }

                if (accept_kw("break"))
                {
                    accept(";");
                    return std::make_unique<ast::Stmt>(ast::Break{}, span_from(start));
                }

                return parse_expr_stmt();
            }


            ast::StmtPtr parse_check_stmt(size_t start)
            {
                ast::ExprPtr expr = parse_expr();
                if (!expr)
                    return nullptr;

                expect("=>");

                const bool reffed = accept_kw("ref");

                auto name = ident();
                if (!name)
                {
                    error_here("expected variable name");
                    return nullptr;
                }

                auto good_branch = parse_body_stmt();
                if (!good_branch)
                    return nullptr;

                ast::StmtPtr bad_branch;
                if (accept_kw("else"))
                {
                    bad_branch = parse_body_stmt();

                    if (!bad_branch)
                        return nullptr;
                }

                return std::make_unique<ast::Stmt>(
                    ast::Check{std::move(expr), std::move(*name), reffed, std::move(good_branch), std::move(bad_branch)},
                    span_from(start));
            }

            ast::PatternPtr parse_pattern()
            {
                const size_t start = _pos;

                if (at_ident())
                {
                    std::string name = *ident();

                    auto pattern = std::make_unique<ast::Pattern>();
                    pattern->span = span_from(start);

                    if (name == "_")
                        pattern->kind = ast::PatWild{};
                    else
                        pattern->kind = ast::PatName{std::move(name)};

                    return pattern;
                }

                if (accept("("))
                {
                    std::vector<ast::PatternPtr> elements;

                    while (true)
                    {
                        auto element = parse_pattern();
                        if (!element)
                            return nullptr;

                        elements.push_back(std::move(element));

                        if (!accept(",") || at(")"))
                            break;
                    }

                    if (!expect(")"))
                        return nullptr;

                    const SourceSpan span = span_from(start);

                    if (elements.size() < 2)
                    {
                        error_at(span, "a tuple pattern needs at least 2 elements");
                        return nullptr;
                    }

                    auto pattern = std::make_unique<ast::Pattern>();
                    pattern->kind = ast::PatTuple{std::move(elements)};
                    pattern->span = span;
                    return pattern;
                }

                error_after_prev("expected a variable name, '_' or '('");
                return nullptr;
            }

            ast::PatternPtr to_place_pattern(ast::ExprPtr expr)
            {
                auto pattern = std::make_unique<ast::Pattern>();
                pattern->span = expr->span;

                if (ast::is_wildcard(*expr))
                {
                    pattern->kind = ast::PatWild{};
                }
                else if (auto* tuple = std::get_if<ast::TupleLit>(&expr->kind))
                {
                    ast::PatTuple result;

                    for (auto& element : tuple->elements)
                        result.elements.push_back(to_place_pattern(std::move(element)));

                    pattern->kind = std::move(result);
                }
                else
                {
                    pattern->kind = ast::PatPlace{std::move(expr)};
                }

                return pattern;
            }

            ast::StmtPtr parse_var_stmt(size_t start)
            {
                const SourceSpan first_token = here_span();

                auto pattern = parse_pattern();
                if (!pattern)
                    return nullptr;

                ast::TypeExprPtr type;
                if (accept(":"))
                {
                    type = parse_type();
                    if (!type)
                        return nullptr;
                }

                ast::ExprPtr init;
                if (accept("="))
                {
                    init = parse_expr();
                    if (!init)
                        return nullptr;
                }

                if (!init)
                {
                    if (!std::holds_alternative<ast::PatName>(pattern->kind))
                        error_at(first_token, "a pattern declaration needs an initializer");
                    else if (!type)
                        error_at(first_token, "variable needs a type or an initializer");
                }

                accept(";");
                return std::make_unique<ast::Stmt>(
                    ast::VarDecl{std::move(pattern), std::move(type), std::move(init)}, span_from(start)
                );
            }

            ast::StmtPtr parse_return_stmt(size_t start)
            {
                ast::ExprPtr value;
                bool reffed = false;

                if (!at(";") && !at("}") && !at_end() && !is_statement_start())
                {
                    reffed = !at_ref_lambda() && accept_kw("ref");

                    value = parse_expr();

                    if (!value)
                        return nullptr;
                }

                accept(";");
                return std::make_unique<ast::Stmt>(ast::Return{std::move(value), reffed}, span_from(start));
            }

            ast::StmtPtr parse_block_stmt(size_t start)
            {
                auto body = parse_block_body();
                if (!body)
                    return nullptr;

                return std::make_unique<ast::Stmt>(std::move(*body), span_from(start));
            }

            ast::StmtPtr parse_body_stmt()
            {
                if (at("{"))
                    return parse_block_stmt(_pos);

                return parse_stmt();
            }

            ast::StmtPtr parse_if_stmt(size_t start)
            {
                auto cond = parse_expr();
                if (!cond)
                    return nullptr;

                auto then_branch = parse_body_stmt();
                if (!then_branch)
                    return nullptr;

                ast::StmtPtr else_branch;
                if (at_kw("else"))
                {
                    const size_t else_start = _pos;
                    bump(4);

                    else_branch = accept_kw("if") ? parse_if_stmt(else_start)
                                                  : parse_body_stmt();

                    if (!else_branch)
                        return nullptr;
                }

                return std::make_unique<ast::Stmt>(
                    ast::If{std::move(cond), std::move(then_branch), std::move(else_branch)}, span_from(start));
            }

            ast::StmtPtr parse_while_stmt(size_t start)
            {
                auto cond = parse_expr();
                if (!cond)
                    return nullptr;

                auto body = parse_body_stmt();
                if (!body)
                    return nullptr;

                return std::make_unique<ast::Stmt>(ast::While{std::move(cond), std::move(body)}, span_from(start));
            }

            ast::StmtPtr parse_for_stmt(size_t start)
            {
                const bool reffed = accept_kw("ref");

                auto name = ident();
                if (!name)
                {
                    error_here("expected loop variable name");
                    return nullptr;
                }

                if (!expect_kw("in"))
                    return nullptr;

                auto iterable = parse_expr();
                if (!iterable)
                    return nullptr;

                auto body = parse_body_stmt();
                if (!body)
                    return nullptr;

                return std::make_unique<ast::Stmt>(
                    ast::ForIn{std::move(*name), reffed, std::move(iterable), std::move(body)}, span_from(start)
                );
            }

            ast::StmtPtr parse_expr_stmt()
            {
                const size_t start = _pos;

                auto expr = parse_expr();
                if (!expr)
                    return nullptr;

                accept(";");

                auto stmt = std::make_unique<ast::Stmt>();
                stmt->kind = ast::ExprStmt{std::move(expr)};
                stmt->span = span_from(start);
                return stmt;
            }


            // expressions
            ast::ExprPtr parse_expr(int min_bp = 0)
            {
                auto left = parse_primary();
                if (!left)
                    return nullptr;

                while (true)
                {
                    const std::string_view op = peek_op();

                    if (op == "(" || op == "[" || op == ".")
                    {
                        if (op != "." && _newline_before)
                            break;

                        if (postfix_bp < min_bp)
                            break;

                        left = parse_postfix(std::move(left));
                    }
                    else if (at_kw("as"))
                    {
                        if (as_bp < min_bp)
                            break;

                        left = parse_as(std::move(left));
                    }
                    else if (auto bp = binary_bp(op))
                    {
                        if (bp->left < min_bp)
                            break;

                        left = parse_binary(std::move(left), bp->right);
                    }
                    else break;

                    if (!left)
                        return nullptr;
                }
                return left;
            }

             ast::ExprPtr parse_primary()
            {
                const size_t start = _pos;
                const char c = peek();

                if (is_digit(c))
                    return parse_number();

                if (c == '"')
                    return parse_string();

                if (c == '\'')
                    return parse_format_string();

                if (c == '|')
                    return parse_lambda();

                if (at_ref_lambda())
                {
                    bump(3);

                    auto lambda = parse_lambda();
                    if (!lambda)
                        return nullptr;

                    std::get<ast::Lambda>(lambda->kind).reffed = true;
                    lambda->span = span_from(start);
                    return lambda;
                }

                if (c == '#')
                    return parse_struct_literal();

                if (const auto word = peek_word(); !word.empty())
                {
                    if (word == "true" || word == "false")
                    {
                        const bool value = word == "true";
                        bump(word.size());
                        return std::make_unique<ast::Expr>(ast::BoolLit{value}, span_from(start));
                    }

                    if (word == "none")
                    {
                        bump(4);
                        return std::make_unique<ast::Expr>(ast::NoneLit{}, span_from(start));
                    }

                    if (at_ident())
                    {
                        std::vector<std::string> parts;
                        parts.push_back(*ident());

                        while (accept("::"))
                        {
                            if (!at_ident())
                            {
                                error_here("expected identifier after '::'");
                                return nullptr;
                            }

                            parts.push_back(*ident());
                        }

                        return std::make_unique<ast::Expr>(ast::Name{std::move(parts)}, span_from(start));
                    }
                }

                if (const auto op = peek_op(); op == "-" || op == "!")
                {
                    const auto unary = (op == "-") ? ast::UnaryOp::Neg : ast::UnaryOp::Not;
                    bump(1);

                    auto operand = parse_expr(unary_bp);
                    if (!operand)
                        return nullptr;

                    return std::make_unique<ast::Expr>(ast::Unary{unary, std::move(operand)}, span_from(start));
                }

                // (expr), or a tuple (a, b, ..)
                if (c == '(')
                {
                    bump(1);

                    auto first = parse_expr();

                    if (!first)
                        return nullptr;

                    if (at(","))
                    {
                        std::vector<ast::ExprPtr> elements;
                        elements.push_back(std::move(first));

                        while (accept(","))
                        {
                            if (at(")")) // trailing comma
                                break;

                            auto next = parse_expr();
                            if (!next)
                                return nullptr;

                            elements.push_back(std::move(next));
                        }

                        if (!expect(")"))
                            return nullptr;

                        const SourceSpan span = span_from(start);

                        if (elements.size() < 2)
                            error_at(span, "a tuple needs at least 2 elements");

                        return std::make_unique<ast::Expr>(ast::TupleLit{std::move(elements)}, span);
                    }

                    if (!expect(")"))
                        return nullptr;

                    first->span = span_from(start);
                    return first;
                }

                if (c == '[')
                {
                    bump(1);

                    std::vector<ast::ExprPtr> elements;

                    if (!at("]"))
                        elements = parse_expr_list("]");

                    if (!expect("]"))
                        return nullptr;

                    return std::make_unique<ast::Expr>(ast::ArrayLit{std::move(elements)}, span_from(start));
                }

                if (c == '@')
                {
                    bump(1);

                    auto type_expr = parse_type();
                    if (!type_expr)
                        return nullptr;

                    if (!at("."))
                    {
                        error_here("expected . after the type");
                        return nullptr;
                    }
                    bump(1);

                    auto name = ident();
                    if (!name)
                    {
                        error_here("expected function name");
                        return nullptr;
                    }

                    if (!expect("("))
                        return nullptr;

                    std::vector<ast::ExprPtr> args;

                    if (!at(")"))
                        args = parse_expr_list(")");

                    if (!expect(")"))
                        return nullptr;

                    return std::make_unique<ast::Expr>(
                        ast::StaticCall{std::move(type_expr), std::move(*name), std::move(args)}, span_from(start));
                }

                error_after_prev("expected expression", false);

                if (!is_expr_boundary())
                    skip_token();

                return nullptr;
            }

            ast::ExprPtr parse_struct_literal()
            {
                const size_t start = _pos;
                bump(1); // '#'

                ast::TypeExprPtr type = parse_type();

                if (!type)
                    return nullptr;

                if (!accept("{"))
                {
                    error_after_prev("expected '{' after type in struct literal");
                    return nullptr;
                }

                std::vector<ast::FieldInit> fields;

                while (!at("}") && !at_end())
                {
                    const size_t field_start = _pos;

                    if (!at_ident())
                    {
                        error_here("expected field name in struct literal");

                        synchronize_delimited("}", field_start);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    std::string name = *ident();

                    if (!accept("="))
                    {
                        error_here("expected '=' after field name");

                        synchronize_delimited("}", field_start);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    const size_t value_start = _pos;

                    auto value = parse_expr(assign_l_bp + 1);

                    if (!value)
                    {
                        synchronize_delimited("}", value_start);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    fields.push_back({
                        std::move(name),
                        std::move(value),
                        span_from(field_start)
                    });

                    if (accept(","))
                        continue;

                    if (at("}"))
                        break;

                    if (at_ident_then("="))
                    {
                        error_here("expected ',' between struct fields");
                        continue;
                    }

                    error_here("expected ',' or '}' after struct field");

                    synchronize_delimited("}", _pos);

                    if (is_recovery_boundary())
                        break;
                }

                if (!expect("}"))
                    return nullptr;

                return std::make_unique<ast::Expr>(ast::StructLit{std::move(type), std::move(fields)}, span_from(start));
            }

             ast::ExprPtr parse_lambda()
            {
                const size_t start = _pos;
                std::vector<ast::Param> params;

                if (!accept("||")) // `||` is means empty, so skip it
                {
                    bump(1); // '|'

                    while (!at("|") && !at_end())
                    {
                        if (!at_ident())
                        {
                            error_here("expected parameter name");
                            return nullptr;
                        }

                        ast::Param param;
                        param.span = here_span();
                        param.name = *ident();

                        if (accept(":"))
                        {
                            param.type = parse_type();
                            if (!param.type)
                                return nullptr;
                        }

                        params.push_back(std::move(param));

                        if (!accept(","))
                            break;
                    }

                    if (!expect("|"))
                        return nullptr;
                }

                ast::TypeExprPtr return_type;
                if (accept("->"))
                {
                    return_type = parse_type();
                    if (!return_type)
                        return nullptr;

                    if (!at("{"))
                    {
                        error_after_prev("expected '{' after the return type of a lambda", false);
                        return nullptr;
                    }
                }

                ast::Block body;

                if (at("{"))
                {
                    auto block = parse_block_body();
                    if (!block)
                        return nullptr;

                    body = std::move(*block);
                }
                else
                {
                    auto value = parse_expr();
                    if (!value)
                        return nullptr;

                    const SourceSpan value_span = value->span;
                    body.statements.push_back(
                        std::make_unique<ast::Stmt>(ast::Return{std::move(value), false}, value_span));
                }

                return std::make_unique<ast::Expr>(
                    ast::Lambda{std::move(params), std::move(return_type), std::move(body)}, span_from(start));
            }

            ast::ExprPtr parse_postfix(ast::ExprPtr target)
            {
                const size_t start = target->span.start;

                // function call
                if (accept("("))
                {
                    std::vector<ast::ExprPtr> args;

                    if (!at(")"))
                        args = parse_expr_list(")");

                    if (!expect(")"))
                        return nullptr;

                    return std::make_unique<ast::Expr>(ast::Call{std::move(target), std::move(args)}, span_from(start));
                }

                // index
                if (accept("["))
                {
                    auto indexer = parse_expr();
                    if (!indexer)
                        return nullptr;

                    if (!expect("]"))
                        return nullptr;

                    return std::make_unique<ast::Expr>(ast::Index{std::move(target), std::move(indexer)}, span_from(start));
                }

                // member
                if (accept("."))
                {
                    auto name = ident();
                    if (!name)
                    {
                        error_here("expected member name after '.'");
                        return nullptr;
                    }

                    return std::make_unique<ast::Expr>(ast::Member{std::move(target), std::move(*name)}, span_from(start));
                }

                return nullptr;
            }

            ast::ExprPtr parse_as(ast::ExprPtr lhs)
            {
                const size_t start = lhs->span.start;

                bump(2); // 'as'

                auto type = parse_type();
                if (!type)
                    return nullptr;

                return std::make_unique<ast::Expr>(ast::As{std::move(lhs), std::move(type)}, span_from(start));
            }

            ast::ExprPtr parse_binary(ast::ExprPtr lhs, int right_bp)
            {
                const size_t start = lhs->span.start;

                const std::string_view op_text = peek_op();
                const auto op = to_binary_op(op_text);
                bump(op_text.size());

                auto rhs = parse_expr(right_bp);
                if (!rhs)
                    return nullptr;

                const SourceSpan span = span_from(start);

                if (*op == ast::BinaryOp::Assign && std::holds_alternative<ast::TupleLit>(lhs->kind))
                    return std::make_unique<ast::Expr>(
                        ast::PatternAssign{to_place_pattern(std::move(lhs)), std::move(rhs)}, span);

                return std::make_unique<ast::Expr>(ast::Binary{*op, std::move(lhs), std::move(rhs)}, span);
            }

            std::vector<ast::ExprPtr> parse_expr_list(std::string_view closing)
            {
                std::vector<ast::ExprPtr> expressions;
                bool after_comma = false;

                while (true)
                {
                    if (at(closing))
                    {
                        if (after_comma)
                            error_here("Expected expression after ','");

                        break;
                    }

                    if (at_end() || is_recovery_boundary())
                        break;

                    const size_t before = _pos;

                    auto expr = parse_expr();

                    if (!expr)
                    {
                        after_comma = false;

                        synchronize_delimited(closing, before);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    expressions.push_back(std::move(expr));
                    after_comma = false;

                    if (accept(","))
                    {
                        after_comma = true;
                        continue;
                    }

                    if (at(closing) || is_recovery_boundary())
                        break;

                    error_here(std::format("expected ',' or '{}'", closing));

                    synchronize_delimited(closing, _pos);

                    after_comma = true;

                    if (is_recovery_boundary())
                        break;
                }

                return expressions;
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