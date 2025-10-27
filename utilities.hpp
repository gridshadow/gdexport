// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#ifndef GDEXPORT_UTILITIES_HPP
#define GDEXPORT_UTILITIES_HPP

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;

namespace std
{
    /**
     * Implement hashing for clang's StringRef class (so can be used as in unordered set/map)
     */
    template<>
    struct hash<StringRef>
    {
        /**
         * Get the hash for the StringRef
         *
         * @param str The StringRef to get has for
         * @return The hash
         */
        std::size_t operator()(const StringRef& str) const
        {
            return hash_value(str);
        }
    };
}

/**
 * Gets the raw source from the source file for the specified expression in the AST
 *
 * @tparam T The type of the expression
 * @param context The current AST context to get the source from
 * @param expr The expression to get the source for
 * @return The source for the expression
 */
template<typename T>
std::string GetRawSource(ASTContext& context, T* expr)
{
    auto& sm = context.getSourceManager();
    SourceLocation begin(expr->getBeginLoc());
    SourceLocation e(expr->getEndLoc());
    SourceLocation end(Lexer::getLocForEndOfToken(e, 0, sm, context.getLangOpts()));
    return (end < begin)
        ? ""
        : std::string(sm.getCharacterData(begin), sm.getCharacterData(end) - sm.getCharacterData(begin));
}

/**
 * Generate an error message in clang with a source location
 *
 * @tparam N The length of the C-string to use for the Clang error message format expression
 * @tparam Args Types of the arguments for the Clang error message format expression
 * @param context The current AST context where the error occurred
 * @param loc The source location where the error occurred
 * @param lvl The level of the error (Warning, Error, etc.)
 * @param msg A format expression, which can take arguments, for the error message
 * @param args The arguments to pass to the format expression
 */
template<unsigned N, typename... Args>
void GenerateError(ASTContext& context, const SourceLocation& loc, DiagnosticsEngine::Level lvl, const char(&msg)[N], Args... args)
{
    DiagnosticsEngine& diag = context.getDiagnostics();
    (diag.Report(loc, diag.getCustomDiagID(lvl, msg)) << ... << args);
}

/**
 * Generate an error message in clang without a source location
 *
 * @tparam N The length of the C-string to use for the Clang error message format expression
 * @tparam Args Types of the arguments for the Clang error message format expression
 * @param context The current AST context where the error occurred
 * @param lvl The level of the error (Warning, Error, etc.)
 * @param msg A format expression, which can take arguments, for the error message
 * @param args The arguments to pass to the format expression
 */
template<unsigned N, typename... Args>
void GenerateError(ASTContext& context, DiagnosticsEngine::Level lvl, const char(&msg)[N], Args... args)
{
    DiagnosticsEngine& diag = context.getDiagnostics();
    (diag.Report(diag.getCustomDiagID(lvl, msg)) << ... << args);
}

/**
 * Generate an error message, with level "Error", in clang with a source location
 *
 * @tparam N The length of the C-string to use for the Clang error message format expression
 * @tparam Args Types of the arguments for the Clang error message format expression
 * @param context The current AST context where the error occurred
 * @param loc The source location where the error occurred
 * @param msg A format expression, which can take arguments, for the error message
 * @param args The arguments to pass to the format expression
 */
template<unsigned N, typename... Args>
void GenerateError(ASTContext& context, const SourceLocation& loc, const char(&msg)[N], Args... args)
{
    DiagnosticsEngine& diag = context.getDiagnostics();
    (diag.Report(loc, diag.getCustomDiagID(DiagnosticsEngine::Error, msg)) << ... << args);
}

/**
 * Generate an error message, with level "Error", in clang without a source location
 *
 * @tparam N The length of the C-string to use for the Clang error message format expression
 * @tparam Args Types of the arguments for the Clang error message format expression
 * @param context The current AST context where the error occurred
 * @param msg A format expression, which can take arguments, for the error message
 * @param args The arguments to pass to the format expression
 */
template<unsigned N, typename... Args>
void GenerateError(ASTContext& context, const char(&msg)[N], Args... args)
{
    DiagnosticsEngine& diag = context.getDiagnostics();
    (diag.Report(diag.getCustomDiagID(DiagnosticsEngine::Error, msg)) << ... << args);
}

/**
 * Parse a string literal from the next expression in an array of expressions, and increment the
 * current position in the array
 *
 * @param current Pointer to the current expression in the array. Increment on success
 * @param end Pointer to one-past-end of the array
 * @param defaultValue The default value to return if no next item (or next item is not a string)
 * @param found true if the next item was a string literalt, false otherwise
 * @return The string literal, or defaultValue if no string literal
 */
std::string ParseString(Expr**& current, Expr** end, const std::string& defaultValue, bool& found);

/**
 * Parse an enum constant (or integer constant) from an expression.
 *
 * @param expr The expression to attempt to parse as an enum constant
 * @param defaultValue The default value to return if expression is not an enum constant
 * @param parsed true if the next expression was an enum constant, false otherwise
 * @param maxValue The maximum value the integer constant can be
 * @return The enum literal, or defaultValue if no enum literal
 */
std::string ParseEnum(Expr* expr, const std::string& defaultValue, bool& parsed, uint64_t maxValue = UINT64_MAX);

/**
 * Parse a nenum constant (or integer constant) from an array of expressions, and increment the
 * current position in the array
 *
 * @param context The current AST context
 * @param current Pointer to the current expression in the array. Increment on success
 * @param end Pointer to one-past-end of the array
 * @param defaultValue The default value to return if expression is not an enum constant
 * @param argument Name of the argument being parsed (used in generated error message if parse fails)
 * @param propertyName Name of the property being parsed (used in generated error message if parse fails)
 * @param maxValue The maximum value the integer constant can be
 * @return The enum literal, or defaultValue if no enum literal
 */
std::string ParseEnum(ASTContext& context, Expr**& current, Expr** end, const std::string& defaultValue,
    const StringRef& argument, const std::string& propertyName, uint64_t maxValue = UINT64_MAX);

/**
 * Parse an enum constant, integer constant, or bitwise OR of these from an expression.
 *
 * @param expr The expression to attempt to parse as an enum constant
 * @param defaultValue The default value to return if expression is not an enum constant
 * @param parsed true if the next expression was an enum constant, false otherwise
 * @param maxValue The maximum value the integer constant can be
 * @return The enum literal, or defaultValue if no enum literal
 */
std::string ParseBitfield(Expr* current, const std::string& defaultValue, bool& parsed);

/**
 * Parse an enum constant, integer constant, or bitwise OR of these from an expression, and increment the
 * current position in the array
 *
 * @param context The current AST context
 * @param current Pointer to the current expression in the array. Increment on success
 * @param end Pointer to one-past-end of the array
 * @param defaultValue The default value to return if expression is not an enum constant
 * @param argument Name of the argument being parsed (used in generated error message if parse fails)
 * @param propertyName Name of the property being parsed (used in generated error message if parse fails)
 * @param maxValue The maximum value the integer constant can be
 * @return The enum literal, or defaultValue if no enum literal
 */
std::string ParseBitfield(ASTContext& context, Expr**& current, Expr** end, const std::string& defaultValue,
    const StringRef& argument, const std::string& propertyName);

/**
 * Structure to gold information about a parsed type compatible with Godot: The Godot "Variant" name,
 * the underlying godot class or built-in type name, and optionally the name of the enum if an enum type
 *
 */
struct GodotType
{
    /**
     * Create an empty type (Nil type in Godot)
     */
    GodotType()
        : VariantType("::godot::Variant::NIL")
        , TypeName("nil")
        , EnumName()
    {
    }

    /**
     * Create a type by parsing the qualified C++ type from the AST
     *
     * @param type The fully qualified name from the clang AST
     * @param expandTemplate true to expand template parameters; false otherwise
     *                       (Godot only supports one level of template arguments)
     */
    GodotType(const QualType& type, bool expandTemplate) : GodotType() { Parse(type, expandTemplate); }

    /**
     * Create a type by parsing the qualified C++ type from the AST
     *
     * @param type The fully qualified name from the clang AST
     * @param variantHint Hint to the actual godot Variant type (don't need to deduce)
     * @param expandTemplate true to expand template parameters; false otherwise
     *                       (Godot only supports one level of template arguments)
     */
    GodotType(const QualType& type, const std::string& variantHint = "", bool expandTemplate = true)
        : GodotType()
    {
        Parse(type, variantHint, expandTemplate);
    }

    /**
     * Update this with the result of parsing the qualified C++ type from the AST
     *
     * @param type The fully qualified name from the clang AST
     * @param expandTemplate true to expand template parameters; false otherwise
     *                       (Godot only supports one level of template arguments)
     */
    GodotType& Parse(const QualType& type, bool expandTemplate) { return Parse(type, "", expandTemplate); }

    /**
     * Update this with the result of parsing the qualified C++ type from the AST
     *
     * @param type The fully qualified name from the clang AST
     * @param variantHint Hint to the actual godot Variant type (don't need to deduce)
     * @param expandTemplate true to expand template parameters; false otherwise
     *                       (Godot only supports one level of template arguments)
     */
    GodotType& Parse(const QualType& type, const std::string& variantHint = "", bool expandTemplate = true);

    /**
     * The Godot "Variant" name (i.e., a value from godot::Variant::Type) as a fully-qualified C++ name
     */
    std::string VariantType;

    /**
     * The Godot class name (or name of subclass) or builtin type (float, int, bool) name.
     *
     * Will be "int" for a enum type.
     */
    std::string TypeName;

    /**
     * If type is an enum, specifies the enum name (TypeName will be `int`)
     */
    std::string EnumName;
};

/**
 * Attempts to deduce the Godot Variant type by recursing through the class hierarchy of the specified class
 *
 * @param cls The clang AST for the C++ class (or struct)
 * @return The Godot "Variant" name (i.e., a value from godot::Variant::Type) as a fully-qualified C++ name, or
 *         empty string if unable to find a a parent Godot type
 */
StringRef FindGodotTypeInInheritance(const CXXRecordDecl* cls);

/**
 * Modified copy of Decl::isInStdNamespace and DeclContext::isStdNamespace
 * to detect if the specified declaration is within the godot namespace
 *
 * @param declaration Declaration to check in godot namespace
 * @return true if in godot namespace; false otherwise
 */
bool IsInGodotNamespace(const Decl* declaration);

/**
 * Get the underlying type for a qualified type; i.e., strip all typedefs, and
 * syntax sugar (const, reference, etc.)
 *
 * @todo Do we need to keep "looping" removing referencing and desugaring?
 *
 * @param type The qualified C++ type from the clang AST
 * @return The actual type after stripping sugar
 */
inline const Type* GetUnderlyingType(const QualType& type)
{
    return type.getNonReferenceType().getTypePtr()->getUnqualifiedDesugaredType();
}

#endif // GDEXPORT_UTILITIES_HPP
