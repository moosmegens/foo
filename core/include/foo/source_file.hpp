#ifndef FOO_SOURCE_FILE_HPP
#define FOO_SOURCE_FILE_HPP


#include <filesystem>
#include <string>


namespace foo
{
    struct SourceFile
    {
        std::string name; // path "folder/folder/name"
        std::filesystem::path path;
        std::string text;
    };
}


#endif //FOO_SOURCE_FILE_HPP
