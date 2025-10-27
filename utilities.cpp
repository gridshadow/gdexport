// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#include "utilities.hpp"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"

#include <unordered_map>

using namespace clang;

namespace clang
{
    /**
     * Gets the begin iterator of a TemplateArgumentList.
     *
     * Utility function to allow TemplateArgumentList to be passed to a foreach loop.
     *
     * @param list The TemplateArgumentList to get begin iterator for
     * @return The begin iterator for the TemplateArgumentList
     */
    const TemplateArgument* begin(const TemplateArgumentList& list) { return list.data(); }

    /**
     * Gets the end iterator of a TemplateArgumentList.
     *
     * Utility function to allow TemplateArgumentList to be passed to a foreach loop.
     *
     * @param list The TemplateArgumentList to get end iterator for
     * @return The end iterator for the TemplateArgumentList
     */
    const TemplateArgument* end(const TemplateArgumentList& list) { return list.data()+list.size(); }
}

std::string ParseString(Expr**& current, Expr** end, const std::string& defaultValue, bool& found)
{
    found = false;
    StringLiteral* str = nullptr;
    if(current != end)
    {
        str = dyn_cast<StringLiteral>((*current)->IgnoreParenCasts());
        ++current;
    }
    found = str;
    return (str)
        ? std::string{ str->getString().data() }
        : defaultValue;
}

std::string ParseEnum(Expr* expr, const std::string& defaultValue, bool& parsed, uint64_t maxValue)
{
    parsed = true;
    auto* enumConstant = expr->getEnumConstantDecl();
    if(enumConstant)
    {
        return "::" + enumConstant->getQualifiedNameAsString();
    }
    IntegerLiteral* value = dyn_cast<IntegerLiteral>(expr);
    if(value)
    {
        auto intValue = value->getValue();
        if(intValue.isNonNegative())
        {
            return std::to_string(value->getValue().getLimitedValue(maxValue));
        }
    }
    parsed = false;
    return defaultValue;
}

std::string ParseEnum(ASTContext& context, Expr**& current, Expr** end, const std::string& defaultValue,
    const StringRef& argument, const std::string& propertyName, uint64_t maxValue)
{
    if(current != end)
    {
        bool parsed = false;
        auto result = ParseEnum((*current)->IgnoreParenCasts(), defaultValue, parsed, maxValue);
        if(!parsed)
        {
            GenerateError(context, (*current)->getBeginLoc(), DiagnosticsEngine::Warning,
                "Unable to parse %0 of property '%1'", argument, propertyName);
        }
        ++current;
        return result;
    }
    return defaultValue;
}

std::string ParseBitfield(Expr* current, const std::string& defaultValue, bool& parsed)
{
    BinaryOperator* op = dyn_cast<BinaryOperator>(current);
    if(op && (op->getOpcode() == BinaryOperator::Opcode::BO_Or))
    {
        bool parsed1 = false;
        bool parsed2 = false;
        auto result = ParseBitfield(op->getLHS()->IgnoreParenCasts(), defaultValue, parsed1) + "|"
            + ParseBitfield(op->getRHS()->IgnoreParenCasts(), defaultValue, parsed2);
        parsed = parsed1 && parsed2;
        return result;
    }
    else
    {
        return ParseEnum(current, "0", parsed);
    }
}

std::string ParseBitfield(ASTContext& context, Expr**& current, Expr** end, const std::string& defaultValue,
    const StringRef& argument, const std::string& propertyName)
{
    if(current != end)
    {
        bool parsed = false;
        auto result = ParseBitfield((*current)->IgnoreParenCasts(), defaultValue, parsed);
        if(!parsed)
        {
            GenerateError(context, (*current)->getBeginLoc(), DiagnosticsEngine::Warning,
                "Unable to parse %0 of property '%1'", argument, propertyName);
        }
        ++current;
        return result;
    }
    return defaultValue;
}

GodotType& GodotType::Parse(const QualType& type, const std::string& variantHint, bool expandTemplate)
{
    auto actualType = GetUnderlyingType(type);
    auto ptr = dyn_cast<PointerType>(actualType);

    if(!ptr)
    {
        auto builtin = dyn_cast<BuiltinType>(actualType);
        if(builtin)
        {
            switch(builtin->getKind())
            {
            case BuiltinType::Kind::Bool:
                VariantType = "::godot::Variant::BOOL";
                TypeName = "bool";
                return *this;
            case BuiltinType::Kind::Void:
                VariantType = "::godot::Variant::NIL";
                TypeName = "nil";
                return *this;
            default:
                if(builtin->isInteger())
                {
                    VariantType = "::godot::Variant::INT";
                    TypeName = "int";
                    return *this;
                }
                else if(builtin->isFloatingPoint())
                {
                    VariantType = "::godot::Variant::FLOAT";
                    TypeName = "float";
                    return *this;
                }
                VariantType = "::godot::Variant::NIL";
                TypeName = "nil";
                return *this;
            }
        }
        auto enumType = dyn_cast_if_present<EnumDecl>(actualType->getAsTagDecl());
        if(enumType)
        {
            VariantType = "::godot::Variant::INT";
            TypeName = "int";
            EnumName = std::string{enumType->getName()};
            return *this;
        }
    }
    else
    {
        actualType = GetUnderlyingType(ptr->getPointeeType());
    }
    auto cls = actualType->getAsCXXRecordDecl();
    TypeName = std::string{cls->getName()};
    // Hack, typed array and dictionary (which we assume is in the Godot namespace) need to
    // pretend to be normal Godot array and dictionary
    if((TypeName == "TypedArray") || (TypeName == "TypedDictionary"))
    {
        TypeName = TypeName.substr(5);
    }
    VariantType = variantHint;
    if(VariantType.empty())
    {
        VariantType = FindGodotTypeInInheritance(cls);
        if(VariantType.empty())
        {
            VariantType = (ptr) ? "::godot::Variant::OBJECT" : "::godot::Variant::NIL";
        }
    }
    auto* templates = dyn_cast<ClassTemplateSpecializationDecl>(cls);
    if(expandTemplate && templates)
    {
        auto& targs = templates->getTemplateArgs();
        if(targs.size() > 0)
        {
            auto prefix = '[';
            for(const auto& arg : targs)
            {
                if(arg.getKind() == TemplateArgument::ArgKind::Type)
                {
                    // We only want to parse the type, not worry about the Godot variant enum value
                    GodotType type(arg.getAsType(), "::godot::Variant::NIL", false);
                    TypeName += prefix + type.TypeName;
                    prefix = ',';
                }
                else if(arg.getKind() == TemplateArgument::ArgKind::Null)
                {
                    (TypeName += prefix) += "void";
                    prefix = ',';
                }
            }
            if(prefix != '[')
            {
                TypeName += ']';
            }
        }
    }
    return *this;
}

StringRef FindGodotTypeInInheritance(const CXXRecordDecl* cls)
{
    std::unordered_map<StringRef, StringRef> g_godotTypes{
        { StringRef{"String"}, StringRef{"::godot::Variant::STRING"} },
        { StringRef{"Vector2"}, StringRef{"::godot::Variant::VECTOR2"} },
        { StringRef{"Vector2i"}, StringRef{"::godot::Variant::VECTOR2I"} },
        { StringRef{"Rect2"}, StringRef{"::godot::Variant::RECT2"} },
        { StringRef{"Rect2i"}, StringRef{"::godot::Variant::RECT2I"} },
        { StringRef{"Vector3"}, StringRef{"::godot::Variant::VECTOR3"} },
        { StringRef{"Vector3i"}, StringRef{"::godot::Variant::VECTOR3I"} },
        { StringRef{"Transform2D"}, StringRef{"::godot::Variant::TRANSFORM2D"} },
        { StringRef{"Vector4"}, StringRef{"::godot::Variant::VECTOR4"} },
        { StringRef{"Vector4i"}, StringRef{"::godot::Variant::VECTOR4I"} },
        { StringRef{"Plane"}, StringRef{"::godot::Variant::PLANE"} },
        { StringRef{"Quaternion"}, StringRef{"::godot::Variant::QUATERNION"} },
        { StringRef{"AABB"}, StringRef{"::godot::Variant::AABB"} },
        { StringRef{"Basis"}, StringRef{"::godot::Variant::BASIS"} },
        { StringRef{"Transform3D"}, StringRef{"::godot::Variant::TRANSFORM3D"} },
        { StringRef{"Projection"}, StringRef{"::godot::Variant::PROJECTION"} },
        { StringRef{"Color"}, StringRef{"::godot::Variant::COLOR"} },
        { StringRef{"StringName"}, StringRef{"::godot::Variant::STRING_NAME"} },
        { StringRef{"NodePath"}, StringRef{"::godot::Variant::NODE_PATH"} },
        { StringRef{"RID"}, StringRef{"::godot::Variant::RID"} },
        { StringRef{"Object"}, StringRef{"::godot::Variant::OBJECT"} },
        { StringRef{"Callable"}, StringRef{"::godot::Variant::CALLABLE"} },
        { StringRef{"Signal"}, StringRef{"::godot::Variant::SIGNAL"} },
        { StringRef{"TypedDictionary"}, StringRef{"::godot::Variant::DICTIONARY"} },
        { StringRef{"Dictionary"}, StringRef{"::godot::Variant::DICTIONARY"} },
        { StringRef{"Array"}, StringRef{"::godot::Variant::ARRAY"} },
        { StringRef{"TypedArray"}, StringRef{"::godot::Variant::ARRAY"} },
        { StringRef{"PackedByteArray"}, StringRef{"::godot::Variant::PACKED_BYTE_ARRAY"} },
        { StringRef{"PackedInt32Array"}, StringRef{"::godot::Variant::PACKED_INT32_ARRAY"} },
        { StringRef{"PackedInt64Array"}, StringRef{"::godot::Variant::PACKED_INT64_ARRAY"} },
        { StringRef{"PackedFloat32Array"}, StringRef{"::godot::Variant::PACKED_FLOAT32_ARRAY"} },
        { StringRef{"PackedFloat64Array"}, StringRef{"::godot::Variant::PACKED_FLOAT64_ARRAY"} },
        { StringRef{"PackedStringArray"}, StringRef{"::godot::Variant::PACKED_STRING_ARRAY"} },
        { StringRef{"PackedVector2Array"}, StringRef{"::godot::Variant::PACKED_VECTOR2_ARRAY"} },
        { StringRef{"PackedVector3Array"}, StringRef{"::godot::Variant::PACKED_VECTOR3_ARRAY"} },
        { StringRef{"PackedColorArray"}, StringRef{"::godot::Variant::PACKED_COLOR_ARRAY"} },
        { StringRef{"PackedVector4Array"}, StringRef{"::godot::Variant::PACKED_VECTOR4_ARRAY"} }
    };
    if(cls)
    {
        if(IsInGodotNamespace(cls))
        {
            auto it = g_godotTypes.find(cls->getName());
            if(it != g_godotTypes.end())
            {
                return it->second;
            }
        }
        for(const auto& base : cls->bases())
        {
            auto type = GetUnderlyingType(base.getType());
            StringRef result = FindGodotTypeInInheritance(type->getAsCXXRecordDecl());
            if(!result.empty())
            {
                return result;
            }
        }
    }
    return "";
}

bool IsInGodotNamespace(const Decl* declaration)
{
    const DeclContext* dc = declaration->getDeclContext();
    if(!dc || !dc->isNamespace())
    {
        return false;
    }

    const auto* ns = cast<NamespaceDecl>(dc);
    if(!dc->getParent()->getRedeclContext()->isTranslationUnit())
    {
        return false;
    }

    return ns->getName() == "godot";
}
