#include <foo/lexer.hpp>

namespace foo
{
    static char peek(std::string_view src, std::size_t offset, std::size_t ahead = 0) noexcept
    {
        return offset + ahead < src.size() ? src[offset + ahead] : '\0';
    }

    static void skip(std::string_view src, std::size_t& offset) noexcept
    {
        while (offset < src.size() && std::isspace(static_cast<unsigned char>(src[offset])))
            ++offset;
    }

    static Token make(TokenKind kind, std::size_t start, std::size_t end)
    {
        return Token{std::move(kind), SourceSpan::between(start, end)};
    }

    Result<Token> lex_token(std::string_view src, std::size_t& offset)
    {
        skip(src, offset);
        const std::size_t start = offset;
        if (offset >= src.size())
            return make(EndOfFile{}, start, offset);

        const char c = src[offset++];
        switch (c)
        {
            case '(': return make(LParen{}, start, offset);
            case ')': return make(RParen{}, start, offset);
        }
    }


    LexOutput tokenize(std::string_view src)
    {
        LexOutput out{};
        size_t offset = 0;

        while (true)
        {
            auto result = lex_token(src, offset);
            if (!result)
            {
                out.errors.push_back(std::move(result.error()));
                continue;
            }

            const bool done = holds_alternative<EndOfFile>(result->kind);
            out.tokens.push_back(std::move(*result));

            if (done)
                break;
        }

        return out;
    }
}