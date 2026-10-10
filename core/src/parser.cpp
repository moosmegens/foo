#include <iostream>
#include <ostream>
#include <optional>

#include <foo/parser.hpp>
#include <foo/ast.hpp>
#include <foo/ast_printer.hpp>

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
                    auto decl = parse_decl();

                    if (decl)
                    {
                        result.decls.push_back(std::move(decl));
                    } else
                    {
                        advance();
                    }
                }

                result.errors = std::move(_errors);
                return result;
            }

        private:
            void init(std::span<Token> tokens)
            {
                _tokens = tokens;
                _offset = 0;
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

            template <class T>
            bool expect()
            {
                if (check<T>())
                {
                    advance();

                    return true;
                }

                report_error(current(), std::format("Invalid token! Expected '{}'", T::name));

                return false;
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

            struct BindingPower { int left; int right; };

            static const int prefix_bp  = 15;
            static const int postfix_bp = 17;

            static std::optional<BindingPower> infix_bp(TokenVariant v)
            {
                if (token_variant_is<tok::AssignOp>(v))
                    return BindingPower{2, 1};

                if (token_variant_is<tok::OrOp>(v))
                    return BindingPower{3, 4};

                if (token_variant_is<tok::AndOp>(v))
                    return BindingPower{5, 6};

                if (token_variant_is<tok::EqualOp, tok::NotEqualOp>(v))
                    return BindingPower{7, 8};

                if (token_variant_is<tok::LessOp, tok::LessEqualOp, tok::GreaterOp, tok::GreaterEqualOp>(v))
                    return BindingPower{9, 10};

                if (token_variant_is<tok::PlusOp, tok::MinusOp>(v))
                    return BindingPower{11, 12};

                if (token_variant_is<tok::MultiplyOp, tok::DivideOp, tok::ModuloOp>(v))
                    return BindingPower{13, 14};

                return std::nullopt;
            }

            static bool is_postfix(const TokenVariant& v)
            {
                return token_variant_is<tok::LParen, tok::LBracket, tok::Dot>(v);
            }

            static std::optional<ast::BinaryOp> to_binary_op(const TokenVariant& v)
            {
                using Op = ast::BinaryOp;

                if (token_variant_is<tok::PlusOp>(v))         return Op::Add;
                if (token_variant_is<tok::MinusOp>(v))        return Op::Sub;
                if (token_variant_is<tok::MultiplyOp>(v))     return Op::Mul;
                if (token_variant_is<tok::DivideOp>(v))       return Op::Div;
                if (token_variant_is<tok::ModuloOp>(v))       return Op::Mod;
                if (token_variant_is<tok::EqualOp>(v))        return Op::Eq;
                if (token_variant_is<tok::NotEqualOp>(v))     return Op::NotEq;
                if (token_variant_is<tok::LessOp>(v))         return Op::Less;
                if (token_variant_is<tok::LessEqualOp>(v))    return Op::LessEq;
                if (token_variant_is<tok::GreaterOp>(v))      return Op::Greater;
                if (token_variant_is<tok::GreaterEqualOp>(v)) return Op::GreaterEq;
                if (token_variant_is<tok::AndOp>(v))          return Op::And;
                if (token_variant_is<tok::OrOp>(v))           return Op::Or;
                if (token_variant_is<tok::AssignOp>(v))       return Op::Assign;

                return std::nullopt;
            }


            // parse:
            //      types

            ast::TypeExprPtr parse_type()
            {
                SourceSpan span = current().source;
                ast::TypeExprPtr base = nullptr;


                if (check<tok::LBracket>())
                {
                    advance(); // consume '['

                    auto key_type = parse_type();
                    if (!key_type)
                        return nullptr;

                    if (!expect<tok::Comma>())
                        return nullptr;

                    auto value_type = parse_type();
                    if (!value_type)
                        return nullptr;

                    if (!check<tok::RBracket>())
                    {
                        report_error(current(), "Expected ']'");
                        return nullptr;
                    }

                    advance(); // consume ']'
                    finish_span(span);

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = ast::MapType{std::move(key_type), std::move(value_type)};
                    base->span = span;
                }
                else if (check<tok::Identifier>())
                {
                    std::vector<std::string> parts;
                    parts.push_back(std::get<tok::Identifier>(current().variant).value);

                    advance();
                    finish_span(span);

                    while (match<tok::ColonColon>())
                    {
                        if (!check<tok::Identifier>())
                        {
                            report_error(current(), "Expected identifier after '::'");
                            break;
                        }
                        parts.push_back(std::get<tok::Identifier>(current().variant).value);
                        advance();
                    }

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = ast::NamedType{std::move(parts)};
                    base->span = span;
                }
                else
                {
                    report_error(current(), "Expected type name or '['");
                    return nullptr;
                }

                while (match<tok::LBracket>())
                {
                    if (!expect<tok::RBracket>())
                        break;

                    finish_span(span);

                    auto arr = std::make_unique<ast::TypeExpr>();
                    arr->type = ast::ArrayType{std::move(base)};
                    arr->span = span;
                    base = std::move(arr);
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
                    return decl;
                }

                if (match<tok::Struct>())
                {
                    auto decl = std::make_unique<ast::Decl>();

                }

                report_error(current(), "Expected declaration");
                return nullptr;
            }

            std::optional<ast::FnDecl> parse_fn_decl()
            {
                if (!check<tok::Identifier>())
                {
                    report_error(current(), "Expected function name");
                    return std::nullopt;
                }

                std::string name = std::get<tok::Identifier>(current().variant).value;
                advance();

                if (!expect<tok::LParen>())
                    return std::nullopt;

                std::vector<ast::Param> params;
                if (!check<tok::RParen>())
                {
                    do {
                        auto param = parse_param();
                        if (!param)
                            return std::nullopt;

                        params.push_back(std::move(*param));
                    } while (match<tok::Comma>());
                }

                if (!expect<tok::RParen>())
                    return std::nullopt;

                ast::TypeExprPtr return_type = nullptr;
                if (match<tok::Colon>())
                {
                    return_type = parse_type();
                    if (!return_type)
                        return std::nullopt;
                }


                return ast::FnDecl{
                    std::move(name), std::move(params), std::move(return_type), {}
                };
            }

            std::optional<ast::Param> parse_param()
            {
                if (!check<tok::Identifier>())
                {
                    report_error(current(), "Expected parameter name");
                    return std::nullopt;
                }

                std::string name = std::get<tok::Identifier>(current().variant).value;
                SourceSpan span = current().source;

                advance();

                if (!expect<tok::Colon>())
                    return std::nullopt;

                bool is_ref = match<tok::Ref>();

                auto type = parse_type();

                if (!type)
                    return std::nullopt;

                span.add(type->span);

                return ast::Param{std::move(name), std::move(type), is_ref, span};
            }


            // parse:
            //      statements


            // parse:
            //      expressions



        private:
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
}