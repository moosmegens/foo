#include <foo/resolver.hpp>


namespace foo
{
// type table
    TypeTable::TypeTable()
    {
        for (TypeKind k : BaseKinds)
            intern(k);
    }

    TypeId TypeTable::intern(TypeKind kind, Id a, Id b)
    {
        Type t{kind, a, b};

        auto [it, inserted] = _ids.try_emplace(t, static_cast<TypeId>(_types.size()));
        if (inserted)
            _types.push_back(t);

        return it->second;
    }

// native reg
    TypeId NativeRegistry::type(TypeTable& types, std::string_view full_name)
    {
        std::string key{full_name};

        auto [it, inserted] = _type_ids.try_emplace(key, static_cast<NativeTypeId>(_type_names.size()));

        if (inserted)
            _type_names.push_back(std::move(key));

        return types.native_of(it->second);
    }

    TypeId NativeRegistry::find_type(TypeTable& types, std::string_view full_name) const
    {
        auto it = _type_ids.find(std::string{full_name});
        if (it == _type_ids.end())
            return EmptyType;

        return types.native_of(it->second);
    }

    NativeFunctionId NativeRegistry::add_function(std::string name, FunctionSig sig)
    {
        return add(std::move(name), false,
            [sig = std::move(sig)](TypeTable&, TypeId) -> std::optional<FunctionSig>
            {
                return sig;
            });
    }

    NativeFunctionId NativeRegistry::add_method(std::string name, TypeId receiver, FunctionSig sig)
    {
        return add(std::move(name), true,
            [receiver, sig = std::move(sig)](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                if (r != receiver)
                    return std::nullopt;

                return sig;
            });
    }

    NativeFunctionId NativeRegistry::add_method(std::string name, NativeResolver resolve)
    {
        return add(std::move(name), true, std::move(resolve));
    }

    NativeFunctionId NativeRegistry::add(std::string name, bool is_method, NativeResolver resolve)
    {
        _entries.push_back({std::move(name), is_method, std::move(resolve)});

        return static_cast<NativeFunctionId>(_entries.size() - 1);
    }

    std::optional<NativeRegistry::Match> NativeRegistry::find(
        bool is_method,
        TypeTable& types,
        TypeId receiver,
        std::string_view name) const
    {
        for (auto i = _entries.size(); i-- > 0;)
        {
            const Entry& e = _entries[i];

            if (e.is_method != is_method || e.name != name)
                continue;

            if (auto sig = e.resolve(types, receiver))
                return Match{static_cast<NativeFunctionId>(i), std::move(*sig)};
        }

        return std::nullopt;
    }


    class Resolver
    {
    public:
        explicit Resolver(const ResolverInput& input) : _input(input) {}

        ResolveOutput resolve()
        {
            declare_files();

            return std::move(_output);
        }

    private:
        [[maybe_unused]] const ResolverInput& _input;
        ResolveOutput _output;

        // per file
        [[maybe_unused]] int _file_id = -1;


        void declare_files()
        {
            for (size_t i = 0; i < _input.files.size(); ++i)
            {
                _file_id = static_cast<int>(i);
            }
        }
    };


    ResolveOutput resolve(const ResolverInput& input)
    {
        Resolver resolver{input};
        return resolver.resolve();
    }
}