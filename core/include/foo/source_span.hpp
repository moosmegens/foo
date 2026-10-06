#ifndef FOO_SOURCE_SPAN_HPP
#define FOO_SOURCE_SPAN_HPP


#include <cstddef>


namespace foo
{
    struct SourceSpan
    {
        size_t start, length;
    };
}

#endif //FOO_SOURCE_SPAN_HPP
