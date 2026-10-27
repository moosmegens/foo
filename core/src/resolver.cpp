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

        // files imports
        std::vector<std::vector<Path>> _imports;


        // ------------------------------------------

        void error(const SourceSpan& span, std::string message)
        {
            _output.errors.push_back({std::move(message), _file_id, span});
        }

        TypeTable& types() { return _output.type_table; }

        const NativeRegistry* natives() const { return _input.natives; }

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

        void resolve_types()
        {

        }


    }; // class



    ResolveOutput resolve(const ResolverInput& input)
    {
        Resolver resolver{input};
        return resolver.resolve();
    }
}
