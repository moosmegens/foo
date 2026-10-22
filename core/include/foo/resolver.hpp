#ifndef FOO_RESOLVER_HPP
#define FOO_RESOLVER_HPP


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

        // full name is with path aswell, lib::name
        TypeId add_struct(std::string full_name);
        TypeId add_native(std::string full_name);


        TypeKind get_kind(TypeId t) const;
        Id get_meta_id(TypeId t) const;


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
    };

    ResolveOutput resolve(const ResolverInput& input);
}

#endif //FOO_RESOLVER_HPP
