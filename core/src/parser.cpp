#include <iostream>
#include <ostream>
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

                auto t = parse_type();
                std::cout << to_string(t) << std::endl;

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

            const Token& current() const { return peek(0); }

            const Token& peek(int ahead = 0) const
            {
                return _tokens[std::min(_offset + static_cast<size_t>(ahead), _tokens.size() - 1)];
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

            void report_error(const Token& invalid_token, std::string_view msg)
            {
                ParseError err;
                err.error = std::string(msg);
                err.source = invalid_token.source;

                _errors.push_back(err);
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

                    span.add(current().source);
                    advance(); // consume ']'

                    base = std::make_unique<ast::TypeExpr>();
                    base->type = ast::MapType{std::move(key_type), std::move(value_type)};
                    base->span = span;
                }
                else if (check<tok::Identifier>())
                {
                    std::vector<std::string> parts;
                    parts.push_back(std::get<tok::Identifier>(current().variant).value);
                    span.add(current().source);
                    advance();

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

                    span.add(current().source);

                    auto arr = std::make_unique<ast::TypeExpr>();
                    arr->type = ast::ArrayType{std::move(base)};
                    arr->span = span;
                    base = std::move(arr);
                }

                return base;
            }


        private:
            std::vector<ParseError> _errors;
            std::span<Token> _tokens;
            size_t _offset;
        };
    }


    ParseOutput parse(std::span<Token> tokens)
    {
        Parser parser;
        return parser.parse(tokens);
    }
}