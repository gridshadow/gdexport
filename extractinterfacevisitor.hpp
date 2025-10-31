// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#ifndef GDEXPORT_EXTRACTINTERFACEVISITOR_HPP
#define GDEXPORT_EXTRACTINTERFACEVISITOR_HPP

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceLocation.h"

#include "utilities.hpp"

using namespace clang;

/**
 * Visitor which handles godot attributes and generates code to export the classes/methods
 */
class ExtractInterfaceVisitor : public RecursiveASTVisitor<ExtractInterfaceVisitor>
{
public:
    /**
     * Create the visitor
     *
     * @param ctxt The AST context
     * @param outFile Output stream to write the generated code to
     * @param funcName The name of the function to call to export the classes/methods
     */
    ExtractInterfaceVisitor(ASTContext* ctxt, std::unique_ptr<llvm::raw_pwrite_stream>&& outFile,
        const std::string& funcName);

    ~ExtractInterfaceVisitor();

    bool TraverseNamespaceDecl(NamespaceDecl* declaration);
    bool TraverseCXXRecordDecl(CXXRecordDecl* declaration);
    bool TraverseEnumDecl(EnumDecl* declaration);

    bool VisitEnumConstantDecl(EnumConstantDecl* declaration);
    bool VisitCXXMethodDecl(CXXMethodDecl* declaration);

protected:
    /**
     * Write whitespace for current indent to the output stream
     *
     * @return The output stream
     */
    llvm::raw_ostream& Indent()
    {
        return Indent(writtenNS);
    }

    /**
     * Write whitespace for indent for a function body (current indent + 1) to the output stream
     *
     * @return The output stream
     */
    llvm::raw_ostream& IndentFunc()
    {
        return Indent(writtenNS + 1);
    }

    /**
     * Write whitespace for the specified indent
     *
     * @param amt The amount of indent (each indent = 4 spaces)
     * @return The output stream
     */
    llvm::raw_ostream& Indent(std::size_t amt)
    {
        return outs().indent(4*amt);
    }

    /**
     * Gets the output stream for the file
     *
     * @return The output stream
     */
    llvm::raw_ostream& outs()
    {
        return (output) ? *output : llvm::outs();
    }

    /**
     * Struct for holding information for a property (member) of a godot class
     */
    struct Property
    {
        Property()
            : Getter()
            , Setter()
            , Type()
            , Hint()
            , HintString()
            , Usage("::godot::PROPERTY_USAGE_DEFAULT")
        {
        }

        /**
         * The name of the getter method
         */
        std::string Getter;
        /**
         * The name of the setter method
         */
        std::string Setter;
        /**
         * The type of the member
         */
        GodotType Type;
        /**
         * The fully qualified enum value for the type of the hint for member (godot::PropertyHint value)
         */
        std::string Hint;
        /**
         * The string for the hint
         */
        std::string HintString;
        /**
         * The fully qualified enum value for the usage of the member (godot::PropertyUsageFlags)
         */
        std::string Usage;
        /**
         * The location in the source file for the getter method
         */
        SourceLocation GetterLoc;
        /**
         * The location in the source file for the setter method
         */
        SourceLocation SetterLoc;
    };

    /**
     * Struct for holding information about an argument to a method or signal
     *
     */
    struct FunctionArgument
    {
        /**
         * Creates information about an argument to a method or signal
         *
         * @param name The name of the argument
         * @param type The AST qualified type of the argument (passed to GodotType constructor)
         * @param signature The C++ signature (raw source) for the argument
         * @param variantHint Optional hint for the variant type (passed to GodotType constructor)
         * @param defaultVal Optional default value for the argument
         */
        FunctionArgument(const std::string& name, const QualType& type, const StringRef& signature,
                const std::string& variantHint = "", const std::optional<std::string>& defaultVal = {})
            : Name(name)
            , Type(type, variantHint)
            , Signature(signature)
            , Default(defaultVal)
        {
        }

        /**
         * The name of the argument
         */
        std::string Name;
        /**
         * The type of the argument
         */
        GodotType Type;
        /**
         * The C++ signature (raw source) for the argument
         */
        std::string Signature;
        /**
         * The (optional) default value for the argument (raw source)
         *
         */
        std::optional<std::string> Default;
    };

    /**
     * Specifies the type of constant an enum represents (enum, bitfield, or constants)
     */
    enum class ConstantType
    {
        /**
         * Not a Godot constant type
         */
        None,
        /**
         * A godot enum type
         */
        Enum,
        /**
         * A godot bitfield
         */
        Bitfield,
        /**
         * A set of Godot constants
         */
        Constants
    };

    /**
     * Method called when a class marked with `[[godot::class]]` or `[[godot::tool]]` is encountered in the AST.
     *
     * All other Process* methods called will for methods, members, signals, etc. for this class
     *
     * Adds the fully-qualified class name to the list of classes to register/export and starts the
     * `_bind_methods` method definition
     *
     * Overrides MUST call this base method.
     *
     * @param name The name of the class
     * @param declaration The declaration for the class
     * @param tool Specifies if this class was marked as `[[godot::tool]]`
     */
    virtual void ProcessStartClass(const StringRef& name, CXXRecordDecl* declaration, bool tool);

    /**
     * Method called when reaching the end of a class definition of a class marked [[godot::class]].
     *
     * Will call no more Process* methods for methods, members, signals, etc. for this class
     *
     * Outputs property information, finishes the _bind_method() definition,
     * and exports any signal call function definitions.
     *
     * Overrides MUST call this base method.
     *
     * @param name The name of the class
     * @param declaration The declaration for the class
     */
    virtual void ProcessEndClass(const StringRef& name, CXXRecordDecl* declaration);

    /**
     * Method called when a `[[godot::group]]` or `[[godot::subgroup]]` attribute is encountered.
     *
     * Writes the necessary exports for defining the group (after writing export information for
     * any previously processed properties)
     *
     * @param name The name of the group
     * @param prefix The prefix required for members to be part of the group
     * @param subgroup true if a subgroup; false if a group
     */
    virtual void ProcessGroup(const std::string& name, const std::string& prefix, bool subgroup);

    /**
     * Method called when encountering a method marked with `[[godot::signal]]`.
     *
     * Writes the necessary exports for defining the signal, and sets up a the necessary data for
     * the signal to be used on ProcessEndClass to define the body of the signal call function.
     *
     * Overrides MUST call this base method.
     *
     * @param name The name of the method
     * @param declaration The declaration of the method
     * @param arguments The arguments to the method
     */
    virtual void ProcessSignal(const std::string& name, CXXMethodDecl* declaration,
        const std::vector<FunctionArgument>& arguments);

    /**
     * Method called when encountering a method marked with `[[godot::getter]]` or `[[godot::setter]]`.
     *
     * Calls ProcessMethod
     *
     * Overrides SHOULD call this base method.
     *
     * @param name The name of the property
     * @param declaration The declaration of the method
     * @param property Information about the property (may not be complete at this point - missing
     *                 either getter or setter)
     * @param function The name of the method
     * @param isSetter true if the setter function for the property, or false if the getter
     */
    virtual void ProcessPropertyFunc(const std::string& propertyName, CXXMethodDecl* declaration,
        const Property& property, const std::string& function, bool isSetter);

    /**
     * Method called when the property information is output (during call to ProcessEndClass); at
     * this point the property will be fully complete (has getter, and optionally setter).
     *
     * Outputs property information
     *
     * Overrides SHOULD call this base method.
     *
     * @param propertyName The name of the property
     * @param property Information about the property
     */
    virtual void ProcessProperty(const std::string& propertyName, const Property& property);

    /**
     * Method called when encountering a method marked with `[[godot::getter]]`, `[[godot::setter]]`
     * or `[[godot::method]]`.
     *
     * Outputs the export information for the property
     *
     * Overrides SHOULD call this base method.
     *
     * @param name The name of the method
     * @param declaration The declaration of the method
     * @param isStatic true if the method is a static method; false if an instance method
     * @param isProperty true if the function is a property getter/setter; false if normal method
     * @param arguments The arguments to the method
     * @param returnType Optional information about the return type of the method
     */
    virtual void ProcessMethod(const std::string& name, CXXMethodDecl* declaration, bool isStatic, bool isProperty,
            const std::vector<FunctionArgument>& arguments, const std::optional<GodotType>& returnType);

    /**
     * Method called when encountering an enum value declaration of an enum marked
     * `[[godot::enum]]`, `[[godot::bitfield]]` or `[[godot::constants]]`
     *
     *
     * @param type The type of the enum
     * @param name The name of the enum constant
     * @param declaration The declaration of the enum constant
     */
    virtual void ProcessConstant(ConstantType type, const StringRef& name, EnumConstantDecl* declaration);

    /**
     * Gets the current AST context
     */
    ASTContext& Context() { return *context; }

    /**
     * Gets the name of the current `[[godot::class]]` class.
     */
    const StringRef& Class() { return currentClass; }

private:
    /**
     * Method called when encountering a method marked with `[[godot::getter]]`, `[[godot::setter]]`
     * or `[[godot::method]]`.
     *
     * Deduces the arguments and return type and calls ProcessMethod.
     *
     * @param name The name of the method
     * @param declaration The declaration of the method
     * @param isStatic true if the method is a static method; false if an instance method
     * @param isProperty true if the function is a property getter/setter; false if normal method
     */
    void ProcessMethod(const std::string& name, CXXMethodDecl* declaration, bool isStatic, bool isProperty = false);

    /**
     * Write the export information for the current properties
     */
    void WriteProperties();

    /**
     * Write the definition of the signal functions
     */
    void WriteSignals();

    /**
     * Stores information about a signal
     */
    struct SignalData
    {
        /**
         * Creates information about a signal
         *
         * @param signal The name of the signal
         * @param loc  The source location of the method declaration for the signal
         */
        SignalData(const std::string& signal, const SourceLocation& loc)
            : Name(signal), Location(loc), Signature(), ArgNames(), ErrorReturn(false)
        {
        }

        /**
         * The name of the signal
         */
        std::string Name;
        /**
         * The source location of the method declaration for the signal
         */
        SourceLocation Location;
        /**
         * The argument signature of the signal
         */
        std::string Signature;
        /**
         * List of arguments names of the arguments of the signature
         */
        std::vector<std::string> ArgNames;
        /**
         * true if the signal method returns godot::Error, false if void return
         */
        bool ErrorReturn;
    };

    ASTContext* context;
    std::vector<std::pair<std::string, bool>> classes;
    std::unordered_map<std::string, Property> properties;
    std::vector<SignalData> signals;
    std::vector<StringRef> currentNamespace;
    std::size_t writtenNS;
    StringRef currentClass;
    bool inClass;
    ConstantType inEnum;
    std::unique_ptr<llvm::raw_pwrite_stream> output;
    std::string docFolder;
    std::string funcName;
};

#endif // GDEXPORT_EXTRACTINTERFACEVISITOR_HPP
