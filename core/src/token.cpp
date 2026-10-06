#include <typeinfo>
#include <foo/token.hpp>

#include <foo/util.hpp>

namespace foo
{
    std::string_view name(const TokenKind& kind)
    {
        return std::visit(overload{[](const auto& e){ return e.name; },}, kind);
    }

    std::string to_string(const TokenKind& kind)
    {
        return std::visit(overload{[](const auto& e){ return e.to_string(); },}, kind);
    }
}
