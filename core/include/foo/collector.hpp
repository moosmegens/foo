#ifndef FOO_COLLECTOR_HPP
#define FOO_COLLECTOR_HPP


#include <filesystem>
#include <string>

#include <foo/source.hpp>
#include <foo/resolver.hpp>


namespace foo
{
    struct CollectError
    {
        std::string file;
        SourceSpan span;
        std::string message;
    };

    struct CollectorOutput
    {
        ResolverInput resolver_input;
        std::vector<CollectError> errors;
    };


    CollectorOutput collect(const std::filesystem::path& root, std::string_view entry);

}


#endif //FOO_COLLECTOR_HPP
