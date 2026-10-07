#ifndef FOO_OVERLOADED_HPP
#define FOO_OVERLOADED_HPP

namespace foo
{
    // https://en.cppreference.com/cpp/utility/variant/visit2
    template<typename ... Ts>
    struct overload : Ts ... { using Ts::operator() ...; };

    template<class... Ts>
    overload(Ts...) -> overload<Ts...>;
}

#endif //FOO_OVERLOADED_HPP
