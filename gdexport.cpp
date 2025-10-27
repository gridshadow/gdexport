// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#include "extractinterfacevisitor.hpp"
#include "extractdocvisitor.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"

#include <filesystem>

using namespace clang;

/**
 * Visitor which print classes marked with [[godot::class]]
 *
 */
class ExtractClassNamesVisitor : public RecursiveASTVisitor<ExtractClassNamesVisitor>
{
public:
    ExtractClassNamesVisitor(ASTContext* ctxt) : context(ctxt) { }

    /**
     * Visit C++ class (or structs) and check if marked with [[godot::class]]
     *
     * @param declaration The C++ class declaration
     * @return true
     */
    bool VisitCXXRecordDecl(CXXRecordDecl* declaration)
    {
        if(context->getSourceManager().isInMainFile(declaration->getLocation()))
        {
            for(const auto& attr : declaration->specific_attrs<AnnotateAttr>())
            {
                if(attr->getAnnotation() == "godot::class")
                {
                    llvm::outs() << declaration->getName() << "\n";
                    break;
                }
            }
        }
        return true;
    }

private:
    ASTContext* context;
};

/**
 * Consumer for printing class names of classes marked with [[godot::class]]
 */
class ExtractClassNamesConsumer : public ASTConsumer
{
public:
    ExtractClassNamesConsumer(ASTContext* context) : visitor(context) { }

    virtual void HandleTranslationUnit(ASTContext& context)
    {
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    ExtractClassNamesVisitor visitor;
};

/**
 * Consumer for the AST with a visitor derived from ExtractInterfaceVisitor
 * (either ExtractInterfaceVisitor or ExtractDocVisitor)
 *
 * @tparam VISITOR The type of the visitor for the AST
 */
template<typename VISITOR>
class ExtractInterfaceConsumer : public ASTConsumer
{
public:
    /**
     * Construct consumer to handle the AST
     *
     * @tparam Args Type of extra argumetns to apss to the visitor
     * @param context The AST context (forwarded to the visitor)
     * @param outFile Output stream to write the generated code to (forwarded to the visitor)
     * @param args Extra arguments to pass to the visitor
     */
    template<typename... Args>
    ExtractInterfaceConsumer(ASTContext* context, std::unique_ptr<llvm::raw_pwrite_stream>&& outFile, Args... args)
        : visitor(context, std::move(outFile), args...)
    {
    }

    virtual void HandleTranslationUnit(ASTContext& context)
    {
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    VISITOR visitor;
};

/**
 * Clang plugin for parsing the godot attributes and generating the export code
 */
class GenerateExtensionInterface : public PluginASTAction
{
    // Hack: for some reason if these are just std::string clang crashes!

    /**
     * Specifies the output file to write the generated code to (or empty for automatic name deduction)
     */
    std::optional<std::string> outputFile;

    /**
     * Specifies the output directory to write XML documentation to (or empty to not write documentation)
     *
     */
    std::optional<std::string> doc;

    /**
     * Specifies whether to just extract a list of names (true) of classes marked with the [[godot::class]]
     * attribute (can be used to generate list of XML documentation files which will be generated)
     */
    bool extractClassNames;

public:
    GenerateExtensionInterface() : outputFile(), doc(), extractClassNames(false) {}

    /**
     * Create the consumer for handling the AST
     *
     * @param compiler The compiler instance
     * @param file The file to parse
     * @return Pointer to a ExtractClassNamesConsumer (if extractClassNames is true),
     *         ExtractInterfaceConsumer<ExtractDocVisitor> (if doc is non-empty), or
     *         ExtractInterfaceConsumer<ExtractInterfaceVisitor> otherwise
     */
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& compiler, StringRef file) override
    {
        if(extractClassNames)
        {
            return std::make_unique<ExtractClassNamesConsumer>(&compiler.getASTContext());
        }
        std::unique_ptr<llvm::raw_pwrite_stream> outFile;
        std::string header;
        std::string funcName = "stdout";
        if(file != "-")
        {
            std::filesystem::path path(file.data());
            if(!outputFile)
            {
                header = path.filename();
                path.replace_extension(".gen.cpp");
                outputFile = path.generic_string();
            }
            else
            {
                header = std::filesystem::relative(std::filesystem::absolute(path),
                    std::filesystem::absolute(*outputFile).parent_path());
            }
            funcName = path.stem().generic_string();
            std::replace_if(funcName.begin(), funcName.end(), [](char c)
                {
                    return ((c < '0') || (c > '9'))
                        && ((c < 'a') || (c > 'z'))
                        && ((c < 'A') || (c > 'Z'))
                        && (c != '_');
                }, '_');
        }
        if(outputFile)
        {
            outFile = compiler.createOutputFile(*outputFile, false, true, true);
        }
        if(outFile)
        {
            if(!header.empty())
            {
                *outFile << "#include \"" << header << "\"\n\n";
            }
            *outFile << "#include <godot_cpp/core/class_db.hpp>\n\n";
        }
        auto& traits = compiler.getASTContext().getCommentCommandTraits();
        traits.registerBlockCommand("tutorial");
        traits.registerBlockCommand("experimental");
        if(doc)
        {
            return std::make_unique<ExtractInterfaceConsumer<ExtractDocVisitor>>(
                &compiler.getASTContext(), std::move(outFile), funcName, *doc);
        }
        else
        {
            return std::make_unique<ExtractInterfaceConsumer<ExtractInterfaceVisitor>>(
                &compiler.getASTContext(), std::move(outFile), funcName);
        }
    }

    /**
     * Parse the plugin's argument list
     *
     * @param ci The compiler instance
     * @param args The arguments to parse
     * @return true
     */
    bool ParseArgs(const CompilerInstance& ci, const std::vector<std::string>& args) override
    {
        auto size = args.size();
        for(unsigned int i = 0; i != size; ++i)
        {
            DiagnosticsEngine& diag = ci.getDiagnostics();
            if(args[i] == "-out")
            {
                ++i;
                if(i != size)
                {
                    outputFile = args[i];
                }
                else
                {
                    diag.Report(diag.getCustomDiagID(DiagnosticsEngine::Error,
                        "missing -out argument"));
                    return false;
                }
            }
            else if(args[i] == "-doc")
            {
                ++i;
                if(i != size)
                {
                    doc = args[i];
                }
                else
                {
                    diag.Report(diag.getCustomDiagID(DiagnosticsEngine::Error,
                        "missing -doc argument"));
                    return false;
                }
            }
            else if(args[i] == "-nameonly")
            {
                extractClassNames = true;
            }
        }
        return true;
    }

    PluginASTAction::ActionType getActionType() override
    {
        return PluginASTAction::AddBeforeMainAction;
    }
};

/**
 * Registers the plugin
 */
static FrontendPluginRegistry::Add<GenerateExtensionInterface> X("gdexport", "Export the interface for the GDExtension");
