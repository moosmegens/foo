#ifndef FOO_SOURCE_HPP
#define FOO_SOURCE_HPP


#include <algorithm>


namespace foo
{
    struct SourceSpan
    {
        size_t start = 0;
        size_t size = 0;


        size_t end() const { return start + size; }

        static SourceSpan between(size_t begin, size_t end)
        {
            return {.start = begin, .size = end > begin ? end - begin : 0};
        }

        static SourceSpan combine(SourceSpan a, SourceSpan b)
        {
            const size_t begin = std::min(a.start, b.start);
            const size_t end = std::max(a.end(), b.end());

            return between(begin, end);
        }

        void add(SourceSpan b)
        {
            *this = combine(*this, b);
        }
    };
}


#endif //FOO_SOURCE_HPP
