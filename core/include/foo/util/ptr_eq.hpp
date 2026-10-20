#ifndef FOO_PTR_EQ_HPP
#define FOO_PTR_EQ_HPP


#include <memory>
#include <span>


namespace foo
{
    template<class PtrT>
    bool ptr_equal(const PtrT& a, const PtrT& b)
    {
        if (!a || !b)
            return !a && !b;

        return *a == *b;
    }


    template<std::ranges::input_range RangeA, std::ranges::input_range RangeB>
    bool ptr_range_equal(const RangeA& a, const RangeB& b)
    {
        return std::ranges::equal(a, b, [](const auto& lhs, const auto& rhs)
        {
            return ptr_equal(lhs, rhs);
        });
    }

}

#endif //FOO_PTR_EQ_HPP
