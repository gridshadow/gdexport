// SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
// SPDX-License-Identifier: Zlib

#include "extractdocvisitor.hpp"
#include "utilities.hpp"

#include <filesystem>

// TODO: Ensure all text from comments is passed through EscapeXML

#define ADMONITION(paragraph, title, color, symbol) \
    paragraph.push_front("[color=" color u8"]\u00A0" symbol u8"\u00A0\u00A0[b]" title  ":[/b][/color] ")

// =============================================================================
// Utilities
// =============================================================================

/**
 * Structure to support XML escaping
 */
struct XMLEscape
{
    XMLEscape(const StringRef& str) : Str(str) { }
    const StringRef& Str;
};

/**
 * IO manipulator to XML escape a string in an output stream
 *
 * @param str The string to XML escape
 * @return Utility object to pass to IO stream to perform writing
 */
inline XMLEscape EscapeXML(const StringRef& str)
{
    return XMLEscape(str);
}

/**
 * Performs the EscapeXML functor to write to the stream
 *
 * @param os Stream to write to
 * @param escape The result of EscapeXML
 * @return The stream
 */
llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const XMLEscape& escape)
{
    static constexpr const char* g_special = "\"'<>&";
    StringRef::size_type start = 0;
    for(auto pos = escape.Str.find_first_of(g_special); pos != StringRef::npos; pos = escape.Str.find_first_of(g_special, start))
    {
        os << escape.Str.substr(start, pos-start);
        switch(escape.Str[pos])
        {
            case g_special[0]:
                os << "&quot;";
                break;
            case g_special[1]:
                os << "&apos;";
                break;
            case g_special[2]:
                os << "&lt;";
                break;
            case g_special[3]:
                os << "&gt;";
                break;
            case g_special[4]:
                os << "&amp;";
                break;
        }
        start = pos+1;
    }
    return os << escape.Str.substr(start);
}

/**
 * Checks if the two specified paragraphs are code blocks with different code
 * types (GDScript and C#)
 *
 * @param current The current paragraph
 * @param next The next paragraph
 * @return true if both are code blocks and are different languages; false otherwise
 */
constexpr bool IsDifferentLanguage(ParagraphType current, ParagraphType next)
{
    return ((current == ParagraphType::GDScript) && (next == ParagraphType::CSharpCode))
        || ((current == ParagraphType::CSharpCode) && (next == ParagraphType::GDScript));
}

/**
 * Gets a segment of BBCode to write a paragraph title
 *
 * @param title The title
 * @return The BB code
 */
std::string Title(const std::string& title)
{
    return "[b]" + title + ":[/b] ";
}

/**
 * Writes a paragraph as a single line to the XML documentation file
 *
 * @param stream The stream for the documentation file
 * @param para The paragraph to write
 * @param indent The amount (number of "tabs") to indent the text by
 * @param prefix Prefix to output ONLY if there is data to output in the paragraph
 * @return true if the paragraph was written (had data); false, otherwise
 */
bool WriteSingleLine(llvm::raw_ostream& stream, const Paragraph& para, std::size_t indent, const std::string& prefix = "")
{
    bool written = false;
    auto end = para.Data.end();
    auto it = para.Data.begin();
    if(it != end)
    {
        --end;
        StringRef last = StringRef{*end}.rtrim();
        while((it != end) && last.empty())
        {
            --end;
            last = StringRef{*end}.rtrim();
        }
        for(; it != end; ++it)
        {
            StringRef str = *it;
            if(str.size() > 0)
            {
                if(!written)
                {
                    stream.indent(indent) << EscapeXML(prefix);
                    str = str.ltrim();
                }
                stream << EscapeXML(str);
                written = true;
            }
        }
        if(!last.empty())
        {
            if(!written)
            {
                stream.indent(indent) << EscapeXML(prefix);
                last = last.ltrim();
            }
            stream << EscapeXML(last);
            written = true;
        }
    }
    return written;
}

/**
 * Writes a verbatim paragraph block to the XML documentation
 *
 * @param stream The stream for the documentation file
 * @param para The verbatim data to write
 * @param indent The amount (number of "tabs") to indent the text by
 * @return true if the paragraph was written (had data); false, otherwise
 */
bool WriteVerbatim(llvm::raw_ostream& stream, const Paragraph& para, std::size_t indent)
{
    bool written = false;
    std::size_t stripAmount = std::numeric_limits<std::size_t>::max();
    for(const auto& part : para.Data)
    {
        StringRef str = part;
        auto wsLen = str.find_first_not_of(" \t\n\v\f\r");
        if(wsLen != StringRef::npos)
        {
            stripAmount = std::min(stripAmount, wsLen);
        }
    }
    for(const auto& part : para.Data)
    {
        StringRef str = part;
        stream.indent(indent);
        if(part.size() > stripAmount)
        {
            stream << EscapeXML(str.substr(stripAmount).rtrim()) << '\n';
        }
        else
        {
            stream << '\n';
        }
        written = true;
    }
    return written;
}

/**
 * Write multiple paragraphs to the XML documentation
 *
 * @param stream The stream for the documentation file
 * @param paras The paragraphs to write
 * @param indent The amount (number of "tabs") to indent each line (paragraph)
 * @param prefix The prefix to prepend to the *first* paragraph which is written
 * @return true if the paragraphs were written (at least one paragraph had data); false, otherwise
 */
bool Write(llvm::raw_ostream& stream, const Paragraphs& paras, std::size_t indent, const std::string& prefix = "")
{
    // TODO: Continue line
    auto end = paras.end();
    bool newPara = false;
    for(auto it = paras.begin(); it != end; ++it)
    {
        if(!it->empty())
        {
            if(newPara)
            {
                stream << "\n";
            }
            switch(it->Type)
            {
                case ParagraphType::List:
                    WriteSingleLine(stream, *it, indent, u8"\u00A0\u2022\u00A0\u00A0");
                    newPara = true;
                    break;
                case ParagraphType::VerbatimText:
                    WriteVerbatim(stream.indent(indent) << "[codeblock lang=text]\n", *it, indent);
                    stream.indent(indent) << "[/codeblock]";
                    newPara = true;
                    break;
                case ParagraphType::GDScript:
                case ParagraphType::CSharpCode:
                {
                    auto next = std::next(it);
                    while((next != end) && (next->empty()))
                    {
                        ++next;
                    }
                    if((next != end) && IsDifferentLanguage(it->Type, next->Type))
                    {
                        WriteVerbatim((stream.indent(indent) << "[codeblocks]\n").indent(indent)
                            << "[" << it->Type << "]\n", *it, indent);
                        WriteVerbatim((stream.indent(indent) << "[/" << it->Type << "]\n").indent(indent)
                            << "[" << next->Type << "]\n", *next, indent);
                        stream.indent(indent) << "[/" << next->Type << "]\n";
                        stream.indent(indent) << "[/codeblocks]";
                        it = next;
                    }
                    else
                    {
                        WriteVerbatim(stream.indent(indent) << "[codeblock lang=" << it->Type << "]\n", *it, indent);
                        stream.indent(indent) << "[/codeblock]";
                        newPara = true;
                    }
                    newPara = true;
                    break;
                }
                case ParagraphType::Normal:
                default:
                    newPara = WriteSingleLine(stream, *it, indent, (newPara) ? "" : prefix) || newPara;
                    break;
            }
        }
    }
    return newPara;
}

/**
 * Write an optional section with title to the documentation, if the paragraph list is non-empty
 *
 * @param os The stream for the documentation file
 * @param newPara Flag to indicate if a new paragraph needs starting before writing this paragraph;
 *                must be updated to true if a paragraph is writtern
 * @param title The title for the section
 * @param paragraphs The paragraphs to write for the section
 * @param indent The amount to indent the paragraphs
 * @return The stream for the documentation file
 */
llvm::raw_ostream& WriteOptionalSection(llvm::raw_ostream& os, bool& newPara,
    const char* title, const Paragraphs& paragraphs, std::size_t indent)
{
    if(!paragraphs.empty())
    {
        if(newPara)
        {
            os << "\n";
        }
        Write(os, paragraphs, indent, Title(title));
        newPara = true;
    }
    return os;
}

/**
 * Parse an `@ref`
 *
 * @param ref The reference to parse
 * @param className The name of the current class to resolve references against
 * @return The BBCode for the reference
 */
std::string ParseReference(const StringRef& ref, const StringRef& className)
{
    auto sep = ref.find(':');
    if(sep != StringRef::npos)
    {
        auto type = ref.substr(0, sep);
        if(type == "operator")
        {
            auto end = ref.find('.', sep+1);
            return (end != StringRef::npos)
                ? "[operator " + std::string{ref.substr(sep+1, end-sep)} + "operator " + std::string{ref.substr(end+1)} + "]"
                : "[operator " + std::string{className} + ".operator " + std::string{ref.substr(sep+1)} + "]";
        }
        else if((type == "annotation")
            || (type == "constant")
            || (type == "enum")
            || (type == "member")
            || (type == "method")
            || (type == "constructor")
            || (type == "signal")
            || (type == "theme_item"))
        {
            auto end = ref.find('.', sep+1);
            return (end != StringRef::npos)
                ? '[' + std::string{type} + ' ' + std::string{ref.substr(sep+1)} + ']'
                : '[' + std::string{type} + ' ' + std::string{className} + '.' + std::string{ref.substr(sep+1)} + ']';
        }
    }
    return "[" + std::string{ref} + "]";
}

/**
 * Parse a paragraph of a comment block
 *
 * @tparam T The type of the paragraph of the comment block (BlockCommandComment, ParagraphComment, etc.)
 * @param comment The comment paragraph to parse
 * @param className The name of the class the comment is for/within
 * @param traits Traits for doc comments (result of getCommentCommandTraits() from ASTContext)
 * @return The aprsed paragraph
 */
template<typename T>
Paragraph ParseComments(T* comment, const StringRef& className, const comments::CommandTraits& traits)
{
    Paragraph result;
    auto end = comment->child_end();
    for(auto it = comment->child_begin(); it != end; ++it)
    {
        switch((*it)->getCommentKind())
        {
            case comments::CommentKind::BlockCommandComment:
            {
                // Don't think these should happen, but handle anyway
                auto block = dyn_cast<comments::BlockCommandComment>(*it);
                result += ParseComments(block, className, traits);
                break;
            }
            case comments::CommentKind::ParagraphComment:
            {
                auto block = dyn_cast<comments::ParagraphComment>(*it);
                result += ParseComments(block, className, traits);
                break;
            }
            case comments::CommentKind::TextComment:
            {
                comments::TextComment* comment = dyn_cast<comments::TextComment>(*it);
                result += comment->getText();
                break;
            }
            case comments::CommentKind::InlineCommandComment:
            {
                /*
                 * Supported commands
                 * a: Displays next word in code; i.e., to refer to arguments in running text; equivalent to [param name] in BBCode (1 arg)
                 * b: Displays next word in bold (1 arg)
                 * c: Displays next word in typewriter font (1 arg)
                 * e/em: Displays next word in italic; i.e., emphasise the text (1 arg)
                 * emoji: Parse the next word as a "named" emoji (1 arg)
                 * n: Insert new line (same as [br])
                 * p: Link to a member of this class (where member is next word) (1 arg)
                 */
                comments::InlineCommandComment* comment = dyn_cast<comments::InlineCommandComment>(*it);
                auto numArgs = comment->getNumArgs();
                auto cmd = comment->getCommandName(traits);
                auto arg = (numArgs > 0) ? std::string{comment->getArgText(0)} : "";
                if(cmd == "a")
                {
                    result += "[param " + arg + "]";
                }
                else if(cmd == "b")
                {
                    result += "[b]" + arg + "[/b]";
                }
                else if(cmd == "c")
                {
                    result += "[code]" + arg + "[/code]";
                }
                else if((cmd == "e") || (cmd == "em"))
                {
                    result += "[i]" + arg + "[/i]";
                }
                else if(cmd == "n")
                {
                    result += "[br]";
                }
                else if(cmd == "p")
                {
                    result += "[member " + std::string{className} + "." + arg + "]";
                }
                else if(cmd == "ref")
                {
                    result += ParseReference(arg, className);
                }
                else if(!arg.empty())
                {
                    result += arg;
                }
                if(numArgs > 1)
                {
                    for(unsigned int i = 1; i != numArgs; ++i)
                    {
                        result += " " + std::string{comment->getArgText(i)};
                    }
                }
                break;
            }
            case comments::CommentKind::HTMLStartTagComment:
            case comments::CommentKind::HTMLEndTagComment:
                // TODO: HTML not supported
                break;
            case comments::CommentKind::VerbatimBlockComment:
            case comments::CommentKind::VerbatimLineComment:
            case comments::CommentKind::VerbatimBlockLineComment:
            case comments::CommentKind::ParamCommandComment:
            case comments::CommentKind::FullComment:
                // Don't think these should happen (ignore if they do)
            case comments::CommentKind::TParamCommandComment:
                // We ignore this
            case comments::CommentKind::None:
                break;
        }
    }
    return result;
}

// =============================================================================
// ParagraphType
// =============================================================================

/**
 * Write the name of the specified code/plain text block
 *
 * @param os The stream to write to
 * @param type The paragraph type (should be a code block)
 * @return The stream
 */
llvm::raw_ostream& operator<<(llvm::raw_ostream& os, ParagraphType type)
{
    switch(type)
    {
        case ParagraphType::VerbatimText:
            return os << "text";
        case ParagraphType::GDScript:
            return os << "gdscript";
        case ParagraphType::CSharpCode:
            return os << "csharp";
        case ParagraphType::Normal:
        case ParagraphType::List:
        default:
            return os;
    }
}

// =============================================================================
// Paragraph
// =============================================================================

bool Paragraph::empty() const
{
    for(const auto& data : Data)
    {
        StringRef str{data};
        if(!str.ltrim().empty())
        {
            return false;
        }
    }
    return true;
}

void Paragraph::push_front(const std::string& str)
{
    if(!Data.empty())
    {
        auto& first = Data.front();
        auto pos = first.find_first_not_of(" \t\n\v\f\r");
        if(pos != std::string::npos)
        {
            first = first.substr(pos);
        }
    }
    Data.push_front(str);
}

Paragraph& operator+=(Paragraph& para, const std::string& text)
{
    para.Data.push_back(text);
    return para;
}

Paragraph& operator+=(Paragraph& para, const StringRef& text)
{
    para.Data.push_back(std::string{text});
    return para;
}

Paragraph& operator+=(Paragraph& para, const char* text)
{
    para.Data.emplace_back(text);
    return para;
}

Paragraph& operator+=(Paragraph& para, Paragraph&& text)
{
    if(para.Type == text.Type)
    {
        para.Data.splice(para.Data.end(), text.Data);
    }
    // TODO: else Error
    return para;
}

// =============================================================================
// Paragraphs
// =============================================================================

Paragraphs& operator+=(Paragraphs& paras, const Paragraph& para)
{
    paras.push_back(para);
    return paras;
}

Paragraphs& operator+=(Paragraphs& paras, Paragraph&& para)
{
    paras.emplace_back(std::move(para));
    return paras;
}

// =============================================================================
// ParsedDocumentation
// =============================================================================

ParsedDocumentation::ParsedDocumentation(const StringRef& className, comments::FullComment* doc, const comments::CommandTraits& traits)
{
    if(doc)
    {
        bool hasBriefTag;
        auto end = doc->child_end();
        for(auto it = doc->child_begin(); it != end; ++it)
        {
            // TODO: These seem like the only sensible top level comments
            // (+TParamCommandComment which we ignore as it makes no sense for godot)
            switch((*it)->getCommentKind())
            {
                case comments::CommentKind::BlockCommandComment:
                {
                    auto block = dyn_cast<comments::BlockCommandComment>(*it);
                    ParseBlock(className, traits, block, hasBriefTag);
                    break;
                }
                case comments::CommentKind::ParagraphComment:
                {
                    auto block = dyn_cast<comments::ParagraphComment>(*it);
                    if(!hasBriefTag && Brief.empty())
                    {
                        Brief = ParseComments(block, className, traits);
                    }
                    else
                    {
                        Detailed += ParseComments(block, className, traits);
                    }
                    break;
                }
                case comments::CommentKind::VerbatimBlockComment:
                {
                    auto block = dyn_cast<comments::VerbatimBlockComment>(*it);
                    ParseVerbatim(block->getCommandName(traits), block);
                    break;
                }
                case comments::CommentKind::VerbatimLineComment:
                {
                    auto& para = Detailed.emplace_back(ParagraphType::VerbatimText);
                    para += dyn_cast<comments::VerbatimLineComment>(*it)->getText().trim();
                    break;
                }
                case comments::CommentKind::ParamCommandComment:
                {
                    auto block = dyn_cast<comments::ParamCommandComment>(*it);
                    // TODO: Don't currently supported var args
                    if(!block->isVarArgParam())
                    {
                        auto idx = block->getParamIndex();
                        if(ParameterDescs.size() <= idx)
                        {
                            ParameterDescs.resize(idx+1);
                        }
                        ParameterDescs[idx].Description = ParseComments(block, className, traits);
                        ParameterDescs[idx].Name = block->getParamName(doc);
                    }
                }
                default:
                    break;
            }
        }
    }
}

llvm::raw_ostream& ParsedDocumentation::WriteDetailed(llvm::raw_ostream& os, bool printBrief,
    bool printFunctionInfo, std::size_t indent) const
{
    bool newPara = false;
    if(printBrief && !Brief.empty())
    {
        WriteSingleLine(os, Brief, indent);
        newPara = true;
    }
    bool hasDetail = false;
    for(const auto& para : Detailed)
    {
        if(!para.empty())
        {
            hasDetail = true;
            break;
        }
    }
    if(hasDetail)
    {
        if(newPara)
        {
            os << "\n";
        }
        Write(os, Detailed, indent);
        newPara = true;
    }
    WriteOptionalSection(os, newPara, "Since", Since, indent);
    if(printFunctionInfo)
    {
        WriteOptionalSection(os, newPara, "Preconditions", Preconditions, indent);
        WriteOptionalSection(os, newPara, "Postconditions", Postconditions, indent);

        if(!ParameterDescs.empty())
        {
            if(newPara)
            {
                os << "\n";
            }
            os.indent(indent) << Title("Parameters");
            for(const auto& param : ParameterDescs)
            {
                WriteSingleLine(os << "\n", param.Description, indent,
                    u8"\u00A0\u2022\u00A0\u00A0[b][code]" + param.Name + "[/code]:[/b] ");
            }
            newPara = true;
        }
        if(!ReturnDesc.empty())
        {
            if(newPara)
            {
                os << "\n";
            }
            newPara = true;
            WriteSingleLine(os, ReturnDesc, indent, Title("Return"));
        }
        if(!ReturnValues.empty())
        {
            newPara = true;
            if(ReturnDesc.empty())
            {
                if(newPara)
                {
                    os << "\n";
                }
                os.indent(indent) << Title("Return");
            }
            for(const auto& values : ReturnValues)
            {
                WriteSingleLine(os << "\n", values.Description, indent,
                    u8"\u00A0\u2022\u00A0\u00A0[b][code]" + values.Name + "[/code]:[/b] ");
            }
        }
    }

    WriteOptionalSection(os, newPara, "Authors", Author, indent);
    WriteOptionalSection(os, newPara, "Version", Version, indent);
    WriteOptionalSection(os, newPara, "Copyright", Copyright, indent);

    return os;
}

llvm::raw_ostream& ParsedDocumentation::WriteAttributes(llvm::raw_ostream& os) const
{
    if(Deprecated.IsTagPresent)
    {
        os << " deprecated=\"";
        WriteSingleLine(os, Deprecated.Message, 0);
        os << "\"";
    }
    if(Experimental.IsTagPresent)
    {
        os << " experimental=\"";
        WriteSingleLine(os, Experimental.Message, 0);
        os << "\"";
    }
    return os;
}

void ParsedDocumentation::ParseBlock(const StringRef& className, const comments::CommandTraits& traits,
    comments::BlockCommandComment* block, bool& hasBriefTag)
{
    auto command = block->getCommandName(traits);
    auto paragraph = ParseComments(block, className, traits);
    if((command == "author") || (command == "authors"))
    {
        Author += std::move(paragraph);
    }
    else if(command == "attention")
    {
        ADMONITION(paragraph, "Attention", "aa6600", u8"\u26A0");
        Detailed += std::move(paragraph);
    }
    else if(command == "brief")
    {
        if(!Brief.empty())
        {
            if(!hasBriefTag)
            {
                if(Detailed.empty())
                {
                    Detailed += Brief;
                }
                else
                {
                    Detailed.push_front(Brief);
                }
                Brief = std::move(paragraph);
            }
            else
            {
                Brief += std::move(paragraph);
            }
        }
        else
        {
            Brief = std::move(paragraph);
        }
        hasBriefTag = true;
    }
    else if(command == "bug")
    {
        ADMONITION(paragraph, "Bug", "dd3311", u8"\u2620");
        Detailed += std::move(paragraph);
    }
    else if(command == "copyright")
    {
        Copyright += std::move(paragraph);
    }
    else if(command == "deprecated")
    {
        Deprecated.IsTagPresent = true;
        Deprecated.Message = std::move(paragraph);
    }
    else if(command == "experimental")
    {
        Experimental.IsTagPresent = true;
        Experimental.Message = std::move(paragraph);
    }
    else if(command == "li")
    {
        paragraph.Type = ParagraphType::List;
        Detailed += std::move(paragraph);
    }
    else if(command == "note")
    {
        ADMONITION(paragraph, "Note", "008855", u8"\u2606");
        Detailed += std::move(paragraph);
    }
    else if(command == "remark")
    {
        ADMONITION(paragraph, "Remark", "0077cc", u8"\u2605");
        Detailed += std::move(paragraph);
    }
    else if(command == "since")
    {
        Since += std::move(paragraph);
    }
    else if(command == "par")
    {
        if(block->getNumArgs() > 0)
        {
            paragraph.push_front(Title(std::string{block->getArgText(0)}));
        }
        Detailed += std::move(paragraph);
    }
    else if(command == "pre")
    {
        Preconditions += std::move(paragraph);
    }
    else if(command == "pos")
    {
        Postconditions += std::move(paragraph);
    }
    else if((command == "result") || (command == "return") || (command == "returns"))
    {
        ReturnDesc += std::move(paragraph);
    }
    else if(command == "retval")
    {
        if(block->getNumArgs() > 0)
        {
            ReturnValues.emplace_back(std::string{block->getArgText(0)}, std::move(paragraph));
        }
        else
        {
            ReturnValues.emplace_back(std::move(paragraph));
        }
    }
    else if(command == "todo")
    {
        ADMONITION(paragraph, "TODO", "aa44dd", u8"\U0001F5F9\uFE0E");
        Detailed += std::move(paragraph);
    }
    else if(command == "tutorial")
    {
        auto it = paragraph.Data.begin();
        auto end = paragraph.Data.end();
        for(; it != end; ++it)
        {
            StringRef str(*it);
            if(!str.ltrim().empty())
            {
                break;
            }
        }
        if(it != end)
        {
            std::string url;
            StringRef str(*it);
            str = str.trim();
            auto pos = str.find_first_of(" \t\n\v\f\r");
            if(pos == StringRef::npos)
            {
                url = std::string{str};
                paragraph.Data.erase(it);
            }
            else
            {
                url = std::string{str.substr(0, pos)};
                *it = std::string{str.substr(pos+1).ltrim()};
            }
            Tutorials.emplace_back(url, std::move(paragraph));
        }
    }
    else if(command == "version")
    {
        Version += std::move(paragraph);
    }
    else if(command == "warning")
    {
        ADMONITION(paragraph, "Warning", "ee0022", u8"\u26A0");
        Detailed += std::move(paragraph);
    }
    else
    {
        Detailed += std::move(paragraph);
    }
}

void ParsedDocumentation::ParseVerbatim(const StringRef& command, comments::VerbatimBlockComment* block)
{
    auto lineEnd = block->child_end();
    auto lineIt = block->child_begin();
    auto paraType = ParagraphType::VerbatimText;
    if(command == "code")
    {
        comments::VerbatimBlockLineComment* line = nullptr;
        while(!line && (lineIt != lineEnd))
        {
            line = dyn_cast<comments::VerbatimBlockLineComment>(*lineIt);
            if(!line)
            {
                ++lineIt;
            }
        }
        if(line)
        {
            auto codeType = line->getText().trim();
            if(codeType == "{.gd}")
            {
                paraType = ParagraphType::GDScript;
                ++lineIt;
            }
            else if(codeType == "{.cs}")
            {
                paraType = ParagraphType::CSharpCode;
                ++lineIt;
            }
        }
    }
    auto& para = Detailed.emplace_back(paraType);
    for(; lineIt != lineEnd; ++lineIt)
    {
        auto line = dyn_cast<comments::VerbatimBlockLineComment>(*lineIt);
        if(line)
        {
            para += line->getText();
        }
    }
}

// =============================================================================
// ExtractDocVisitor
// =============================================================================

void ExtractDocVisitor::ProcessStartClass(const StringRef& name, CXXRecordDecl* declaration)
{
    ExtractInterfaceVisitor::ProcessStartClass(name, declaration);
    auto path = (root / (name.str() + ".xml")).generic_string();
    std::error_code err;
    file.reset(new llvm::raw_fd_stream(path, err));
    if(err)
    {
        file.reset();
        GenerateError(Context(), declaration->getLocation(),
            "Unable to open output XML file for documentation for class '%0'\n"
            "    File:  %1\n"
            "    Error: %3 (%2)", name, path, err.value(), err.message());
    }
    if(file)
    {
        *file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                << "<class name=\"" << name << "\"";
        // TODO: We assume the first class is the godot class we are inheriting from
        for(const auto& base : declaration->bases())
        {
            auto cls = GetUnderlyingType(base.getType())->getAsCXXRecordDecl();
            if(cls)
            {
                *file << " inherits=\"" << cls->getName() << "\"";
            }
        }

        *file << " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
              << " xsi:noNamespaceSchemaLocation=\"https://raw.githubusercontent.com/godotengine/godot/master/doc/class.xsd\"";

        ParsedDocumentation doc(name, Context().getLocalCommentForDeclUncached(declaration),
            Context().getCommentCommandTraits());
        doc.WriteAttributes(*file);
        *file << ">\n";
        *file << "    <brief_description>\n";
        WriteSingleLine(*file, doc.Brief, 8);
        *file << "\n    </brief_description>\n"
              << "    <description>\n";
        doc.WriteDetailed(*file, false, false, 8);
        *file << "\n    </description>\n"
              << "    <tutorials>\n";
        for(const auto& tutorial : doc.Tutorials)
        {
            *file << "        <link";
            if(!tutorial.Title.empty())
            {
                *file << " title=\"";
                WriteSingleLine(*file, tutorial.Title, 0);
                *file << "\"";
            }
            *file << '>' << EscapeXML(tutorial.URL) << "</link>\n";
        }
        *file  << "    </tutorials>\n";
    }
}

void ExtractDocVisitor::ProcessEndClass(const StringRef& name, CXXRecordDecl* declaration)
{
    ExtractInterfaceVisitor::ProcessEndClass(name, declaration);
    if(file)
    {
        *file << "    <methods>\n";
        for(const auto& method : methods)
        {
            // TODO: default, is_bitfield, & returns_error
            *file << "        <method name=\"" << method.first << "\"";
            if(!method.second.Qualifiers.empty())
            {
                *file << " qualifiers=\"" << method.second.Qualifiers << "\"";
            }
            method.second.WriteAttributes(*file);
            *file << ">\n";
            if(!method.second.ReturnType)
            {
                *file << "            <return type=\"void\"/>\n";
            }
            else
            {
                *file << "            <return type=\"" << method.second.ReturnType->TypeName;
                if(!method.second.ReturnType->EnumName.empty())
                {
                    *file << "\" enum=\"" << method.second.ReturnType->EnumName;
                }
                *file << "\"/>\n";
            }
            std::size_t index = 0;
            for(const auto& param : method.second.Arguments)
            {
                *file << "            <param index=\"" << index << "\" name=\"" << param.Name
                      << "\" type=\"" << param.Type.TypeName;
                if(!param.Type.EnumName.empty())
                {
                    *file << "\" enum=\"" << param.Type.EnumName;
                }
                *file << "\"/>\n";
                ++index;
            }
            *file << "            <description>\n";
            method.second.WriteDetailed(*file, true, true, 16);
            *file << "\n            </description>\n        </method>\n";
        }
        *file << "    </methods>\n    <members>\n";
        for(const auto& property : properties)
        {
            // TODO: default & is_bitfield
            *file << "        <member name=\"" << property.first << "\" type=\""
                  << property.second.Property.Type.TypeName << "\" setter=\""
                  << property.second.Property.Setter << "\" getter=\""
                  << property.second.Property.Getter << "\"";
            if(!property.second.Property.Type.EnumName.empty())
            {
                *file << " enum=\"" << property.second.Property.Type.EnumName << "\"";
            }
            if(property.second.Documentation)
            {
                property.second.Documentation->WriteAttributes(*file);
                *file << ">\n";
                property.second.Documentation->WriteDetailed(*file, true, false, 12);
                *file  << "\n        </member>\n";
            }
            else
            {
                *file << "/>\n";
            }
        }
        *file << "    </members>\n    <signals>\n";
        for(const auto& signal : signals)
        {
            *file << "        <signal name=\"" << signal.first << "\"";
            signal.second.WriteAttributes(*file);
            *file << ">\n";
            std::size_t index = 0;
            for(const auto& param : signal.second.Arguments)
            {
                *file << "            <param index=\"" << index << "\" name=\"" << param.Name
                      << "\" type=\"" << param.Type.TypeName << "\"/>\n";
                ++index;
            }
            *file << "            <description>\n";
            signal.second.WriteDetailed(*file, true, true, 16);
            *file << "\n            </description>\n        </signal>\n";
        }
        *file << "    </signals>\n    <constants>\n";
        for(const auto& constant : constants)
        {
            *file << "        <constant name=\"" << constant.first << "\" value=\""
                  << constant.second.Value << "\" is_bitfield=\"" << constant.second.IsBitfield << "\"";
            if(!constant.second.Enum.empty())
            {
                *file << " enum=\"" << constant.second.Enum << "\"";
            }
            constant.second.WriteAttributes(*file);
            *file << ">\n";
            constant.second.WriteDetailed(*file, true, false, 12);
            *file  << "\n        </constant>\n";
        }
        *file << "    </constants>\n</class>\n";
        file.reset();
    }
}

void ExtractDocVisitor::ProcessConstant(ConstantType type, const StringRef& name, EnumConstantDecl* declaration)
{
    ExtractInterfaceVisitor::ProcessConstant(type, name, declaration);
    auto doc = Context().getLocalCommentForDeclUncached(declaration);
    StringRef parentEnum = "";
    if(type != ConstantType::Constants)
    {
        auto enumType = dyn_cast<clang::EnumDecl>(declaration->getDeclContext());
        parentEnum = enumType->getName();
    }
    auto& traits = Context().getCommentCommandTraits();
    constants.try_emplace(name, declaration->getValue().getLimitedValue(), type, Class(),
        doc, traits, parentEnum);
}

void ExtractDocVisitor::ProcessPropertyFunc(const std::string& propertyName, CXXMethodDecl* declaration,
    const Property& property, const std::string& function, bool isSetter)
{
    ExtractInterfaceVisitor::ProcessPropertyFunc(propertyName, declaration, property, function, isSetter);
    auto& propDoc = properties[propertyName];
    if(!isSetter || !propDoc.Documentation)
    {
        auto doc = Context().getLocalCommentForDeclUncached(declaration);
        if(doc)
        {
            propDoc.Documentation = ParsedDocumentation{Class(), doc, Context().getCommentCommandTraits()};
        }
    }
}

void ExtractDocVisitor::ProcessProperty(const std::string& propertyName, const Property& property)
{
    ExtractInterfaceVisitor::ProcessProperty(propertyName, property);
    auto& propDoc = properties[propertyName];
    propDoc.Property = property;
}

void ExtractDocVisitor::ProcessSignal(const std::string& name, CXXMethodDecl* declaration,
        const std::vector<FunctionArgument>& arguments)
{
    ExtractInterfaceVisitor::ProcessSignal(name, declaration, arguments);
    auto doc = Context().getLocalCommentForDeclUncached(declaration);
    auto& traits = Context().getCommentCommandTraits();
    signals.try_emplace(name, arguments, Class(), doc, traits);
}

void ExtractDocVisitor::ProcessMethod(const std::string& name, CXXMethodDecl* declaration, bool isStatic,
    bool isProperty, const std::vector<FunctionArgument>& arguments, const std::optional<GodotType>& returnType)
{
    ExtractInterfaceVisitor::ProcessMethod(name, declaration, isStatic, isProperty, arguments, returnType);
    if(!isProperty)
    {
        auto doc = Context().getLocalCommentForDeclUncached(declaration);
        auto& traits = Context().getCommentCommandTraits();
        std::string qualifiers;
        if(isStatic)
        {
            qualifiers = "static";
        }
        else if(declaration->isConst())
        {
            if(declaration->isVirtual())
            {
                qualifiers = "virtual const";
            }
            else
            {
                qualifiers = "const";
            }
        }
        else if(declaration->isVirtual())
        {
            qualifiers = "virtual";
        }
        methods.try_emplace(name, arguments, Class(), doc, traits, returnType, qualifiers);
    }
}
