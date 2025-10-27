// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#include "extractinterfacevisitor.hpp"

#include "utilities.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"

#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/classes/global_constants.hpp>

#include <sstream>

// TODO: Check if method/properties/signals are inside an exported class
using namespace clang;

ExtractInterfaceVisitor::ExtractInterfaceVisitor(ASTContext* ctxt,
    std::unique_ptr<llvm::raw_pwrite_stream>&& outFile, const std::string& func)
    : context(ctxt)
    , classes()
    , properties()
    , signals()
    , currentNamespace()
    , writtenNS(0)
    , currentClass("")
    , inClass(false)
    , inEnum(ConstantType::None)
    , output(std::move(outFile))
    , funcName(func)
{
}

ExtractInterfaceVisitor::~ExtractInterfaceVisitor()
{
    if(!classes.empty())
    {
        outs() << "// Export: initialize_" << funcName << " ====================\n"
            "void initialize_" << funcName << "()\n{";
        for(const auto& cls : classes)
        {
            outs() << "    GDREGISTER_RUNTIME_CLASS(" << cls << ");\n";
        }
        outs() << "}\n";
    }
}

bool ExtractInterfaceVisitor::TraverseNamespaceDecl(NamespaceDecl* declaration)
{
    if(context->getSourceManager().isInMainFile(declaration->getLocation()))
    {
        currentNamespace.push_back(declaration->getName());
        auto result = RecursiveASTVisitor<ExtractInterfaceVisitor>::TraverseNamespaceDecl(declaration);
        currentNamespace.pop_back();
        for(; writtenNS > currentNamespace.size(); --writtenNS)
        {
            Indent(writtenNS - 1) << "}\n";
        }
        return result;
    }
    return true;
}

bool ExtractInterfaceVisitor::TraverseCXXRecordDecl(CXXRecordDecl* declaration)
{
    if(context->getSourceManager().isInMainFile(declaration->getLocation()))
    {
        inClass = false;
        bool popClass = false;
        for(const auto& attr : declaration->specific_attrs<AnnotateAttr>())
        {
            if(attr->getAnnotation() == "godot::class")
            {
                if(currentClass.empty())
                {
                    currentClass = declaration->getName();
                    inClass = true;
                    popClass = true;
                    ProcessStartClass(declaration->getName(), declaration);
                }
                break;
            }
        }
        auto result = RecursiveASTVisitor<ExtractInterfaceVisitor>::TraverseCXXRecordDecl(declaration);
        if(popClass)
        {
            ProcessEndClass(declaration->getName(), declaration);
            inClass = false;
            currentClass = "";
        }
        return result;
    }
    return true;
}

bool ExtractInterfaceVisitor::TraverseEnumDecl(EnumDecl* declaration)
{
    if(!currentClass.empty() && inClass)
    {
        for(const auto& attr : declaration->specific_attrs<AnnotateAttr>())
        {
            auto annotation = attr->getAnnotation();
            if(annotation == "godot::enum")
            {
                inEnum = ConstantType::Enum;
                break;
            }
            else if(annotation == "godot::bitfield")
            {
                inEnum = ConstantType::Bitfield;
                break;
            }
            else if(annotation == "godot::constants")
            {
                inEnum = ConstantType::Constants;
                break;
            }
        }
        auto result = RecursiveASTVisitor<ExtractInterfaceVisitor>::TraverseEnumDecl(declaration);
        inEnum = ConstantType::None;
        return result;
    }
    return true;
}

bool ExtractInterfaceVisitor::VisitEnumConstantDecl(EnumConstantDecl* declaration)
{
    if(inEnum != ConstantType::None)
    {
        ProcessConstant(inEnum, declaration->getName(), declaration);
    }
    return true;
}

bool ExtractInterfaceVisitor::VisitCXXMethodDecl(CXXMethodDecl* declaration)
{
    if(!currentClass.empty() && inClass)
    {
        for(const auto& attr : declaration->specific_attrs<AnnotateAttr>())
        {
            auto nameInfo = declaration->getDeclName();
            std::string name;
            auto annotation = attr->getAnnotation();
            switch(nameInfo.getNameKind())
            {
                case DeclarationName::Identifier:
                    name = nameInfo.getAsIdentifierInfo()->getName();
                    break;
                case DeclarationName::CXXOperatorName:
                    if(annotation == "godot::method")
                    {
                        // TODO
                        auto op = nameInfo.getCXXOverloadedOperator();
                        if(op == OverloadedOperatorKind::OO_Plus)
                        {
                            name = "operator +";
                        }
                    }
                    else
                    {
                        GenerateError(*context, attr->getLocation(), DiagnosticsEngine::Error,
                            "%0 is attached to a C++ operator overload, which is invalid for this annotation", annotation);
                        return false;
                    }
                    break;
                default:
                    GenerateError(*context, attr->getLocation(), DiagnosticsEngine::Error,
                        "%0 is not attached to a class, function, or C++ operator overload", annotation);
                    return false;
            }
            // std::string name(declaration->getName().data());
            if((annotation == "godot::group") || (annotation == "godot::subgroup"))
            {
                bool parsed = false;
                auto it = attr->args_begin();
                auto end = attr->args_end();
                std::string groupName = ParseString(it, end, "", parsed);
                if(!parsed || groupName.empty())
                {
                    GenerateError(*context, attr->getLocation(), DiagnosticsEngine::Error,
                        "%0 does not have a group name", annotation);
                }
                std::string prefix = ParseString(it, end, "", parsed);
                ProcessGroup(groupName, prefix, annotation == "godot::subgroup");
            }
            else if(annotation == "godot::signal")
            {
                auto loc = declaration->getLocation();
                std::vector<FunctionArgument> args;
                args.reserve(declaration->param_size());
                auto end = declaration->param_end();
                for(auto it = declaration->param_begin(); it != end; ++it)
                {
                    auto paramName = (*it)->getQualifiedNameAsString();
                    if(paramName.empty())
                    {
                        GenerateError(*context, loc, DiagnosticsEngine::Warning,
                            "Signal '%0' has an argument with no name; generated code may be invalid", name);
                        paramName = "arg" + std::to_string(args.size());
                    }
                    args.emplace_back(paramName, (*it)->getType(), GetRawSource(*context, *it));
                }
                ProcessSignal(name, declaration, args);
            }
            else
            {
                Property* property = nullptr;
                if(annotation == "godot::getter")
                {
                    auto it = attr->args_begin();
                    auto end = attr->args_end();
                    bool parsed = false;
                    std::string propertyName = ParseString(it, end, name, parsed);
                    if(!parsed)
                    {
                        GenerateError(*context, declaration->getLocation(), DiagnosticsEngine::Warning,
                            "Getter does not have a property name, or it was not deduced correctly");
                    }
                    property = &properties[propertyName];
                    property->Getter = name;
                    property->GetterLoc = declaration->getLocation();
                    auto type = ParseEnum(*context, it, end, "", "property type", propertyName,
                        godot::Variant::VARIANT_MAX - 1);
                    property->Type.Parse(declaration->getReturnType(), type);
                    property->Usage = ParseBitfield(*context, it, end, "::godot::PROPERTY_USAGE_DEFAULT",
                        "property usage", propertyName);

                    if(GetUnderlyingType(declaration->getReturnType())->isVoidType())
                    {
                        GenerateError(*context, property->GetterLoc, DiagnosticsEngine::Warning,
                            "Getter for property '%0' should have non-void return", propertyName);
                    }
                    if(declaration->param_size() > 0)
                    {
                        GenerateError(*context, property->GetterLoc, DiagnosticsEngine::Warning,
                            "Getter for property '%0' should take no arguments", propertyName);
                    }
                    ProcessPropertyFunc(propertyName, declaration, *property, name, false);
                }
                else if(annotation == "godot::setter")
                {
                    auto it = attr->args_begin();
                    auto end = attr->args_end();
                    bool found = false;
                    std::string propertyName = ParseString(it, end, name, found);
                    if(!found)
                    {
                        GenerateError(*context, declaration->getLocation(), DiagnosticsEngine::Warning,
                            "Setter does not have a property name, or it was not deduced correctly");
                    }
                    property = &properties[propertyName];
                    property->Setter = name;
                    property->SetterLoc = declaration->getLocation();
                    property->Hint = ParseEnum(*context, it, end, "::godot::PROPERTY_HINT_NONE",
                        "property type", propertyName, godot::PROPERTY_HINT_MAX - 1);
                    property->HintString = ParseString(it, end, "", found);

                    if(!GetUnderlyingType(declaration->getReturnType())->isVoidType())
                    {
                        GenerateError(*context, property->SetterLoc, DiagnosticsEngine::Warning,
                            "Setter for property '%0' should have void return", propertyName);
                    }
                    if(declaration->param_size() != 1)
                    {
                        GenerateError(*context, property->SetterLoc, DiagnosticsEngine::Warning,
                            "Setter for property '%0' should take exactly one argument", propertyName);
                    }
                    ProcessPropertyFunc(propertyName, declaration, *property, name, true);
                }
                else if(annotation == "godot::method")
                {
                    ProcessMethod(name, declaration, declaration->isStatic());
                }
            }
        }
    }
    return true;
}

void ExtractInterfaceVisitor::ProcessStartClass(const StringRef& className, CXXRecordDecl*)
{
    llvm::outs() << className.str() << "\n";
    std::ostringstream fullyQualified;
    if(!currentNamespace.empty())
    {
        for(const auto& ns : currentNamespace)
        {
            fullyQualified << "::" << ns.data();
        }
        fullyQualified << "::";
    }
    fullyQualified << className.data();
    classes.push_back(fullyQualified.str());
    for(; writtenNS < currentNamespace.size(); ++writtenNS)
    {
        Indent() << "namespace " << currentNamespace[writtenNS] << '\n';
        Indent() << "{\n";
    }
    Indent() << "void " << currentClass << "::_bind_methods()\n";
    Indent() << "{\n";
}

void ExtractInterfaceVisitor::ProcessEndClass(const StringRef& name, CXXRecordDecl* declaration)
{
    WriteProperties();
    Indent() << "}\n";
    WriteSignals();
}

void ExtractInterfaceVisitor::ProcessGroup(const std::string& name, const std::string& prefix, bool subgroup)
{
    WriteProperties();

    outs() << '\n';
    IndentFunc() << "ADD_";
    if(subgroup)
    {
        outs() << "SUB";
    }
    outs() << "GROUP(\"" << name << "\", \"" << prefix << "\");\n";
}

void ExtractInterfaceVisitor::ProcessSignal(const std::string& name, CXXMethodDecl* declaration,
        const std::vector<FunctionArgument>& arguments)
{
    auto& signal = signals.emplace_back(name, declaration->getLocation());
    IndentFunc() << "ADD_SIGNAL(MethodInfo(\"" << name << '\"';
    signal.ArgNames.reserve(arguments.size());

    for(const auto& param : arguments)
    {
        outs() << ", PropertyInfo(" << param.Type.VariantType << ", \"" << param.Name << "\")";
        if(!signal.Signature.empty())
        {
            signal.Signature += ", ";
        }
        signal.Signature += param.Signature;
        // TODO: if paramName is empty!
        signal.ArgNames.push_back(param.Name);
    }
    outs() << "));\n";
    auto returnType = GetUnderlyingType(declaration->getReturnType());
    if(CXXRecordDecl* cls = returnType->getAsCXXRecordDecl())
    {
        if(IsInGodotNamespace(cls) && (cls->getName() == "Error"))
        {
            signal.ErrorReturn = true;
        }
    }
    if(!signal.ErrorReturn && !returnType->isVoidType())
    {
        GenerateError(*context, signal.Location, DiagnosticsEngine::Error,
            "Signal '%0' must be void return or have godot::Error return type", name);
    }
}

void ExtractInterfaceVisitor::ProcessPropertyFunc(const std::string&,
    CXXMethodDecl* declaration, const Property&, const std::string& function, bool)
{
    ProcessMethod(function, declaration, false, true);
}

void ExtractInterfaceVisitor::ProcessProperty(const std::string& propertyName, const Property& property)
{
    IndentFunc() << "ADD_PROPERTY(::godot::PropertyInfo(" << property.Type.VariantType << ", \""
        << propertyName << "\", " << property.Hint << ", \"" << property.HintString << "\", "
        << property.Usage << "), \"" << property.Setter << "\", \"" << property.Getter << "\");\n";
}

void ExtractInterfaceVisitor::ProcessMethod(const std::string& name, CXXMethodDecl* declaration,
    bool isStatic, bool isProperty)
{
    std::vector<FunctionArgument> args;
    args.reserve(declaration->param_size());
    auto end = declaration->param_end();
    for(auto it = declaration->param_begin(); it != end; ++it)
    {
        auto paramName = (*it)->getQualifiedNameAsString();
        if(paramName.empty())
        {
            paramName = "arg" + std::to_string(args.size());
        }
        std::optional<std::string> defaultVal;
        auto* defaultArg = (*it)->getDefaultArg();
        if(defaultArg)
        {
            defaultVal = GetRawSource(*context, defaultArg);
        }
        args.emplace_back(paramName, (*it)->getType(), GetRawSource(*context, *it), "", defaultVal);
    }
    std::optional<GodotType> returnType;
    auto type = GetUnderlyingType(declaration->getReturnType());
    if(!type->isVoidType())
    {
        returnType = GodotType{declaration->getReturnType()};
    }
    ProcessMethod(name, declaration, isStatic, isProperty, args, returnType);
}

void ExtractInterfaceVisitor::ProcessMethod(const std::string& name, CXXMethodDecl* declaration,
    bool isStatic, bool isProperty, const std::vector<FunctionArgument>& arguments,
    const std::optional<GodotType>& returnType)
{
    IndentFunc();
    if(isStatic)
    {
        outs() << "::godot::ClassDB::bind_static_method(\"" << currentClass
            << "\", D_METHOD(\"" << name << '\"';
    }
    else
    {
        outs() << "::godot::ClassDB::bind_method(D_METHOD(\""
            << name << '\"';
    }
    for(const auto& param : arguments)
    {
        outs() << ", \"" << param.Name << "\"";
    }
    outs() << "), &" << currentClass << "::" << name;
    for(const auto& param : arguments)
    {
        if(param.Default)
        {
            outs() << ", DEFVAL(" << *param.Default << ")";
        }
    }
    outs() << ");\n";
}

void ExtractInterfaceVisitor::ProcessConstant(ConstantType type, const StringRef& name, EnumConstantDecl*)
{
    switch(type)
    {
    case ConstantType::Enum:
        IndentFunc() << "BIND_ENUM_CONSTANT(" << name << ")\n";
        break;
    case ConstantType::Bitfield:
        IndentFunc() << "BIND_BITFIELD_FLAG(" << name << ")\n";
        break;
    case ConstantType::Constants:
        IndentFunc() << "BIND_CONSTANT(" << name << ")\n";
        break;
    case ConstantType::None:
    default:
        break;
    }
}

void ExtractInterfaceVisitor::WriteProperties()
{
    if(!properties.empty())
    {
        outs() << '\n';
        for(const auto& [name, property] : properties)
        {
            if(property.Getter.empty())
            {
                GenerateError(*context, property.SetterLoc,
                    "Property '%0' does not have a getter defined", name);
            }
            ProcessProperty(name, property);
        }
        properties.clear();
    }
}

void ExtractInterfaceVisitor::WriteSignals()
{
    for(const auto& signal : signals)
    {
        const char* returnType = (signal.ErrorReturn) ? "::godot::Error" : "void";
        outs() << "\n";
        Indent() << returnType << ' ' << currentClass << "::" << signal.Name
            << '(' << signal.Signature << ")\n";
        Indent() << "{\n";
        IndentFunc();
        if(signal.ErrorReturn)
        {
            outs() << "return ";
        }
        outs() << "emit_signal(\"" << signal.Name << '\"';
        for(const auto& arg : signal.ArgNames)
        {
            outs() << ", " << arg;
        }
        outs() << ");\n";
        Indent() << "}\n";
    }
    signals.clear();
}
