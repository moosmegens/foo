#include <algorithm>
#include <iostream>
#include <utility>
#include <foo/resolver.hpp>

#include <foo/debug/debug_out.hpp>

#include <ranges>

#include <foo/util/overloaded.hpp>


namespace foo
{
// type table
    TypeTable::TypeTable()
    {
        for (TypeKind k : BaseKinds)
            intern(k);
    }

    TypeId TypeTable::intern(TypeKind kind, Id a, Id b, std::vector<TypeId> list)
    {
        Type t{kind, a, b, std::move(list)};

        auto [it, inserted] = _ids.try_emplace(t, static_cast<TypeId>(_types.size()));
        if (inserted)
            _types.push_back(t);

        return it->second;
    }

    TypeId TypeTable::variant_of(std::vector<TypeId> alternatives)
    {
        std::vector<TypeId> flat;

        for (TypeId a : alternatives)
        {
            if (a == BaseTypes::Error)
                return BaseTypes::Error;

            if (kind(a) == TypeKind::Variant)
                flat.insert(flat.end(), members(a).begin(), members(a).end());
            else
                flat.push_back(a);
        }

        std::ranges::sort(flat);
        flat.erase(std::unique(flat.begin(), flat.end()), flat.end());

        if (flat.size() == 1)
            return flat[0];

        return intern(TypeKind::Variant, EmptyId, EmptyId, std::move(flat));
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
        return add(std::move(name), Kind::Function,
            [sig = std::move(sig)](TypeTable&, TypeId) -> std::optional<FunctionSig>
            {
                return sig;
            });
    }

    NativeFunctionId NativeRegistry::add_method(std::string name, TypeId receiver, FunctionSig sig)
    {
        return add(std::move(name), Kind::Method,
            [receiver, sig = std::move(sig)](TypeTable&, TypeId r) -> std::optional<FunctionSig>
            {
                if (r != receiver)
                    return std::nullopt;

                return sig;
            });
    }

    NativeFunctionId NativeRegistry::add_method(std::string name, NativeResolver resolve)
    {
        return add(std::move(name), Kind::Method, std::move(resolve));
    }

    NativeFunctionId NativeRegistry::add_static_function(std::string name, TypeId target, FunctionSig sig)
    {
        return add(std::move(name), Kind::Static,
            [target, sig = std::move(sig)](TypeTable&, TypeId t) -> std::optional<FunctionSig>
            {
                if (t != target)
                    return std::nullopt;

                return sig;
            });
    }

    NativeFunctionId NativeRegistry::add_static_function(std::string name, NativeResolver resolve)
    {
        return add(std::move(name), Kind::Static, std::move(resolve));
    }


    NativeFunctionId NativeRegistry::add(std::string name, Kind kind, NativeResolver resolve)
    {
        _entries.push_back({std::move(name), kind, std::move(resolve)});

        return static_cast<NativeFunctionId>(_entries.size() - 1);
    }

    std::optional<NativeRegistry::Match> NativeRegistry::find(
        Kind kind, TypeTable& types, TypeId receiver, std::string_view name) const
    {
        for (auto i = _entries.size(); i-- > 0;)
        {
            const Entry& e = _entries[i];

            if (e.kind != kind || e.name != name)
                continue;

            if (auto sig = e.resolve(types, receiver))
                return Match{static_cast<NativeFunctionId>(i), std::move(*sig)};
        }

        return std::nullopt;
    }

// type name
    std::string type_name(TypeId type, const ResolveOutput& output)
    {
        const auto& natives = output.natives;

        if (type == EmptyType)
            return "<none>";

        const TypeTable& types = output.type_table;

        switch (types.kind(type))
        {
            case TypeKind::Error:   return "ERROR";
            case TypeKind::Void:    return "void";
            case TypeKind::Bool:    return "bool";
            case TypeKind::Int:     return "int";
            case TypeKind::Float:   return "float";
            case TypeKind::String:  return "string";
            case TypeKind::Any:     return "any";
            case TypeKind::None:    return "none";
            case TypeKind::Infer:   return "_";

            case TypeKind::Range:
                return "range<" + type_name(types.element(type), output) + ">";

            case TypeKind::Array:
                return "array<" + type_name(types.element(type), output) + ">";

            case TypeKind::Optional:
                return "optional<" + type_name(types.element(type), output) + ">";

            case TypeKind::Map:
                return "map<" + type_name(types.key(type), output)
                    + ", " + type_name(types.value(type), output) + ">";

            case TypeKind::Set:
                return "set<" + type_name(types.element(type), output) + ">";

            case TypeKind::Tuple:
            case TypeKind::Variant:
            {
                const bool tuple = types.kind(type) == TypeKind::Tuple;

                std::string out = "(";
                bool first = true;

                for (TypeId member : types.members(type))
                {
                    if (!first)
                        out += tuple ? ", " : " | ";

                    first = false;
                    out += type_name(member, output);
                }

                return out + ")";
            }

            case TypeKind::Function:
            {
                std::string out = "fn(";
                bool first = true;

                for (TypeId param : types.members(type))
                {
                    if (!first)
                        out += ", ";

                    first = false;
                    out += type_name(param, output);
                }

                out += ")";

                if (types.function_return(type) != BaseTypes::Void)
                    out += " -> " + type_name(types.function_return(type), output);

                return out;
            }

            case TypeKind::Struct:
            {
                StructId id = types.struct_id(type);
                return to_string(output.structs[static_cast<size_t>(id)].path);
            }

            case TypeKind::Enum:
            {
                EnumId id = types.enum_id(type);
                return to_string(output.enums[static_cast<size_t>(id)].path);
            }

            case TypeKind::Native:
            {
                NativeTypeId id = types.native_id(type);
                return natives.type_names()[static_cast<size_t>(id)];
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
            _output.natives = _input.natives;

            discover_decls_from_files();
            resolve_types();
            resolve_function_sigs();
            resolve_functions();

            return std::move(_output);
        }

    private:
        enum class StructVisitState
        {
            NotVisited,
            Visiting,
            Done
        };

        struct type_result
        {
            TypeId type;
            bool inmutable = false;
        };

        struct LocalScope
        {
            std::unordered_map<std::string, LocalInfo> names;
            SlotId first_slot;
        };

        struct Callee
        {
            FunctionSig sig;
            RefKind ref = RefKind::None;
            Id index = -1;

            const ast::Expr* receiver = nullptr;
        };

        struct AliasInfo : DeclInfo
        {
            const ast::AliasDecl* decl = nullptr;

            TypeId type = EmptyType;
            StructVisitState state = StructVisitState::NotVisited;
        };

        // while the body of a lambda is resolved, the members _locals, _next_slot, ... describe the lambda,
        // and the function (or lambda) around it is parked in _enclosing.
        // _lambdas[i] is the lambda whose surrounding context is _enclosing[i]
        struct SavedContext
        {
            std::vector<LocalScope> locals;
            SlotId next_slot = 0;
            SlotId max_slot = 0;
            TypeId return_type = -1;
            int loop_depth = 0;
        };

        struct LambdaScope
        {
            std::vector<LambdaCapture> captures;
            std::vector<TypeId> types; // parallel to captures
            std::unordered_map<std::string, size_t> by_name;
        };

        struct VarRef
        {
            TypeId type = EmptyType;
            RefKind ref = RefKind::None;
            Id index = -1;
        };


        // ------------------------------------------

        const ResolverInput& _input;
        ResolveOutput _output;

        std::map<Path, StructId> _struct_map;
        std::map<Path, EnumId> _enum_map;
        std::map<Path, FunctionId> _function_map;

        std::vector<AliasInfo> _aliases;
        std::map<Path, size_t> _alias_map;

        int _loop_depth = 0;

        // true while resolving the written type of a `var`, only there `_` may be used
        bool _allow_infer = false;

        // per file
        int _file_id = -1;
        Path _scope;

        // files imports
        std::vector<std::vector<Path>> _imports;


        std::vector<LocalScope> _locals;
        SlotId _next_slot = 0;
        SlotId _max_slot = 0;
        TypeId _current_return_type = -1;
        FunctionInfo* _current_resolving_function = nullptr;

        std::vector<SavedContext> _enclosing;
        std::vector<LambdaScope> _lambdas;



        void error(const SourceSpan& span, std::string message)
        {
            _output.errors.push_back({std::move(message), _file_id, span});
        }

        void warn(const SourceSpan& span, std::string message)
        {
            _output.warnings.push_back({std::move(message), _file_id, span});
        }

        const std::vector<Path>& imports_of_current_file() const
        {
            return _imports[static_cast<size_t>(_file_id)];
        }

        TypeTable& types() { return _output.type_table; }

        const NativeRegistry& natives() const { return _output.natives; }


        StructInfo& struct_at(StructId id)          { return _output.structs[static_cast<size_t>(id)]; }
        EnumInfo& enum_at(EnumId id)                { return _output.enums[static_cast<size_t>(id)]; }
        FunctionInfo& function_at(FunctionId id)    { return _output.functions[static_cast<size_t>(id)]; }


        ExprInfo make_einfo(TypeId type, RefKind ref = RefKind::None, Id index = -1)
        {
            ExprInfo info;
            info.type = type;
            info.ref = ref;
            info.index = index;
            return info;
        }



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
            _scope.pop_back(); // i remove the struct from the path, so lib::struct to just lib
        }

        void enter(const FunctionInfo& info)
        {
            _file_id = info.file_index;

            _scope = info.path;
            _scope.pop_back();  // i remove the function from the path, so lib::function to just lib

            bool is_in_struct = (info.owner_type != EmptyType);
            if (is_in_struct)
                _scope.pop_back(); // i remove the struct from the path, so lib::struct::function to just lib
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

        StructInfo& struct_of_type(TypeId type)
        {
            return struct_at(types().struct_id(type));
        }

        bool is_struct_type(TypeId type)
        {
            return type != EmptyType && types().kind(type) == TypeKind::Struct;
        }

        bool is_function_type(TypeId type)
        {
            return type != EmptyType && types().kind(type) == TypeKind::Function;
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
                    foo::println("DeclPtr is NULL!");
                    continue;
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
                    [&](const ast::EnumDecl& e)
                    {
                        discover_enum(e, d->span, scope);
                    },
                    [&](const ast::NamespaceDecl& n)
                    {
                        discover_decls(n.decls, path_child(scope, n.name));
                    },
                    [&](const ast::AliasDecl& a)
                    {
                        discover_alias(a, d->span, scope);
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
            info.span = fn.decl_span;
            info.decl = &fn;
            info.pub = fn.pub;


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

        void discover_enum(const ast::EnumDecl& decl, const SourceSpan& span, const Path& scope)
        {
            const EnumId id = static_cast<EnumId>(_output.enums.size());

            EnumInfo info;
            info.path = path_child(scope, decl.name);
            info.file_index = _file_id;
            info.span = span;
            info.decl = &decl;
            info.type = types().enum_of(id);

            // enums, structs and aliases share one namespace, they are all types
            if (_struct_map.contains(info.path) || _alias_map.contains(info.path)
                || !_enum_map.emplace(info.path, id).second)
            {
                error(span, "'" + to_string(info.path) + "' is already declared");
                return;
            }

            if (decl.variants.empty())
                error(span, "enum '" + decl.name + "' needs at least one variant");

            for (const ast::EnumVariant& v : decl.variants)
            {
                if (std::ranges::find(info.variants, v.name) != info.variants.end())
                {
                    error(v.span, "enum '" + decl.name + "' already has a variant '" + v.name + "'");
                    continue;
                }

                info.variants.push_back(v.name);
            }

            _output.to_file_index[&decl] = _file_id;
            _output.enums.push_back(std::move(info));
        }

        void discover_alias(const ast::AliasDecl& decl, const SourceSpan& span, const Path& scope)
        {
            AliasInfo info;
            info.path = path_child(scope, decl.name);
            info.file_index = _file_id;
            info.span = span;
            info.decl = &decl;

            // aliases, structs and enums share one namespace, they are all types
            if (_struct_map.contains(info.path) || _enum_map.contains(info.path)
                || !_alias_map.emplace(info.path, _aliases.size()).second)
            {
                error(span, "'" + to_string(info.path) + "' is already declared");
                return;
            }

            _output.to_file_index[&decl] = _file_id;
            _aliases.push_back(std::move(info));
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
            info.pub = m.pub;

            _output.to_file_index[&m] = s.file_index;
            _output.functions.push_back(std::move(info));
        }

        bool add_struct(const Path& path, StructId id, const SourceSpan& span)
        {
            if (!_enum_map.contains(path) && !_alias_map.contains(path) && _struct_map.emplace(path, id).second)
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

        std::optional<EnumId> lookup_enum(const Path& name) const
        {
            for (const Path& candidate : candidate_paths(name))
            {
                auto found = _enum_map.find(candidate);
                if (found != _enum_map.end())
                    return found->second;
            }

            return std::nullopt;
        }

        std::optional<size_t> lookup_alias(const Path& name) const
        {
            for (const Path& candidate : candidate_paths(name))
            {
                auto found = _alias_map.find(candidate);
                if (found != _alias_map.end())
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

        // an alias is just another name for its type, it is resolved the first time somebody needs it
        TypeId resolve_alias(size_t index)
        {
            AliasInfo& alias = _aliases[index];

            if (alias.state == StructVisitState::Done)
                return alias.type;

            if (alias.state == StructVisitState::Visiting)
            {
                _output.errors.push_back({
                    "type alias '" + to_string(alias.path) + "' refers to itself",
                    alias.file_index,
                    alias.span
                });

                return BaseTypes::Error;
            }

            alias.state = StructVisitState::Visiting;

            // an alias is resolved in its own file and scope, no matter who asked for it
            const int file_id = _file_id;
            const Path scope = _scope;
            const bool allow_infer = _allow_infer;

            _file_id = alias.file_index;
            _scope = alias.path;
            _scope.pop_back();
            _allow_infer = false;

            const TypeId type = resolve_type(alias.decl->type);

            _file_id = file_id;
            _scope = scope;
            _allow_infer = allow_infer;

            alias.type = type;
            alias.state = StructVisitState::Done;

            return type;
        }

        void resolve_types()
        {
            // aliases first, so a broken alias is reported even when nothing uses it
            for (size_t i = 0; i < _aliases.size(); i++)
                resolve_alias(i);

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

                    info.fields.push_back({field.name, type, field.pub});
                }
            }

            std::vector states(_output.structs.size(), StructVisitState::NotVisited);

            for (size_t i = 0; i < _output.structs.size(); i++)
                check_struct_contains_itself(static_cast<StructId>(i), states);
        }

        void check_struct_contains_itself(StructId id, std::vector<StructVisitState>& states)
        {
            // i have a state per struct

            // when checking my fields, i set my structs state to visiting
            // and after checking i set it to done.

            // and then call check_struct_contains_itself,
            // this means that when i encounter a struct whose state is visiting it must mean that i have already seen it from the original struct

            const size_t index = static_cast<size_t>(id);
            StructInfo& info = struct_at(id);

            if (states[index] == StructVisitState::Done)
                return;

            if (states[index] == StructVisitState::Visiting)
            {
                enter(info);
                error(info.span, "struct '" + to_string(info.path) + "' contains itself by value");
                return;
            }

            states[index] = StructVisitState::Visiting;

            for (const FieldInfo& field : info.fields)
                check_type_contains(field.type, states);

            states[index] = StructVisitState::Done;
        }

        // a struct is also contained by value when it sits inside a tuple field
        void check_type_contains(TypeId type, std::vector<StructVisitState>& states)
        {
            switch (types().kind(type))
            {
                case TypeKind::Struct:
                    check_struct_contains_itself(types().struct_id(type), states);
                    break;

                case TypeKind::Tuple:
                    for (TypeId member : types().members(type))
                        check_type_contains(member, states);
                    break;

                default:
                    break;
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
                [&](const ast::SetType& s) -> TypeId
                {
                    TypeId n = resolve_type(s.element_type);
                    return types().set_of(n);
                },
                [&](const ast::OptionalType& o) -> TypeId
                {
                    TypeId e = resolve_type(o.element_type);
                    return types().optional_of(e);
                },
                [&](const ast::TupleType& t) -> TypeId
                {
                    std::vector<TypeId> elements;

                    for (const ast::TupleElem& element : t.elements)
                    {
                        TypeId id = resolve_type(element.type);

                        if (id == BaseTypes::Void)
                        {
                            error(element.type->span, "a tuple element cannot be void");
                            id = BaseTypes::Error;
                        }

                        elements.push_back(id);
                    }

                    return types().tuple_of(std::move(elements));
                },
                [&](const ast::VariantType& v) -> TypeId
                {
                    std::vector<TypeId> alternatives;

                    for (const ast::TypeExprPtr& alternative : v.alternatives)
                    {
                        TypeId id = resolve_type(alternative);

                        if (id == BaseTypes::Void || id == BaseTypes::Any)
                        {
                            error(alternative->span, "a variant cannot contain '" + type_name(id) + "'");
                            id = BaseTypes::Error;
                        }

                        alternatives.push_back(id);
                    }

                    return types().variant_of(std::move(alternatives));
                },
                [&](const ast::FnType& f) -> TypeId
                {
                    // `_` is only supported in the written type of a variable, not inside a function type
                    const bool allow_infer = std::exchange(_allow_infer, false);

                    std::vector<TypeId> params;

                    for (const ast::TypeExprPtr& param : f.params)
                    {
                        TypeId id = resolve_type(param);

                        if (id == BaseTypes::Void)
                        {
                            error(param->span, "a function type parameter cannot be void");
                            id = BaseTypes::Error;
                        }

                        params.push_back(id);
                    }

                    const TypeId ret = f.ret ? resolve_type(f.ret) : BaseTypes::Void;

                    _allow_infer = allow_infer;

                    return types().function_of(std::move(params), ret);
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
                if (s == "any")     return BaseTypes::Any;

                if (s == "_")
                {
                    if (_allow_infer)
                        return BaseTypes::Infer;

                    error(span, "'_' can only be used in the type of a variable, it is inferred from the initializer");
                    return BaseTypes::Error;
                }
            }

            std::optional<StructId> found_struct = lookup_struct(parts);
            if (found_struct.has_value())
                return struct_at(*found_struct).type;

            std::optional<EnumId> found_enum = lookup_enum(parts);
            if (found_enum.has_value())
                return enum_at(*found_enum).type;

            std::optional<size_t> found_alias = lookup_alias(parts);
            if (found_alias.has_value())
                return resolve_alias(*found_alias);

            if (TypeId t = find_native_type(parts); t != EmptyType)
                return t;

            error(span, "unknown type '" + to_string(parts) + "'");
            return BaseTypes::Error;
        }

        TypeId find_native_type(const Path& parts)
        {
            TypeId t = natives().find_type(types(), to_string(parts));
            return t;
        }

        [[maybe_unused]] std::string type_name(TypeId type) const
        {
            return foo::type_name(type, _output);
        }


        void resolve_function_sigs()
        {
            for (FunctionInfo& info : _output.functions)
            {
                enter(info);
                info.sig = make_signature(info);
            }
        }

        FunctionSig make_signature(const FunctionInfo& info)
        {
            const ast::FnDecl& decl = *info.decl;

            FunctionSig sig;

            size_t first_param = 0;
            if (info.is_method)
            {
                first_param = 1;
                sig.self_by_ref = decl.params[0].reffed;
            }

            for (size_t i = first_param; i < decl.params.size(); ++i)
            {
                const ast::Param& param = decl.params[i];

                TypeId type = resolve_type(param.type);
                if (type == BaseTypes::Void)
                {
                    error(param.span, "a parameter cannot be void");
                    type = BaseTypes::Error;
                }

                sig.params.push_back({type, param.reffed});
            }

            if (decl.return_type)
                sig.ret = resolve_type(decl.return_type);
            else
                sig.ret = BaseTypes::Void;

            return sig;
        }


        // ------------------------------------------

        bool has_default(TypeId type, std::vector<StructId>& visiting)
        {
            switch (types().kind(type))
            {
                case TypeKind::Bool:
                case TypeKind::Int:
                case TypeKind::Float:
                case TypeKind::String:
                case TypeKind::Array:
                case TypeKind::Map:
                case TypeKind::Optional:
                case TypeKind::Void:
                case TypeKind::Set:
                    return true;

                case TypeKind::Error:
                    return true;

                case TypeKind::Tuple:
                {
                    for (TypeId member : types().members(type))
                        if (!has_default(member, visiting))
                            return false;

                    return true;
                }

                case TypeKind::Struct:
                {
                    const StructId id = types().struct_id(type);

                    if (std::ranges::find(visiting, id) != visiting.end())
                        return true;

                    visiting.push_back(id);

                    bool ok = true;
                    for (const FieldInfo& f : struct_at(id).fields)
                        ok = ok && has_default(f.type, visiting);

                    visiting.pop_back();

                    return ok;
                }

                // no default for variants and enums on purpose, giving one later is not a breaking change.
                // functions have none either, there is no function that makes sense as a default
                default:
                    return false;
            }
        }

        bool has_default(TypeId type)
        {
            std::vector<StructId> visiting;
            return has_default(type, visiting);
        }

        bool ensure_access_field(
            const StructInfo& info,
            const FieldInfo& field,
            const SourceSpan& span,
            const FunctionInfo* caller = nullptr,
            const std::string& extra = "")
        {
            const bool is_owner = caller != nullptr && caller->owner_type == info.type;

            if (field.pub || is_owner)
                return true;

            error(span,
                "field '" + field.name +
                "' is private" + extra);

            return false;
        }

        bool ensure_access_function(
            const FunctionInfo& fn,
            const SourceSpan& span,
            const FunctionInfo* caller = nullptr,
            const std::string& extra = "")
        {
            //println(fn.owner_type, fn.pub, fn.decl->name);

            if (fn.pub)
            {
                //println("fn.pub");
                return true;
            }

            if (!caller)
            {
                //println("!caller");
                return false;
            }

            if (fn.owner_type != EmptyType)
            {
                //println("fn.owner_type != EmptyType");
                if (fn.owner_type != caller->owner_type)
                {
                    //println("fn.owner_type != caller->owner_type");
                    goto error_label;
                }
            }

            if (path_same_start(fn.path, caller->path))
            {
                //println("path_same_start(fn.path, caller->path)");
                return true;
            }


        error_label:
            error(span,
                "function '" + to_string(fn.path) +
                "' is private" + extra);

            return false;
        }

        void resolve_functions()
        {
            for (FunctionInfo& info : _output.functions)
            {
                enter(info);
                resolve_function(info);
            }
        }

        void resolve_function(FunctionInfo& info)
        {
            const ast::FnDecl& decl = *info.decl;
            _current_resolving_function = &info;


            _current_return_type = info.sig.ret;
            _locals.clear();
            _next_slot = 0;
            _max_slot = 0;

            push_locals();

            size_t first_param = 0;
            if (info.is_method)
            {
                declare_local("self", info.owner_type, decl.params[0].span);
                first_param = 1;
            }

            for (size_t i = first_param; i < decl.params.size(); i++)
            {
                const ast::Param& param = decl.params[i];
                TypeId type = info.sig.params[i - first_param].type;

                declare_local(param.name, type, param.span);
            }

            resolve_block(decl.body);

            if (_current_return_type != BaseTypes::Void && block_always_returns(decl.body) == false)
                error(info.span, "'" + to_string(info.path) + "' does not return a value on every path");

            pop_locals();

            info.slot_count = _max_slot;

            _current_resolving_function = nullptr;
        }

        void push_locals()
        {
            LocalScope scope;
            scope.first_slot = _next_slot;

            _locals.push_back(std::move(scope));
        }

        void pop_locals()
        {
            _next_slot = _locals.back().first_slot;
            _locals.pop_back();
        }

        LocalInfo declare_local(const std::string& name, TypeId type, const SourceSpan& span)
        {
            LocalInfo local;
            local.type = type;
            local.slot = _next_slot;

            bool inserted = _locals.back().names.try_emplace(name, local).second;
            if (inserted == false)
            {
                error(span, "'" + name + "' is already declared in this scope");
                return local;
            }

            // std::cout << "  slot " << local.slot << ": " << name << " : " << type_name(type) << "\n";

            _next_slot += 1;
            _max_slot = std::max(_max_slot, _next_slot);

            return local;
        }

        static const LocalInfo* find_in_scopes(const std::vector<LocalScope>& scopes, const std::string& name)
        {
            for (const auto& scope : std::views::reverse(scopes))
            {
                auto found = scope.names.find(name);

                if (found != scope.names.end())
                    return &found->second;
            }

            return nullptr;
        }

        std::optional<VarRef> find_variable(const std::string& name)
        {
            return find_variable_at(name, _lambdas.size());
        }

        // level == _lambdas.size() is the context that is being resolved right now,
        // level 0 is the function around all the lambdas.
        // a name that is not a local of a lambda is captured from the context around it
        std::optional<VarRef> find_variable_at(const std::string& name, size_t level)
        {
            const std::vector<LocalScope>& scopes =
                (level == _lambdas.size()) ? _locals : _enclosing[level].locals;

            if (const LocalInfo* local = find_in_scopes(scopes, name))
                return VarRef{local->type, RefKind::Local, local->slot};

            if (level == 0)
                return std::nullopt;

            LambdaScope& lambda = _lambdas[level - 1];

            if (auto known = lambda.by_name.find(name); known != lambda.by_name.end())
                return VarRef{lambda.types[known->second], RefKind::Capture, static_cast<Id>(known->second)};

            const std::optional<VarRef> outer = find_variable_at(name, level - 1);

            if (!outer)
                return std::nullopt;

            const size_t index = lambda.captures.size();
            lambda.captures.push_back({outer->ref, outer->index});
            lambda.types.push_back(outer->type);
            lambda.by_name.emplace(name, index);

            return VarRef{outer->type, RefKind::Capture, static_cast<Id>(index)};
        }

        void resolve_block(const ast::Block& block)
        {
            push_locals();

            for (const ast::StmtPtr& stmt : block.statements)
                resolve_stmt(*stmt);

            pop_locals();
        }

        void check_stmt_in_own_scope(const ast::StmtPtr& stmt)
        {
            push_locals();

            resolve_stmt(*stmt);

            pop_locals();
        }

        void resolve_stmt(const ast::Stmt& stmt)
        {
            const SourceSpan span = stmt.span;

            std::visit(overload{
                [&](const ast::Block& s)    { resolve_block(s); },
                [&](const ast::VarDecl& s)  { resolve_var_decl(s, span); },
                [&](const ast::Return& s)   { resolve_return(s, span); },
                [&](const ast::If& s)       { resolve_if(s); },
                [&](const ast::While& s)    { resolve_while(s); },
                [&](const ast::ForIn& s)    { resolve_for_in(s, span); },
                [&](const ast::ExprStmt& s) { resolve_expr(*s.expr); },
                [&](const ast::Check& s)    { resolve_check_stmt(s, span); },
                [&](const ast::Break&)      { if (_loop_depth == 0) error(span, "'break' outside of a loop"); },
                [&](const ast::Continue&)   { if (_loop_depth == 0) error(span, "'continue' outside of a loop"); },
            }, stmt.kind);
        }


        bool contains_infer(TypeId type)
        {
            if (type == BaseTypes::Infer)
                return true;

            switch (types().kind(type))
            {
                case TypeKind::Array:
                case TypeKind::Optional:
                case TypeKind::Set:
                case TypeKind::Range:
                    return contains_infer(types().element(type));

                case TypeKind::Map:
                    return contains_infer(types().key(type)) || contains_infer(types().value(type));

                case TypeKind::Tuple:
                case TypeKind::Variant:
                    for (TypeId member : types().members(type))
                        if (contains_infer(member))
                            return true;

                    return false;

                default:
                    return false;
            }
        }

        // pattern is a written type with holes in it, actual is the type of the initializer
        std::optional<TypeId> fill_holes(TypeId pattern, TypeId actual)
        {
            if (pattern == BaseTypes::Infer)
            {
                if (actual == BaseTypes::Void || actual == BaseTypes::None)
                    return std::nullopt;

                return actual;
            }

            if (!contains_infer(pattern))
                return pattern;

            if (actual == BaseTypes::Error)
                return BaseTypes::Error;

            const TypeKind pattern_kind = types().kind(pattern);
            const TypeKind actual_kind = types().kind(actual);

            if (pattern_kind == TypeKind::Optional)
            {
                // `_?` takes the element of an optional initializer, and wraps a plain one
                const TypeId inner_actual = (actual_kind == TypeKind::Optional) ? types().element(actual) : actual;

                auto inner = fill_holes(types().element(pattern), inner_actual);
                if (!inner)
                    return std::nullopt;

                return types().optional_of(*inner);
            }

            if (pattern_kind != actual_kind)
                return std::nullopt;

            switch (pattern_kind)
            {
                case TypeKind::Array:
                {
                    auto e = fill_holes(types().element(pattern), types().element(actual));
                    if (!e)
                        return std::nullopt;

                    return types().array_of(*e);
                }

                case TypeKind::Set:
                {
                    auto e = fill_holes(types().element(pattern), types().element(actual));
                    if (!e)
                        return std::nullopt;

                    return types().set_of(*e);
                }

                case TypeKind::Range:
                {
                    auto e = fill_holes(types().element(pattern), types().element(actual));
                    if (!e)
                        return std::nullopt;

                    return types().range_of(*e);
                }

                case TypeKind::Map:
                {
                    auto k = fill_holes(types().key(pattern), types().key(actual));
                    auto v = fill_holes(types().value(pattern), types().value(actual));
                    if (!k || !v)
                        return std::nullopt;

                    return types().map_of(*k, *v);
                }

                case TypeKind::Tuple:
                {
                    const std::vector<TypeId> wanted = types().members(pattern);
                    const std::vector<TypeId> found = types().members(actual);

                    if (wanted.size() != found.size())
                        return std::nullopt;

                    std::vector<TypeId> elements;
                    for (size_t i = 0; i < wanted.size(); ++i)
                    {
                        auto member = fill_holes(wanted[i], found[i]);
                        if (!member)
                            return std::nullopt;

                        elements.push_back(*member);
                    }

                    return types().tuple_of(std::move(elements));
                }

                default: // a '_' inside a variant would be ambiguous
                    return std::nullopt;
            }
        }

        TypeId infer_type(TypeId written, TypeId init_type, const SourceSpan& span)
        {
            if (auto filled = fill_holes(written, init_type))
                return *filled;

            error(span, "cannot infer '_' from '" + type_name(init_type) + "'");
            return BaseTypes::Error;
        }

        // ---- patterns

        // the element types of a tuple with exactly `count` elements, or `count` Errors (after reporting)
        std::vector<TypeId> tuple_members(TypeId type, size_t count, const SourceSpan& span)
        {
            if (type != BaseTypes::Error)
            {
                if (types().kind(type) == TypeKind::Tuple && types().members(type).size() == count)
                    return types().members(type); // a copy

                error(span, "cannot destructure '" + type_name(type) + "' into " + std::to_string(count) + " elements");
            }

            return std::vector<TypeId>(count, BaseTypes::Error);
        }

        // `type` is the type of the value that gets stored in the pattern.
        // names are declared, places are checked to be assignable
        void resolve_pattern(const ast::Pattern& pattern, TypeId type)
        {
            std::visit(overload{
                [&](const ast::PatWild&) {},
                [&](const ast::PatName& n)
                {
                    _output.pattern_vars[&n] = declare_local(n.name, type, pattern.span);
                },
                [&](const ast::PatPlace& n)
                {
                    const TypeId place_type = resolve_expr(*n.place);

                    if (place_type == BaseTypes::Error)
                        return;

                    if (!is_lvalue(*n.place))
                        error(n.place->span, "this is not an lvalue");
                    else
                        expect_assignable(place_type, type, n.place->span);
                },
                [&](const ast::PatTuple& t)
                {
                    const std::vector<TypeId> members = tuple_members(type, t.elements.size(), pattern.span);

                    for (size_t i = 0; i < t.elements.size(); ++i)
                        resolve_pattern(*t.elements[i], members[i]);
                }
            }, pattern.kind);
        }

        void resolve_var_decl(const ast::VarDecl& var, const SourceSpan& span)
        {
            // set for `var x`, null for `var _` and `var (a, b)`
            const auto* single = std::get_if<ast::PatName>(&var.pattern->kind);

            TypeId written_type = EmptyType;
            if (var.type)
            {
                _allow_infer = true;
                written_type = resolve_type(var.type);
                _allow_infer = false;
            }

            const bool has_hole = written_type != EmptyType && contains_infer(written_type);
            TypeId type = written_type;

            if (var.init)
            {
                // a type with '_' in it can't be used as a hint for the initializer
                TypeId init_type = resolve_expr(*var.init, has_hole ? EmptyType : written_type);

                if (written_type == EmptyType)
                    type = init_type;
                else
                {
                    if (has_hole)
                        type = infer_type(written_type, init_type, var.init->span);

                    expect_assignable(type, init_type, var.init->span);
                }
            }
            else if (has_hole)
            {
                error(span, "this declaration uses '_' and needs an initializer to infer from");
                type = BaseTypes::Error;
            }
            else if (!single)
            {
                type = BaseTypes::Error; // the parser already reported the missing initializer
            }
            else if (written_type == EmptyType)
            {
                error(span, "variable '" + single->name + "' needs a type or an initializer");
                type = BaseTypes::Error;
            }
            else
            {
                if (!has_default(written_type))
                    error(span, "variable '" + single->name + "' has type '" + type_name(written_type)
                        + "' which has no default value, give it an initializer");
                else
                    warn(span, "variable '" + single->name + "' doesn't have a initial value");
            }

            resolve_pattern(*var.pattern, type);
        }

        void resolve_return(const ast::Return& ret, const SourceSpan& span)
        {
            // a lambda without a written return type: the first `return` decides what it returns
            if (_current_return_type == EmptyType)
            {
                TypeId type = ret.value ? resolve_expr(*ret.value) : BaseTypes::Void;

                if (type == BaseTypes::None)
                {
                    error(span, "cannot infer the return type from 'none', write the return type down");
                    type = BaseTypes::Error;
                }

                _current_return_type = type;
                return;
            }

            if (!ret.value)
            {
                if (_current_return_type != BaseTypes::Void)
                    error(span, "missing return value, expected '" + type_name(_current_return_type) + "'");

                return;
            }

            TypeId value_type = resolve_expr(*ret.value, _current_return_type);

            if (_current_return_type == BaseTypes::Void)
                error(ret.value->span, "a void function cannot return a value");
            else
            {
                if (!assignable(_current_return_type, value_type))
                {
                    error(span,
                        "type mismatch: expected return type '" + type_name(_current_return_type)
                        + "' but got '"+ type_name(value_type) + "'");
                }
            }
        }

        void resolve_if(const ast::If& if_stmt)
        {
            TypeId cond_type = resolve_expr(*if_stmt.cond);

            if (!assignable(BaseTypes::Bool, cond_type))
            {
                error(if_stmt.cond->span, "type mismatch: condition doesn't result in bool, got '" + type_name(cond_type) + "'");
            }

            check_stmt_in_own_scope(if_stmt.then_branch);

            if (if_stmt.else_branch)
                check_stmt_in_own_scope(if_stmt.else_branch);
        }


        void resolve_while(const ast::While& while_stmt)
        {
            TypeId cond_type = resolve_expr(*while_stmt.cond);
            if (!assignable(BaseTypes::Bool, cond_type))
            {
                error(while_stmt.cond->span, "type mismatch: condition doesn't result in bool, got '" + type_name(cond_type) + "'");
            }

            _loop_depth++;
            check_stmt_in_own_scope(while_stmt.body);
            _loop_depth--;
        }

        type_result iteration_type(TypeId type, const SourceSpan& span)
        {
            if (type == BaseTypes::Error)
                return {BaseTypes::Error};

            TypeKind kind = types().kind(type);

            if (kind == TypeKind::Range || kind == TypeKind::Array)
                return {types().element(type)};

            if (kind == TypeKind::Map)
                return {types().key(type), true};

            if (kind == TypeKind::Set)
                return {types().element(type), true};

            error(span, "cannot iterate over '" + type_name(type) + "'");
            return {BaseTypes::Error};
        }

        void resolve_for_in(const ast::ForIn& for_stmt, const SourceSpan& span)
        {
            TypeId iterable_type = resolve_expr(*for_stmt.iterable);
            auto element = iteration_type(iterable_type, for_stmt.iterable->span);

            if (for_stmt.reffed && element.inmutable)
            {
                error(span, "cannot ref iterate over '" + type_name(iterable_type) + "'");
            }

            push_locals();

            _output.for_vars[&for_stmt] = declare_local(for_stmt.variable_name, element.type, span);
            _loop_depth++;
            resolve_stmt(*for_stmt.body);
            _loop_depth--;

            pop_locals();
        }

        void resolve_check_stmt(const ast::Check& check, const SourceSpan& span)
        {
            TypeId expr_type = resolve_expr(*check.expr);
            TypeId inner_type = BaseTypes::Error;

            if (expr_type != BaseTypes::Error)
            {
                if (types().kind(expr_type) == TypeKind::Optional)
                    inner_type = types().element(expr_type);
                else
                    error(check.expr->span, "'check' needs an optional, found '" + type_name(expr_type) + "'");
            }

            push_locals();

            _output.check_vars[&check] = declare_local(check.variable_name, inner_type, span);
            resolve_stmt(*check.good_branch);

            pop_locals();

            if (check.bad_branch)
                check_stmt_in_own_scope(check.bad_branch);
        }

        static bool block_always_returns(const ast::Block& block)
        {
            for (const ast::StmtPtr& stmt : block.statements)
            {
                if (stmt_always_returns(*stmt))
                    return true;
            }

            return false;
        }

        static bool stmt_always_returns(const ast::Stmt& stmt)
        {
            // this works by making sure stmnt ALWAYS returns, recusivly

            // so for block it must have a return,
            // but for branchable statments it means that both need to be true
            // and then return is the only place where it pushes up true

            return std::visit(overload{
                [](const ast::Return&)
                {
                    return true;
                },
                [](const ast::Block& block)
                {
                    return block_always_returns(block);
                },
                [](const ast::If& if_stmt)
                {
                    if (!if_stmt.else_branch)
                        return false;

                    return stmt_always_returns(*if_stmt.then_branch) && stmt_always_returns(*if_stmt.else_branch);
                },
                [](const ast::Check& check)
                {
                    if (!check.bad_branch)
                        return false;

                    return stmt_always_returns(*check.good_branch) && stmt_always_returns(*check.bad_branch);
                },
                [](const auto&)
                {
                    return false;
                }
            }, stmt.kind);
        }


        // exprs

        // lvalues, this is true for anything on the left side of =.
        // a name only counts when it is a local (or a captured local),
        // Color::red is a name but not something you can assign to
        bool is_lvalue(const ast::Expr& expr) const
        {
            return std::visit(overload{
                [&](const ast::Name&)
                {
                    const auto it = _output.exprs.find(&expr);
                    return it != _output.exprs.end()
                        && (it->second.ref == RefKind::Local || it->second.ref == RefKind::Capture);
                },
                [&](const ast::Index& index)
                {
                    return is_lvalue(*index.target);
                },
                [&](const ast::Member& member)
                {
                    return is_lvalue(*member.target);
                },
                [](const auto&)
                {
                    return false;
                }
            }, expr.kind);
        }

        // this checks if its assignable,
        // it has some leeway in that int's can be converted to floats, and T's to T?'s etc
        // strict means strict
        bool assignable(TypeId to, TypeId from, bool strict = false)
        {
            if (!strict)
            {
                if (to == BaseTypes::Error || from == BaseTypes::Error)
                    return true;

                if (to == from)
                    return true;

                if (to == BaseTypes::Any)
                    return from != BaseTypes::Void;

                if (to == BaseTypes::Float && from == BaseTypes::Int)
                    return true;

                if (types().kind(to) == TypeKind::Variant)
                {
                    const std::vector<TypeId>& alternatives = types().members(to);

                    // (int | string) goes into (int | string | bool), every alternative must exist in the target
                    if (types().kind(from) == TypeKind::Variant)
                    {
                        return std::ranges::all_of(types().members(from), [&](TypeId f)
                        {
                            return std::ranges::find(alternatives, f) != alternatives.end();
                        });
                    }

                    return std::ranges::find(alternatives, from) != alternatives.end();
                }

                if (types().kind(to) == TypeKind::Optional && from == BaseTypes::None)
                    return true;

                if (types().kind(to) == TypeKind::Optional)
                    return assignable(types().element(to), from, strict);
            } else
            {
                return (to == from);
            }

            return false;
        }

        void expect_assignable(TypeId to, TypeId from, const SourceSpan& span, bool strict = false)
        {
            if (assignable(to, from, strict))
                return;

            error(span, "type mismatch: cannot assign '" + type_name(from) + "' to '" + type_name(to) + "'" +
                (strict ? " <strict: they need to be the same type>": ""));
        }


        TypeId resolve_expr(const ast::Expr& expr, TypeId expected_type = EmptyType)
        {
            const SourceSpan& span = expr.span;

            ExprInfo info = std::visit(overload{
            [&](const ast::NoneLit& )           { return make_einfo(BaseTypes::None); },
            [&](const ast::IntLit&)             { return make_einfo(BaseTypes::Int); },
            [&](const ast::FloatLit&)           { return make_einfo(BaseTypes::Float); },
            [&](const ast::StringLit&)          { return make_einfo(BaseTypes::String); },
            [&](const ast::BoolLit&)            { return make_einfo(BaseTypes::Bool); },
            [&](const ast::ArrayLit& e)         { return resolve_array_expr(e, span, expected_type); },
            [&](const ast::TupleLit& e)         { return resolve_tuple_lit(e, expected_type); },
            [&](const ast::StructLit& e)        { return resolve_struct_lit(e, span); },
            [&](const ast::Name &e)             { return resolve_name(e, span); },
            [&](const ast::Unary& e)            { return resolve_unary(e, span); },
            [&](const ast::Binary& e)           { return resolve_binary(e, span); },
            [&](const ast::Index& e)            { return resolve_index(e, span); },
            [&](const ast::Member& e)           { return resolve_member(e, span); },
            [&](const ast::Call& e)             { return resolve_call(e, span); },
            [&](const ast::StaticCall& e)       { return resolve_static_call(e, span); },
            [&](const ast::As& e)               { return resolve_as(e, span); },
            [&](const ast::PatternAssign& e)    { return resolve_pattern_assign(e); },
            [&](const ast::Lambda& e)           { return resolve_lambda(e, span, expected_type); }
            }, expr.kind);

            _output.exprs[&expr] = info;
            return info.type;
        }

        ExprInfo resolve_array_expr(const ast::ArrayLit& array, const SourceSpan& span, TypeId expected)
        {
            if (expected != EmptyType && types().kind(expected) == TypeKind::Optional)
                expected = types().element(expected);

            TypeId element_type = EmptyType;
            if (expected != EmptyType && types().kind(expected) == TypeKind::Array)
                element_type = types().element(expected);

            for (const ast::ExprPtr& element : array.elements)
            {
                TypeId type = resolve_expr(*element, element_type);

                // if no type hint, just take the type from the first expression
                // so ["string",1,2] would take the first, string, as the arrays type, so erroring on the ints
                if (element_type == EmptyType)
                    element_type = type;
                else
                    expect_assignable(element_type, type, element->span);
            }

            if (element_type == EmptyType)
            {
                error(span, "cannot infer the element type of an empty array, add a type annotation");
                element_type = BaseTypes::Error;
            }

            return make_einfo(types().array_of(element_type));
        }

        ExprInfo resolve_tuple_lit(const ast::TupleLit& tuple, TypeId expected)
        {
            if (expected != EmptyType && types().kind(expected) == TypeKind::Optional)
                expected = types().element(expected);

            // only a tuple of the same size gives hints for the elements
            std::vector<TypeId> hints;
            if (expected != EmptyType && types().kind(expected) == TypeKind::Tuple
                && types().members(expected).size() == tuple.elements.size())
                hints = types().members(expected);

            std::vector<TypeId> actual;
            bool fits = !hints.empty();

            for (size_t i = 0; i < tuple.elements.size(); ++i)
            {
                TypeId type = resolve_expr(*tuple.elements[i], hints.empty() ? EmptyType : hints[i]);

                if (type == BaseTypes::Void)
                {
                    error(tuple.elements[i]->span, "a tuple element cannot be void");
                    type = BaseTypes::Error;
                }

                if (!hints.empty() && !assignable(hints[i], type))
                    fits = false;

                actual.push_back(type);
            }

            // when every element converts to the expected one, the literal gets the expected type
            // and the runtime converts each element (the same way arrays do it)
            return make_einfo(fits ? expected : types().tuple_of(std::move(actual)));
        }

        ExprInfo resolve_as(const ast::As& as, const SourceSpan& span)
        {
            const TypeId source = resolve_expr(*as.expr);
            const TypeId target = resolve_type(as.type);

            if (source == BaseTypes::Error || target == BaseTypes::Error)
                return make_einfo(BaseTypes::Error);

            if (target == BaseTypes::Void || types().kind(target) == TypeKind::Optional)
            {
                error(as.type->span, "cannot use '" + type_name(target) + "' with 'as'");
                return make_einfo(BaseTypes::Error);
            }

            if (types().kind(source) == TypeKind::Variant)
            {
                const std::vector<TypeId>& alternatives = types().members(source);

                if (std::ranges::find(alternatives, target) == alternatives.end())
                {
                    error(as.type->span, "'" + type_name(target) + "' is not part of '" + type_name(source) + "'");
                    return make_einfo(BaseTypes::Error);
                }
            }
            else if (source != BaseTypes::Any)
            {
                error(span, "'as' needs a variant or any, found '" + type_name(source) + "'");
                return make_einfo(BaseTypes::Error);
            }

            return make_einfo(types().optional_of(target));
        }

        ExprInfo resolve_pattern_assign(const ast::PatternAssign& assign)
        {
            resolve_pattern(*assign.pattern, resolve_expr(*assign.value));
            return make_einfo(BaseTypes::Void);
        }

        ExprInfo resolve_lambda(const ast::Lambda& lambda, const SourceSpan& span, TypeId expected)
        {
            if (expected != EmptyType && types().kind(expected) == TypeKind::Optional)
                expected = types().element(expected);

            std::vector<TypeId> hint_params;
            TypeId hint_ret = EmptyType;

            if (is_function_type(expected) && types().members(expected).size() == lambda.params.size())
            {
                hint_params = types().members(expected);
                hint_ret = types().function_return(expected);
            }

            std::vector<TypeId> param_types;

            for (size_t i = 0; i < lambda.params.size(); ++i)
            {
                const ast::Param& param = lambda.params[i];
                TypeId type = BaseTypes::Error;

                if (param.type)
                    type = resolve_type(param.type);
                else if (!hint_params.empty())
                    type = hint_params[i];
                else
                    error(param.span, "cannot infer the type of parameter '" + param.name + "', add a type annotation");

                if (type == BaseTypes::Void)
                {
                    error(param.span, "a parameter cannot be void");
                    type = BaseTypes::Error;
                }

                param_types.push_back(type);
            }

            // EmptyType means: infer it from the body
            TypeId ret = EmptyType;

            if (lambda.return_type)
                ret = resolve_type(lambda.return_type);
            else if (hint_ret != EmptyType && hint_ret != BaseTypes::Void && hint_ret != BaseTypes::Error)
                ret = hint_ret;

            // function around the lambda, the lambda gets its own slots
            _enclosing.push_back({std::move(_locals), _next_slot, _max_slot, _current_return_type, _loop_depth});
            _lambdas.emplace_back();

            _locals.clear();
            _next_slot = 0;
            _max_slot = 0;
            _current_return_type = ret;
            _loop_depth = 0;

            push_locals();

            for (size_t i = 0; i < lambda.params.size(); ++i)
                declare_local(lambda.params[i].name, param_types[i], lambda.params[i].span);

            resolve_block(lambda.body);

            const TypeId final_ret = _current_return_type == EmptyType ? BaseTypes::Void : _current_return_type;

            if (final_ret != BaseTypes::Void && final_ret != BaseTypes::Error && !block_always_returns(lambda.body))
                error(span, "lambda does not return a value on every path");

            pop_locals();

            LambdaInfo info;

            for (TypeId type : param_types)
                info.sig.params.push_back({type, false});

            info.sig.ret = final_ret;
            info.slot_count = _max_slot;
            info.file_index = _file_id;
            info.captures = std::move(_lambdas.back().captures);

            _lambdas.pop_back();

            SavedContext saved = std::move(_enclosing.back());
            _enclosing.pop_back();

            _locals = std::move(saved.locals);
            _next_slot = saved.next_slot;
            _max_slot = saved.max_slot;
            _current_return_type = saved.return_type;
            _loop_depth = saved.loop_depth;

            _output.lambdas[&lambda] = std::move(info);

            return make_einfo(types().function_of(std::move(param_types), final_ret));
        }

        ExprInfo resolve_struct_lit(const ast::StructLit& lit, const SourceSpan& span)
        {
            TypeId type = resolve_type(lit.type);

            if (type != BaseTypes::Error && is_struct_type(type) == false)
            {
                error(span, "'" + type_name(type) + "' is not a struct");
                type = BaseTypes::Error;
            }

            if (type == BaseTypes::Error)
            {
                for (const ast::FieldInit& init : lit.fields)
                    resolve_expr(*init.value);

                return make_einfo(BaseTypes::Error);
            }

            StructInfo& info = struct_of_type(type);
            std::vector<bool> field_was_set(info.fields.size(), false);

            std::vector<int> field_indices;

            field_indices.reserve(lit.fields.size());


            for (const ast::FieldInit& init : lit.fields)
            {
                int field_index = find_field(info, init.name);
                field_indices.push_back(field_index);

                if (field_index < 0)
                {
                    error(init.span, "'" + to_string(info.path) + "' has no field '" + init.name + "'");
                    resolve_expr(*init.value);
                    continue;
                }

                const size_t index = static_cast<size_t>(field_index);
                const FieldInfo& field = info.fields[index];

                ensure_access_field(info, field, init.span, _current_resolving_function, ", make a factory function instead");


                if (field_was_set[index])
                    error(init.span, "field '" + init.name + "' is set twice");

                field_was_set[index] = true;

                TypeId field_type = field.type;

                TypeId value_type = resolve_expr(*init.value, field_type);
                expect_assignable(field_type, value_type, init.value->span);
            }


            for (size_t i = 0; i < field_was_set.size(); i++)
            {
                if (field_was_set[i])
                    continue;

                if (!has_default(info.fields[i].type))
                {
                    error(span, "missing field '" + info.fields[i].name + "', type '"
                        + type_name(info.fields[i].type) + "' has no default value");
                }
            }

            _output.struct_lit_fields[&lit] = std::move(field_indices);

            return make_einfo(type, RefKind::StructLit, types().struct_id(type));
        }

        ExprInfo resolve_name(const ast::Name& name, const SourceSpan& span)
        {
            if (name.path.size() == 1)
            {
                if (const auto variable = find_variable(name.path[0]))
                    return make_einfo(variable->type, variable->ref, variable->index);
            }

            // Color::red
            if (name.path.size() >= 2)
            {
                const Path type_path(name.path.begin(), name.path.end() - 1);

                std::optional<EnumId> found = lookup_enum(type_path);

                if (!found)
                {
                    if (const auto alias = lookup_alias(type_path))
                    {
                        const TypeId aliased = resolve_alias(*alias);

                        if (types().kind(aliased) == TypeKind::Enum)
                            found = types().enum_id(aliased);
                    }
                }

                if (found)
                {
                    const EnumInfo& info = enum_at(*found);
                    const auto variant = std::ranges::find(info.variants, name.path.back());

                    if (variant == info.variants.end())
                    {
                        error(span, "enum '" + to_string(info.path) + "' has no variant '" + name.path.back() + "'");
                        return make_einfo(BaseTypes::Error);
                    }

                    return make_einfo(info.type, RefKind::EnumVariant, static_cast<Id>(variant - info.variants.begin()));
                }
            }

            error(span, "unknown variable '" + to_string(name.path) + "'");
            return make_einfo(BaseTypes::Error);
        }

        static bool is_numeric(TypeId type)
        {
            return type == BaseTypes::Int || type == BaseTypes::Float;
        }

        static TypeId promote_numeric(TypeId lhs, TypeId rhs)
        {
            if (lhs == BaseTypes::Float || rhs == BaseTypes::Float)
                return BaseTypes::Float;

            return BaseTypes::Int;
        }

        ExprInfo resolve_unary(const ast::Unary& unary, const SourceSpan& span)
        {
            TypeId type = resolve_expr(*unary.operant);

            if (type == BaseTypes::Error)
                return make_einfo(BaseTypes::Error);

            if (unary.op == ast::UnaryOp::Neg && is_numeric(type))
                return make_einfo(type);

            if (unary.op == ast::UnaryOp::Not && type == BaseTypes::Bool)
                return make_einfo(BaseTypes::Bool);

            error(span, "operator cannot be applied to '" + type_name(type) + "'");
            return make_einfo(BaseTypes::Error);
        }

        ExprInfo resolve_binary(const ast::Binary& binary, const SourceSpan& span)
        {
            using Op = ast::BinaryOp;

            if (binary.op == Op::Assign)
            {
                return resolve_assign(binary);
            }

            TypeId lhs = resolve_expr(*binary.lhs);
            TypeId rhs = resolve_expr(*binary.rhs);

            if (lhs == BaseTypes::Error || rhs == BaseTypes::Error)
                return make_einfo(BaseTypes::Error);

            bool same_type = lhs == rhs;
            const bool both_numeric = is_numeric(lhs) && is_numeric(rhs);

            switch (binary.op)
            {
                case Op::Add:
                    if (both_numeric)
                        return make_einfo(promote_numeric(lhs, rhs));

                    if (lhs == BaseTypes::String && rhs == BaseTypes::String)
                        return make_einfo(BaseTypes::String);

                    if (same_type)
                    {
                        const TypeKind kind = types().kind(lhs);

                        if (kind == TypeKind::Array || kind == TypeKind::Set)
                            return make_einfo(lhs);
                    }

                    return operator_error(span, lhs, rhs);

                case Op::Sub:
                case Op::Mul:
                case Op::Div:
                    if (both_numeric)
                        return make_einfo(promote_numeric(lhs, rhs));

                    return operator_error(span, lhs, rhs);

                case Op::Mod:
                    if (lhs == BaseTypes::Int && rhs == BaseTypes::Int)
                        return make_einfo(BaseTypes::Int);

                    return operator_error(span, lhs, rhs);

                case Op::Eq:
                case Op::NotEq:
                    if (both_numeric || (same_type && lhs != BaseTypes::Void))
                        return make_einfo(BaseTypes::Bool);

                    return operator_error(span, lhs, rhs);

                case Op::Less:
                case Op::LessEq:
                case Op::Greater:
                case Op::GreaterEq:
                    if (both_numeric || (same_type && lhs == BaseTypes::String))
                        return make_einfo(BaseTypes::Bool);

                    return operator_error(span, lhs, rhs);

                case Op::And:
                case Op::Or:
                    if (lhs == BaseTypes::Bool && rhs == BaseTypes::Bool)
                        return make_einfo(BaseTypes::Bool);

                    return operator_error(span, lhs, rhs);

                case Op::Range:
                case Op::RangeInclusive:
                    if (lhs == BaseTypes::Int && rhs == BaseTypes::Int)
                        return make_einfo(types().range_of(BaseTypes::Int));

                    return operator_error(span, lhs, rhs);

                case Op::Assign:
                    break;
            }

            return make_einfo(BaseTypes::Error);
        }

        ExprInfo operator_error(const SourceSpan& span, TypeId lhs, TypeId rhs)
        {
            error(span, "operator cannot be applied to '" + type_name(lhs) + "' and '" + type_name(rhs) + "'");
            return make_einfo(BaseTypes::Error);
        }

        ExprInfo resolve_assign(const ast::Binary& assign)
        {
            TypeId target_type = resolve_expr(*assign.lhs);
            TypeId value_type = resolve_expr(*assign.rhs, target_type);

            // an unknown name already got its own error
            if (target_type != BaseTypes::Error && is_lvalue(*assign.lhs) == false)
                error(assign.lhs->span, "the left side of '=' is not an lvalue");
            else
                expect_assignable(target_type, value_type, assign.rhs->span);

            return make_einfo(BaseTypes::Void);
        }

        ExprInfo resolve_index(const ast::Index& index, const SourceSpan& span)
        {
            TypeId target_type = resolve_expr(*index.target);
            TypeId index_type = resolve_expr(*index.indexer);

            if (target_type == BaseTypes::Error)
                return make_einfo(BaseTypes::Error);

            TypeKind kind = types().kind(target_type);

            if (kind == TypeKind::Array)
            {
                expect_assignable(BaseTypes::Int, index_type, index.indexer->span);
                return make_einfo(types().element(target_type));
            }

            if (kind == TypeKind::Map)
            {
                expect_assignable(types().key(target_type), index_type, index.indexer->span);
                return make_einfo(types().value(target_type));
            }

            // t[0], the index has to be a literal so the type of the element is known
            if (kind == TypeKind::Tuple)
            {
                const auto* literal = std::get_if<ast::IntLit>(&index.indexer->kind);

                if (!literal)
                {
                    error(index.indexer->span, "a tuple can only be indexed with an integer literal");
                    return make_einfo(BaseTypes::Error);
                }

                const std::vector<TypeId>& members = types().members(target_type);

                if (literal->value < 0 || static_cast<uint64_t>(literal->value) >= members.size())
                {
                    error(index.indexer->span, "tuple index " + std::to_string(literal->value)
                        + " is out of range (size " + std::to_string(members.size()) + ")");
                    return make_einfo(BaseTypes::Error);
                }

                return make_einfo(members[static_cast<size_t>(literal->value)]);
            }

            error(span, "cannot index into '" + type_name(target_type) + "'");
            return make_einfo(BaseTypes::Error);
        }

        ExprInfo resolve_member(const ast::Member& member, const SourceSpan& span)
        {
            TypeId target_type = resolve_expr(*member.target);


            if (target_type == BaseTypes::Error)
                return make_einfo(BaseTypes::Error);


            if (is_struct_type(target_type))
            {
                StructInfo& info = struct_of_type(target_type);
                int field_index = find_field(info, member.name);

                if (field_index >= 0)
                {
                    const FieldInfo& field =
                        info.fields[static_cast<size_t>(field_index)];

                    if (!ensure_access_field(info, field, span, _current_resolving_function))
                        return make_einfo(BaseTypes::Error);

                    return make_einfo(field.type, RefKind::Field, field_index);
                }
            }

            error(span, "'" + type_name(target_type) + "' has no field '" + member.name + "'");
            return make_einfo(BaseTypes::Error);
        }

        static Callee make_callee(const FunctionSig& sig, RefKind ref, Id index, const ast::Expr* receiver = nullptr)
        {
            Callee callee;
            callee.sig = sig;
            callee.ref = ref;
            callee.index = index;
            callee.receiver = receiver;
            return callee;
        }

        ExprInfo resolve_call(const ast::Call& call, const SourceSpan& span)
        {
            std::optional<Callee> callee = resolve_callee(*call.callee);

            if (callee.has_value() == false)
            {
                _output.exprs[call.callee.get()] = make_einfo(BaseTypes::Error);
                return fail_call(call.args);
            }

            const bool closure = callee->ref == RefKind::Closure;

            // a function value keeps the info it already has (a local, a capture, a field, ...),
            // the runtime evaluates it to get the function
            if (!closure)
                _output.exprs[call.callee.get()] = make_einfo(callee->sig.ret, callee->ref, callee->index);

            if (callee->ref == RefKind::Native || callee->ref == RefKind::NativeMethod)
                _output.native_sigs[&call] = callee->sig;


            //bool needs_variable = (callee->receiver != nullptr) && callee->sig.self_by_ref;
            // if (needs_variable && is_lvalue(*callee->receiver) == false)
            //     error(callee->receiver->span, "this method takes self by reference, the receiver must be a lvalue");

            resolve_args(callee->sig, call.args, span);

            return make_einfo(callee->sig.ret, closure ? RefKind::Closure : RefKind::None);
        }

        ExprInfo resolve_static_call(const ast::StaticCall& call, const SourceSpan& span)
        {
            TypeId type = resolve_type(call.target);

            if (type == BaseTypes::Error)
                return fail_call(call.args);

            std::optional<Callee> callee;

            if (is_struct_type(type))
                callee = find_static_function(type, call.method, span);
            else
                callee = find_native_static_function(type, call.method, span);

            if (callee.has_value() == false)
                return fail_call(call.args);

            // this needs saving as natives are generated
            // and their types arent pushed to the runtime
            if (callee->ref == RefKind::Native || callee->ref == RefKind::NativeMethod)
                _output.native_sigs[&call] = callee->sig;

            resolve_args(callee->sig, call.args, span);

            return make_einfo(callee->sig.ret, callee->ref, callee->index);
        }

        std::optional<Callee> find_static_function(TypeId struct_type, const std::string& name, const SourceSpan& span)
        {
            StructInfo& info = struct_of_type(struct_type);

            auto found = info.methods.find(name);
            if (found == info.methods.end())
            {
                error(span, "'" + to_string(info.path) + "' has no function '" + name + "'");
                return std::nullopt;
            }

            FunctionInfo& function = function_at(found->second);
            if (function.is_method)
            {
                error(span, "'" + name + "' is a method, call it on a value");
                return std::nullopt;
            }

            if (!ensure_access_function(function, span, _current_resolving_function))
                return std::nullopt;

            return make_callee(function.sig, RefKind::Function, found->second);
        }

        std::optional<Callee> find_native_static_function(TypeId type, const std::string& name, const SourceSpan& span)
        {
            if (types().kind(type) == TypeKind::Native)
            {
                const std::string full_name = type_name(type) + "::" + name;

                if (auto match = natives().find_function(types(), full_name))
                    return make_callee(match->sig, RefKind::Native, match->id);
            }

            if (auto match = natives().find_static_function(types(), type, name))
                return make_callee(match->sig, RefKind::Native, match->id);

            error(span, "'" + type_name(type) + "' has no static function '" + name + "'");
            return std::nullopt;
        }

        Callee closure_callee(TypeId type)
        {
            FunctionSig sig;

            for (TypeId param : types().members(type))
                sig.params.push_back({param, false});

            sig.ret = types().function_return(type);

            return make_callee(sig, RefKind::Closure, -1);
        }

        std::optional<Callee> resolve_local_function(const ast::Expr& expr, const ast::Name& name)
        {
            if (name.path.size() != 1)
                return std::nullopt;

            const auto variable = find_variable(name.path[0]);

            if (!variable || !is_function_type(variable->type))
                return std::nullopt;

            _output.exprs[&expr] = make_einfo(variable->type, variable->ref, variable->index);
            return closure_callee(variable->type);
        }

        std::optional<Callee> resolve_function_value(const ast::Expr& expr)
        {
            const TypeId type = resolve_expr(expr);

            if (type == BaseTypes::Error)
                return std::nullopt;

            if (!is_function_type(type))
            {
                error(expr.span, "'" + type_name(type) + "' cannot be called");
                return std::nullopt;
            }

            return closure_callee(type);
        }

        std::optional<Callee> resolve_callee(const ast::Expr& expr)
        {
            if (const ast::Name* name = std::get_if<ast::Name>(&expr.kind))
            {
                if (auto closure = resolve_local_function(expr, *name))
                    return closure;

                return resolve_function_callee(*name, expr.span);
            }

            if (const ast::Member* member = std::get_if<ast::Member>(&expr.kind))
                return resolve_method_callee(expr, *member);

            return resolve_function_value(expr);
        }

        std::optional<Callee> resolve_function_callee(const ast::Name& name, const SourceSpan& span)
        {
            std::optional<FunctionId> found = lookup_function(name.path);
            if (found.has_value())
            {
                const FunctionInfo& function = function_at(*found);

                if (!ensure_access_function(function, span, _current_resolving_function))
                    return std::nullopt;

                return make_callee(function.sig, RefKind::Function, *found);
            }

            std::string full_name = to_string(name.path);


            auto match = natives().find_function(types(), full_name);
            if (match.has_value())
                return make_callee(match->sig, RefKind::Native, match->id);


            error(span, "unknown function '" + full_name + "'");
            return std::nullopt;
        }

        std::optional<Callee> resolve_method_callee(const ast::Expr& expr, const ast::Member& member)
        {
            const SourceSpan& span = expr.span;

            TypeId receiver_type = resolve_expr(*member.target);

            if (receiver_type == BaseTypes::Error)
                return std::nullopt;

            if (is_struct_type(receiver_type))
            {
                StructInfo& info = struct_of_type(receiver_type);

                auto found = info.methods.find(member.name);
                if (found != info.methods.end())
                {
                    FunctionInfo& function = function_at(found->second);

                    if (function.is_method == false)
                    {
                        error(span, "'" + member.name + "' is a static function, not a method");

                        return std::nullopt;
                    }

                    if (!ensure_access_function(function, span, _current_resolving_function))
                        return std::nullopt;

                    return make_callee(function.sig, RefKind::Method, found->second, member.target.get());
                }

                const int field_index = find_field(info, member.name);

                if (field_index >= 0 && is_function_type(info.fields[static_cast<size_t>(field_index)].type))
                {
                    const FieldInfo& field = info.fields[static_cast<size_t>(field_index)];

                    if (!ensure_access_field(info, field, span, _current_resolving_function))
                        return std::nullopt;

                    _output.exprs[&expr] = make_einfo(field.type, RefKind::Field, field_index);
                    return closure_callee(field.type);
                }
            }


            auto match = natives().find_method(types(), receiver_type, member.name);
            if (match.has_value())
                return make_callee(match->sig, RefKind::NativeMethod, match->id, member.target.get());


            error(span, "'" + type_name(receiver_type) + "' has no method '" + member.name + "'");
            return std::nullopt;
        }

        static const ParamSig* param_for_argument(const FunctionSig& sig, size_t arg_index)
        {
            if (arg_index < sig.params.size())
                return &sig.params[arg_index];

            if (sig.variadic && !sig.params.empty())
                return &sig.params.back();

            return nullptr;
        }

        void resolve_args(const FunctionSig& sig, const std::vector<ast::ExprPtr>& args, const SourceSpan& span)
        {
            const size_t param_count = sig.params.size();
            const size_t arg_count = args.size();

            if (sig.variadic)
            {
                size_t min_args = 0;
                if (param_count > 0)
                    min_args = param_count - 1;

                if (arg_count < min_args)
                    error(span, "expected at least " + std::to_string(min_args) + " argument(s), found " + std::to_string(arg_count));
            }
            else if (arg_count != param_count)
            {
                error(span, "expected " + std::to_string(param_count) + " argument(s), found " + std::to_string(arg_count));
            }

            for (size_t i = 0; i < arg_count; ++i)
            {
                const ast::Expr& arg = *args[i];
                const ParamSig* param = param_for_argument(sig, i);

                if (param == nullptr)
                {
                    resolve_expr(arg);
                    continue;
                }

                TypeId arg_type = resolve_expr(arg, param->type);

                if (param->by_ref)
                {
                    expect_assignable(param->type, arg_type, arg.span, true);

                    if (is_lvalue(arg) == false)
                        warn(arg.span, "this parameter is by reference, you are passing a rvalue");

                } else
                {
                    expect_assignable(param->type, arg_type, arg.span);
                }

            }
        }

        ExprInfo fail_call(const std::vector<ast::ExprPtr>& args)
        {
            for (const ast::ExprPtr& arg : args)
                resolve_expr(*arg);

            return make_einfo(BaseTypes::Error);
        }



    }; // class



    ResolveOutput resolve(const ResolverInput& input)
    {
        Resolver resolver{input};
        return resolver.resolve();
    }


    std::string to_string(const ResolveOutput& o, const StructInfo& _struct, bool nerd)
    {
        std::string result = foo::to_string(_struct.path);


        result += "{ ";
        bool first = true;
        for (const auto& f : _struct.fields)
        {
            if (!first)
                result += ", ";

            result += f.name;
            if (nerd)
            {
                result += ": ";
                result += type_name(f.type, o);
            }

            first = false;
        }

        result += " }";


        return result;
    }

    std::string to_string(const ResolveOutput& o, const FunctionSig& sig)
    {
        std::string result;

        result += "(";
        bool first = true;
        for (const ParamSig& arg : sig.params)
        {
            if (!first)
                result += ", ";

            result += type_name(arg.type, o);
            first = false;
        }
        if (sig.variadic)
        {
            result += "...";
        }
        result += ")";

        result += " -> " + type_name(sig.ret, o);

        return result;
    }

    std::string to_string(const ResolveOutput& o, const FunctionInfo& _function, [[maybe_unused]] bool nerd)
    {
        std::string result = foo::to_string(_function.path);

        result += to_string(o, _function.sig);

        return result;
    }
}