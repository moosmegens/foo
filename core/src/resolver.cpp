#include <iostream>
#include <foo/resolver.hpp>

#include "foo/debug/debug_out.hpp"
#include "foo/util/overloaded.hpp"


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

// type name
    std::string type_name(TypeId type, const ResolveOutput& output, const NativeRegistry* natives)
    {
        if (type == EmptyType)
            return "<none>";

        const TypeTable& types = output.type_table;

        switch (types.kind(type))
        {
            case TypeKind::Error:   return "<error>";
            case TypeKind::Void:    return "void";
            case TypeKind::Bool:    return "bool";
            case TypeKind::Int:     return "int";
            case TypeKind::Float:   return "float";
            case TypeKind::String:  return "string";
            case TypeKind::Any:     return "any";

            case TypeKind::Range:
                return "range<" + type_name(types.element(type), output, natives) + ">";

            case TypeKind::Array:
                return "array<" + type_name(types.element(type), output, natives) + ">";

            case TypeKind::Optional:
                return "optional<" + type_name(types.element(type), output, natives) + ">";

            case TypeKind::Map:
                return "map<" + type_name(types.key(type), output, natives)
                    + ", " + type_name(types.value(type), output, natives) + ">";

            case TypeKind::Struct:
            {
                StructId id = types.struct_id(type);
                return to_string(output.structs[static_cast<size_t>(id)].path);
            }

            case TypeKind::Native:
            {
                if (natives == nullptr)
                    return "<native>";

                NativeTypeId id = types.native_id(type);
                return natives->type_names()[static_cast<size_t>(id)];
            }
        }

        return "<unknown>";
    }

// resolver
    class Resolver
    {
    public:
        explicit Resolver(const ResolverInput& input) : _input(input) {}

        ResolveOutput resolve()
        {
            _output.type_table = _input.types;

            discover_decls_from_files();
            resolve_types();

            return std::move(_output);
        }

    private:
        const ResolverInput& _input;
        ResolveOutput _output;

        std::map<Path, StructId> _struct_map;
        std::map<Path, FunctionId> _function_map;

        // per file
        int _file_id = -1;
        Path _scope;

        // files imports
        std::vector<std::vector<Path>> _imports;


        // ------------------------------------------

        void error(const SourceSpan& span, std::string message)
        {
            _output.errors.push_back({std::move(message), _file_id, span});
        }

        const std::vector<Path>& imports_of_current_file() const
        {
            return _imports[static_cast<size_t>(_file_id)];
        }

        TypeTable& types() { return _output.type_table; }

        const NativeRegistry* natives() const { return _input.natives; }


        StructInfo& struct_at(StructId id)          { return _output.structs[static_cast<size_t>(id)]; }
        FunctionInfo& function_at(FunctionId id)    { return _output.functions[static_cast<size_t>(id)]; }



        // ------------------------------------------

        template<class Map>
        std::optional<typename Map::mapped_type> lookup(const Map& map, const Path& path) const
        {
            Path scope = _scope;

            while (true)
            {
                if (auto it = map.find(path_concat(scope, path)); it != map.end())
                    return it->second;

                if (scope.empty())
                    break;

                scope.pop_back();
            }

            // import "lib",
            // a, it can also bind to lib::a
            for (const Path& imp : _imports[static_cast<size_t>(_file_id)])
                if (auto it = map.find(path_concat(imp, path)); it != map.end())
                    return it->second;

            return std::nullopt;
        }

        [[nodiscard]] std::vector<Path> candidate_paths(const Path& name) const
        {
            std::vector<Path> candidates;

            Path scope = _scope;
            while (true)
            {
                candidates.push_back(path_concat(scope, name));

                if (scope.empty())
                    break;

                scope.pop_back();
            }

            for (const Path& import : imports_of_current_file())
                candidates.push_back(path_concat(import, name));

            return candidates;
        }

        void enter(const StructInfo& info)
        {
            _file_id = info.file_index;

            _scope = info.path;
            _scope.pop_back();
        }

        static int find_field(const StructInfo& info, const std::string& name)
        {
            for (size_t i = 0; i < info.fields.size(); i++)
            {
                if (info.fields[i].name == name)
                    return static_cast<int>(i);
            }

            return -1;
        }

        // ------------------------------------------

        void discover_decls_from_files()
        {
            for (size_t i = 0; i < _input.files.size(); ++i)
            {
                _file_id = static_cast<int>(i);
                const ResolverFile& f = _input.files[i];

                std::vector<Path> imports;
                imports.reserve(f.imports.size());

                for (const ast::ImportDecl& imp : f.imports)
                    imports.push_back(from_file_path(imp.name));

                _imports.push_back(std::move(imports));

                discover_decls(f.decls, from_file_path(f.path));
            }
        }

        void discover_decls(const std::vector<ast::DeclPtr>& decls, const Path& scope)
        {
            for (const ast::DeclPtr& d : decls)
            {
                if (!d)
                {
                    foo::print_trace("DeclPtr is NULL!");
                    return;
                }

                std::visit(overload{
                    [&](const ast::FnDecl& fn)
                    {
                        discover_function(fn, d->span, scope);
                    },
                    [&](const ast::StructDecl& s)
                    {
                        discover_struct(s, d->span, scope);
                    },
                    [&](const ast::NamespaceDecl& n)
                    {
                        discover_decls(n.decls, path_child(scope, n.name));
                    }
                }, d->kind);
            }
        }

        void discover_function(const ast::FnDecl& fn, const SourceSpan& span, const Path& scope)
        {
            const FunctionId id = static_cast<FunctionId>(_output.functions.size());

            FunctionInfo info;
            info.path = path_child(scope, fn.name);
            info.file_index = _file_id;
            info.span = span;
            info.decl = &fn;


            if (add_function(info.path, id, span) == false)
                return;

            _output.to_file_index[&fn] = _file_id;
            _output.functions.push_back(std::move(info));
        }

        void discover_struct(const ast::StructDecl& decl, const SourceSpan& span, const Path& scope)
        {
            const StructId id = static_cast<StructId>(_output.structs.size());

            StructInfo info;
            info.path = path_child(scope, decl.name);
            info.file_index = _file_id;
            info.span = span;
            info.decl = &decl;
            info.type = types().struct_of(id);

            if (add_struct(info.path, id, span) == false)
                return;

            _output.to_file_index[&decl] = _file_id;
            _output.structs.push_back(std::move(info));

            for (const ast::FnDecl& m : decl.methods)
                discover_method(id, m, span);
        }

        void discover_method(StructId owner, const ast::FnDecl& m, const SourceSpan& span)
        {
            StructInfo& s = _output.structs[static_cast<size_t>(owner)];
            const FunctionId id = static_cast<FunctionId>(_output.functions.size());

            if (!s.methods.try_emplace(m.name, id).second)
            {
                error(span, "'" + to_string(s.path) + "' already has a method '" + m.name + "'");
                return;
            }

            FunctionInfo info;
            info.path = path_child(s.path, m.name);
            info.file_index = s.file_index;
            info.span = span;
            info.decl = &m;
            info.owner_type = s.type;
            info.is_method = !m.params.empty() && m.params[0].name == "self";

            _output.to_file_index[&m] = s.file_index;
            _output.functions.push_back(std::move(info));
        }

        bool add_struct(const Path& path, StructId id, const SourceSpan& span)
        {
            if (_struct_map.emplace(path, id).second)
                return true;

            error(span, "'" + to_string(path) + "' is already declared");
            return false;
        }

        bool add_function(const Path& path, FunctionId id, const SourceSpan& span)
        {
            if (_function_map.emplace(path, id).second)
                return true;

            error(span, "'" + to_string(path) + "' is already declared");
            return false;
        }

        // ------------------------------------------

        std::optional<StructId> lookup_struct(const Path& name) const
        {
            for (const Path& candidate : candidate_paths(name))
            {
                auto found = _struct_map.find(candidate);
                if (found != _struct_map.end())
                    return found->second;
            }

            return std::nullopt;
        }

        std::optional<FunctionId> lookup_function(const Path& name) const
        {
            for (const Path& candidate : candidate_paths(name))
            {
                auto found = _function_map.find(candidate);
                if (found != _function_map.end())
                    return found->second;
            }

            return std::nullopt;
        }

        void resolve_types()
        {
            for (StructInfo& info : _output.structs)
            {
                enter(info);

                for (const ast::StructField& field : info.decl->fields)
                {
                    if (find_field(info, field.name) >= 0)
                    {
                        error(field.span, "duplicate field '" + field.name + "'");
                        continue;
                    }

                    TypeId type = resolve_type(field.type);
                    if (type == BaseTypes::Void)
                    {
                        error(field.span, "a field cannot be void");
                        type = BaseTypes::Error;
                    }

                    info.fields.push_back({field.name, type});
                }
            }
        }

        TypeId resolve_type(const ast::TypeExprPtr& te)
        {
            if (!te)
                return BaseTypes::Error;

            return std::visit(overload{
                [&](const ast::NamedType& n) -> TypeId
                {
                    return resolve_named(n.path, te->span);
                },
                [&](const ast::ArrayType& a) -> TypeId
                {
                    TypeId e = resolve_type(a.element_type);
                    return types().array_of(e);
                },
                [&](const ast::MapType& m) -> TypeId
                {
                    TypeId k = resolve_type(m.key_type);
                    TypeId v = resolve_type(m.value_type);
                    return types().map_of(k, v);
                },
                [&](const ast::OptionalType& o) -> TypeId
                {
                    TypeId e = resolve_type(o.element_type);
                    return types().optional_of(e);
                }
            }, te->type);
        }

        TypeId resolve_named(const Path& parts, const SourceSpan& span)
        {
            if (parts.size() == 1)
            {
                const std::string& s = parts[0];

                if (s == "int")     return BaseTypes::Int;
                if (s == "float")   return BaseTypes::Float;
                if (s == "bool")    return BaseTypes::Bool;
                if (s == "string")  return BaseTypes::String;
                if (s == "void")    return BaseTypes::Void;
            }

            std::optional<StructId> found_struct = lookup_struct(parts);
            if (found_struct.has_value())
                return struct_at(*found_struct).type;

            if (TypeId t = find_native_type(parts); t != EmptyType)
                return t;

            error(span, "unknown type '" + to_string(parts) + "'");
            return BaseTypes::Error;
        }

        TypeId find_native_type(const Path& parts)
        {
            if (!natives())
                return EmptyType;

            TypeId t = natives()->find_type(types(), to_string(parts));
            return t;
        }

        [[maybe_unused]] std::string type_name(TypeId type) const
        {
            return foo::type_name(type, _output, natives());
        }


    }; // class



    ResolveOutput resolve(const ResolverInput& input)
    {
        Resolver resolver{input};
        return resolver.resolve();
    }
}
