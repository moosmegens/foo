#include <iostream>
#include <foo/resolver.hpp>
#include <ranges>

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

        struct LocalScope
        {
            std::unordered_map<std::string, LocalInfo> names;
            SlotId first_slot;
        };


        // ------------------------------------------

        const ResolverInput& _input;
        ResolveOutput _output;

        std::map<Path, StructId> _struct_map;
        std::map<Path, FunctionId> _function_map;

        // per file
        int _file_id = -1;
        Path _scope;

        // files imports
        std::vector<std::vector<Path>> _imports;


        std::vector<LocalScope> _locals;
        SlotId _next_slot = 0;
        SlotId _max_slot = 0;
        TypeId _current_return_type = -1;



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
            _scope.pop_back();
        }

        void enter(const FunctionInfo& info)
        {
            _file_id = info.file_index;

            _scope = info.path;
            _scope.pop_back();

            bool is_in_struct = (info.owner_type != EmptyType);
            if (is_in_struct)
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

            std::vector states(_output.structs.size(), StructVisitState::NotVisited);

            for (size_t i = 0; i < _output.structs.size(); i++)
                check_struct_contains_itself(static_cast<StructId>(i), states);
        }

        void check_struct_contains_itself(StructId id, std::vector<StructVisitState>& states)
        {
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
            {
                if (types().kind(field.type) == TypeKind::Struct)
                    check_struct_contains_itself(types().struct_id(field.type), states);
            }

            states[index] = StructVisitState::Done;
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


            _current_return_type = info.sig.ret;
            _locals.clear();
            _next_slot = 0;
            _max_slot = 0;

            std::cout << "RESOLVING fn " << to_string(info.path) << '\n';

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

            std::cout << "  slot " << local.slot << ": " << name << " : " << type_name(type) << "\n";

            _next_slot += 1;
            _max_slot = std::max(_max_slot, _next_slot);

            return local;
        }

        const LocalInfo* find_local(const std::string& name) const
        {
            for (const auto & local : std::views::reverse(_locals))
            {
                auto found = local.names.find(name);

                if (found != local.names.end())
                    return &found->second;
            }

            return nullptr;
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
                [&](const ast::ExprStmt& s) {  },
                [&](const ast::Check& s)    { resolve_check_stmt(s, span); },
            }, stmt.kind);
        }

        void resolve_var_decl(const ast::VarDecl& var, const SourceSpan& span)
        {
            TypeId written_type = EmptyType;
            if (var.type)
                written_type = resolve_type(var.type);

            TypeId type = written_type;

            if (var.init)
            {
                TypeId init_type = resolve_expr(*var.init, written_type);

                if (written_type == EmptyType)
                    type = init_type;
                else
                    expect_assignable(written_type, init_type, var.init->span);
            }
            else if (written_type == EmptyType)
            {
                error(span, "variable '" + var.name + "' needs a type or an initializer");
                type = BaseTypes::Error;
            }

            _output.var_decls[&var] = declare_local(var.name, type, span);
        }

        void resolve_return(const ast::Return& ret, const SourceSpan& span)
        {

        }

        void resolve_if(const ast::If& if_stmt)
        {
            check_stmt_in_own_scope(if_stmt.then_branch);

            if (if_stmt.else_branch)
                check_stmt_in_own_scope(if_stmt.else_branch);
        }


        void resolve_while(const ast::While& while_stmt)
        {
            check_stmt_in_own_scope(while_stmt.body);
        }

        void resolve_for_in(const ast::ForIn& for_stmt, const SourceSpan& span)
        {
            push_locals();

            _output.for_vars[&for_stmt] = declare_local(for_stmt.variable_name, EmptyType, span);
            resolve_stmt(*for_stmt.body);

            pop_locals();
        }

        void resolve_check_stmt(const ast::Check& check, const SourceSpan& span)
        {
            push_locals();

            _output.check_vars[&check] = declare_local(check.variable_name, EmptyType, span);
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

        static bool is_lvalue(const ast::Expr& expr)
        {
            return std::visit(overload{
                [](const ast::Name&)
                {
                    return true;
                },
                [](const ast::Index& index)
                {
                    return is_lvalue(*index.target);
                },
                [](const ast::Member& member)
                {
                    return is_lvalue(*member.target);
                },
                [](const auto&)
                {
                    return false;
                }
            }, expr.kind);
        }

        bool assignable(TypeId to, TypeId from)
        {
            if (to == from)
                return true;

            if (to == BaseTypes::Any)
                return from != BaseTypes::Void;

            if (to == BaseTypes::Float && from == BaseTypes::Int)
                return true;

            if (types().kind(to) == TypeKind::Optional)
                return assignable(types().element(to), from);

            return false;
        }

        void expect_assignable(TypeId to, TypeId from, const SourceSpan& span)
        {
            if (assignable(to, from))
                return;

            error(span, "expected '" + type_name(to) + "', found '" + type_name(from) + "'");
        }


        TypeId resolve_expr(const ast::Expr& expr, TypeId expected_type = EmptyType)
        {
            const SourceSpan& span = expr.span;

            ExprInfo info = std::visit(overload{
            [&](const ast::IntLit&)             { return make_einfo(BaseTypes::Int); },
            [&](const ast::FloatLit&)           { return make_einfo(BaseTypes::Float); },
            [&](const ast::StringLit&)          { return make_einfo(BaseTypes::String); },
            [&](const ast::BoolLit&)            { return make_einfo(BaseTypes::Bool); },
            [&](const ast::ArrayLit& e)         { return resolve_array_expr(e, span, expected_type); },
                    [&](const auto&)            { return make_einfo(BaseTypes::Error); }
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



    }; // class



    ResolveOutput resolve(const ResolverInput& input)
    {
        Resolver resolver{input};
        return resolver.resolve();
    }
}
