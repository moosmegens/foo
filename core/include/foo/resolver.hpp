#ifndef FOO_RESOLVER_HPP
#define FOO_RESOLVER_HPP

#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

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

    constexpr TypeId EmptyType = -1;


    enum class TypeKind
    {
        Error, Void, Bool, Int, Float, String, Any, None,

        Range, Array, Map, Optional, Struct, Native
    };

    constexpr std::array BaseKinds =
    {
        TypeKind::Error,
        TypeKind::Void,
        TypeKind::Bool,
        TypeKind::Int,
        TypeKind::Float,
        TypeKind::String,
        TypeKind::Any,
        TypeKind::None,
    };

    constexpr TypeId base_id(TypeKind k)
    {
        for (size_t i = 0; i < BaseKinds.size(); ++i)
            if (BaseKinds[i] == k)
                return static_cast<TypeId>(i);

        throw "not a base kind";
    }

    namespace BaseTypes
    {
        constexpr TypeId Error  = base_id(TypeKind::Error);
        constexpr TypeId Void   = base_id(TypeKind::Void);
        constexpr TypeId Bool   = base_id(TypeKind::Bool);
        constexpr TypeId Int    = base_id(TypeKind::Int);
        constexpr TypeId Float  = base_id(TypeKind::Float);
        constexpr TypeId String = base_id(TypeKind::String);
        constexpr TypeId Any    = base_id(TypeKind::Any);
        constexpr TypeId None   = base_id(TypeKind::None);
    }


    class TypeTable
    {
    public:
        TypeTable();


        TypeId array_of(TypeId e)           { return intern(TypeKind::Array, e); }
        TypeId map_of(TypeId k, TypeId v)   { return intern(TypeKind::Map, k, v); }
        TypeId optional_of(TypeId e)        { return intern(TypeKind::Optional, e); }
        TypeId range_of(TypeId e)           { return intern(TypeKind::Range, e); }
        TypeId struct_of(StructId s)        { return intern(TypeKind::Struct, s); }
        TypeId native_of(NativeTypeId n)    { return intern(TypeKind::Native, n); }


        TypeKind kind(TypeId t) const           { return at(t).kind; }

        TypeId element(TypeId t) const          { return at(t).a; }
        TypeId key(TypeId t) const              { return at(t).a; }
        TypeId value(TypeId t) const            { return at(t).b; }
        StructId struct_id(TypeId t) const      { return at(t).a; }
        NativeTypeId native_id(TypeId t) const  { return at(t).a; }

    private:
        struct Type
        {
            TypeKind kind;
            Id a = EmptyId;
            Id b = EmptyId;

            auto operator<=>(const Type&) const = default;
        };

        const Type& at(TypeId t) const
        {
            assert(static_cast<size_t>(t) < _types.size());
            return _types[static_cast<size_t>(t)];
        }

        TypeId intern(TypeKind kind, Id a = EmptyId, Id b = EmptyId);

        // indexed by TypeID.
        std::vector<Type> _types;

        std::map<Type, TypeId> _ids;
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
        
        // for last param
        bool variadic = false;

        // for methods
        bool self_by_ref = false;

        TypeId ret = BaseTypes::Void;
    };


    using NativeResolver = std::function<std::optional<FunctionSig>(TypeTable&, TypeId receiver)>;


    class NativeRegistry
    {
    public:
        struct Match
        {
            NativeFunctionId id;
            FunctionSig sig;
        };

        TypeId type(TypeTable& types, std::string_view full_name);

        TypeId find_type(TypeTable& types, std::string_view full_name) const;

        const std::vector<std::string>& type_names() const { return _type_names; }


        NativeFunctionId add_function(std::string name, FunctionSig sig);

        NativeFunctionId add_static_function(std::string name, TypeId target, FunctionSig sig);

        NativeFunctionId add_static_function(std::string name, NativeResolver resolve);

        NativeFunctionId add_method(std::string name, TypeId target, FunctionSig sig);

        NativeFunctionId add_method(std::string name, NativeResolver resolve);


        std::optional<Match> find_function(TypeTable& types, std::string_view name) const
        {
            return find(Kind::Function, types, EmptyType, name);
        }

        std::optional<Match> find_method(TypeTable& types, TypeId receiver, std::string_view name) const
        {
            return find(Kind::Method, types, receiver, name);
        }

        std::optional<Match> find_static_function(TypeTable& types, TypeId target, std::string_view name) const
        {
            return find(Kind::Static, types, target, name);
        }

    private:
        enum class Kind { Function, Static, Method };
        struct Entry
        {
            std::string name;
            Kind kind;
            NativeResolver resolve;
        };

        NativeFunctionId add(std::string name, Kind kind, NativeResolver resolve);

        std::optional<Match> find(
            Kind kind, TypeTable& types, TypeId receiver, std::string_view name) const;

        std::vector<std::string> _type_names;
        std::unordered_map<std::string, NativeTypeId> _type_ids;
        std::vector<Entry> _entries;
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
        NativeRegistry natives;
    };





    struct DeclInfo
    {
        // name with path:
        //  io::filesystem::dir
        Path path;

        bool pub = true;

        int file_index = -1;
        SourceSpan span = {};
    };

    struct FieldInfo
    {
        std::string name;
        TypeId type = EmptyType;
        bool pub = true;
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

        int slot_count = 0;
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

    struct ResolveWarning
    {
        std::string message;
        int file_index;
        SourceSpan span;
    };

    struct ResolveOutput
    {
        std::vector<ResolveError> errors;
        std::vector<ResolveWarning> warnings;

        NativeRegistry natives;

        TypeTable type_table;
        std::vector<StructInfo> structs;
        std::vector<FunctionInfo> functions;

        std::unordered_map<const ast::StructLit*, std::vector<int>> struct_lit_fields;

        // Call and StaticCall
        std::unordered_map<const void*, FunctionSig> native_sigs;

        std::unordered_map<const ast::Expr*, ExprInfo> exprs;
        std::unordered_map<const ast::VarDecl*, LocalInfo> var_decls;
        std::unordered_map<const ast::ForIn*, LocalInfo> for_vars;
        std::unordered_map<const ast::Check*, LocalInfo> check_vars;

        std::unordered_map<const void*, int> to_file_index;
    };

    ResolveOutput resolve(const ResolverInput& input);


    std::string type_name(TypeId type, const ResolveOutput& output);

    std::string to_string(const ResolveOutput& o, const FunctionSig& sig);
    std::string to_string(const ResolveOutput& o, const StructInfo& _struct, bool nerd = false);
    std::string to_string(const ResolveOutput& o, const FunctionInfo& _function, bool nerd = false);
}

#endif //FOO_RESOLVER_HPP
