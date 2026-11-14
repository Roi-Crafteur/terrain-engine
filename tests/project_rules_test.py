"""The rules of the project in report mode (scripts/rules_report.py, agent 42), without a build.

- C++: the functions found by their braces, with their first line (their name) and their length:
  free functions, methods in a class, constructors with initializers in parentheses and braces,
  templates, operators; not the initializers, enumerations, lambdas and blocks of a function;
  braces in comments, strings, raw strings, characters and preprocessor lines ignored; digit
  separators (1'000) taken for no character.
- Python: every def, nested ones included, by the syntax tree.
- check_project_rules.py on a small tree: a .h, a Python file and functions too long are reported
  as warnings and do not fail; a .cpp of terrain_core too long still fails.
Usage: python3 project_rules_test.py <scripts>
"""

import contextlib
import io
import pathlib
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import check_project_rules  # noqa: E402
import rules_report  # noqa: E402

failures = []


def check(passed, what):
    if not passed:
        failures.append(what)
        print(f"FAILED: {what}")


CPP = """\
#include <array>
#define BRACES { {
namespace terrain {
namespace {
constexpr std::array<int, 3> kSizes = {{1, 2, 3}};
enum class Kind { A, B };
} // namespace

template <class T>
T twice(T value) {
    const auto add = [](T a) { return a + a; };
    if (value > T{1'000}) { // } in a comment
        return add(value);
    }
    return value;
}

class Cell {
public:
    Cell(int a, int b) : a_(a), b_{b} {
        const char* text = "{";
        const char c = '{';
        static_cast<void>(text);
        static_cast<void>(c);
    }
    bool operator==(const Cell& other) const noexcept { return a_ == other.a_; }
    int sum() const;

private:
    int a_ = 0;
    int b_{0};
};

int Cell::sum() const {
    const char* raw = R"x({ ")" })x";
    static_cast<void>(raw);
    return a_ + b_;
}
} // namespace terrain

extern "C" {
int plain(int x) {
    /* { */
    return x;
}
}
"""


def test_cpp():
    found = {name: (first, count) for name, first, count in rules_report.cpp_functions(CPP)}
    check(found.get("twice") == (10, 7), f"the template function, its lambda in it: {found}")
    check(found.get("Cell") == (20, 6), f"the constructor with its initializers: {found}")
    check(found.get("operator==") == (26, 1), f"the operator on one line: {found}")
    check(found.get("Cell::sum") == (34, 5), f"the method out of its class: {found}")
    check(found.get("plain") == (42, 4), f"the function in extern \"C\": {found}")
    check(len(found) == 5, f"only the functions, no initializer, enum or lambda: {found}")
    blanked = rules_report.code_of(CPP)
    check(blanked.count("\n") == CPP.count("\n"), "the lines kept by the blanking")
    check("1'000" in blanked and "#define" not in blanked, "numbers kept, directives blanked")


def test_python():
    text = "def outer():\n    def inner():\n        return 1\n    return inner()\n\n\n" \
           "class A:\n    async def run(self):\n        pass\n"
    found = sorted(rules_report.python_functions(text))
    check(found == [("inner", 2, 2), ("outer", 1, 4), ("run", 8, 2)], f"Python: {found}")


def long_function(name, lines):
    return f"int {name}() {{\n" + "    int x = 0;\n" * lines + "    return 0;\n}\n"


def run_check(root):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        code = check_project_rules.main(root)
    return code, output.getvalue()


def test_report_mode():
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        for directory in ("terrain_core/src", "terrain_viewer", "tests", "scripts",
                          "terrain_unreal/Scripts", "terrain_unreal/Source"):
            (root / directory).mkdir(parents=True)
        (root / "terrain_core/src/long_function.cpp").write_text(long_function("f", 90))
        (root / "terrain_unreal/Source/Big.h").write_text("// line\n" * 501)
        (root / "scripts/big.py").write_text("def g():\n" + "    x = 1\n" * 90)
        code, output = run_check(root)
        check(code == 0, f"warnings only: exit {code}\n{output}")
        check("::warning file=terrain_core/src/long_function.cpp,line=1::function f: 93 lines"
              in output, f"the C++ function too long reported\n{output}")
        check("::warning file=terrain_unreal/Source/Big.h,line=1::501 lines" in output,
              f"the .h of terrain_unreal too long reported\n{output}")
        check("::warning file=scripts/big.py,line=1::function g: 91 lines" in output,
              f"the Python function too long reported\n{output}")
        (root / "terrain_core/src/too_long.cpp").write_text("// line\n" * 501)
        code, output = run_check(root)
        check(code == 1 and "::error file=terrain_core/src/too_long.cpp" in output,
              f"a .cpp of terrain_core too long still fails: exit {code}\n{output}")


test_cpp()
test_python()
test_report_mode()
print(f"project_rules_test: {len(failures)} failure(s)")
sys.exit(1 if failures else 0)
