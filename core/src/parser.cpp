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

            void synchronize_declaration(size_t start_pos)
            {
                while (!at_end())
                {
                    if (_pos > start_pos && is_declaration_start())
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
                    || at_kw("import")
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


                if (accept_kw("enum"))
                {
                    auto enumeration = parse_enum_decl();
                    if (!enumeration)
                        return nullptr;

                    return std::make_unique<ast::Decl>(std::move(*enumeration), span_from(start));
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


                error_here("expected declaration");
                return nullptr;
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
                        // auto fn = parse_fn_decl(pub);
                        //
                        // if (fn)
                        //     fns.push_back(std::move(*fn));
                        // else
                        //     synchronize_struct_member(member_start);

                        continue;
                    }

                    // the old parser looped forever here
                    error_here("expected a field or a method");

                    if (_pos == member_start)
                        skip_token();

                    synchronize_struct_member(member_start);
                }

                expect("}");

                return ast::StructDecl{
                    std::move(*name), std::move(fields), std::move(fns)
                };
            }

            // expressions

            // stmts


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