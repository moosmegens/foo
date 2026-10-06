#ifndef FOO_SOURCE_HPP
#define FOO_SOURCE_HPP


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
    };
}


#endif //FOO_SOURCE_HPP
