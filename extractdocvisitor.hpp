// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#ifndef GDEXPORT_EXTRACTDOCVISITOR_HPP
#define GDEXPORT_EXTRACTDOCVISITOR_HPP

#include "extractinterfacevisitor.hpp"
#include "utilities.hpp"

#include "llvm/Support/raw_ostream.h"
#include "clang/AST/Comment.h"

#include <filesystem>

/**
 * Specifies the type of a paragraph of text.
 *
 */
enum class ParagraphType
{
    /**
     * The paragraph is a normal paragraph.
     */
    Normal,
    /**
     * The paragraph is a list item in an unordered list
     */
    List,
    /**
     * The paragraph is verbatim text. Each entry is one line in the text
     */
    VerbatimText,
    /**
     * The paragraph is verbatim GDScript code. Each entry is one line in the code
     */
    GDScript,
    /**
     * The paragraph is verbatim C# code. Each entry is one line in the code
     */
    CSharpCode
};

/**
 * Write the name of the specified code/plain text block
 *
 * @param os The stream to write to
 * @param type The paragraph type (should be a code block)
 * @return The stream
 */
llvm::raw_ostream& operator<<(llvm::raw_ostream& os, ParagraphType type);

/**
 * Stores information about a paragraph of text in the documentation
 *
 */
struct Paragraph
{
    /**
     * Creates an empty paragraph
     *
     * @param type The type of the paragraph
     */
    Paragraph(ParagraphType type = ParagraphType::Normal) : Data(), Type(type) { }

    /**
     * Gets if the paragraph is empty
     *
     * @return true if empty (no items, or all items are empty after trimming); false, otherwise
     */
    bool empty() const;

    /**
     * Push the specified string to the front of the paragraph
     *
     * @param str
     */
    void push_front(const std::string& str);

    /**
     * Sub-spans of text in the paragraph
     */
    std::list<std::string> Data;

    /**
     * The type of the paragraph
     */
    ParagraphType Type;
};

/**
 * Appends text to the paragraph
 *
 * @param para Paragraph to append to
 * @param text Text to append
 * @return The modified paragraph
 */
Paragraph& operator+=(Paragraph& para, const std::string& text);
Paragraph& operator+=(Paragraph& para, const StringRef& text);
Paragraph& operator+=(Paragraph& para, const char* text);

/**
 * Appends an data from another paragraph to the end of the paragraph
 *
 * @param para Paragraph to append to
 * @param text The paragraph to append the data from
 * @return The modified paragraph
 */
Paragraph& operator+=(Paragraph& para, Paragraph&& text);

/**
 * List of paragraphs
 */
typedef std::list<Paragraph> Paragraphs;

/**
 * Appends a paragraph to the list of paragraphs
 *
 * @param paras The list to append to
 * @param para The paragraph to append
 * @return The modified paragrpah list
 */
Paragraphs& operator+=(Paragraphs& paras, const Paragraph& para);
Paragraphs& operator+=(Paragraphs& paras, Paragraph&& para);

/**
 * Specifies if a class/method has a status (deprecated or experimental), and a possible message
 * for the status
 */
struct StatusTag
{
    /**
     * Constructs default "status not present" tag
     */
    StatusTag() : IsTagPresent(false), Message() { }

    /**
     * Specifies if the status is present
     */
    bool IsTagPresent;

    /**
     * Specifies the message for the status
     */
    Paragraph Message;
};

/**
 * Stores information about a tutorial
 *
 */
struct Tutorial
{
    /**
     * Creates a tutorial without title
     *
     * @param url The URL of the tutorial
     */
    Tutorial(const std::string& url) : URL(url), Title() { }

    /**
     * Creates a tutorial with title
     *
     * @param url The URL of the tutorial
     * @param title The title of the tutorial
     */
    Tutorial(const std::string& url, Paragraph&& title) : URL(url), Title(title) { }

    /**
     * The URL to the tutorial
     */
    std::string URL;

    /**
     * The title for the tutorial
     */
    Paragraph Title;
};

/**
 * Specifies documentation details for method/signal arguments and return values
 */
struct Parameters
{
    /**
     * Constructs empty argument documentation
     */
    Parameters() : Name(), Description() { }

    /**
     * Constructs argument documentation with specified description
     *
     * @param desc The description of the argument
     */
    Parameters(Paragraph&& desc) : Name(), Description(desc) { }

    /**
     * Constructs argument documentation with specified description and name
     *
     * @param name The name of the argument
     * @param desc The description of the argument
     */
    Parameters(const std::string& name, Paragraph&& desc) : Name(name), Description(desc) { }

    /**
     * The name of the argument
     */
    std::string Name;

    /**
     * The description of the argument
     */
    Paragraph Description;
};

/**
 * Parsed documentation for an item (class, method, signal, member, etc.)
 *
 */
class ParsedDocumentation
{
public:
    /**
     * The paragraphs for the detailed description
     */
    Paragraphs Detailed;
    /**
     * The paragraph for the brief description (@@brief)
     *
     * Combine with detailed for non-class
     */
    Paragraph Brief;
    /**
     * The paragraphs containing author information (@@author)
     */
    Paragraphs Author;
    /**
     * The paragraphs containing copyright information (@@copyright)
     */
    Paragraphs Copyright;
    /**
     * The paragraphs containing "since" information (@@since)
     */
    Paragraphs Since;
    /**
     * The paragraphs containing version information (@@version)
     */
    Paragraphs Version;
    /**
     * Paragraphs containing pre-conditions for a method
     *
     * Not valid for class
     */
    Paragraphs Preconditions; // Not valid for class!
    /**
     * Paragraphs containing post-conditions for a method
     *
     * Not valid for class
     */
    Paragraphs Postconditions;
    /**
     * Paragraphs containing method return information
     *
     * Not valid for class
     */
    Paragraph ReturnDesc;
    /**
     * Specifies if the item is deprecated (and why)
     */
    StatusTag Deprecated;
    /**
     * Specifies if the item is experimental (and why)
     */
    StatusTag Experimental;
    /**
     * List of tutorials for a class
     *
     * Only valid for class
     */
    std::vector<Tutorial> Tutorials;
    /**
     * List of parameters and descriptions for a method
     *
     * Not valid for class
     */
    std::vector<Parameters> ParameterDescs;
    /**
     * List of possible return values and descriptions for a method
     *
     * Not valid for class
     */
    std::vector<Parameters> ReturnValues;

    /**
     * Creates empty documentation
     */
    ParsedDocumentation() { }

    /**
     * Parses the documentation from the comment
     *
     * @param className The name of the class the comment is for/within
     * @param doc The comment to parse
     * @param traits Traits for doc comments (result of getCommentCommandTraits() from ASTContext)
     */
    ParsedDocumentation(const StringRef& className, comments::FullComment* doc, const comments::CommandTraits& traits);

    /**
     * Write detailed documentation
     *
     * @param os Stream to write to
     * @param printBrief true to write the brief description as part of the detailed; false otherwise
     * @param printFunctionInfo true to write function information (parameters, returns etc.); false otherwise
     * @param indent The amount to indent the output
     * @return The stream
     */
    llvm::raw_ostream& WriteDetailed(llvm::raw_ostream& os, bool printBrief,
        bool printFunctionInfo, std::size_t indent) const;

    /**
     * Write XML attributes for the item in the documentation
     *
     * @param os Stream to write to
     * @return The stream
     */
    llvm::raw_ostream& WriteAttributes(llvm::raw_ostream& os) const;

private:
    void ParseBlock(const StringRef& className, const comments::CommandTraits& traits,
        comments::BlockCommandComment* block, bool& hasBriefTag);
    void ParseVerbatim(const StringRef& command, comments::VerbatimBlockComment* block);
};

/**
 * Visitor for processing both attributes (due to being subclass of ExtractInterfaceVisitor)
 * and documentation comments.
 */
class ExtractDocVisitor : public ExtractInterfaceVisitor
{
public:
    /**
     * Create the visitor
     *
     * @param ctxt The AST context
     * @param outFile Output stream to write the generated code to
     * @param funcName The name of the function to call to export the classes/methods
     * @param outputFolder Folder to write documentation foles to
     */
    ExtractDocVisitor(ASTContext* ctxt, std::unique_ptr<llvm::raw_pwrite_stream>&& outFile,
        const std::string& funcName, const std::string& outputFolder)
        : ExtractInterfaceVisitor(ctxt, std::move(outFile), funcName)
        , root(outputFolder)
        , file()
        , methods()
        , properties()
        , signals()
        , constants()
    {
    }

protected:
    virtual void ProcessStartClass(const StringRef& name, CXXRecordDecl* declaration) override;
    virtual void ProcessEndClass(const StringRef& name, CXXRecordDecl* declaration) override;
    virtual void ProcessConstant(ConstantType type, const StringRef& name, EnumConstantDecl* declaration) override;
    virtual void ProcessPropertyFunc(const std::string& propertyName, CXXMethodDecl* declaration,
        const Property& property, const std::string& function, bool isSetter) override;
    virtual void ProcessProperty(const std::string& propertyName, const Property& property) override;
    virtual void ProcessSignal(const std::string& name, CXXMethodDecl* declaration,
        const std::vector<FunctionArgument>& arguments) override;
    virtual void ProcessMethod(const std::string& name, CXXMethodDecl* declaration, bool isStatic, bool isProperty,
        const std::vector<FunctionArgument>& arguments, const std::optional<GodotType>& returnType) override;

private:
    std::filesystem::path root;
    std::unique_ptr<llvm::raw_fd_stream> file;

    /**
     * Parsed documentation for a constant
     */
    struct ConstantDoc : public ParsedDocumentation
    {
        ConstantDoc(uint64_t value, ConstantType type, const StringRef& className, comments::FullComment* doc,
                const comments::CommandTraits& traits, const StringRef& parentEnum)
            : ParsedDocumentation(className, doc, traits)
            , Value(value)
            , Enum(parentEnum)
            , IsBitfield(type == ConstantType::Bitfield)
        {
        }

        uint64_t Value;
        StringRef Enum;
        bool IsBitfield;
    };

    /**
     * Parsed documentation for a property (member)
     *
     */
    struct PropertyDoc
    {
        std::optional<ParsedDocumentation> Documentation;
        Property Property;
    };

    struct FunctionDoc : public ParsedDocumentation
    {
        FunctionDoc(const std::vector<FunctionArgument>& args, const StringRef& className, comments::FullComment* doc,
                const comments::CommandTraits& traits)
            : ParsedDocumentation(className, doc, traits)
            , Arguments(args)
            , ReturnType()
        {
        }

        std::vector<FunctionArgument> Arguments;
        std::optional<GodotType> ReturnType;
    };

    /**
     * Parsed documentation for a method or signal
     */
    struct MethodDoc : public FunctionDoc
    {
        MethodDoc(const std::vector<FunctionArgument>& args, const StringRef& className, comments::FullComment* doc,
                const comments::CommandTraits& traits, const  std::optional<GodotType>& returnType, const std::string& qualifiers)
            : FunctionDoc(args, className, doc, traits)
            , ReturnType(returnType)
            , Qualifiers(qualifiers)
        {
        }

        std::optional<GodotType> ReturnType;
        std::string Qualifiers;
    };

    std::unordered_map<std::string, MethodDoc> methods;
    std::unordered_map<std::string, PropertyDoc> properties;
    std::unordered_map<std::string, FunctionDoc> signals;
    std::unordered_map<StringRef, ConstantDoc> constants;
};

#endif // GDEXPORT_EXTRACTDOCVISITOR_HPP
