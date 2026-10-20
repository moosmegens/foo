#ifndef FOO_DYNAMIC_RUNNER_HPP
#define FOO_DYNAMIC_RUNNER_HPP


#include <foo/ast.hpp>

#include <string>
#include <optional>
#include <span>


namespace foo
{
    struct RuntimeError
    {
        std::string message;
        SourceSpan span;
    };

    std::optional<RuntimeError> run(std::span<const ast::DeclPtr> decls);
}


#endif /* FOO_DYNAMIC_RUNNER_HPP */