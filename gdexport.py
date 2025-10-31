#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2025 Gridshadows Gaming <https://www.gridshadows.co.uk>
# SPDX-License-Identifier: Zlib

"""
Package to generate the export interface for a GDExtension from the C++ source
files.

In order to successfully run, it requires a built version of the
[`godot-cpp` Git repository](https://github.com/godotengine/godot-cpp) and
a recent Clang version (tested with Clang 19)

See README.md for details on the attributes necessary for the export.
"""

__author__ = "Gridshadows Gaming"
__copyright__ = "Copyright 2025 Gridshadows Gaming"
__license__ = "ZLib"
__version__ = "0.1.0"
__status__ = "Development"

import subprocess
import re
import pathlib
import os
from importlib import resources
import platform

def _get_plugin_path():
    """
    Gets a context manager object for the resource file for the clang plugin:

    ```
    with _get_plugin_path() as plugin:
        # 'plugin' is now the path to the plugin
    ```
    """
    gdexport_plugin_lib = ''
    match platform.system():
        case "Linux":
            gdexport_plugin_lib = 'libgdexport.so'
        case "Darwin":
            gdexport_plugin_lib = 'libgdexport.dylib'
        case "Windows":
            gdexport_plugin_lib = 'gdexport.dll'
        case _:
            gdexport_plugin_lib = 'libgdexport.so'
    if __package__ is None:
        return resources.as_file(resources.files("lib") / gdexport_plugin_lib)
    else:
        return resources.as_file(resources.files(__package__) / "lib" / gdexport_plugin_lib)

def _dest_folder(folder : str|None, create_folders : bool, desc : str) -> pathlib.Path|None:
    """
    Gets a pathlib.Path path to the specified folder, creating it if necessary,
    or None if no folder specified

    :param str|None folder:     Path to folder to get path object for
    :param bool create_folders: Create the specified folder if it does not exists
    :param str desc:            Description of the folder "type/use" for use in error messages

    :raises FileExistsError:    If the folder does not exist and `create_folders` is `False`
    :raises NotADirectoryError: If the folder is a file
    """
    if folder:
        dest = pathlib.Path(str(folder))
        if not dest.exists():
            if create_folders:
                os.makedirs(str(folder))
            else:
                raise FileExistsError("The specified {} path does not exist".format(desc))
        elif not dest.is_dir():
            raise NotADirectoryError("The specified {} path is not a folder".format(desc))
        return dest
    else:
        return None

def validate_name(name : str):
    """
    Checks if the specified name is a valid C++ identifier
    """
    return re.match('^[a-zA-Z_][a-zA-Z0-9_]*$', name) is not None

def _check_clang_version(clang : str):
    """
    Check if the specified path actually points to a valid version of clang

    :todo: Check version number

    :param str clang: Path (relative, absolute, or exe name in PATH) to the clang executable

    :raises subprocess.CalledProcessError: if the return from `clang --version` is invalid

    :return: The version number of the specified clang executable
    """
    check_version = subprocess.run([str(clang), "--version"], capture_output=True, encoding="utf-8", check=True)
    version = re.search(r"clang.*?(([0-9]+)((?:[.](?:[0-9]+))*))", check_version.stdout)
    if not version:
        raise subprocess.CalledProcessError(0, "clang --version", check_version.stdout,
                                            check_version.stderr)
    # TODO: Validate required minimum clang version X?
    return version[1]+'.'+version[2]

def _load_godot_paths(godot : str|None, sysincludes : list[str]) -> list[str]:
    """
    If path to `godot-cpp` is specified adds the include paths for `godot-cpp` to the specified `sysincludes`

    :param str|None godot:         Path to `godot-cpp` folder to deduce include paths from
    :param list[str]) sysincludes: Current list of `sysincludes` to add include paths to

    :return: The modified `sysincludes` list
    """
    if godot:
        godot_path = pathlib.Path(str(godot))
        sysincludes.append(str(godot_path / "gdextension"))
        sysincludes.append(str(godot_path / "include"))
        sysincludes.append(str(godot_path / "gen/include"))
    return sysincludes

def _load_arguments(clang          : str,
                    plugin         : pathlib.Path,
                    sysincludes    : list[str],
                    includes       : list[str],
                    documentation  : str|None,
                    args           : list[str]) -> list[str]:
    """
    Generates the argument list for calling clang with the plugin to process a header file

    :param str clang:              Path (relative, absolute, or exe name in PATH) to the clang executable
    :param str plugin:             Path to the plugin for gdexport
    :param list[str] sysincludes:  List of paths to treat as system include directories;
                                   i.e., `-isystem` paths to Clang
    :param list[str] includes:     List of paths to treat as normal include directories;
                                   i.e., `-I` paths to Clang
    :param str|None documentation: Specify whether to also extract Doxygen style comments from the
                                   C++ source and generate the Godot XML documentation for the extension.
    :param list[str] args:         List of extra command line arguments to pass to clang

    :return: List of arguments (strings) to pass to `subprocess` run methods to run clang. The last
             two arguments of the returned array will be the empty string and should be replaced with
             the output and input file paths, respectively, for the header file to process
    """
    arguments = [str(clang), "-fsyntax-only", "-Xclang", "-std=c++17", "-fplugin="+str(plugin)]
    for inc in sysincludes:
        arguments += ["-Xclang", "-isystem", "-Xclang", str(inc)]
    for inc in includes:
        arguments += ["-Xclang", "-I", "-Xclang", str(inc)]
    arguments += [y for x in args for y in ("-Xclang", str(x))]
    if documentation:
        arguments += [
            "-Xclang", "-plugin-arg-gdexport", "-Xclang", "-doc",
            "-Xclang", "-plugin-arg-gdexport", "-Xclang", str(documentation)
        ]
    arguments += [
        "-Xclang", "-plugin-arg-gdexport", "-Xclang", "-out",
        "-Xclang", "-plugin-arg-gdexport", "-Xclang", ""
    ]
    arguments.append("")
    return arguments

def generate_all(name           : str,
                 files          : list[str],
                 godot          : str|None  = 'godot-cpp',
                 clang          : str       = "clang",
                 sysincludes    : list[str] = [],
                 includes       : list[str] = [],
                 destination    : str|None  = None,
                 documentation  : str|None  = None,
                 create_folders : bool = True,
                 quiet          : bool = False,
                 args           : list[str] = []) -> tuple[list[str],list[str]|None,str]:
    """
    Generates C++ source files which export the C++ classes, methods, enums,
    signals, etc., marked with `godot::` attributes to be accessible via a
    Godot GDExtension.

    Generates a C++ source file for each input file `<file>.gen.cpp` and
    one file for the main entry point `<name>.lib.cpp`.

    :param str name:               Name of the GDExtension. Used for generating
                                   the "entry_symbol" function name (`<name>_library_init`)
                                   and the name of the generated C++ file for the
                                   entry point (`<name>.lib.cpp`).
                                   Must be a valid C++ identifier
    :param list[str] files:        List of C++ header files to process to export
                                   Godot classes from
    :param str|None  godot:        Path to the root of the checkout of the `godot-cpp` repo
                                   Default is 'godot-cpp' folder in the current working directory.
                                   If `None` does not automatically deduce godot-cpp header paths.
    :param str       clang:        Path to the clang executable
                                   (default = `clang` in PATH)
    :param list[str] sysincludes:  List of paths to treat as system include directories;
                                   i.e., `-isystem` paths to Clang
    :param list[str] includes:     List of paths to treat as normal include directories;
                                   i.e., `-I` paths to Clang
    :param str|None destination:   Output directory for generated files.
                                   (default = current working directory)
    :param str|None documentation: Specify whether to also extract Doxygen style
                                   comments from the C++ source and generate the
                                   Godot XML documentation for the extension.
                                   Specify `None` to not generate documentation,
                                   `""` (empty string) to generate in default
                                   location (`doc_classes` in current working),
                                   or path to directory to generate in otherwise
    :param bool create_folders:    Specify whether to create output folders
                                   (`destination` and `documentation`) if they do not exist
    :param bool quiet:             Specifies whether to suppress status messages
    :param list[str] args:         List of extra command line arguments to pass to clang

    :raises ValueError:         If name is not a valid C++ identifier (valid identifier
                                contains only [a-zA-Z0-9_] and cannot start with a number)
    :raises ValueError:         If no files are specified, or a specified file does not exist
    :raises FileExistsError:    If `destination` or `documentation` folder does not exist,
                                and `create_folders` is `False`
    :raises NotADirectoryError: If `destination` or `documentation` folder is a file
    :raises OSError:            If `destination` or `documentation` folder does not exist,
                                `create_folders` is `True` and the folder creation
                                failed; i.e., the error raised by `os.makedirs`
    :raises subprocess.CalledProcessError: if an error occurs when calling `clang` (compiler error etc.),
                                or if the return from `clang --version` is invalid
                                (unable to deduce version number)

    :return: Three-tuple containing:
               - List of strings containing the file paths/names of the generated C++ source file
               - If `documentation` is not `None` then a list of strings containing the file
                 paths/names of the generated XML documentation files; otherwise None
               - The name of the C++ function generated as the extension's entry point;
                 i.e., the value returned by `entry_point_name`.
    """
    if not validate_name(name):
        raise ValueError("Specified name is not a valid C++ identifier: "+name)

    if len(files) == 0:
        raise ValueError("No files to process are specified")
    for file in files:
        filepath = pathlib.Path(str(file))
        if not filepath.exists():
            raise ValueError("Specified file does not exist: "+str(file))

    version = _check_clang_version(clang)
    if not quiet:
        print("Exporting C++ GDExtension interface with clang v{} [{}]{}".format(
            version, clang, " with documentation" if documentation is not None else ""
        ))

    dest = _dest_folder(destination, create_folders, 'destination')
    if documentation == "":
        documentation = "doc_classes"
    docdest = _dest_folder(documentation, create_folders, 'documentation')

    sysincludes = _load_godot_paths(godot, sysincludes)

    result = []
    docs = []
    with _get_plugin_path() as library:
        arguments = _load_arguments(clang, library, sysincludes, includes, documentation, args)

        for file in files:
            filepath = pathlib.Path(str(file))
            destfile = filepath.with_suffix(".gen.cpp").name
            if dest:
                destfile = str(dest / destfile)
            arguments[-2] = destfile
            result.append(destfile)

            if not quiet:
                print(" - Processing {} > {}".format(str(file), destfile))
            arguments[-1] = str(file)
            generated,generated_docs = _export_header(arguments, docdest)
            result.append(generated)
            if generated_docs:
                docs += generated_docs

    library_cpp = name+".lib.cpp"
    if dest:
        library_cpp = str(dest / library_cpp)
    result.append(library_cpp)
    if not quiet:
        print(" - Generating {}".format(library_cpp))
    entry = _entry_point(name, files, library_cpp)
    if documentation:
        return result,docs,entry
    else:
        return result,None,entry

def _export_header(arguments : list[str], documentation : pathlib.Path|None) -> tuple[str,list[str]|None]:
    """
    Call clang with the plugin to process a header file

    :param list[str] arguments: The arguments to pass to `subprocess` to run clang; i.e., the
                                arguments returned by `_load_arguments` with the last two arguments
                                replaced with output and input file for the header file to process
    :param pathlib.Path|None documentation: Path to documentation output folder, or None for no doc output

    :raises subprocess.CalledProcessError: if an error occurs when calling `clang`

    :return: Two-tuple containing:
               - String containing the file path/name of the generated C++ source file
               - If `documentation` is not `None` then a list of strings containing the file
                 paths/names of the generated XML documentation files; otherwise None
    """
    if documentation:
        result = subprocess.check_output(arguments, encoding='utf-8')
        return arguments[-2],[str(documentation/(x.strip()+".xml")) for x in result.splitlines() if x.strip() != '']
    else:
        subprocess.run(arguments, check=True)
        return arguments[-2],None

def export_header(file           : str,
                  output         : str|None  = None,
                  godot          : str|None  = 'godot-cpp',
                  clang          : str       = "clang",
                  sysincludes    : list[str] = [],
                  includes       : list[str] = [],
                  destination    : str|None  = None,
                  documentation  : str|None  = None,
                  create_folders : bool      = True,
                  args           : list[str] = []) -> tuple[str,list[str]|None]:
    """
    Generates a C++ source file, and optionally XML documentation, which export the C++ classes,
    methods, enums, signals, etc., marked with `godot::` attributes from the specified C++ header file.
    This function does not generates the *entry point* to the GDExtension.

    :param str       file:         C++ header file to process to export Godot classes from
    :param str       output:       Specifies the file to output the generated code to. If not
                                   specified uses an automatically generated name with
                                   the specified `destination` folder
    :param str|None  godot:        Path to the root of the checkout of the `godot-cpp` repo
                                   Default is 'godot-cpp' folder in the current working directory.
                                   If `None` does not automatically deduce godot-cpp header paths.
    :param str       clang:        Path to the clang executable
                                   (default = `clang` in PATH)
    :param list[str] sysincludes:  List of paths to treat as system include directories;
                                   i.e., `-isystem` paths to Clang
    :param list[str] includes:     List of paths to treat as normal include directories;
                                   i.e., `-I` paths to Clang
    :param str|None destination:   Output directory for generated file. Only applies if `output`
                                   is not specified. (default = current working directory)
    :param str|None documentation: Specify whether to also extract Doxygen style
                                   comments from the C++ source and generate the
                                   Godot XML documentation for the extension.
                                   Specify `None` to not generate documentation,
                                   `""` (empty string) to generate in default
                                   location (`doc_classes` in current working),
                                   or path to directory to generate in otherwise
    :param bool create_folders:    Specify whether to create output folders
                                   (`destination` and `documentation) if they do not exist
    :param bool quiet:             Specifies whether to suppress status messages
    :param list[str] args:         List of extra command line arguments to pass to clang

    :raises ValueError:         If the specified input file does not exist
    :raises FileExistsError:    If `destination` (and `output` not specified) or `documentation`
                                folder does not exist, and `create_folders` is `False`
    :raises NotADirectoryError: If `destination` (and `output` not specified) or `documentation` folder is a file
    :raises OSError:            If `destination` (and `output` not specified) or `documentation` folder does not exist,
                                `create_folders` is `True` and the folder creation
                                failed; i.e., the error raised by `os.makedirs`
    :raises subprocess.CalledProcessError: if an error occurs when calling `clang` (compiler error etc.),
                                or if the return from `clang --version` is invalid
                                (unable to deduce version number)

    :return: Two-tuple containing:
               - String containing the file path/name of the generated C++ source file
               - If `documentation` is not `None` then a list of strings containing the file
                 paths/names of the generated XML documentation files; otherwise None
    """
    _check_clang_version(clang)

    filepath = pathlib.Path(str(file))
    if not filepath.exists():
        raise ValueError("Specified file does not exist: "+str(file))
    if not output:
        dest = _dest_folder(destination, create_folders, "destination")
        output = filepath.with_suffix(".gen.cpp").name
        if dest:
            output = str(dest / output)
    if documentation == "":
        documentation = "doc_classes"
    docdest = _dest_folder(documentation, create_folders, 'documentation')

    sysincludes = _load_godot_paths(godot, sysincludes)

    with _get_plugin_path() as library:
        arguments = _load_arguments(clang, library, sysincludes, includes, documentation, args)
        arguments[-2] = str(output)
        arguments[-1] = str(file)
        return _export_header(arguments, docdest)

def _entry_point(name : str, files : list[str], output : str) -> str:
    """
    Generates a C++ source file containing the entry point of the GDExtension for registering all
    the exported classes etc. which will be exported from the specified C++ header files.

    See README.md for details on the attributes necessary for the export.

    :param str name:         Name of the GDExtension.
    :param list[str] files:  List of C++ header files for which files have been (or will be)
                             generated
    :param stroutput:        Specifies the file to output the generated code to.

    :return: The name of the C++ function generated as the extension's entry point;
             i.e., the value returned by `entry_point_name`
    """
    ids = [re.sub("[^a-zA-Z0-9_]", '_', pathlib.Path(str(x)).stem) for x in files]
    with open(str(output), 'w', encoding='utf-8') as f:
        f.write('#include <gdextension_interface.h>\n'
                '#include <godot_cpp/core/defs.hpp>\n'
                '#include <godot_cpp/godot.hpp>\n'
                '\n'
                'using namespace godot;\n'
                '\n')
        for identifier in ids:
            f.write('void initialize_{0}();\n'.format(identifier))
        f.write(('\n'
                 'void initialize_{0}_module(ModuleInitializationLevel p_level)\n'
                 '{{\n'
                 '    if(p_level != MODULE_INITIALIZATION_LEVEL_SCENE)\n'
                 '    {{\n'
                 '        return;\n'
                 '    }}\n').format(name))
        for identifier in ids:
            f.write('    initialize_{0}();\n'.format(identifier))
        f.write(('}}\n'
                 '\n'
                 'void uninitialize_{0}_module(ModuleInitializationLevel p_level)\n'
                 '{{\n'
                 '    if(p_level != MODULE_INITIALIZATION_LEVEL_SCENE)\n'
                 '    {{\n'
                 '        return;\n'
                 '    }}\n'
                 '}}\n'
                 '\n'
                 'extern "C"\n'
                 '{{\n'
                 'GDExtensionBool GDE_EXPORT {0}_library_init('
                 'GDExtensionInterfaceGetProcAddress p_get_proc_address, '
                 'const GDExtensionClassLibraryPtr p_library,'
                 'GDExtensionInitialization *r_initialization)\n'
                 '{{\n'
                 '    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);\n'
                 '    init_obj.register_initializer(initialize_{0}_module);\n'
                 '    init_obj.register_terminator(uninitialize_{0}_module);\n'
                 '    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);\n'
                 '    return init_obj.init();\n'
                 '}}\n'
                 '}}\n').format(name))
    return entry_point_name(name)

def entry_point(name           : str,
                files          : list[str],
                output         : str|None = None,
                destination    : str|None = None,
                create_folders : bool = True) -> str:
    """
    Generates a C++ source file containing the entry point of the GDExtension for registering all
    the exported classes etc. which will be exported from the specified C++ header files.

    See README.md for details on the attributes necessary for the export.

    :param str name:               Name of the GDExtension. Used for generating
                                   the "entry_symbol" function name (`<name>_library_init`)
                                   and the name of the generated C++ file for the
                                   entry point (`<name>.lib.cpp`).
                                   Must be a valid C++ identifier
    :param list[str] files:        List of C++ header files for which files have been (or will be)
                                   generated
    :param str       output:       Specifies the file to output the generated code to. If not
                                   specified uses an automatically generated name (`<name>.lib.cpp`)
                                   with the specified `destination` folder
    :param str|None destination:   Output directory for generated file if `output` is not specified
                                   (default = current working directory)
    :param bool create_folders:    Specify whether to create the output folder
                                   (`destination`) if it does not exist (and `output` is not set)

    :raises ValueError:         If name is not a valid C++ identifier (valid identifier
                                contains only [a-zA-Z0-9_] and cannot start with a number)
    :raises ValueError:         If no files are specified
    :raises FileExistsError:    If `output` is not set, `destination` folder does not exist,
                                and `create_folders` is `False`
    :raises NotADirectoryError: If `output` is not set, and `destination` folder is a file
    :raises OSError:            If `output` is not set, `destination` folder does not exist,
                                `create_folders` is `True` and the folder creation
                                failed; i.e., the error raised by `os.makedirs`

    :return: The name of the C++ function generated as the extension's entry point;
             i.e., the value returned by `entry_point_name`
    """
    if not validate_name(name):
        raise ValueError("Specified name is not a valid C++ identifier: "+name)
    if len(files) == 0:
        raise ValueError("No files to process are specified")
    # TODO: Add output/destination validation
    if not output:
        dest = _dest_folder(destination, create_folders, "destination")
        if dest:
            output = str(dest / (name+".lib.cpp"))
        else:
            output = name+".lib.cpp"
    return _entry_point(name, files, output)

def entry_point_name(name : str) -> str:
    """
    This function takes the name of the GDExtension and returns the name of the C++ function which
    will be generated as the extension's entry point. The arguments *MUST* be a valid C++
    identifier; if not, a `ValueError` will be raised.
    """
    if not validate_name(name):
        raise ValueError("Specified name is not a valid C++ identifier: "+name)
    return '{0}_library_init'.format(name)

def list_doc_files(files          : list[str],
                   godot          : str|None  = 'godot-cpp',
                   clang          : str       = "clang",
                   sysincludes    : list[str] = [],
                   includes       : list[str] = [],
                   documentation  : str|None  = '',
                   args           : list[str] = []) -> list[str]:
    """
    Gets the list of XML documentation files which will be generated for the specified input files.

    :param list[str] files:        List of C++ header files to containing Godot classes
                                   for which documentation will be generated
    :param str|None  godot:        Path to the root of the checkout of the `godot-cpp` repo
                                   Default is 'godot-cpp' folder in the current working directory.
                                   If `None` does not automatically deduce godot-cpp header paths.
    :param str       clang:        Path to the clang executable
                                   (default = `clang` in PATH)
    :param list[str] sysincludes:  List of paths to treat as system include directories;
                                   i.e., `-isystem` paths to Clang
    :param list[str] includes:     List of paths to treat as normal include directories;
                                   i.e., `-I` paths to Clang
    :param str|None documentation: Specify location where the XML documentation will be generated.
                                   Use `""` (empty string) or 'Non' to generate in default
                                   location (`doc_classes` in current working)
    :param list[str] args:         List of extra command line arguments to pass to clang

    :raises ValueError:         If no files are specified, or a specified file does not exist

    :return: List of string denoting the path to the XML documentation files which will be created
             by `generate_all` or `export_header`.
    """

    if len(files) == 0:
        raise ValueError("No files to process are specified")
    for file in files:
        filepath = pathlib.Path(str(file))
        if not filepath.exists():
            raise ValueError("Specified file does not exist: "+file)

    version = _check_clang_version(clang)

    if not documentation:
        documentation = "doc_classes"
    dest = pathlib.Path(str(documentation))

    sysincludes = _load_godot_paths(godot, sysincludes)

    result = []
    with _get_plugin_path() as library:
        arguments = [str(clang), "-fsyntax-only", "-Xclang", "-std=c++17", "-fplugin="+str(library)]
        for inc in sysincludes:
            arguments += ["-Xclang", "-isystem", "-Xclang", str(inc)]
        for inc in includes:
            arguments += ["-Xclang", "-I", "-Xclang", str(inc)]
        arguments += [y for x in args for y in ("-Xclang", str(x))]
        arguments += [
            "-Xclang", "-plugin-arg-gdexport", "-Xclang", "-nameonly"
        ]
        arguments.append("")

        for file in files:
            arguments[-1] = str(file)
            output = subprocess.run(arguments, encoding='utf-8', capture_output=True)
            result += [str(dest/(x.strip()+".xml")) for x in output.stdout.splitlines() if x.strip() != '']
    return result

if __name__ == "__main__":
    import argparse
    import sys

    parser = argparse.ArgumentParser(description="Script to generate the export interface for a GDExtension from the C++ source files")
    parser.add_argument("--license", help="Print the license information and quit", action="store_true")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--godot", "-g", metavar="DIR", dest='godot', default='godot-cpp',
                       help="Path to the root of the checkout of the godot-cpp repo (automatically calculates godot include)")
    group.add_argument("--no-godot", action='store_const', dest='godot', const=None,
                       help="Don't automatically calculate include folders from godot-cpp repo directory")
    parser.add_argument("--clang", "-c", metavar="EXE", help="Path to the clang executable to use", default="clang")
    parser.add_argument("--isystem", "-s", metavar="DIR", action="append", default=[],
                        help="List of paths to treat as system include directories; i.e., -isystem paths to clang")
    parser.add_argument("--include", "-I", metavar="DIR", action="append", default=[],
                        help="List of paths to treat as system include directories; i.e., -I paths to clang")
    parser.add_argument("--output", "-o", metavar="DIR", help="Specifies the output directory (default = current working directory)")
    parser.add_argument("--doc", "-d", metavar="DIR", nargs="?", default=None, const="",
                        help="Specifies to export XML documentation from Doxygen comments to specified folder (current working directory if no argument specified)")
    parser.add_argument("--make-dirs", "-m", action="store_true", default=False,
                        help="Specifies to create output and doc folder if they don't exist")
    parser.add_argument("--quiet", "-q", action="store_true", default=False,
                        help="Don't output informational status messages")
    parser.add_argument("--clang-arg", "-a", metavar="ARG", action="append", default=[],
                        help="Specifies that the next argument should be passed as an extra argument to clang")
    parser.add_argument("name", help="Name of the GDExtension. Used for generating the entry_symbol name '<name>_library_init'")
    parser.add_argument("file", nargs='+', help="List of C++ header files to process to export Godot classes from")
    args = parser.parse_args()

    if args.license:
        if __package__ is None:
            print(resources.files("LICENSE.md").read_text())
        else:
            print((resources.files(__package__) / "LICENSE.md").read_text())
        exit(0)

    if not validate_name(args.name):
        print('Specified name is not a valid C++ identifier', file=sys.stderr)

    try:
        generate_all(args.name,
                        args.file,
                        godot = args.godot,
                        clang = args.clang,
                        sysincludes = args.isystem,
                        includes = args.include,
                        destination = args.output,
                        documentation = args.doc,
                        create_folders = args.make_dirs,
                        quiet = args.quiet,
                        args = args.clang_arg)
    except ValueError as e:
        print('Unable to generate interface - {}'.format(e), file=sys.stderr)
    except FileExistsError as e:
        print('Destination or documentation folder does not exist', file=sys.stderr)
    except NotADirectoryError as e:
        print('Destination or documentation folder is a file', file=sys.stderr)
    except OSError as e:
        print('Unable to create folders or files - {}'.format(e), file=sys.stderr)
    except subprocess.CalledProcessError as e:
        if e.returncode == 0:
            print('The specified clang does not appear to be a valid clang executable', file=sys.stderr)
        else:
            print('Clang returned as error - return code {}'.format(e.returncode), file=sys.stderr)
    except BaseException as e:
        print('An unknown error occurred: {}'.format(e))
