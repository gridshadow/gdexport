# GDExtension Export

Clang plugin for auto-generating the export interface for a Godot GDExtension from <nobr>C++</nobr> attributes.

  * [Motivation](#motivation)
    * [Currently Implemented](#currently-implemented)
    * [Issues & TODO](#issues-and-todo)
  * [Compiling](#compiling)
    * [Requirements](#requirements)
    * [Building](#building)
  * [Usage](#usage)
    * [<nobr>C++</nobr> Attributes](#adding-attributes-to-the-c-classes-and-methods)
      * [Classes](#class)
      * [Enums, Bitfields, & Constants](#enum-bitfield-and-constants)
      * [Members](#members)
      * [Signals](#signals)
      * [Groups/Subgroups](#member-groupssubgroups)
    * [Documentation Comments](#documentation-comments-support)
    * [Generating the Interface](#generating-the-interface)
      * [Command-line Python Script](#python-script)
      * [Using Python Package](#python-package)
      * [Using SCons](#scons)

## Motivation

When writing a GDExtension in <nobr>C++</nobr>, see
[Godot Tutorial](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_cpp_example.html),
it is necessary to write boilerplate code to export the classes, members, methods, and signals. As
most of this is purely repeating the <nobr>C++</nobr> signatures, it should be possible to auto-generate most of
this. Since <nobr>C++17</nobr>, [attributes](https://en.cppreference.com/w/cpp/language/attributes.html) from
unknown namespaces should be ignored by compilers, and therefore, it should be possible to 
write a tool which parses custom attributes (with the `godot` namespace) to automatically generate
the code.

Rather than write a complete parser for <nobr>C++</nobr> to handle the attributes, the idea was to use clang to
generate the [Abstract Syntax Tree (AST)](https://clang.llvm.org/docs/IntroductionToTheClangAST.html);
notably, using the [Clang Python Bindings](https://libclang.readthedocs.io/en/latest/) to integrate
into the extension's SCons build. However, this doesn't expose the unknown, custom attributes. In
order to make clang support custom attributes its necessary to do one of two things:

  - Customise a few source files of clang and build a custom clang executable
  - Write a [Clang plugin](https://clang.llvm.org/docs/ClangPlugins.html) to handle the custom
    attributes.

Using the second option was best, and now the custom tool could be integrated into the plugin.
This however, now means that a clang executable is required (rather than just the python bindings
and the underlying library), and the execution is fairly complicated. Therefore, a python wrapper
(which can integrate with SCons) is provided.

### Currently Implemented

  * Defining classes to export in the GDExtension
  * Class members, with getter and setter functions, including limited support for type hinting
  * Class methods and signals
  * Static methods
  * Enums, bitfields, and constants in a class
  * Generation of [XML comments](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_docs_system.html)
    from [Doxygen-style](https://www.doxygen.nl/manual/docblocks.html) comments in the <nobr>C++</nobr> code.

### Issues and TODO

  * Ensure works on macOS and Windows
  * Still need to manually add `VARIANT_ENUM_CAST` and `VARIANT_BITFIELD_CAST` to headers for enum
    and bitfields
  * Add support for marking enums outside of class as an enum/constant/bitfield
  * Add better support for property/argument type hinting, notably for methods/signals, and
    automatically deducing type hints in some cases)
  * Add support for virtual (can be overridden by GDScript) and override (override method in GDScript).
    Maybe can be deduced by the `virtual` and `overload` keywords on <nobr>C++</nobr> functions; however, virtual
    methods are generally defined by `GDVIRTUAL*` macros, which means an attribute cannot be attached
    to the function (as the function definition is NOT the first thing defined in the macro) &mdash; would
    have to reimplement this (maybe that is possible, could generate the helper function code).
  * Investigate using [cppast](https://github.com/standardese/cppast)

## Compiling

Currently the compilation uses CMake rather than SCons, for ease of finding the LLVM/Clang libraries
that are required, and MUST be compiled with clang. At the current moment this tool
is only tested and likely to work on Linux (Apple Clang may not support plugins).

### Requirements

  * [CMake 3.24](https://cmake.org/) or later
  * [LLVM/Clang 19](https://clang.llvm.org/) or later
  * libclang
  * Checkout of the [godot-cpp repository](https://github.com/godotengine/godot-cpp)

### Building

Using CMake the compilation of the tool requires only two steps ran from the root of the `gdexport`
folder:

1.  **Configure the build system**:
    ```sh
    cmake [options] -DCMAKE_BUILD_TYPE=Release -S . -B build
    ```

    where `[options]` can contain the following:
      * `-DCMAKE_C_COMPILER=/path/to/clang` &mdash; Specifies the path to `clang`, where
        `/path/to/clang` is replaced with the path to the `clang` executable.
      * `-DCMAKE_CXX_COMPILER=/path/to/clang` &mdash; Specifies the path to `clang++`, where
        `/path/to/clang` is replaced with the path to the `clang++` executable.
      * `-DGDEXPORT_GODOT_CPP=/path/to/godot-cpp` &mdash; Specifies the path to `godot-cpp`
        git repository checkout, where `/path/to/godot-cpp` is replaced with the path to the
        folder containing `godot-cpp`. **This *should* be specified**

2.  **Build the clang plugin**:
    ```sh
    cmake --build build --config Release [options]
    ```

## Usage

In order to automatically generate the interface for exported classes in a GDExtension the following
steps are necessary:
  * [Add attributes to the <nobr>C++</nobr> classes and methods](#adding-attributes-to-the-c-classes-and-methods)
  * *Optionally* [Add Doxygen-style comments for automatic documentation generation](#documentation-comments-support)
  * [Generate <nobr>C++</nobr> source files for the interface](#generating-the-interface)
  * Include the generated <nobr>C++</nobr> files (and documentation XML) in the GDExtension 

### Adding Attributes to the <nobr>C++</nobr> Classes and Methods

Auto-generation of the interface is supported by adding
[<nobr>C++11</nobr>](https://en.cppreference.com/w/cpp/language/attributes.html)-,
[C23](https://en.cppreference.com/w/c/language/attributes.html)-, or
[GCC](https://gcc.gnu.org/onlinedocs/gcc/Attributes.html)-style attributes
of the form
```cpp
[[godot::name(args)]]
[[godot_name(args)]]
__attribute__((godot_name(args)))
```
to a class, enum, or function, where `name` is the name of the attribute and
`(args)` specifies optional arguments. <nobr>C++17</nobr> compilers are meant to
ignore unknown attributes/unknown namespace; therefore, this shouldn't cause
a compile error when compiling the GDExtensions. However, on some compilers
warnings may be generated for unknown attributes. Most compilers have options
to turn this off; for example, GCC can be passed the following flag to ignore
the namespace prefixed attributes:
```sh
-Wno-attributes=godot::
```
This can be added to the SConstruct file for the extensions; e.g.,
```python
env.Append(CCFLAGS=["-Wno-attributes=godot::"])
```

There are essentially two forms for the attributes:

  * **namespace prefixed**: `godot::name`. While this is the preferred format it
    appears to not always work with arguments (maybe a bug in clang)
  * **unprefixed**: `godot_name`. This appears to work with arguments, although
    the `-Wno-attributes` flag above will not suppress the warnings from GCC for these

In the following documentation we only use the *namespace prefixed* form.

Arguments must be compiler-time literals. The attributes used can take one or
more of the following types:
  - String literal &mdash; must be a <nobr>C++</nobr> string literal of form `"literal"` (or macro
    that expands to this)
  - An enum literal &mdash; which must be specified as the enum literal of form
    `namespace::ENUM_VALUE_NAME`, or an integer literal
  - A bitfield &mdash; this can be a single enum or integer literal, or a bitwise
    or `|` of two or more enum/integer literals

The actual name, location, and arguments of the attribute depends on what needs
exporting.

#### Class

One of the two attributes is attached to a `class` or `struct` to export the class

> **`godot::class`** &mdash; Specifies that the annotated class `ClassName` is
> exported as a *runtime* class in the GDExtension (no code running in the editor)
>
> ```cpp
> class [[godot::class]] ClassName
> ```

> **`godot::tool`** &mdash; Specifies that the annotated class `ClassName` is
> exported as a *tool* class in the GDExtension (code runs in the editor)
>
> ```cpp
> class [[godot::tool]] ClassName
> ```

#### Enum, Bitfield, and Constants

One of the following attributes is attached to an `enum`, which must be within
an exported class, to export constants.

> **`godot::enum`** &mdash; Specifies that the annotated enum `EnumName`, and 
> its values, are exported as an enum.
>
> ```cpp
> enum [[godot::enum]] EnumName
> ```

> **`godot::bitfield`** &mdash; Specifies that the annotated enum `EnumName`, 
> and its values, are exported as a bitfield.
>
> ```cpp
> enum [[godot::bitfield]] EnumName
> ```

> **`godot::constants`** &mdash; Specifies that the values of the annotated enum
> `EnumName` are exported as class level constants.
>
> ```cpp
> enum [[godot::constants]] EnumName
> ```

#### Members

Members (properties) must have a getter method, and optional a setter method set, which are
instance function *declaration* within an exported class marked with attributes. A method
can only be set as either a getter or setter. In general, these methods should be public.

> **`godot::getter`** &mdash; Specifies that the function `get_name` is exported
> as the getter for the property `name` with type `Type`. 
>
> ```cpp
> [[godot::getter]]
> Type get_name() const;
> ```
> ```cpp
> [[godot::getter("name")]]
> Type get_name() const;
> ```
> ```cpp
> [[godot::getter(godot::Variant::OBJECT)]]
> Type get_name() const;
> ```
> ```cpp
> [[godot::getter("name", godot::Variant::OBJECT)]]
> Type get_name() const;
> ```
> ```cpp
> [[godot::getter(godot::Variant::OBJECT, godot::PROPERTY_USAGE_DEFAULT)]]
> Type get_name() const;
> ```
> ```cpp
> [[godot::getter("name", godot::Variant::OBJECT, godot::PROPERTY_USAGE_DEFAULT)]]
> Type get_name() const;
> ```
>
> This annotation can take three arguments:
>   - A string literal denoting the property name (optional). If not specified
>     the property name is deduced by removing `get_` or `get` (ignoring case)
>     from the front of the function name.
>   - A `godot::Variant` value denoting the type of the property (optional, but
>     required if the next property is specified). If not specified, type is
>     attempted to be deduced from `Type`, which must be a <nobr>C++</nobr> built-in
>     (`bool`, any integer type, `float`, `double`), an enum type, a basic Godot type
>     in the `godot` namespace (String, Vector, Array, Dictionary, Object etc.),
>     a class which inherits from a Godot type, or a typedef to any of these
>   - A bitfield of `godot::PropertyUsageFlags` values denoting the usage for
>     the property (optional). If not specified, defaults to
>     `godot::PROPERTY_USAGE_DEFAULT`

> **`godot::setter`** &mdash; Specifies that the function `set_name` is exported
> as the setter for the property `name` with type `Type`.
>
> ```cpp
> [[godot::setter]]
> void set_name(Type value);
> ```
> ```cpp
> [[godot::setter("name")]]
> void set_name(Type value);
> ```
> ```cpp
> [[godot::setter(godot::PropertyHint)]]
> void set_name(Type value);
> ```
> ```cpp
> [[godot::setter("name", godot::PropertyHint)]]
> void set_name(Type value);
> ```
> ```cpp
> [[godot::setter(godot::PropertyHint, "hint")]]
> void set_name(Type value);
> ```
> ```cpp
> [[godot::setter("name", godot::PropertyHint, "hint")]]
> void set_name(Type value);
> ```
>
> This annotation can take three arguments:
>   - A string literal denoting the property name (optional). If not specified
>     the property name is deduced by removing `set_` or `set` (ignoring case)
>     from the front of the function name.
>   - A `godot::PropertyHint` value denoting the type of property hint
>     (optional, but required if the next property is specified). If not
>     specified, indicates no property hint.
>   - A string literal for the property hint (optional). If not specified,
>     defaults to the empty string

> [!WARNING]
>
> If using [Member Groups/Subgroups](#member-groupssubgroups), both the getter and setter MUST be
> specified within the same group; e.g., the `[[godot::group]]` for the members group must come
> before *BOTH* getter and setter and the `[[godot::group]]` for the next group must come after
> *BOTH* getter and setter (and the same for `[[godot::subgroup]]`)

#### Methods

The following attribute is attached to either an instance or static function
*declaration* within an exported class, to export the method. These attributes
**should not** be added to a method marked as a *getter* or *setter* for a
property. In general, this method should be public.

> **`godot::method`** &mdash; Specifies to export the method with name
> `method_name` as an instance method of the current class.
>
> ```cpp
> [[godot::method]]
> ReturnType method_name(Args args);
> ```
>
> `ReturnType` can be any type supported by Godot, or
> `void`. Arguments are optional but names **should** be specified otherwise
> the names will be automatically generated.

> **`godot::method`** &mdash; Specifies to export the method with name
> `method_name` as a static method contained in the current class.
>
> ```cpp
> [[godot::method]]
> static ReturnType method_name(Args args);
> ```
>
> `ReturnType` can be any type supported by
> Godot, or `void`. Arguments are optional but names **should** be specified
> otherwise the names will be automatically generated.

> **TODO**
>
> Support for virtual methods (which can be overridden by GDScript) and
> methods which override GDScript methods is currently missing.

#### Signals

The following attribute is attached to to an instance function *declaration*
within an exported class with **no definition**, to export the method signature
as a signal with the specified arguments. These attribute **should not** be
added to a method marked as a *getter*, *setter*, or *method*.
In general, this method should be private or protected.

> **`godot::signal`** &mdash; Specifies to export a signal with the name
> `signal_name` which takes the specified arguments.
>
> ```cpp
> [[godot::signal]]
> void signal_name(Args args);
> ```
> ```cpp
> [[godot::signal]]
> godot::Error signal_name(Args args);
> ```
>
>  The argument names **MUST** be specified in order
> for valid code to be created. Additionally, this method **MUST NOT** be
> defined (no function body), as one will be automatically generated.
> This method can be called to emit the signal (the generated
> definition forwards to the `emit_signal` function). A signal can have a `void`
> return or a return of type `godot::Error`. In the later case the return value
> from `emit_signal` is returned, while in the former it is discarded.

#### Member Groups/Subgroups

Two attributes are provided which function similarly to the `@export_group`
and `@export_subgroup` annotations in GDScript. These *MUST* be attached to
functions, i.e., directly before a `godot::getter` or `godot::setter`
attribute.

> **`godot::group`** &mdash; Specifies a group with `name` and optional `prefix`
>
> ```cpp
> [[godot::group("name")]]
> ```
> ```cpp
> [[godot::group("name", "prefix")]]
> ```
>
> See [Grouping exports](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html)
> in the GDScript documentation.

> **`godot::subgroup`** &mdash; Specifies a subgroup with `name` and optional `prefix`
>
> ```cpp
> [[godot::subgroup("name")]]
> ```
> ```cpp
> [[godot::subgroup("name", "prefix")]]
> ```
>
> See [Grouping exports](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html)
> in the GDScript documentation.

> [!WARNING]
>
> For [Members](#members), both the getter and setter MUST be specified within the same group; e.g.,
> the `[[godot::group]]` for the members group must come before *BOTH* getter and setter and the
> `[[godot::group]]` for the next group must come after *BOTH* getter and setter (and the same for
> `[[godot::subgroup]]`)

### Documentation Comments Support

[Doxygen-style](https://www.doxygen.nl/manual/docblocks.html) comments on
classes, enums, methods, signals, and property getters or setters (preferring
getter over setter) can be automatically exported into the XML format expected
for [Godot documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_docs_system.html).

> [!IMPORTANT]
>
> It appears that clang only supports Javadoc-style comments of the form
> ```cpp
> /**
>  * Doc Comment
>  */
> ```
> Notably, single line comments after a definition (often useful for enums), such as 
> ```cpp
> //!< Doc Comment
> ```
> are not supported.

Due to limitations in clang's doxygen style processing, how it adds comments
into the AST, and the limited documentation format in Godot there are several
differences to Doxygen and Godot comment formats:
  * By default splits text into brief and detailed by assuming the first
    paragraph is the *brief* description
  * [BBCode commands](https://docs.godotengine.org/en/stable/engine_details/class_reference/index.html#doc-class-reference-bbcode)
    supported by Godot can be used providing they do not rely on preservation of
    new lines. Notably, `[codeblocks]` and `[codeblock]` will not work; instead,
    use the `\code` Doxygen command
  * *Only* the following Doxygen commands, using `@` or `\` are supported. Other
   doxygen commands may be treated as different types (often verbatim), or even
   ignored completely (and any content excluded from the output). Note that
   some commands have different syntax/behaviour compared to Doxygen:
    - [`a`](https://www.doxygen.nl/manual/commands.html#cmda) &mdash;
      Displays next word in code; i.e., to refer to arguments in running text;
      equivalent to `[param name]` in BBCode. This is slightly different
      behaviour compared to Doxygen
    - [`author`](https://www.doxygen.nl/manual/commands.html#cmdauthor)
      / [`authors`](https://www.doxygen.nl/manual/commands.html#cmdauthors) &mdash;
      Give list of authors
    - [`attention`](https://www.doxygen.nl/manual/commands.html#cmdattention) &mdash;
      Starts paragraph of text which needs attention
    - [`b`](https://www.doxygen.nl/manual/commands.html#cmdb) &mdash;
      Displays next word in bold
    - [`brief`](https://www.doxygen.nl/manual/commands.html#cmdbrief)
      / [`short`](https://www.doxygen.nl/manual/commands.html#cmdshort) &mdash;
      Sets the brief description. If not specified first paragraph is assumed to be brief
    - [`bug`](https://www.doxygen.nl/manual/commands.html#cmdbug) &mdash;
      Starts paragraph to list a bug
    - [`c`](https://www.doxygen.nl/manual/commands.html#cmdc) &mdash;
      Displays next word in typewriter font
    - [`code`](https://www.doxygen.nl/manual/commands.html#cmdcode)
      ... [`endcode`](https://www.doxygen.nl/manual/commands.html#cmdendcode) &mdash;
      Defines a block of formatted code in the specified language (can only be
      GDScript, C#, or plain text). Default to GDScript if not specified
    - [`copyright`](https://www.doxygen.nl/manual/commands.html#cmdcopyright) &mdash;
      Starts paragraph to display copyright information
    - [`deprecated`](https://www.doxygen.nl/manual/commands.html#cmddeprecated) &mdash;
      Marks as deprecated and adds optional paragraph describing why.
      **Warning: This tag is slightly different to the GDScript version**
    - [`e`](https://www.doxygen.nl/manual/commands.html#cmde)
      / [`em`](https://www.doxygen.nl/manual/commands.html#cmdem) &mdash;
      Displays next word in italic; i.e., emphasise the text
    - [`li`](https://www.doxygen.nl/manual/commands.html#cmdli) &mdash;
      Starts paragraph defining a list item. One or more consecutive list items
      are treated as a single list (no indent support)
    - [`n`](https://www.doxygen.nl/manual/commands.html#cmdn) &mdash;
      Insert new line (same as `[br]`)
    - [`note`](https://www.doxygen.nl/manual/commands.html#cmdnote) &mdash;
      Starts paragraph to list a note
    - [`p`](https://www.doxygen.nl/manual/commands.html#cmdp) &mdash;
      Link to a member of *this class* (where member is next word)
    - [`par`](https://www.doxygen.nl/manual/commands.html#cmdpar) &mdash;
      Starts a paragraph with optional paragraph title
    - [`param`](https://www.doxygen.nl/manual/commands.html#cmdpar) &mdash;
      Starts a paragraph to describe a methods/signals parameters
    - [`pre`](https://www.doxygen.nl/manual/commands.html#cmdpre) &mdash;
      Starts paragraph to list a precondition
    - [`post`](https://www.doxygen.nl/manual/commands.html#cmdpost) &mdash;
      Starts paragraph to list a postcondition
    - [`remark`](https://www.doxygen.nl/manual/commands.html#cmdremark) &mdash;
      Starts paragraph to list a remark
    - [`result`](https://www.doxygen.nl/manual/commands.html#cmdresult)
      / [`return`](https://www.doxygen.nl/manual/commands.html#cmdreturn)
      / [`returns`](https://www.doxygen.nl/manual/commands.html#cmdreturns) &mdash;
      Starts paragraph to describe a method's return
    - [`retval`](https://www.doxygen.nl/manual/commands.html#cmdresult) &mdash;
      Starts paragraph to describe a possible return value for a method
    - [`since`](https://www.doxygen.nl/manual/commands.html#cmdsince) &mdash;
      Starts paragraph to state *since when* the method was available
    - [`todo`](https://www.doxygen.nl/manual/commands.html#cmdtodo) &mdash;
      Starts paragraph to list a todo
    - [`verbatim`](https://www.doxygen.nl/manual/commands.html#verbatim)
      ... [`endverbatim`](https://www.doxygen.nl/manual/commands.html#cmdverbatim) &mdash;
      Defines a block of unformatted typewriter text
    - [`version`](https://www.doxygen.nl/manual/commands.html#cmdversion) &mdash;
      Starts paragraph to describe the version
    - [`warning`](https://www.doxygen.nl/manual/commands.html#cmdwarning) &mdash;
      Starts paragraph to describe the version
  - The Doxygen [`ref`](https://www.doxygen.nl/manual/commands.html#cmdref) has slightly
    different format and behaviour. It can be used in any of the following forms:
    - `ref Class` &mdash; Link to the specified class, equivalent to `[Class]` BBCode
    - `ref annotation:Class.name`,`ref constant:Class.name`, `ref enum:Class.name`,
      `ref member:Class.member`, `ref method:Class.name`, `ref constructor:Class.name`,
      `ref signal:Class.name`, `ref theme_item:Class.name` &mdash; Link to the
      specified annotation, constant, enum, member, method, constructor, signal,
      or theme_item; equivalent to `[annotation Class.name]` etc.
      **Warning: There must be *NO* whitespace in the command (except the required space after `ref`)**
    - `ref operator:Class.op` &mdash; Link to the operator, where `op` is the 
      operator *without* the `operator` prefix; e.g, `\ref operator:Color.+=`.
      Equivalent to `[operator Class.operator op]`
      **Warning: There must be *NO* whitespace in the command (except the required space after `ref`)**
  * The following extra Doxygen-style commands are supported
    (using `@` or `\`) similar to the corresponding GDScript tags:
    - `tutorial url [title]` &mdash; Adds the link to a tutorial, with optional title
    - `experimental [desc]` &mdash; Marks as experimental and adds optional paragraph
      describing why. 
  * Markdown is not currently supported
  * HTML is not supported

As `[codeblock]` and `[codeblocks]` are not supported, `\code`/`@code` 
or `\verbatim`/`@verbatim` Doxygen blocks can be used instead:

  * To define a plain text code block (equivalent to
    `[codeblock lang=text]`) use
    ```
    \code{.txt}
    ...GDScript here...
    \endcode
    ```
    or
    ```
    \verbatim
    ...GDScript here...
    \endverbatim
    ```
  * To define a GDScript only code block (equivalent to
    `[codeblock lang=gdscript]`) use
    ```
    \code{.gd}
    ...GDScript here...
    \endcode
    ```
  * To define a C# only code block (equivalent to
    `[codeblock lang=csharp]`) use
    ```
    \code{.cs}
    ...C# here...
    \endcode
    ```
  * To define a codeblock containing both GDScript and C# code as alternatives;
    i.e.,  equivalent to
    ```
    [codeblocks lang=csharp]
    [gdscript]
    ...
    [/gdscript]
    [csharp]
    ...
    [/csharp]
    [/codeblocks]
    ````
    use a GDScript code block followed immediately by a C# code block, with no
    space between; e.g.,
    ```
    \code{.gd}
    ...GDScript here...
    \endcode
    \code{.cs}
    ...C# here...
    \endcode
    ```

### Generating the Interface

Generating the <nobr>C++</nobr> source files to include can be done in three different ways:
  * [Via a python script on the command line](#python-script)
  * [Calling a function in a python package](#python-package)
  * [During the SCons build of the extension](#scons)

#### Python Script

The python script `gdexport.py` can be used to generate the required <nobr>C++</nobr> source files. The basic
usage is

```sh
python gdexport.py --godot <godot-cpp> <ext-name> <file> [<file>...]
```

where `<godot-cpp>` is the (relative) path to the checkout of the 
[`godot-cpp` Git repository](https://github.com/godotengine/godot-cpp),
`<ext-name>` is a name to use to generate the entry point to the extension (`<ext-name>_library_init`),
which must be a valid <nobr>C++</nobr>-style identifier (containing only numbers, letters, or `_`, and not
starting with a number), and `<file>` is one or more <nobr>C++</nobr> header files to parse to extract the
interface from.

For each `<file>` specified a <nobr>C++</nobr> file with the name `<filename>.gen.cpp` will be generated in the
current working directory (where `<filename>` is the filename of `<file>` without extension).
Additionally, a file `<ext-name>.lib.cpp` containing the entry point `<ext-name>_library_init`
will also be generated on the current working directory.

> **Warning**
>
> In order for the files to be generated clang must be able to parse and understand the header
> files (class hierarchy etc.). Therefore, it is necessary that the clang is able to find all
> header file dependencies; see usage of `--isystem`, `--include` below

For example, for the [example project in the Godot documentation of godot-cpp](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_cpp_example.html),
within the `src` directory run the following command (assuming that the `gdexport`
repository is checked out in the root of the project):

```sh
python ../gdexport/gdexport.py --godot ../godot-cpp example gdexample.h
```

Several extra options can be specified, see [detailed script usage](#detailed-script-usage) and
[script options](#script-options) below. The most useful options are the following:

  * `--output <dir>` &mdash; Specifies to output generated <nobr>C++</nobr> files to `<dir>`. For example, this allows
    the above example to be written as
    ```sh
    python gdexport/gdexport.py --output src/gen  example gdexample.h
    ```
    and run from the **root** folder of the project to generate in the `src/gen` directory. Note, we can
    drop the `--godot` options as it defaults to the `godot-cpp` folder within the current working directory
  * `--include <dir>`, `--isystem <dir>` &mdash; Specify additional include normal and system directories,
    respectively, necessary to ensure the header files can be parsed correctly.
  * `--doc <dir>` &mdash; Specifies to export the documentation XML files to the specified directory.
    If no directory is specified it defaults to `doc_classes` within the current working directory.

##### Detailed Script Usage:

```sh
gdexport.py [-h] [--godot DIR | --no-godot] [--clang EXE] [--isystem DIR] [--include DIR] [--output DIR] [--doc [DIR]] [--make-dirs] [--quiet] [--clang-arg ARG] name file [file ...]
```

##### Positional Arguments:

`name`

  - Name of the GDExtension. Used for generating the `entry_symbol` name `<name>_library_init`

`file`

  - List of <nobr>C++</nobr> header files to process to export Godot classes from

##### Script Options:

`--godot, -g DIR`
  
  - Path to the root of the checkout of the `godot-cpp` repo (automatically calculates godot include)

`--no-godot`
  
  - Don't automatically calculate include folders from `godot-cpp` repo directory

`--clang, -c EXE`
  
  - Path to the clang executable to use

`--isystem, -s DIR`
  
  - List of paths to treat as system include directories; i.e., `-isystem` paths to clang

`--include, -I DIR`
  
  - List of paths to treat as system include directories; i.e., `-I` paths to clang

`--output, -o DIR`
  
  - Specifies the output directory (default = current working directory)

`--doc, -d [DIR]`
  
  - Specifies to export XML documentation from Doxygen comments to specified folder (current working directory if no argument specified)

`--make-dirs, -m`
  
  - Specifies to create output and doc folder if they don't exist

`--quiet, -q`
  
  - Don't output informational status messages

`--clang-arg, -a ARG`
  
  - Specifies that the next argument should be passed as an extra argument to clang

#### Python Package

The python script can also be used as a python package assuming that the `gdexport` directory
is a subdirectory of the python script using the package.
```python
import gdexport
```

This module exposes the following methods:

  * [`generate_all`](#generate_all) &mdash; This can be used to generate all necessary <nobr>C++</nobr> source files
    necessary, it is essentially the same as calling the script from the command line
  * [`export_header`](#export_header) &mdash; Creates the source file for a single <nobr>C++</nobr> header file
  * [`entry_point`](#entry_point) &mdash; Creates the entry point function, given a list of
    previously generated <nobr>C++</nobr> files.
  * [`entry_point_name`](#entry_point_name) &mdash; Gets the name of the <nobr>C++</nobr> entry point function which will be generated
  * [`list_doc_files`](#list_doc_files) &mdash; Gets a list of the filenames of the XML documentation
    files which will be exported
    

Essentially, `generate` is equivalent to calling `export_header` for each source header file, and
then calling `entry_point` passing all the previously generated files:

```python
result = []
for file in files:
    result.append(export_header(file,
                                godot=godot,
                                clang=clang,
                                sysincludes=sysincludes,
                                includes=includes,
                                destination=destination,
                                documentation=documentation,
                                create_folders=True,
                                args=args))
entry_point_file = destination+'/'+name+'.lib.cpp'
result.append(entry_point_file)
entry_point(name, files, entry_point_file)
return result
```

Note that [`generate_all`](#generate_all) is better optimised than this (extracts common behaviour
out of the functions), so in general it will be faster than the above. However, it is currently
only sequential, so calling the individual methods in parallel may be faster. Additionally, it always
processes every file; therefore, as part of a build system calling the individual methods may be
better.

> [!NOTE]
>
> In the following documentation most of the arguments are specified as `str` type. In actually fact,
> most of these arguments can take any type which can be converted to string with the `str` function
> and which can be tested for being empty with `if [arg]`; e.g., pathlib.Path values could be passed.

##### `generate_all`

```python
gdexport.generate_all(name           : str,
                      files          : list[str],
                      godot          : str|None  = 'godot-cpp',
                      clang          : str       = "clang",
                      sysincludes    : list[str] = [],
                      includes       : list[str] = [],
                      destination    : str|None  = None,
                      documentation  : str|None  = None,
                      create_folders : bool      = True,
                      quiet          : bool      = False,
                      args           : list[str] = []) -> tuple[list[str],list[str]|None,str]:
```

Generates all <nobr>C++</nobr> source files, and optionally XML documentation, which export the <nobr>C++</nobr> classes,
methods, enums, signals, etc., marked with `godot::` attributes from the specified <nobr>C++</nobr> header files
to be accessible via a Godot GDExtension. Also generates the *entry point* to the GDExtension.

The arguments of this function correspond to similar arguments from the script:

  * `name` (string) &mdash; Name of the GDExtension. Used for generating the "entry_symbol" function name 
  (`<name>_library_init`) and the name of the generated <nobr>C++</nobr> file for the entry point (`<name>.lib.cpp`). Must be a valid <nobr>C++</nobr> identifier
  * `files` (list of strings) &mdash; List of <nobr>C++</nobr> header files to process to export Godot classes from
  * `godot` (string) &mdash; Path to the root of the checkout of the `godot-cpp` repo. Default is `godot-cpp` folder in the current working directory. If `None` does not automatically deduce godot-cpp header paths.
  * `clang` (string) &mdash; Path to the clang executable (default = `clang` in PATH)
  * `sysincludes` (list of strings) &mdash; List of paths to treat as system include directories; i.e., `-isystem` paths to Clang
  * `includes` (list of strings) &mdash; List of paths to treat as normal include directories; i.e., `-I` paths to Clang
  * `destination` (string) &mdash; Output directory for generated files. (default = current working directory)
  * `documentation` (string) &mdash; Specify whether to also extract Doxygen style comments from the <nobr>C++</nobr> source and generate the Godot XML documentation for the extension. Specify `None` to not generate documentation, `""` (empty string) to generate in default location (`doc_classes` in current working), or path to directory to generate in otherwise
  * `create_folders` (boolean) &mdash; Specify whether to create output folders (`destination` and `documentation`) if they do not exist
  * `quiet` (boolean) &mdash; Specifies whether to suppress status messages
  * `args` (list of strings) &mdash; List of extra command line arguments to pass to clang

This function returns a three-tuple containing the following on success:
  * List of strings containing the file paths/names of the generated <nobr>C++</nobr> source files
  * If `documentation` is not `None` then a list of strings containing the file
    paths/names of the generated XML documentation files; otherwise `None`
  * The name of the <nobr>C++</nobr> function generated as the extension's entry point;
    i.e., the value returned by [`entry_point_name`](#entry_point_name).

On failure, several specific exceptions can be raised:

  * `ValueError` &mdash; If name is not a valid <nobr>C++</nobr> identifier (valid identifier contains only `[a-zA-Z0-9_]` and cannot start with a number)
  * `ValueError` &mdash; If no files are specified, or a specified file does not exist
  * `FileExistsError` &mdash; If `destination` or `documentation` folder does not exist, and `create_folders` is `False`
  * `NotADirectoryError` &mdash; If `destination` or `documentation` folder is a file
  * `OSError` &mdash; If `destination` or `documentation` folder does not exist, `create_folders` is `True` and the folder creation failed; i.e., the error raised by `os.makedirs`
  * `subprocess.CalledProcessError` &mdash; if an error occurs when calling `clang` (compiler error etc.), or if the return from `clang --version` is invalid (unable to deduce version number)

##### `export_header`

```python
def export_header(file           : str,
                  output         : str,
                  godot          : str|None  = 'godot-cpp',
                  clang          : str       = "clang",
                  sysincludes    : list[str] = [],
                  includes       : list[str] = [],
                  destination    : str|None  = None,
                  documentation  : str|None  = None,
                  create_folders : bool      = True,
                  args           : list[str] = []) -> tuple[str,list[str]|None]:
```

Generates a <nobr>C++</nobr> source file, and optionally XML documentation, which export the <nobr>C++</nobr> classes,
methods, enums, signals, etc., marked with `godot::` attributes from the specified <nobr>C++</nobr> header file.
This function does not generates the *entry point* to the GDExtension.

Most of the arguments are the same as [`generate_all`](#generate_all) function, with the following
changes:

  * `file` (string) &mdash; Specifies a single <nobr>C++</nobr> header file to export Godot classes from
  * `output` (string) *or* `destination` &mdash; If `output` is specified this is the name of the
    generated <nobr>C++</nobr> source file and `destination` is ignored. If not specified (`None`), `destination` is
    used to specify the folder in which to create the generated <nobr>C++</nobr> source file (with an automatically
    generated name); i.e., same behaviour as [`generate_all`](#generate_all)

This function returns a two-tuple containing the following on success:
  * String containing the file path/name of the generated <nobr>C++</nobr> source file
  * If `documentation` is not `None` then a list of strings containing the file
    paths/names of the generated XML documentation files; otherwise `None`

On failure, several specific exceptions can be raised:

  * `ValueError` &mdash; If no files are specified, or a specified file does not exist
  * `FileExistsError`, `NotADirectoryError`, `OSError`, or `subprocess.CalledProcessError` &mdash; Same
    as for [`generate_all`](#generate_all)

##### `entry_point`

```python
def entry_point(name           : str,
                files          : list[str],
                output         : str|None = None,
                destination    : str|None = None,
                create_folders : bool = True) -> str:
```

Generates a <nobr>C++</nobr> source file containing the entry point of the GDExtension for registering all
the exported classes etc. which will be exported from the specified <nobr>C++</nobr> header files.

This function takes *some* of the same arguments as the [`generate_all`](#generate_all) function:

  * `name` (string) &mdash; Name of the GDExtension. Used for generating the "entry_symbol" function name
    (`<name>_library_init`) and the name of the generated <nobr>C++</nobr> file for the entry point (`<name>.lib.cpp`). Must be a valid <nobr>C++</nobr> identifier
  * `files` (list of strings) &mdash; List of <nobr>C++</nobr> header files for which files have been (or will be) generated
  * `output` (string) *or* `destination` &mdash; If `output` is specified this is the name of the
    generated <nobr>C++</nobr> source file and `destination` is ignored. If not specified (`None`), `destination` is
    used to specify the folder to create the generated <nobr>C++</nobr> header file (with an automatically
    generated name); i.e., same behaviour as [`generate_all`](#generate_all)
  * `create_folders` (boolean) &mdash; Specify whether to create  the `destination` folder if it does not exist

On success returns the name of the <nobr>C++</nobr> function generated as the extension's
entry point; i.e., the value returned by [`entry_point_name`](#entry_point_name).

On failure, several specific exceptions can be raised:

  * `ValueError` &mdash; If name is not a valid <nobr>C++</nobr> identifier (valid identifier contains only `[a-zA-Z0-9_]` and cannot start with a number)
  * `ValueError` &mdash; If no files are specified
  * `FileExistsError` &mdash;  If `output` is not set, `destination` folder does not exist, and `create_folders` is `False`
  * `NotADirectoryError` &mdash; If `output` is not set, and `destination` folder is a file
  * `OSError` &mdash;  If `output` is not set, `destination` folder does not exist, `create_folders` is
    `True` and the folder creation failed; i.e., the error raised by `os.makedirs`

##### `entry_point_name`

```python
gdexport.entry_point_name(name : str) -> str:
```

This function takes the name of the GDExtension and returns the name of the <nobr>C++</nobr> function which will
be generated as the extension's entry point. The argument *MUST* be a valid <nobr>C++</nobr> identifier; if not,
a `ValueError` will be raised.

##### `list_doc_files`

>[!TIP]
>
> This method is relatively expensive for a simple query as it has to uses clang to parse the
> input header files to deduce the classes which will have documentation exported. Therefore,
> it is advised where possible to use the return values from `generate_all` or `export_header`.
> However, in some cases this method may be necessary; for example,
> in automated builds such as [SCons](#scons).

```python
gdexport.list_doc_files(files          : list[str],
                        godot          : str|None  = 'godot-cpp',
                        clang          : str       = "clang",
                        sysincludes    : list[str] = [],
                        includes       : list[str] = [],
                        documentation  : str|None  = None,
                        args           : list[str] = []) -> str[list]:
```

Gets the list of XML documentation files which will be generated for the specified input files.

Arguments are similar to [`generate_all`](#generate_all), except that if `documentation` is `None`
it is treated as if it was the empty string (means the `doc_classes` folder in current working
directory) as it should behave as if the documentation export is requested.

Returns a list of string denoting the path to the XML documentation files which will be created
by [`generate_all`](#generate_all) or [`export_header`](#export_header) on success.

A `ValueError` is raised if no files are specified, or a specified file does not exist. 

> [!CAUTION]
>
> This method ignores compile errors reported by clang, so we need to be able to build this list
> if possible regardless of compilation errors. This MAY result in inaccuracies as a result.


#### SCons

As an SCons SConstruct file is simply a python script it is possible to automatically
configure the generation of the files, and include them directly within the build.

The `gdexport` module contains an `scons` submodule:

```python
import gdexport.scons
```

This module contains one function which can setup the build:

```python
gdexport.scons.configure_generate(env            : SCons.Environment
                                  name           : str,
                                  files          : list[str],
                                  godot          : str|None       = None,
                                  clang          : str            = "clang",
                                  sysincludes    : list[str]|None = None,
                                  includes       : list[str]      = [],
                                  destination    : str|None       = None,
                                  documentation  : str|None       = None,
                                  args           : list[str]      = []) -> list[SCons.Node]:
```

The arguments are similar to the [`generate_all`](#generate_all) function, with a few changes:

  * `env` (SCons Environment) &mdash; The SCons environment to add the builders to
  * `godot`, `sysincludes` &mdash; If `sysincludes` is `None` (the default) the path is initialised
    with `env["CPPPATH"]`; hence, the path to `godot-cpp` (`godot` argument) is not required as the
    header paths will be included from `env["CPPPATH"]` (default set to `None`). If `sysincludes` is a
    list then it is not populated from `env["CPPPATH"]` and `godot` may need specifying.
  * `documentation` &mdash; As well as generating the XML documentation in the specified path (if this
    argument is not `None`), the method will also automatically embed the documentation in the library; see
    [Godot documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_docs_system.html). Essentially, it will call the `env.GodotCPPDocData` builder passing the list of XML
    documentation files, and will add the <nobr>C++</nobr> source that method generates to the returned source list

This function returns a list of source files which will be generated by the builders (to add to
the sources for the extension).

For example, for the previous example considered,
```python
sources = Glob("src/*.cpp")
sources += gdexport.scons.configure_generate(env, 'example', Glob("src/*.hpp"))
```

Alternatively, instead import the `gdexport` module and call the various functions directly.
For example, in its basic form, `scons.configure_generate` generates the following:

```python
def gdexport_entry_point(env,target,source):
    gdexport.entry_point(name, source, target[0])

def gdexport_header(env, target, source):
    gdexport.export_header(source[0],
                           output=target[0],
                           sysincludes=env["CPPPATH"],
                           create_folders=True,
                           godot=None)

env.Append(BUILDERS={
    "GDExportEntryPoint" : Builder(action=lambda e,t,s:gdexport.entry_point(s, t)),
    "GDExportHeader" : Builder(action=gdexport_header,
                               suffix='.gen.cpp',
                               src_suffix='.hpp')
})

sources = [env.GDExportHeader(destination+'/'+pathlib.Path(str(x)).stem, source=x) for x in files]
sources.append(env.GDExportEntryPoint(destination+'/'+name+'.lib.cpp', source=files))
```
