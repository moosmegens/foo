#ifndef FOO_RUNTIME_HPP
#define FOO_RUNTIME_HPP

#include <foo/resolver.hpp>


namespace foo
{
    class Runtime
    {
    public:
        Runtime() = default;

        static void inject(TypeTable& types, NativeRegistry& natives);

    private:
    };
}


#endif //FOO_RUNTIME_HPP
