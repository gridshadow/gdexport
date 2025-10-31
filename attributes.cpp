// SPDX-FileCopyrightText: 2025 Gridshadows <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/IR/Attributes.h"

/**
 * Defines and registers handlers (classes) for custom attributes in the godot namespace
 * for handling by clang.
 */

using namespace clang;

/**
 * Const expression to evaluate string length of a C-style string
 *
 * @tparam N The length of the C-style sring array (deduced from argument)
 * @param Str The C-style string with terminating null
 * @return The length of the C-style sring array (deduced from argument)
 */
template<std::size_t N> constexpr int64_t ConstStrLen(const char (&Str)[N]) { return N-1; }

/**
 * Defines, and registers, the class for managing a custom attribute for a C++ functions
 * which can take required or optional arguments (first argument must be a string)
 *
 * @param NAME The name of the attribute, within the godot namespace
 * @param REQ The number of required arguments to the attribute
 * @param OPT The number of optional arguments to the attribute
 * @param NAME_PREFIX Specifies an (optional) prefix to strip of the function name to generate the
 *                    first argument of not specified (or empty string to not process)
 */
#define DefineFunctionAttrInfo(NAME, REQ, OPT, NAME_PREFIX) \
    namespace \
    { \
        struct NAME##AttrInfo : public ParsedAttrInfo \
        { \
            NAME##AttrInfo() \
            { \
                NumArgs = REQ; \
                OptArgs = OPT; \
                static constexpr Spelling s[] = { {ParsedAttr::AS_GNU, "godot_" #NAME}, \
                                                  {ParsedAttr::AS_C23, "godot_" #NAME}, \
                                                  {ParsedAttr::AS_CXX11, "godot_" #NAME}, \
                                                  {ParsedAttr::AS_CXX11, "godot::" #NAME} }; \
                Spellings = s; \
            } \
            \
            bool diagAppertainsToDecl(Sema& s, const ParsedAttr& attr, const Decl* d) const override \
            { \
                if(!isa<CXXMethodDecl>(d)) \
                { \
                    s.Diag(attr.getLoc(), diag::warn_attribute_wrong_decl_type) \
                        << attr << attr.isRegularKeywordAttribute() << ExpectedFunction; \
                    return false; \
                } \
                return true; \
            } \
            \
            AttrHandling handleDeclAttribute(Sema& s, Decl* d, const ParsedAttr& attr) const override \
            { \
                if(!d->getDeclContext()->isRecord()) \
                { \
                    unsigned id = s.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                        "'godot::" #NAME "' attribute only allowed at class/struct scope"); \
                    s.Diag(attr.getLoc(), id); \
                    return AttributeNotApplied; \
                } \
                if constexpr(REQ+OPT > 0) \
                { \
                    unsigned int index = 0; \
                    unsigned int argCount = attr.getNumArgs(); \
                    StringLiteral* name = nullptr; \
                    SmallVector<Expr*, REQ+OPT> args; \
                    if(index < argCount) \
                    { \
                        auto* arg = attr.getArgAsExpr(index); \
                        name = dyn_cast<StringLiteral>(arg->IgnoreParenCasts()); \
                        if(name) \
                        { \
                            args.push_back(arg); \
                            ++index; \
                        } \
                    } \
                    if(!name) \
                    { \
                        auto* func = dyn_cast<CXXMethodDecl>(d); \
                        auto name = func->getName(); \
                        if constexpr(ConstStrLen(NAME_PREFIX) > 0) \
                        { \
                            if(name.starts_with_insensitive(NAME_PREFIX "_")) \
                            { \
                                name = name.substr(ConstStrLen(NAME_PREFIX)+1); \
                            } \
                            else if(name.starts_with_insensitive(NAME_PREFIX)) \
                            { \
                                name = name.substr(ConstStrLen(NAME_PREFIX)); \
                            } \
                        } \
                        StringLiteral* literal = StringLiteral::Create(s.Context, name, \
                            StringLiteralKind::Ordinary, false, QualType{}, func->getNameInfo().getLoc()); \
                        args.push_back(literal); \
                    } \
                    for(unsigned int i = 0; (i != REQ+OPT-1) && (index < argCount); ++i, ++index) \
                    { \
                        auto* arg = attr.getArgAsExpr(index); \
                        args.push_back(arg); \
                    } \
                    if(index < argCount) \
                    { \
                        unsigned id = s.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                            "Incorrect arguments to 'godot::" #NAME "'"); \
                        s.Diag(attr.getLoc(), id); \
                        return AttributeNotApplied; \
                    } \
                    d->addAttr(AnnotateAttr::Create(s.Context, "godot::" #NAME, args.data(), \
                        args.size(), attr.getRange())); \
                } \
                else \
                { \
                    d->addAttr(AnnotateAttr::Create(s.Context, "godot::" #NAME, nullptr, 0, attr.getRange())); \
                } \
                return AttributeApplied; \
            } \
            \
            bool diagAppertainsToStmt(Sema&, const ParsedAttr&, const Stmt*) const override \
            { \
                return false; \
            } \
        };\
    }\
    static ParsedAttrInfoRegistry::Add<NAME##AttrInfo> Godot##NAME("godot_" #NAME, "")

/**
 * Defines, and registers, the class for managing a custom attribute for a C++ functions
 * which takes no arguments
 *
 * @param NAME The name of the attribute, within the godot namespace
 */
#define DefineFunctionAttrInfoNoArgs(NAME) DefineFunctionAttrInfo(NAME, 0, 0, "")

/**
 * Defines, and registers, the class for managing a custom attribute for a C++ type definitions
 * (class, enum, etc)
 *
 * @param NAME The name of the attribute, within the godot namespace
 * @param TYPE The clang AST type denoting the C++ type (for example CXXRecordDecl or EnumDecl)
 * @param MUST_BE_SUB Specifies if the defined type must be an inner type of another class
 */
#define DefineTypeAttrInfo(NAME, TYPE, MUST_BE_SUB) \
    namespace \
    { \
        struct NAME##AttrInfo : public ParsedAttrInfo \
        { \
            NAME##AttrInfo() \
            { \
                static constexpr Spelling s[] = { {ParsedAttr::AS_GNU, "godot_" #NAME}, \
                                                {ParsedAttr::AS_C23, "godot_" #NAME}, \
                                                {ParsedAttr::AS_CXX11, "godot_" #NAME}, \
                                                {ParsedAttr::AS_CXX11, "godot::" #NAME} }; \
                Spellings = s; \
            } \
            \
            bool diagAppertainsToDecl(Sema& s, const ParsedAttr& attr, const Decl* d) const override \
            { \
                if(!isa<TYPE>(d)) \
                { \
                    s.Diag(attr.getLoc(), diag::warn_attribute_wrong_decl_type) \
                        << attr << attr.isRegularKeywordAttribute() << ExpectedTypeOrNamespace; \
                    return false; \
                } \
                return true; \
            } \
            \
            AttrHandling handleDeclAttribute(Sema& s, Decl* d, const ParsedAttr& attr) const override \
            { \
                if constexpr(MUST_BE_SUB) \
                { \
                    if(!d->getDeclContext()->isRecord()) \
                    { \
                        unsigned id = s.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                            "'godot::" #NAME "' attribute only allowed at class/struct scope"); \
                        s.Diag(attr.getLoc(), id); \
                        return AttributeNotApplied; \
                    } \
                } \
                else \
                { \
                    if(d->getDeclContext()->isRecord()) \
                    { \
                        unsigned id = s.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, \
                            "'godot::" #NAME "' attribute only allowed at namespace/root scope"); \
                        s.Diag(attr.getLoc(), id); \
                        return AttributeNotApplied; \
                    } \
                } \
                d->addAttr(AnnotateAttr::Create(s.Context, "godot::" #NAME, nullptr, 0, attr.getRange())); \
                return AttributeApplied; \
            } \
            \
            bool diagAppertainsToStmt(Sema&, const ParsedAttr&, const Stmt*) const override \
            { \
                return false; \
            } \
        }; \
    } \
    static ParsedAttrInfoRegistry::Add<NAME##AttrInfo> Godot##NAME("godot_" #NAME, "")

/**
 * Defines the godot::method attribute for defining methods of a Godot class to export
 */
DefineFunctionAttrInfoNoArgs(method);

/**
 * Defines the godot::signal attribute for defining signals from a Godot class to export
 */
DefineFunctionAttrInfoNoArgs(signal);

/**
 * Defines the godot::getter attribute for defining the getter for a Godot class member to export
 */
DefineFunctionAttrInfo(getter, 0, 3, "get");

/**
 * Defines the godot::setter attribute for defining the setter for a Godot class member to export
 */
DefineFunctionAttrInfo(setter, 0, 3, "set");

/**
 * Defines the godot::group attribute for defining the a group for Godot members
 */
DefineFunctionAttrInfo(group, 1, 1, "");

/**
 * Defines the godot::group attribute for defining the a subgroup for Godot members
 */
DefineFunctionAttrInfo(subgroup, 1, 1, "");

/**
 * Defines the godot::tool attribute for defining a Godot class for use as a "tool"
 */
DefineTypeAttrInfo(tool, CXXRecordDecl, false);

/**
 * Defines the godot::class attribute for defining a Godot class
 */
DefineTypeAttrInfo(class, CXXRecordDecl, false);

/**
 * Defines the godot::enum attribute for defining a Godot enumeration
 */
DefineTypeAttrInfo(enum, EnumDecl, true);

/**
 * Defines the godot::enum attribute for defining a Godot bitfield
 */
DefineTypeAttrInfo(bitfield, EnumDecl, true);

/**
 * Defines the godot::enum attribute for defining a set of Godot constants
 */
DefineTypeAttrInfo(constants, EnumDecl, true);

// TODO: Add support for groups, subgroups
