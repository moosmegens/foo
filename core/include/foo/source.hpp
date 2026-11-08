#ifndef FOO_SOURCE_HPP
#define FOO_SOURCE_HPP


#include <algorithm>
#include <sstream>
#include <vector>
#include <string_view>
#include <string>


namespace foo
{
    struct SourceSpan
    {
        size_t start = 0;
        size_t size = 0;

        bool operator==(const SourceSpan&) const = default;


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

        void add(size_t a)
        {
            size += a;
        }

        void eat_up_front(size_t a)
        {
            a = std::min(a, size);
            size -= a;
            start += a;
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

    inline std::vector<DiagnosticLine> get_diagnostic_lines(
        std::string_view source,
        const SourceSpan& span,
        size_t context_lines = 2)
    {
        std::vector<DiagnosticLine> lines;

        const size_t start = std::min(span.start, source.size());
        const size_t end = std::min(span.end(), source.size());


        size_t current = 0;
        if (start > 0)
        {
            size_t nl = source.rfind('\n', start - 1);
            current = (nl == std::string_view::npos) ? 0 : nl + 1;
        }


        for (size_t i = 0; i < context_lines && current > 0; i++)
        {

            if (current == 1)
            {
                current = 0;
                break;
            }

            size_t prev_nl = source.rfind('\n', current - 2);
            current = (prev_nl == std::string_view::npos) ? 0 : prev_nl + 1;
        }


        size_t last_line_start = 0;
        {
            const size_t last_char = end > start ? end - 1 : start;
            if (last_char > 0)
            {
                size_t nl = source.rfind('\n', last_char - 1);
                last_line_start = (nl == std::string_view::npos) ? 0 : nl + 1;
            }
        }

        size_t last_line_end = source.find('\n', last_line_start);
        if (last_line_end == std::string_view::npos)
            last_line_end = source.size();

        size_t range_end = last_line_end;
        for (size_t i = 0; i < context_lines && range_end < source.size(); ++i)
        {
            range_end = source.find('\n', range_end + 1);
            if (range_end == std::string_view::npos)
            {
                range_end = source.size();
                break;
            }
        }

        int line_num = SourceSpan::get_line(source, current);

        while (true)
        {
            size_t line_end = source.find('\n', current);
            if (line_end == std::string_view::npos)
                line_end = source.size();

            size_t text_end = line_end;
            if (text_end > current && source[text_end - 1] == '\r')
                text_end--;

            size_t u_start = std::min(std::max(current, start), text_end);
            size_t u_end = std::min(text_end, end);
            size_t u_len = u_end > u_start ? u_end - u_start : 0;

            if (u_len == 0 &&
                start >= current && start <= line_end &&
                (start == end || end > text_end))
            {
                u_start = std::min(start, text_end);
                u_len = 1;
            }

            lines.push_back({
                line_num,
                source.substr(current, text_end - current),
                u_start - current,
                u_len
            });

            if (line_end >= range_end || line_end >= source.size())
                break;

            current = line_end + 1;
            line_num++;
        }

        return lines;
    }
}


#endif //FOO_SOURCE_HPP
