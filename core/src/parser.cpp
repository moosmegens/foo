#include <foo/parser.hpp>
#include <foo/ast.hpp>

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
            }

            const Token& peek(int ahead = 0) const
            {
                return _tokens[std::min(_offset + ahead, _tokens.size() - 1)];
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