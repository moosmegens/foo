#include <typeinfo>

#include <foo/token.hpp>
#include <foo/util/str.hpp>
#include <foo/util/overloaded.hpp>


namespace foo
{
    std::string_view name(const TokenVariant& token)
    {
        return std::visit(overload{[](const auto& e){ return e.name; },}, token);
    }

    std::string to_string(const TokenVariant& token)
    {
        return std::visit(overload{[](const auto& e){ return e.to_string(); },}, token);
    }
}
