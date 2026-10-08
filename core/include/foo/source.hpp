#ifndef FOO_SOURCE_HPP
#define FOO_SOURCE_HPP


#include <algorithm>
#include <sstream>
#include <vector>


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

        static int get_line(std::string_view source, size_t offset)
        {
            int line = 1;

            for (size_t i = 0; i < offset && i < source.size(); ++i)
            {
                if (source[i] == '\n')
                    line++;
            }

            return line;
        }

        static int get_column(std::string_view source, size_t offset)
        {
            int col = 1;

            while (offset > 0 && source[offset - 1] != '\n')
            {
                offset--;
                col++;
            }

            return col;
        }

        int line(std::string_view source) const { return get_line(source, start); }
        int column(std::string_view source) const { return get_column(source, start); }
    };

    struct DiagnosticLine
    {
        int line_number{};
        std::string_view text;

        size_t underline_start{};
        size_t underline_length{};
    };

    inline std::vector<DiagnosticLine> get_diagnostic_lines(std::string_view source, const SourceSpan& span)
    {
        std::vector<DiagnosticLine> lines;
        if (span.size == 0)
            return lines;

        size_t span_end = std::min(span.end(), source.size());

        size_t current = source.rfind('\n', span.start);
        current = (current == std::string_view::npos) ? 0 : current + 1;

        int line_num = SourceSpan::get_line(source, current);

        while (current < span_end)
        {
            size_t line_end = source.find('\n', current);
            if (line_end == std::string_view::npos)
                line_end = source.size();

            size_t u_start = std::max(current, span.start);
            size_t u_end = std::min(line_end, span_end);

            lines.push_back({
                line_num,
                source.substr(current, line_end - current),
                u_start - current,
                u_end - u_start
            });

            current = line_end + 1;
            line_num++;
        }

        return lines;
    }
}


#endif //FOO_SOURCE_HPP
