#ifndef FOO_UTIL_HPP
#define FOO_UTIL_HPP



// https://en.cppreference.com/cpp/utility/variant/visit2

template<typename ... Ts>
struct overload : Ts ... { using Ts::operator() ...; };

template<class... Ts>
overload(Ts...) -> overload<Ts...>;


#endif //FOO_UTIL_HPP
