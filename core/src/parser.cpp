#include <expected>
#include <iostream>
#include <ostream>
#include <optional>

#include <foo/parser.hpp>

#include <foo/debug/debug_out.hpp>

#include <foo/ast.hpp>
#include <foo/ast_printer.hpp>
#include <source_location>

#ifndef NDEBUG
    #define FOO_TRACE()  \
        std::cout << std::source_location::current().file_name() << ':' \
                  << std::source_location::current().line() << " in " \
                  << std::source_location::current().function_name() << '\n'
#else
    #define FOO_TRACE()
#endif

namespace foo
{
    namespace
    {
        class Parser
        {
        public:
            Parser() = default;

            ParseOutput parse(std::span<Token> tokens)
            {
                init(tokens);
                ParseOutput result;

                while (!is_at_end())
                {
                    if (match<tok::Import>())
                    {
                        if (check<tok::String>())
                        {
                            _imports.push_back({std::get<tok::String>(current().variant).value, current().source});
                            advance();
                        }

                        while (check<tok::Comma>())
                        {
                            advance();
                            if (check<tok::String>())
                            {
                                _imports.push_back({std::get<tok::String>(current().variant).value, current().source});
                                advance();
                            }
                        }

                        expect<tok::Semicolon>(true);

                        continue;
                    }

                    size_t before = _offset;
                    auto decl = parse_decl();

                    if (decl)
                    {
                        result.decls.push_back(std::move(decl));
                    }
                    else
                    {
                        if (_offset == before && !is_at_end())
                            advance();

                        synchronize_declaration(before);
                    }
                }

                result.errors = std::move(_errors);
                result.imports = std::move(_imports);
                return result;
            }

            IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::span<Token> tokens)
            {
                init(tokens);
                auto value = parse_type();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::ExprPtr> parse_expr(std::span<Token> tokens)
            {
                init(tokens);
                auto value = parse_expr();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::StmtPtr> parse_statement(std::span<Token> tokens)
            {
                init(tokens);
                auto value = parse_stmt();

                return {std::move(_errors), std::move(value)};
            }

            IntermediateParseOutput<ast::DeclPtr> parse_decl(std::span<Token> tokens)
            {
                init(tokens);
                auto value = parse_decl();

                return {std::move(_errors), std::move(value)};
            }

        private:
            void init(std::span<Token> tokens)
            {
                _tokens = tokens;
                _offset = 0;
                _imports = {};
                _errors = {};
            }

            bool is_at_end() const { return current().is<tok::End>(); }

            const Token& current() const { return peek(0); }

            const Token& peek(int ahead = 0) const
            {
                int target = static_cast<int>(_offset) + ahead;

                if (target < 0)
                    target = 0;

                if (target >= static_cast<int>(_tokens.size()))
                    target = static_cast<int>(_tokens.size()) - 1;

                return _tokens[static_cast<size_t>(target)];
            }

            const Token& advance()
            {
                const Token& t = peek();

                if (!t.is<tok::End>())
                    _offset++;

                return t;
            }

            template <class T>
            bool check(int ahead = 0) const
            {
                return peek(ahead).is<T>();
            }

            template <class T>
            bool match()
            {
                if (!check<T>())
                    return false;

                advance();
                return true;
            }

            // phantom == true: doesnt report error, but still checks
            template <class T>
            bool expect(bool phantom = false)
            {
                if (check<T>())
                {
                    advance();

                    return true;
                }

                if (!phantom)
                {
                    if (_offset > 1)
                    {
                        auto span = peek(-1).source;
                        span.add(1);
                        span.eat_up_front(peek(-1).source.size);

                        report_error(
                            span,
                            std::format(
                                 "expected '{}' after '{}'",
                                 to_human_readable<T>(),
                                 to_human_readable(peek(-1))
                            ));
                    } else
                    {
                        report_error(current(), std::format("invalid token! expected '{}'", to_human_readable<T>()));
                    }
                }

                return false;
            }

            bool is_statement_start() const
            {
                return check<tok::Var>()
                    || check<tok::Return>()
                    || check<tok::If>()
                    || check<tok::While>()
                    || check<tok::For>()
                    || check<tok::Check>()
                    || check<tok::LBrace>()
                    || check<tok::Continue>()
                    || check<tok::Break>()

                    ;
            }

            bool is_declaration_start() const
            {
                return check<tok::Function>()
                    || check<tok::Struct>()
                    || check<tok::Enum>()
                    || check<tok::Import>()

                    ;
            }

            bool is_expr_boundary() const
            {
                return check<tok::Comma>()
                    || check<tok::Semicolon>()
                    || check<tok::RParen>()
                    || check<tok::RBracket>()
                    || check<tok::RBrace>()
                    || check<tok::FatForwards>()
                    || is_at_end();
            }

            bool is_closing_delimiter() const
            {
                return check<tok::RParen>()
                    || check<tok::RBracket>()
                    || check<tok::RBrace>();
            }

            bool is_recovery_boundary() const
            {
                return is_closing_delimiter()
                    || check<tok::Semicolon>()
                    || check<tok::FatForwards>()
                    || is_at_end();
            }

            template <class Closing>
            void synchronize_delimited(size_t start_offset)
            {
                int parens = 0;
                int brackets = 0;
                int braces = 0;

                auto account = [&](const Token& token)
                {
                    if (token.is<tok::LParen>())
                        ++parens;
                    else if (token.is<tok::RParen>() && parens > 0)
                        --parens;

                    if (token.is<tok::LBracket>())
                        ++brackets;
                    else if (token.is<tok::RBracket>() && brackets > 0)
                        --brackets;

                    if (token.is<tok::LBrace>())
                        ++braces;
                    else if (token.is<tok::RBrace>() && braces > 0)
                        --braces;
                };

                for (size_t i = start_offset; i < _offset; ++i)
                    account(_tokens[i]);

                while (!is_at_end())
                {
                    if (parens == 0 && brackets == 0 && braces == 0)
                    {
                        if (check<Closing>())
                            return;

                        if (match<tok::Comma>())
                            return;

                        if (is_closing_delimiter()
                            || check<tok::Semicolon>()
                            || check<tok::FatForwards>())
                            return;
                    }

                    account(current());
                    advance();
                }
            }

            void synchronize_statement(size_t start_offset)
            {
                while (!is_at_end() && !check<tok::RBrace>())
                {
                    if (match<tok::Semicolon>())
                        return;

                    if (_offset > start_offset && is_statement_start())
                        return;

                    advance();
                }
            }

            void synchronize_declaration(size_t start_offset)
            {
                while (!is_at_end())
                {
                    if (_offset > start_offset && is_declaration_start())
                        return;

                    advance();
                }
            }

            void synchronize_parameter()
            {
                while (!is_at_end() && !check<tok::RParen>())
                {
                    if (match<tok::Comma>())
                        return;

                    advance();
                }
            }

            bool is_struct_member_start() const
            {
                return check<tok::Function>()
                    || check<tok::Priv>()
                    || (check<tok::Identifier>() && check<tok::Colon>(1));
            }

            void synchronize_struct_member(size_t start_offset)
            {
                while (!is_at_end() && !check<tok::RBrace>())
                {
                    if (_offset > start_offset && is_struct_member_start())
                        return;

                    if (match<tok::Semicolon>())
                        return;

                    advance();
                }
            }

            void finish_span(SourceSpan& span)
            {
                if (_offset > 0)
                {
                    const Token& last_token = _tokens[_offset - 1];

                    size_t end_offset = last_token.source.start + last_token.source.size;

                    if (end_offset > span.start)
                    {
                        span.size = end_offset - span.start;
                    }
                }
            }

            void report_error(const Token& invalid_token, std::string_view msg)
            {
                ParseError err;
                err.error = std::string(msg);
                err.source = invalid_token.source;

                _errors.push_back(err);
            }

            void report_error_after(const Token& invalid_token, std::string_view msg, bool after_msg = true)
            {
                ParseError err;

                err.error = std::string(msg);

                if (_offset > 1)
                {
                    auto span = peek(-1).source;
                    span.add(1);
                    span.eat_up_front(peek(-1).source.size);
                    err.source = span;

                    if (after_msg)
                    {
                        err.error += " after '";
                        err.error += to_human_readable(peek(-1));
                        err.error += '\'';
                    }
                } else
                {
                    err.source = invalid_token.source;
                }

                _errors.push_back(err);
            }

            void report_error(const SourceSpan& invalid_span, std::string_view msg)
            {
                ParseError err;
                err.error = std::string(msg);
                err.source = invalid_span;

                _errors.push_back(err);
            }




            struct BindingPower { int left; int right; };

            static const int as_bp = 18;
            static const int unary_bp = 19;
            static const int postfix_bp = 21;
            static const int assign_l_bp = 2;

            static std::optional<BindingPower> binary_bp(TokenVariant v)
            {
                if (token_variant_is<tok::AssignOp>(v))
                    return BindingPower{assign_l_bp, 1};

                if (token_variant_is<tok::DotDot, tok::DotDotEq>(v))
                    return BindingPower{4, 5};

                if (token_variant_is<tok::OrOp>(v))
                    return BindingPower{6, 7};

                if (token_variant_is<tok::AndOp>(v))
                    return BindingPower{8, 9};

                if (token_variant_is<tok::EqualOp, tok::NotEqualOp>(v))
                    return BindingPower{10, 11};

                if (token_variant_is<tok::LessOp, tok::LessEqualOp, tok::GreaterOp, tok::GreaterEqualOp>(v))
                    return BindingPower{12, 13};

                if (token_variant_is<tok::PlusOp, tok::MinusOp>(v))
                    return BindingPower{14, 15};

                if (token_variant_is<tok::MultiplyOp, tok::DivideOp, tok::ModuloOp>(v))
                    return BindingPower{16, 17};

                return std::nullopt;
            }

            static bool is_postfix(const TokenVariant& v)
            {
                return token_variant_is<tok::LParen, tok::LBracket, tok::Dot>(v);
            }

            static bool is_range_op(const TokenVariant& v)
            {
                return token_variant_is<tok::DotDot, tok::DotDotEq>(v);
            }

            static std::optional<ast::BinaryOp> to_binary_op(const TokenVariant& v)
            {
                using Op = ast::BinaryOp;

                if (token_variant_is<tok::PlusOp>(v))           return Op::Add;
                if (token_variant_is<tok::MinusOp>(v))          return Op::Sub;
                if (token_variant_is<tok::MultiplyOp>(v))       return Op::Mul;
                if (token_variant_is<tok::DivideOp>(v))         return Op::Div;
                if (token_variant_is<tok::ModuloOp>(v))         return Op::Mod;
                if (token_variant_is<tok::EqualOp>(v))          return Op::Eq;
                if (token_variant_is<tok::NotEqualOp>(v))       return Op::NotEq;
                if (token_variant_is<tok::LessOp>(v))           return Op::Less;
                if (token_variant_is<tok::LessEqualOp>(v))      return Op::LessEq;
                if (token_variant_is<tok::GreaterOp>(v))        return Op::Greater;
                if (token_variant_is<tok::GreaterEqualOp>(v))   return Op::GreaterEq;
                if (token_variant_is<tok::AndOp>(v))            return Op::And;
                if (token_variant_is<tok::OrOp>(v))             return Op::Or;
                if (token_variant_is<tok::AssignOp>(v))         return Op::Assign;
                if (token_variant_is<tok::DotDot>(v))           return Op::Range;
                if (token_variant_is<tok::DotDotEq>(v))         return Op::RangeInclusive;

                return std::nullopt;
            }

            static std::optional<ast::UnaryOp> to_unary_op(const TokenVariant& v)
            {
                using Op = ast::UnaryOp;

                if (token_variant_is<tok::MinusOp>(v))  return Op::Neg;
                if (token_variant_is<tok::NotOp>(v))    return Op::Not;

                return std::nullopt;
            }


            // parse:
            //      types

            ast::TypeExprPtr parse_type()
            {
                SourceSpan span = current().source;
                ast::TypeExprPtr base = nullptr;


                // core types: map / set, tuple / variant and named

                // map
                if (check<tok::LBracket>())
                {
                    advance(); // consume '['

                    auto first_type = parse_type();
                    if (!first_type)
                        return nullptr;

                    std::unique_ptr<ast::TypeExpr> second_type;

                    if (check<tok::Comma>())
                    {
                        advance(); // consume ','

                        second_type = parse_type();
                        if (!second_type)
                            return nullptr;
                    }

                    if (!check<tok::RBracket>())
                    {
                        report_error(current(), second_type ? "expected ']' after map value type"
                                                            : "expected ',' or ']'");
                        return nullptr;
                    }

                    advance(); // consume ']'
                    finish_span(span);

                    base = std::make_unique<ast::TypeExpr>();
                    if (second_type)
                        base->type = ast::MapType{std::move(first_type), std::move(second_type)};
                    else
                        base->type = ast::SetType{std::move(first_type)};
                    base->span = span;
                }
                // tuple (int, string), or variant (int | string)
                else if (match<tok::LParen>())
                {
                    std::vector<ast::TupleElem> items;
                    bool is_variant = false;

                    while (true)
                    {
                        ast::TupleElem item;

                        // an optional name, only documentation
                        if (check<tok::Identifier>() && check<tok::Colon>(1))
                        {
                            item.name = std::get<tok::Identifier>(current().variant).value;
                            advance(); // name
                            advance(); // ':'
                        }

                        item.type = parse_type();
                        if (!item.type)
                            return nullptr;

                        items.push_back(std::move(item));

                        if (check<tok::Comma>() || check<tok::Pipe>())
                        {
                            const bool pipe = check<tok::Pipe>();

                            if (items.size() == 1)
                                is_variant = pipe;
                            else if (pipe != is_variant)
                            {
                                report_error(current(), "cannot mix ',' and '|', wrap one of them in parentheses");
                                return nullptr;
                            }

                            advance();
                            continue;
                        }

                        break;
                    }

                    if (!expect<tok::RParen>())
                        return nullptr;

                    finish_span(span);

                    if (items.size() < 2)
                    {
                        report_error(span, "a tuple or variant needs at least 2 types");
                        return nullptr;
                    }

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
                else if (check<tok::Identifier>())
                {
                    // named


                    std::vector<std::string> parts;
                    parts.push_back(std::get<tok::Identifier>(current().variant).value);

                    advance();

                    while (match<tok::ColonColon>())
                    {
                        if (!check<tok::Identifier>())
                        {
                            report_error(current(), "expected identifier after '::'");
                            return nullptr;
                        }
                        parts.push_back(std::get<tok::Identifier>(current().variant).value);
                        advance();
                    }

                    finish_span(span);

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = ast::NamedType{std::move(parts)};
                    base->span = span;
                }
                else
                {
                    report_error_after(current(), "expected type expression");
                    return nullptr;
                }

                // 2 modifiers, optional and array

                bool last_was_optional = false;
                while (true)
                {
                    if (match<tok::LBracket>()) // array
                    {
                        if (!expect<tok::RBracket>())
                            break;

                        finish_span(span);

                        auto arr = std::make_unique<ast::TypeExpr>();
                        arr->type = ast::ArrayType{std::move(base)};
                        arr->span = span;
                        base = std::move(arr);

                        last_was_optional = false;
                    }
                    else
                    if (match<tok::QuestionMark>()) // optional
                    {
                        if (last_was_optional)
                        {
                            report_error(peek(-1), "you can't have an optional of an optional");
                            continue;
                        }

                        finish_span(span);

                        auto optional = std::make_unique<ast::TypeExpr>();
                        optional->type = ast::OptionalType{std::move(base)};
                        optional->span = span;
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


            // parse:
            //      decl's

            ast::DeclPtr parse_decl()
            {
                SourceSpan span{current().source.start, 0};

                if (match<tok::Function>())
                {
                    auto fn = parse_fn_decl();
                    if (!fn)
                        return nullptr;

                    auto decl = std::make_unique<ast::Decl>();
                    decl->kind = std::move(*fn);
                    finish_span(span);
                    decl->span = span;

                    return decl;
                }

                if (match<tok::Struct>())
                {
                    auto strukt = parse_struct_decl();
                    if (!strukt)
                        return nullptr;

                    auto decl = std::make_unique<ast::Decl>();
                    decl->kind = std::move(*strukt);
                    finish_span(span);
                    decl->span = span;

                    return decl;
                }

                if (match<tok::Enum>())
                {
                    auto enumeration = parse_enum_decl();
                    if (!enumeration)
                        return nullptr;

                    auto decl = std::make_unique<ast::Decl>();
                    decl->kind = std::move(*enumeration);
                    finish_span(span);
                    decl->span = span;

                    return decl;
                }

                report_error(current(), "expected declaration");
                return nullptr;
            }

            std::optional<ast::EnumDecl> parse_enum_decl()
            {
                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected enum name");
                    return std::nullopt;
                }

                std::string name = std::get<tok::Identifier>(current().variant).value;
                advance();

                if (!expect<tok::LBrace>())
                    return std::nullopt;

                std::vector<ast::EnumVariant> variants;

                while (!check<tok::RBrace>() && !is_at_end())
                {
                    if (!check<tok::Identifier>())
                    {
                        report_error(current(), "expected variant name");

                        while (!is_at_end() && !check<tok::Comma>() && !check<tok::RBrace>())
                            advance();

                        match<tok::Comma>();
                        continue;
                    }

                    variants.push_back({std::get<tok::Identifier>(current().variant).value, current().source});
                    advance();

                    if (match<tok::Comma>())
                        continue;

                    if (!check<tok::RBrace>())
                        report_error(current(), "expected ',' or '}' after enum variant");
                }

                if (!expect<tok::RBrace>())
                    return std::nullopt;

                return ast::EnumDecl{std::move(name), std::move(variants)};
            }

            std::optional<ast::StructDecl> parse_struct_decl()
            {
                std::string name;

                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected struct name");
                    return std::nullopt;
                }
                name = std::get<tok::Identifier>(current().variant).value;
                advance();

                if (!check<tok::LBrace>())
                {
                    report_error(current(), "expected open brace");
                    return std::nullopt;
                }
                advance();

                std::vector<ast::StructField> fields;
                std::vector<ast::FnDecl> fns;

                while (!check<tok::RBrace>() && !is_at_end())
                {
                    size_t member_start = _offset;

                    bool pub = true;
                    if (check<tok::Priv>())
                    {
                        pub = false;
                        advance();
                    }


                    if (check<tok::Identifier>())
                    {
                        SourceSpan inner_span{current().source.start, 0};

                        std::string field_name =
                            std::get<tok::Identifier>(current().variant).value;

                        advance();

                        if (!expect<tok::Colon>())
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

                        finish_span(inner_span);

                        fields.push_back({
                            std::move(field_name),
                            std::move(type),
                            pub,
                            inner_span
                        });

                        expect<tok::Semicolon>(true);
                        continue;
                    }

                    if (check<tok::Function>())
                    {
                        advance();

                        auto fn = parse_fn_decl();

                        if (fn)
                            fns.push_back(std::move(*fn));
                        else
                            synchronize_struct_member(member_start);

                        continue;
                    }

                    expect<tok::RBrace>();
                }

                advance();
                return ast::StructDecl{
                    std::move(name), std::move(fields), std::move(fns)
                };
            }

            std::optional<ast::FnDecl> parse_fn_decl()
            {
                SourceSpan decl_span{current().source.start, 0};
                bool pub = true;

                if (check<tok::Priv>())
                {
                    pub = false;
                    advance();
                }

                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected function name");
                    return std::nullopt;
                }

                std::string name = std::get<tok::Identifier>(current().variant).value;
                advance();

                if (!expect<tok::LParen>())
                    return std::nullopt;

                std::vector<ast::Param> params;
                while (!check<tok::RParen>() && !is_at_end())
                {
                    size_t before = _offset;

                    auto param = parse_param();

                    if (param)
                    {
                        params.push_back(std::move(*param));
                    }
                    else
                    {
                        synchronize_delimited<tok::RParen>(before);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    if (match<tok::Comma>())
                    {
                        if (check<tok::RParen>())
                        {
                            report_error(current(), "expected parameter after ','");
                            break;
                        }
                    }
                }

                if (!expect<tok::RParen>())
                    return std::nullopt;

                ast::TypeExprPtr return_type = nullptr;
                if (match<tok::RArrow>())
                {
                    return_type = parse_type();
                    if (!return_type)
                        return std::nullopt;
                }

                finish_span(decl_span);

                auto body = parse_block_body();
                if (!body)
                    return std::nullopt;


                return ast::FnDecl{
                    std::move(name), std::move(params), std::move(return_type), std::move(*body),
                    pub,
                    decl_span
                };
            }

            std::optional<ast::Param> parse_param()
            {
                bool is_ref = match<tok::Ref>();

                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected parameter name");
                    return std::nullopt;
                }

                std::string name = std::get<tok::Identifier>(current().variant).value;
                SourceSpan span = current().source;

                advance();

                ast::TypeExprPtr type;

                if (match<tok::Colon>())
                {
                    type = parse_type();

                    if (!type)
                        return std::nullopt;

                    span.add(type->span);
                } else
                {
                    report_error_after(current(), "expected ':', parameter requires a type");
                    return std::nullopt;
                }

                return ast::Param{
                    std::move(name), std::move(type), is_ref, span
                };
            }


            // parse:
            //      statements

            std::optional<ast::Block> parse_block_body()
            {
                if (!expect<tok::LBrace>())
                    return std::nullopt;

                std::vector<ast::StmtPtr> stmts;

                while (!check<tok::RBrace>() && !is_at_end())
                {
                    size_t before = _offset;

                    if (auto stmt = parse_stmt())
                    {
                        if (_offset == before)
                        {
                            report_error(current(), "internal parser error: statement consumed no tokens");
                            advance();
                        }

                        stmts.push_back(std::move(stmt));
                        continue;
                    }

                    if (_offset == before && !is_at_end() && !check<tok::RBrace>())
                        advance();

                    synchronize_statement(before);
                }

                expect<tok::RBrace>();
                return ast::Block{std::move(stmts)};
            }

            ast::StmtPtr parse_stmt()
            {
                SourceSpan span = current().source;

                if (match<tok::Var>())      return parse_var_stmt(span);
                if (match<tok::Return>())   return parse_return_stmt(span);
                if (match<tok::If>())       return parse_if_stmt(span);
                if (match<tok::While>())    return parse_while_stmt(span);
                if (match<tok::For>())      return parse_for_stmt(span);
                if (match<tok::Check>())    return parse_check_stmt(span);

                if (check<tok::LBrace>())   return parse_block_stmt(span);

                if (match<tok::Continue>())
                {
                    expect<tok::Semicolon>(true);
                    finish_span(span);
                    return std::make_unique<ast::Stmt>(ast::Continue{}, span);
                }

                if (match<tok::Break>())
                {
                    expect<tok::Semicolon>(true);
                    finish_span(span);
                    return std::make_unique<ast::Stmt>(ast::Break{}, span);
                }

                // when not met with keyword, it is an expression
                return parse_expr_stmt();
            }

            ast::StmtPtr parse_check_stmt(SourceSpan span)
            {
                ast::ExprPtr expr = parse_expr();
                expect<tok::FatForwards>();

                bool reffed = match<tok::Ref>();

                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected variable name");
                    return nullptr;
                }

                const Token& name_token = current();
                std::string name = std::get<tok::Identifier>(name_token.variant).value;
                advance();

                auto good_branch = parse_body_stmt(current().source);
                if (!good_branch)
                    return nullptr;

                ast::StmtPtr bad_branch;
                if (check<tok::Else>())
                {
                    SourceSpan else_span = current().source;
                    advance();

                    bad_branch = parse_body_stmt(else_span);

                    if (!bad_branch)
                        return nullptr;
                }

                finish_span(span);
                return std::make_unique<ast::Stmt>(
                    ast::Check{std::move(expr), name, reffed, std::move(good_branch), std::move(bad_branch)},
                    span);
            }

            // parse:
            //      patterns, for declarations: a name, '_' or a tuple of patterns
            //          a
            //          _
            //          (a, (b, _))

            ast::PatternPtr parse_pattern()
            {
                SourceSpan span = current().source;

                if (check<tok::Identifier>())
                {
                    std::string name = std::get<tok::Identifier>(current().variant).value;
                    advance();
                    finish_span(span);

                    auto pattern = std::make_unique<ast::Pattern>();
                    pattern->span = span;

                    if (name == "_")
                        pattern->kind = ast::PatWild{};
                    else
                        pattern->kind = ast::PatName{std::move(name)};

                    return pattern;
                }

                if (match<tok::LParen>())
                {
                    std::vector<ast::PatternPtr> elements;

                    while (true)
                    {
                        auto element = parse_pattern();
                        if (!element)
                            return nullptr;

                        elements.push_back(std::move(element));

                        if (!match<tok::Comma>() || check<tok::RParen>())
                            break;
                    }

                    if (!expect<tok::RParen>())
                        return nullptr;

                    finish_span(span);

                    if (elements.size() < 2)
                    {
                        report_error(span, "a tuple pattern needs at least 2 elements");
                        return nullptr;
                    }

                    auto pattern = std::make_unique<ast::Pattern>();
                    pattern->kind = ast::PatTuple{std::move(elements)};
                    pattern->span = span;
                    return pattern;
                }

                report_error_after(current(), "expected a variable name, '_' or '('");
                return nullptr;
            }

            // for assignments the pattern was parsed as an expression, (a, (b.x, _)) = value
            // a tuple becomes a tuple pattern, `_` a wildcard, and anything else is a place to assign to
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

            ast::StmtPtr parse_var_stmt(SourceSpan span)
            {
                const Token& first_token = current();

                auto pattern = parse_pattern();
                if (!pattern)
                    return nullptr;

                ast::TypeExprPtr type;
                if (match<tok::Colon>())
                {
                    type = parse_type();
                    if (!type)
                        return nullptr;
                }

                ast::ExprPtr init;
                if (match<tok::AssignOp>())
                {
                    init = parse_expr();
                    if (!init)
                        return nullptr;
                }

                if (!init)
                {
                    if (!std::holds_alternative<ast::PatName>(pattern->kind))
                        report_error(first_token, "a pattern declaration needs an initializer");
                    else if (!type)
                        report_error(first_token, "variable needs a type or an initializer");
                }

                expect<tok::Semicolon>(true);
                finish_span(span);
                return std::make_unique<ast::Stmt>(
                    ast::VarDecl{std::move(pattern), std::move(type), std::move(init)}, span);
            }

            ast::StmtPtr parse_return_stmt(SourceSpan span)
            {
                ast::ExprPtr value;
                bool reffed = false;

                if (!check<tok::Semicolon>() && !check<tok::RBrace>() && !is_at_end() && !is_statement_start())
                {
                    reffed = match<tok::Ref>();

                    value = parse_expr();

                    if (!value)
                        return nullptr;
                }

                expect<tok::Semicolon>(true);
                finish_span(span);
                return std::make_unique<ast::Stmt>(ast::Return{std::move(value), reffed}, span);
            }

            ast::StmtPtr parse_block_stmt(SourceSpan span)
            {
                auto body = parse_block_body();
                if (!body)
                    return nullptr;

                finish_span(span);
                return std::make_unique<ast::Stmt>(std::move(*body), span);
            }

            ast::StmtPtr parse_body_stmt(SourceSpan span)
            {
                if (check<tok::LBrace>())
                    return parse_block_stmt(span);

                return parse_stmt();
            }

            ast::StmtPtr parse_if_stmt(SourceSpan span)
            {
                auto cond = parse_expr();
                if (!cond)
                    return nullptr;

                auto then_branch = parse_body_stmt(current().source);
                if (!then_branch)
                    return nullptr;

                ast::StmtPtr else_branch;
                if (check<tok::Else>())
                {
                    SourceSpan else_span = current().source;
                    advance();

                    else_branch = match<tok::If>()  ? parse_if_stmt(else_span)
                                                    : parse_body_stmt(else_span);

                    if (!else_branch)
                        return nullptr;
                }

                finish_span(span);
                return std::make_unique<ast::Stmt>(ast::If{std::move(cond), std::move(then_branch), std::move(else_branch)}, span);
            }

            ast::StmtPtr parse_while_stmt(SourceSpan span)
            {
                auto cond = parse_expr();
                if (!cond)
                    return nullptr;

                auto body = parse_body_stmt(current().source);
                if (!body)
                    return nullptr;

                finish_span(span);
                return std::make_unique<ast::Stmt>(ast::While{std::move(cond), std::move(body)}, span);
            }

            ast::StmtPtr parse_for_stmt(SourceSpan span)
            {
                bool reffed = match<tok::Ref>();

                if (!check<tok::Identifier>())
                {
                    report_error(current(), "expected loop variable name");
                    return nullptr;
                }
                std::string name = std::get<tok::Identifier>(current().variant).value;
                advance();

                if (!expect<tok::In>())
                    return nullptr;

                auto iterable = parse_expr();
                if (!iterable)
                    return nullptr;

                auto body = parse_body_stmt(current().source);
                if (!body)
                    return nullptr;

                finish_span(span);
                return std::make_unique<ast::Stmt>(ast::ForIn{std::move(name), reffed, std::move(iterable), std::move(body)}, span);
            }

            ast::StmtPtr parse_expr_stmt()
            {
                SourceSpan span = current().source;

                auto expr = parse_expr();
                if (!expr)
                    return nullptr;

                expect<tok::Semicolon>(true);

                finish_span(span);

                auto stmt = std::make_unique<ast::Stmt>();
                stmt->kind = ast::ExprStmt{std::move(expr)};
                stmt->span = span;
                return stmt;
            }


            // parse:
            //      expressions

            // https://en.wikipedia.org/wiki/Operator-precedence_parser
            ast::ExprPtr parse_expr(int min_bp = 0)
            {
                auto left = parse_primary();
                if (!left)
                    return nullptr;

                while (true)
                {
                    const TokenVariant& next = peek().variant;

                    // (call, index, .member) binds tighter than any binary op
                    if (is_postfix(next))
                    {
                        if (postfix_bp < min_bp)
                            break;

                        left = parse_postfix(std::move(left));
                    }
                    else if (token_variant_is<tok::As>(next))
                    {
                        // `value as T` binds tighter than * but looser than unary minus
                        if (as_bp < min_bp)
                            break;

                        left = parse_as(std::move(left));
                    }
                    else if (auto bp = binary_bp(next))
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
                SourceSpan span = current().source;
                const TokenVariant& v = current().variant;

                if (auto* t = std::get_if<tok::Integer>(&v))
                {
                    auto value = t->value;
                    advance();
                    return std::make_unique<ast::Expr>(ast::IntLit{value}, span);
                }
                if (auto* t = std::get_if<tok::Float>(&v))
                {
                    auto value = t->value;
                    advance();
                    return std::make_unique<ast::Expr>(ast::FloatLit{value}, span);
                }
                if (auto* t = std::get_if<tok::String>(&v))
                {
                    auto value = t->value;
                    advance();
                    return std::make_unique<ast::Expr>(ast::StringLit{std::move(value)}, span);
                }
                if (auto* t = std::get_if<tok::Boolean>(&v))
                {
                    auto value = t->value;
                    advance();
                    return std::make_unique<ast::Expr>(ast::BoolLit{value}, span);
                }
                if (auto* t = std::get_if<tok::None>(&v))
                {
                    advance();
                    return std::make_unique<ast::Expr>(ast::NoneLit{}, span);
                }
                if (std::get_if<tok::Hash>(&v))
                {
                    advance();

                    ast::TypeExprPtr type = parse_type();

                    if (!type)
                        return nullptr;

                    if (!match<tok::LBrace>())
                    {
                        report_error_after(current(), "expected '{' after type in struct literal");
                        return nullptr;
                    }

                    std::vector<ast::FieldInit> fields;

                    while (!check<tok::RBrace>() && !is_at_end())
                    {
                        size_t field_start = _offset;
                        SourceSpan field_span = current().source;

                        if (!check<tok::Identifier>())
                        {
                            report_error(current(), "expected field name in struct literal");

                            synchronize_delimited<tok::RBrace>(field_start);

                            if (is_recovery_boundary())
                                break;

                            continue;
                        }

                        std::string name = std::get<tok::Identifier>(current().variant).value;

                        advance();

                        if (!match<tok::AssignOp>())
                        {
                            report_error(current(), "expected '=' after field name");

                            synchronize_delimited<tok::RBrace>(field_start);

                            if (is_recovery_boundary())
                                break;

                            continue;
                        }

                        size_t value_start = _offset;

                        auto value = parse_expr(assign_l_bp + 1);

                        if (!value)
                        {
                            synchronize_delimited<tok::RBrace>(value_start);

                            if (is_recovery_boundary())
                                break;

                            continue;
                        }

                        finish_span(field_span);

                        fields.push_back({
                            std::move(name),
                            std::move(value),
                            field_span
                        });

                        if (match<tok::Comma>())
                            continue;

                        if (check<tok::RBrace>())
                            break;

                        if (check<tok::Identifier>() && check<tok::AssignOp>(1))
                        {
                            report_error(current(), "expected ',' between struct fields");
                            continue;
                        }

                        report_error(current(), "expected ',' or '}' after struct field");

                        synchronize_delimited<tok::RBrace>(_offset);

                        if (is_recovery_boundary())
                            break;
                    }

                    if (!expect<tok::RBrace>())
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::StructLit{std::move(type), std::move(fields)}, span);
                }


                if (auto* t = std::get_if<tok::Identifier>(&v))
                {
                    std::vector<std::string> parts;
                    parts.push_back(t->value);
                    advance();

                    while (match<tok::ColonColon>())
                    {
                        if (!check<tok::Identifier>())
                        {
                            report_error(current(), "expected identifier after '::'");
                            return nullptr;
                        }

                        parts.push_back(std::get<tok::Identifier>(current().variant).value);
                        advance();
                    }

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::Name{std::move(parts)}, span);
                }

                if (check<tok::MinusOp>() || check<tok::NotOp>())
                {
                    auto op = to_unary_op(peek().variant);
                    advance();

                    auto operand = parse_expr(unary_bp);
                    if (!operand)
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::Unary{*op, std::move(operand)}, span);
                }

                // (expr), or a tuple (a, b, ..)
                if (match<tok::LParen>())
                {
                    auto first = parse_expr();

                    if (!first)
                        return nullptr;

                    if (check<tok::Comma>())
                    {
                        std::vector<ast::ExprPtr> elements;
                        elements.push_back(std::move(first));

                        while (match<tok::Comma>())
                        {
                            if (check<tok::RParen>()) // trailing comma
                                break;

                            auto next = parse_expr();
                            if (!next)
                                return nullptr;

                            elements.push_back(std::move(next));
                        }

                        if (!expect<tok::RParen>())
                            return nullptr;

                        finish_span(span);

                        if (elements.size() < 2)
                            report_error(span, "a tuple needs at least 2 elements");

                        return std::make_unique<ast::Expr>(ast::TupleLit{std::move(elements)}, span);
                    }

                    if (!expect<tok::RParen>())
                        return nullptr;

                    finish_span(span);
                    first->span = span;
                    return first;
                }

                if (match<tok::LBracket>())
                {

                    std::vector<ast::ExprPtr> elements;

                    if (!check<tok::RBracket>())
                        elements = parse_expr_list<tok::RBracket>();

                    if (!expect<tok::RBracket>())
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::ArrayLit{std::move(elements)}, span);
                }

                if (match<tok::At>())
                {
                    auto type_expr = parse_type();
                    if (!type_expr)
                        return nullptr;

                    if (!check<tok::Dot>())
                    {
                        report_error(current(), "expected . after the type");
                        return nullptr;
                    }
                    advance();

                    if (!check<tok::Identifier>())
                    {
                        report_error(current(), "expected function name");
                        return nullptr;
                    }
                    std::string name = std::get<tok::Identifier>(current().variant).value;
                    advance();


                    if (!expect<tok::LParen>())
                        return nullptr;


                    std::vector<ast::ExprPtr> args;

                    if (!check<tok::RParen>())
                        args = parse_expr_list<tok::RParen>();

                    if (!expect<tok::RParen>())
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::StaticCall{std::move(type_expr), name, std::move(args)}, span);
                }

                report_error_after(current(), "expected expression", false);

                if (!is_expr_boundary())
                    advance();

                return nullptr;
            }

            ast::ExprPtr parse_postfix(ast::ExprPtr target)
            {
                SourceSpan span = target->span;

                // function call
                if (match<tok::LParen>())
                {
                    std::vector<ast::ExprPtr> args;

                    if (!check<tok::RParen>())
                        args = parse_expr_list<tok::RParen>();

                    if (!expect<tok::RParen>())
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::Call{std::move(target), std::move(args)}, span);
                }

                // index
                if (match<tok::LBracket>())
                {
                    auto indexer = parse_expr();
                    if (!indexer)
                        return nullptr;

                    if (!expect<tok::RBracket>())
                        return nullptr;

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::Index{std::move(target), std::move(indexer)}, span);
                }

                // member
                if (match<tok::Dot>())
                {
                    if (!check<tok::Identifier>())
                    {
                        report_error(current(), "expected member name after '.'");
                        return nullptr;
                    }
                    std::string name = std::get<tok::Identifier>(current().variant).value;
                    advance();

                    finish_span(span);
                    return std::make_unique<ast::Expr>(ast::Member{std::move(target), std::move(name)}, span);
                }

                return nullptr;
            }

            // value as T
            ast::ExprPtr parse_as(ast::ExprPtr lhs)
            {
                SourceSpan span = lhs->span;

                advance(); // 'as'

                auto type = parse_type();
                if (!type)
                    return nullptr;

                finish_span(span);
                return std::make_unique<ast::Expr>(ast::As{std::move(lhs), std::move(type)}, span);
            }

            ast::ExprPtr parse_binary(ast::ExprPtr lhs, int right_bp)
            {
                SourceSpan span = lhs->span;

                auto op = to_binary_op(current().variant);
                advance();

                auto rhs = parse_expr(right_bp);
                if (!rhs)
                    return nullptr;

                finish_span(span);

                // (a, (b, _)) = value, the tuple on the left is a pattern
                if (*op == ast::BinaryOp::Assign && std::holds_alternative<ast::TupleLit>(lhs->kind))
                    return std::make_unique<ast::Expr>(
                        ast::PatternAssign{to_place_pattern(std::move(lhs)), std::move(rhs)}, span);

                return std::make_unique<ast::Expr>(ast::Binary{*op, std::move(lhs), std::move(rhs)}, span);
            }

            template <class Closing>
            std::vector<ast::ExprPtr> parse_expr_list()
            {
                std::vector<ast::ExprPtr> expressions;
                bool after_comma = false;

                while (true)
                {
                    if (check<Closing>())
                    {
                        if (after_comma)
                            report_error(current(), "Expected expression after ','");

                        break;
                    }

                    if (is_at_end() || is_recovery_boundary())
                        break;

                    size_t before = _offset;

                    auto expr = parse_expr();

                    if (!expr)
                    {
                        after_comma = false;

                        synchronize_delimited<Closing>(before);

                        if (is_recovery_boundary())
                            break;

                        continue;
                    }

                    expressions.push_back(std::move(expr));
                    after_comma = false;

                    if (match<tok::Comma>())
                    {
                        after_comma = true;
                        continue;
                    }

                    if (check<Closing>() || is_recovery_boundary())
                        break;

                    report_error(current(), "expected ',' or '" + std::string{Closing{}.to_string()} + "'");

                    synchronize_delimited<Closing>(_offset);

                    after_comma = true;

                    if (is_recovery_boundary())
                        break;
                }

                return expressions;
            }

            std::vector<ast::ExprPtr> parse_expr_group()
            {
                std::vector<ast::ExprPtr> args;

                do {
                    auto arg = parse_expr();
                    if (!arg)
                        return {};

                    args.push_back(std::move(arg));
                } while (match<tok::Comma>());

                return args;
            }


        private:
            std::vector<ast::ImportDecl> _imports;
            std::vector<ParseError> _errors;
            std::span<Token> _tokens;
            size_t _offset{};
        };
    }


    ParseOutput parse(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse(tokens);
    }

    IntermediateParseOutput<ast::TypeExprPtr> parse_type(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse_type(tokens);
    }

    IntermediateParseOutput<ast::ExprPtr> parse_expr(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse_expr(tokens);
    }

    IntermediateParseOutput<ast::StmtPtr> parse_statement(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse_statement(tokens);
    }

    IntermediateParseOutput<ast::DeclPtr> parse_decl(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse_decl(tokens);
    }
}