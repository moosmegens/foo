#include <foo/resolver.hpp>


namespace foo
{
    TypeTable::TypeTable()
    {
        for (int k = 0; k < static_cast<int>(TypeKind::COUNT); k++)
        {
            _types.push_back(Type{static_cast<TypeKind>(k)});
        }
    }

    TypeId TypeTable::lookup(Type t)
    {
        auto key = std::tuple{static_cast<int>(t.kind), t.a, t.b};
        if (auto it = _type_to_id.find(key); it != _type_to_id.end())
            return it->second;

        auto id = static_cast<TypeId>(_types.size());
        _types.push_back(t);
        _type_to_id.emplace(key, id);
        return id;
    }

    TypeId TypeTable::array_of(TypeId e)
    {
        return lookup(Type{TypeKind::Array, e});
    }

    TypeId TypeTable::map_of(TypeId k, TypeId v)
    {
        return lookup(Type{TypeKind::Map, k, v});
    }

    TypeId TypeTable::optional_of(TypeId e)
    {
        return lookup(Type{TypeKind::Optional, e});
    }

    TypeId TypeTable::add_struct(std::string qualified_name)
    {
        auto mid = static_cast<StructId>(_struct_names.size());
        _struct_names.push_back(std::move(qualified_name));

        auto id = static_cast<TypeId>(_types.size());
        _types.push_back(Type{TypeKind::Struct, EmptyType, EmptyType, mid});
        return id;
    }

    TypeId TypeTable::add_native(std::string qualified_name)
    {
        auto mid = static_cast<NativeTypeId>(_native_names.size());
        _native_names.push_back(std::move(qualified_name));

        auto id = static_cast<TypeId>(_types.size());
        _types.push_back(Type{TypeKind::Native, EmptyType, EmptyType, mid});
        return id;
    }

    std::string TypeTable::to_string(TypeKind k) const
    {
        switch (k)
        {
            case TypeKind::Error:       return "<error>";
            case TypeKind::Void:        return "void";
            case TypeKind::Bool:        return "bool";
            case TypeKind::Int:         return "int";
            case TypeKind::Float:       return "float";
            case TypeKind::String:      return "string";
            case TypeKind::Any:         return "any";
            case TypeKind::Range:       return "range";
            case TypeKind::Array:       return "array";
            case TypeKind::Map:         return "map";
            case TypeKind::Optional:    return "optional";
            case TypeKind::Struct:      return "struct";
            case TypeKind::Native:      return "native";

            default:
                return "<?>";
        }
    }

    std::string TypeTable::to_string(TypeId t) const
    {
        if (t == EmptyId)
            return "<none>";

        switch (_types[static_cast<size_t>(t)].kind)
        {
            case TypeKind::Error:       return "<error>";
            case TypeKind::Void:        return "void";
            case TypeKind::Bool:        return "bool";
            case TypeKind::Int:         return "int";
            case TypeKind::Float:       return "float";
            case TypeKind::String:      return "string";
            case TypeKind::Any:         return "any";
            case TypeKind::Range:       return "range";
            case TypeKind::Array:       return to_string(element(t)) + "[]";
            case TypeKind::Map:         return "[" + to_string(key(t)) + ", " + to_string(value(t)) + "]";
            case TypeKind::Optional:    return to_string(element(t)) + "?";
            case TypeKind::Struct:      return _struct_names[static_cast<size_t>(_types[static_cast<size_t>(t)].meta_id)];
            case TypeKind::Native:      return _native_names[static_cast<size_t>(_types[static_cast<size_t>(t)].meta_id)];


            default:
                return "<?>";
        }
    }



    ResolveOutput resolve(const ResolverInput& input)
    {
        (void)input;

        return {};
    }
}