#include <iostream>
#include <ostream>

#include <foo/lexer.hpp>

int main()
{
    std::string str = R"(
//this is a comment
fn main(args: string[])
{
    //this is another comment
    print("hello\"");
    5.2+1 <= 1.2+hello+(2<= 1.0) -2000
}
)";

    foo::LexOutput tokens = foo::tokenize(str);

    for (const auto& token : tokens.tokens)
    {
        std::cout << foo::to_string(token.kind) << std::endl;
    }

    for (const auto& error : tokens.errors)
    {
        std::cout << "error: " << error.text  << std::endl << std::endl <<
        str.substr(error.location.start, error.location.size) << std::endl;
    }

    return 0;
}