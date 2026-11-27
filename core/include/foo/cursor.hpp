#ifndef FOO_CURSOR_HPP
#define FOO_CURSOR_HPP


#include <algorithm>
#include <cstddef>
#include <string_view>


namespace foo
{

    class Cursor
    {
    public:
        explicit Cursor(std::string_view text)
            : Cursor(text, 0, text.size()) {}

        Cursor(std::string_view text, size_t begin, size_t end)
            : _text(text), _end(std::min(end, text.size())), _pos(std::min(begin, _end)) {}

        std::string_view text() const { return _text; }
        size_t pos() const { return _pos; }
        size_t end() const { return _end; }
        bool at_end() const { return _pos >= _end; }

        char peek(size_t ahead = 0) const { return _pos + ahead < _end ? _text[_pos + ahead] : '\0'; }

        void seek(size_t pos) { _pos = std::min(pos, _end); }

    private:
        std::string_view _text;
        size_t _end;
        size_t _pos;
    };

}


#endif //FOO_CURSOR_HPP