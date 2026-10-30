#ifndef FOO_VALUE_HPP
#define FOO_VALUE_HPP

#include <memory>
#include <vector>

namespace foo
{
    template<typename T>
    using Rc = std::shared_ptr<T>;

    template<class T, class... Args>
    Rc<T> make(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }


    struct Value;
    using Ref = Rc<Value>;
}


#endif /* FOO_VALUE_HPP */