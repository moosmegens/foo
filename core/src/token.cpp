#include <typeinfo>
#include <foo/token.hpp>

#include <foo/util.hpp>

namespace foo
{
    std::string_view name(const Token& token)
    {
        return std::visit(overload{[](const auto& e){ return e.name; },}, token);
    }

    std::string to_string(const Token& token)
    {
        return std::visit(overload{[](const auto& e){ return e.to_string(); },}, token);
    }
}
