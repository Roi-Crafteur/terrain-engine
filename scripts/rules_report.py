"""The rules of the project in report mode (agent 42): warnings, never a failure.

- Size: a file longer than 500 lines among those the blocking check of check_project_rules.py
  leaves out: the C++ of terrain_unreal (.cpp, .h, .hpp), every .h, and the Python of scripts/,
  terrain_unreal/Scripts/ and tests/.
- Functions: a function longer than 80 lines, from the line of its name to its last line, in every
  C++ and Python file checked (terrain_core included). Python by its syntax tree (ast). C++ by its
  braces, once its comments, literals and preprocessor lines are blanked: a brace opened at the
  level of a namespace or a class right after a closing parenthesis (and its qualifiers), or after
  the initializers of a constructor, opens a function, and every brace nested in it belongs to it
  (its lambdas and its blocks). A heuristic: a macro that hides a brace, or #if branches that
  repeat a signature, can mislead it; it reports, it does not judge.
"""

import ast
import bisect
import re
from pathlib import Path

MAX_LINES = 500
MAX_FUNCTION_LINES = 80
CPP_SUFFIXES = {".cpp", ".hpp", ".h"}
CPP_DIRS = ("terrain_core", "terrain_viewer", "tests", "terrain_unreal")
PYTHON_DIRS = ("scripts", "terrain_unreal/Scripts", "tests")
# The files whose size the blocking check leaves out: (directory, suffixes).
SIZE_REPORTED = (("terrain_unreal", {".cpp", ".hpp", ".h"}), ("terrain_core", {".h"}),
                 ("terrain_viewer", {".h"}), ("tests", {".h"}), ("scripts", {".py"}),
                 ("terrain_unreal/Scripts", {".py"}), ("tests", {".py"}))

# What is not code in C++: numbers first (their digit separators 1'000 are no character
# literals), then raw strings, comments, strings, characters and preprocessor lines.
NOT_CODE = re.compile(
    r"""(?P<number>\b\d[\w.']*)
      | (?P<raw>\b(?:u8|u|U|L)?R"(?P<delimiter>[^()\\\s"]{0,16})\(.*?\)(?P=delimiter)")
      | //[^\n]*
      | /\*.*?\*/
      | "(?:\\.|[^"\\\n])*"
      | '(?:\\.|[^'\\\n])*'
      | ^[ \t]*\#(?:\\\n|[^\n])*
    """, re.S | re.M | re.X)
QUALIFIERS = r"(?:const|volatile|noexcept|override|final|mutable|&&|&|throw|try)\s*(?:\(\)\s*)?"
FUNCTION_END = re.compile(r"\)\s*(?:" + QUALIFIERS + r")*(?:->[^{};]*)?$")
CONSTRUCTOR_END = re.compile(r"\)\s*:(?!:)[^;]*\}\s*$")  # Foo() : a_(1), b_{2} {
CONTROL = {"if", "for", "while", "switch", "catch", "return"}
MACRO = re.compile(r"[A-Z][A-Z0-9_]*")
NAME = re.compile(r"((?:[\w:]*operator\s*(?:\(\s*\)|[^\s(]+))|[~\w:]+)\s*\(")  # a name, then (


def code_of(text):
    """The C++ text with its comments, literals and preprocessor lines blanked (line breaks kept):
    the code alone, at its places."""
    def blank(match):
        return match.group() if match.group("number") else re.sub(r"[^\n]", " ", match.group())
    return NOT_CODE.sub(blank, text)


def _without_templates(header):
    """The header without its lists of template parameters (template <class T> holds no class)."""
    while True:
        match = re.search(r"\btemplate\s*<", header)
        if not match:
            return header
        depth, end = 0, match.end() - 1
        while end < len(header):
            depth += {"<": 1, ">": -1}.get(header[end], 0)
            end += 1
            if depth == 0:
                break
        header = header[:match.start()] + " " * (end - match.start()) + header[end:]


def _flat(header):
    """The header with what its parentheses, brackets and braces hold blanked: Foo(int a) gives
    Foo(     ), so that only its level 0 is read."""
    out, depth = [], 0
    for c in header:
        if c in ")]}":
            depth -= 1
        out.append(c if depth <= 0 or c == "\n" else " ")
        if c in "([{":
            depth += 1
    return "".join(out)


def _name(flat):
    """The name of the function a header declares and where it starts, or None: the first word
    before a parenthesis that is not a macro (UFUNCTION, GENERATED_BODY...)."""
    for match in NAME.finditer(flat):
        word = match.group(1).split("::")[-1].lstrip("~")
        if not MACRO.fullmatch(word):
            return match.group(1), match.start()
    return None


def _opened(code, start, end):
    """What the brace at end opens, its header being code[start:end] at the level of a scope:
    ("scope", ...) a namespace or a class, ("function", name, offset), or ("init", ...) an
    initializer or an enumeration, which holds no function."""
    flat = _flat(_without_templates(code[start:end]))
    if re.search(r"\benum\b", flat):
        return "init", None, start
    if re.search(r"\b(?:namespace|class|struct|union)\b", flat) or flat.strip() == "extern":
        return "scope", None, start
    if FUNCTION_END.search(flat) or CONSTRUCTOR_END.search(flat):
        named = _name(flat)
        if named and named[0] in CONTROL:
            return "init", None, start
        name, offset = named if named else ("(function)", len(flat) - len(flat.lstrip()))
        return "function", name, start + offset
    return "init", None, start


def cpp_functions(text):
    """The functions of a C++ text: (name, first line, number of lines)."""
    code = code_of(text)
    breaks = [m.start() for m in re.finditer("\n", code)]

    def line(position):
        return bisect.bisect_left(breaks, position) + 1

    functions, stack, statement = [], [], 0
    for match in re.finditer(r"[{};]", code):
        position, c = match.start(), match.group()
        inside = bool(stack) and stack[-1][0] != "scope"
        if c == "{":
            opened = ("nested", None, 0) if inside else _opened(code, statement, position)
            stack.append(opened)
            statement = position + 1 if opened[0] in ("scope", "function") else statement
        elif c == "}" and stack:
            kind, name, start = stack.pop()
            if kind == "function":
                first = line(start)
                functions.append((name, first, line(position) - first + 1))
            statement = position + 1 if kind in ("scope", "function") else statement
        elif c == ";" and not inside:
            statement = position + 1
    return functions


def python_functions(text):
    """The functions of a Python text: (name, first line, number of lines), from its def."""
    return [(node.name, node.lineno, node.end_lineno - node.lineno + 1)
            for node in ast.walk(ast.parse(text))
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))]


def _files(root, directory, suffixes):
    base = Path(root) / directory
    return sorted(p for p in base.rglob("*") if p.is_file() and p.suffix in suffixes)


def _read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def size_warnings(root):
    """(files checked, warnings): the files too long that the blocking check leaves out."""
    paths = sorted({p for d, suffixes in SIZE_REPORTED for p in _files(root, d, suffixes)})
    warnings = []
    for path in paths:
        count = len(_read(path).splitlines())
        if count > MAX_LINES:
            warnings.append((path, 1, f"{count} lines, more than {MAX_LINES} (reported)"))
    return len(paths), warnings


def function_warnings(root):
    """(files checked, warnings): the functions too long, C++ and Python."""
    sources = [(p, cpp_functions) for d in CPP_DIRS for p in _files(root, d, CPP_SUFFIXES)]
    sources += [(p, python_functions) for p in
                sorted({p for d in PYTHON_DIRS for p in _files(root, d, {".py"})})]
    warnings = []
    for path, functions in sources:
        try:
            found = functions(_read(path))
        except SyntaxError as error:
            warnings.append((path, error.lineno or 1, f"not read as Python: {error.msg}"))
            continue
        for name, first, count in found:
            if count > MAX_FUNCTION_LINES:
                message = f"function {name}: {count} lines, more than {MAX_FUNCTION_LINES}"
                warnings.append((path, first, message + " (reported)"))
    return len(sources), warnings
