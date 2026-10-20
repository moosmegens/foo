#include "doctest.hpp"

#include <foo/ast.hpp>
#include <foo/parser.hpp>

#include <string_view>

#include "foo/lexer.hpp"


using namespace foo;

static ParseOutput simple_parse(std::string_view src, bool should_fail = false)
{
    auto tkns = tokenize(src);
    auto r = parse(tkns.tokens);

    if (should_fail)
    {
        REQUIRE(!r.errors.empty());
    } else
    {
        REQUIRE(r.errors.empty());
    }

    return r;
}

bool program_equal(std::span<const ast::DeclPtr> expected, std::span<const ast::DeclPtr> actual)
{
    if (expected.size() != actual.size())
        return false;

    for (std::size_t i = 0; i < expected.size(); ++i)
    {

    }
}


TEST_SUITE("parser")
{
    TEST_CASE("main")
    {
        auto r = simple_parse("fn main() {}");
    }
}

