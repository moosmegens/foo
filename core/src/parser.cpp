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


                return {};
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


            // actual parsing

            ast::TypeExprPtr parse_type()
            {
                if (!check<tok::Identifier>())
                {
                    report_error(current(), "Expected type name");
                    return nullptr;
                }

                std::vector<std::string> parts;
                SourceSpan span = current().source;

                parts.push_back(std::get<tok::Identifier>(advance().variant).value);

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


                auto base = std::make_unique<ast::TypeExpr>();
                base->type = ast::NamedType{std::move(parts)};
                base->span = span;

                while (match<tok::LBracket>())
                {
                    if (!expect<tok::RBracket>())
                        break;

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