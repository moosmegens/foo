#ifndef FOO_RESOLVER_HPP
#define FOO_RESOLVER_HPP


#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <foo/ast.hpp>

#include <foo/source_file.hpp>


namespace foo
{
    using Id = int;

    constexpr Id EmptyId    = -1;

    using TypeId            = Id;

    using StructId          = Id;
    using NativeTypeId      = Id;

    using FunctionId        = Id;
    using NativeFunctionId  = Id;

    using SlotId            = Id;


    enum class TypeKind
    {
        Error, Void, Bool, Int, Float, String, Any, Range,
        Array, Map, Optional, Struct, Native,

        COUNT
    };

    namespace BaseTypes
    {
        constexpr TypeId Error  = 0;
        constexpr TypeId Void   = 1;
        constexpr TypeId Bool   = 2;
        constexpr TypeId Int    = 3;
        constexpr TypeId Float  = 4;
        constexpr TypeId String = 5;
        constexpr TypeId Any    = 6;
    }

    constexpr TypeId EmptyType = -1;


    class TypeTable
    {
    public:
        TypeTable();


        TypeId array_of(TypeId element);
        TypeId map_of(TypeId key, TypeId value);
        TypeId optional_of(TypeId element);
        TypeId range_of(TypeId element);

        // full name is with path aswell, lib::name
        TypeId add_struct(std::string full_name);
        TypeId add_native(std::string full_name);


        TypeKind get_kind(TypeId t) const   { return _types[static_cast<size_t>(t)].kind; }
        Id get_meta_id(TypeId t) const      { return _types[static_cast<size_t>(t)].meta_id; }


        std::string to_string(TypeKind t) const;
        std::string to_string(TypeId t) const;


        TypeId element(TypeId t) const { return _types[static_cast<size_t>(t)].a; }
        TypeId key(TypeId t) const     { return _types[static_cast<size_t>(t)].a; }
        TypeId value(TypeId t) const   { return _types[static_cast<size_t>(t)].b; }

    private:
        struct Type
        {
            TypeKind kind;
            TypeId a = EmptyId;
            TypeId b = EmptyId;

            Id meta_id = -1;
        };

        TypeId lookup(Type t);

        // indexed by TypeID.
        std::vector<Type> _types;

        // indexed by meta.
        std::vector<std::string> _struct_names;
        std::vector<std::string> _native_names;

        std::map<std::tuple<int, TypeId, TypeId>, TypeId> _type_to_id;
    };


    struct ParamSig
    {
        TypeId type = EmptyType;
        bool by_ref = false;
    };

    struct FunctionSig
    {
        // when method:
        //  params is without self.
        std::vector<ParamSig> params;

        // for methods
        bool self_by_ref = false;

        TypeId ret = BaseTypes::Void;
    };


    // this can generate functions for specific types, like generating a .push(T) for T[]
    using SignatureGenerator = std::function<FunctionSig(TypeTable&, TypeId receiver)>;

    // if this function should bind to a type, so you can add .clear() to T[], [K, V], string
    using TypePredicate = std::function<bool(const TypeTable&, TypeId)>;

    class NativeRegistry
    {
    public:
        NativeFunctionId add_function(
            std::string_view name,
            FunctionSig sig)
        {
            return add_function_entry({
                std::string{name},
                {},
                [sig = std::move(sig)](TypeTable&, TypeId) {
                    return sig;
                }
            });
        }

        NativeFunctionId add_method(std::string_view name, TypePredicate receiver, SignatureGenerator sig)
        {
            return add_function_entry({
                std::string{name},
                std::move(receiver),
                std::move(sig)
            });
        }

        NativeFunctionId find_function(std::string_view name) const
        {
            return find_last_function([&](const FunctionEntry& e)
            {
                return !e.receiver && e.name == name;
            });
        }

        NativeFunctionId find_method(const TypeTable& types, TypeId receiver, std::string_view name) const
        {
            return find_last_function([&](const FunctionEntry& e) {
                return e.receiver &&
                       e.name == name &&
                       (*e.receiver)(types, receiver);
            });
        }

        FunctionSig signature(NativeFunctionId id, TypeTable& types, TypeId receiver = EmptyType) const
        {
            return _function_entries[static_cast<size_t>(id)].sig(types, receiver);
        }


    private:
        struct FunctionEntry
        {
            std::string name;

            std::optional<TypePredicate> receiver;
            SignatureGenerator sig;
        };

        struct TypeInfo
        {
            std::string name;
        };


        NativeFunctionId add_function_entry(FunctionEntry e)
        {
            _function_entries.push_back(std::move(e));
            return static_cast<NativeFunctionId>(_function_entries.size() - 1);
        }

        template <class Pred>
        NativeFunctionId find_last_function(Pred pred) const
        {
            for (auto i = static_cast<NativeFunctionId>(_function_entries.size()); i-- > 0;)
                if (pred(_function_entries[static_cast<size_t>(i)]))
                    return i;

            return EmptyId;
        }


        std::vector<FunctionEntry> _function_entries;
    };


    struct ResolverFile
    {
        std::string path;
        int source_id = -1;

        std::vector<ast::DeclPtr> decls;
        std::vector<ast::ImportDecl> imports;
    };

    struct ResolverInput
    {
        std::vector<SourceFile> sources;
        std::vector<ResolverFile> files;

        TypeTable types{};
        const NativeRegistry* natives = nullptr;
    };





    struct DeclInfo
    {
        // name with path:
        //  io::filesystem::dir
        std::string name;

        int file_index = -1;
        SourceSpan span = {};
    };

    struct FieldInfo
    {
        std::string name;
        TypeId type = EmptyType;
    };

    struct StructInfo : DeclInfo
    {
        const ast::StructDecl* decl = nullptr;

        TypeId type = EmptyType;

        std::vector<FieldInfo> fields;
        std::unordered_map<std::string, FunctionId> methods;
    };

    struct FunctionInfo : DeclInfo
    {
        const ast::FnDecl* decl = nullptr;

        TypeId owner_type = EmptyType; // for methods
        bool is_method = false;

        FunctionSig sig;
    };

    enum class RefKind
    {
        None,
        Local,          // index = slot
        Field,          // index = field index
        Function,       // index = FunctionId (function or static)
        Method,         // index = FunctionId
        Native,         // index = NativeId
        NativeMethod,   // index = NativeId
        StructLit       // StructLit: index = StructId
    };

    struct ExprInfo
    {
        TypeId type = EmptyType;
        RefKind ref = RefKind::None;

        Id index = -1;
    };

    struct LocalInfo
    {
        TypeId type = EmptyType;

        SlotId slot = -1;
    };


    struct ResolveError
    {
        std::string message;
        int file_index;
        SourceSpan span;
    };

    struct ResolveOutput
    {
        std::vector<ResolveError> errors;

        TypeTable type_table;
        std::vector<StructInfo> structs;
        std::vector<FunctionInfo> functions;

        std::unordered_map<const ast::Expr*, ExprInfo> exprs;
        std::unordered_map<const ast::VarDecl*, LocalInfo> var_decls;
        std::unordered_map<const ast::ForIn*, LocalInfo> for_vars;
        std::unordered_map<const ast::Check*, LocalInfo> check_vars;

        std::unordered_map<const void*, int> to_file_index;
    };

    ResolveOutput resolve(const ResolverInput& input);
}

#endif //FOO_RESOLVER_HPP
