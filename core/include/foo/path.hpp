#ifndef FOO_PATH_HPP
#define FOO_PATH_HPP

#include <string>
#include <vector>

namespace foo
{
    using Path = std::vector<std::string>;

    inline std::string to_string(const Path& p)
    {
        std::string out;

        for (size_t i = 0; i < p.size(); i++)
            out += (i ? "::" : "") + p[i];

        return out;
    }

    inline Path concat(Path a, const Path& b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    inline Path path_child(const Path& a, std::string name)
    {
        Path out = a;
        out.push_back(name);

        return out;
    }

    inline std::string item_from_path(const Path& p)
    {
        if (p.empty())
            return "";

        return p.back();
    }

    inline Path from_file_path(const std::string& s)
    {
        Path out(1);
        for (char c : s)
            c == '/' ? out.emplace_back() : out.back() += c;

        return out;
    }
}

#endif //FOO_PATH_HPP
