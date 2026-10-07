#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

int main()
{
//     std::string str = R"(
// //this is a comment
// fn main(args: string[])
// {
//     //this is another comment
//     print("hello\"");
//     5.2+1 <= 1.2+hello+(2<= 1.0) -2000
// }
// )";

    std::string str = R"(
    10000000000000000000000000
)";

    foo::LexOutput tokens = foo::tokenize(str);

    for (const auto& token : tokens.tokens)
    {
        std::cout << foo::to_string(token) << std::endl;
    }

    for (const auto& error : tokens.errors)
    {
        std::cout << "error: " << error.error  << std::endl << std::endl <<
        str.substr(error.source.start, error.source.size) << std::endl;
    }

    return 0;
}