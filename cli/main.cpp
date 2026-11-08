#include "vendor/argparse.hpp"

#include <foo/runtime.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <ranges>
#include <string_view>
#include <algorithm>

#include <foo/lexer.hpp>
#include <foo/util/scoped_timer.hpp>
#include <foo/parser.hpp>
#include <foo/source.hpp>
#include <foo/ast_printer.hpp>

#include <foo/collector.hpp>

#include <foo/runtime.hpp>
#include <foo/util/str.hpp>

#include "foo/natives.hpp"
#include "foo/debug/debug_out.hpp"

namespace fs = std::filesystem;

namespace clr
{
    inline constexpr std::string_view black   = "\033[1;30m";
    inline constexpr std::string_view red     = "\033[1;31m";
    inline constexpr std::string_view green   = "\033[1;32m";
    inline constexpr std::string_view yellow  = "\033[1;33m";
    inline constexpr std::string_view blue    = "\033[1;34m";
    inline constexpr std::string_view magenta = "\033[1;35m";
    inline constexpr std::string_view cyan    = "\033[1;36m";
    inline constexpr std::string_view white   = "\033[1;37m";
    inline constexpr std::string_view reset   = "\033[0m";

    inline constexpr std::string_view bright_black   = "\033[1;90m";
    inline constexpr std::string_view bright_red     = "\033[1;91m";
    inline constexpr std::string_view bright_green   = "\033[1;92m";
    inline constexpr std::string_view bright_yellow  = "\033[1;93m";
    inline constexpr std::string_view bright_blue    = "\033[1;94m";
    inline constexpr std::string_view bright_magenta = "\033[1;95m";
    inline constexpr std::string_view bright_cyan    = "\033[1;96m";
    inline constexpr std::string_view bright_white   = "\033[1;97m";
}

static std::string colored(std::string_view v, std::string_view color)
{
    return std::string{color} + std::string{v} + std::string{clr::reset};
}

enum class Severity
{
    Error,
    Warning,
};

static void print_diagnostics(
    std::string_view message,
    std::string_view src,
    foo::SourceSpan span,
    Severity severity,
    const fs::path& file = {})
{
    const bool is_error = severity == Severity::Error;
    const std::string_view col = is_error ? clr::red : clr::yellow;

    std::cout << col << (is_error ? "error" : "warning") << clr::reset << ": " << message << '\n';

    if (!file.empty())
        std::cout << file.string() << '\n';

    const auto lines = foo::get_diagnostic_lines(src, span, is_error ? 2 : 1);

    if (lines.empty())
    {
        std::cout << "NO LINES FOUND\n";
        return;
    }

    const auto pad_len = std::to_string(lines.back().line_number).size();
    const std::string pad(pad_len, ' ');

    for (const auto& dl : lines)
    {
        const std::string num = std::to_string(dl.line_number);

        std::cout << clr::cyan << num << std::string(pad_len - num.size(), ' ')
                  << " |" << clr::bright_white << ' ' << dl.text << clr::reset << '\n';

        if (dl.underline_length > 0)
        {
            std::cout << pad << ' ' << clr::cyan << '|' << clr::reset << ' '
                      << std::string(dl.underline_start, ' ')
                      << col << std::string(dl.underline_length, '^') << ' ' << message
                      << clr::reset << '\n';
        }
    }
}

static void inject(foo::TypeTable& types, foo::NativeRegistry& natives, foo::NativeTable& table)
{
    foo::Runtime::inject(types, natives, table);

    foo::inject_io(types, natives, table);
    foo::inject_os(types, natives, table);
    foo::inject_rng(types, natives, table);
}

static void inject(foo::TypeTable& types, foo::NativeRegistry& natives)
{
    foo::NativeTable table{};

    foo::Runtime::inject(types, natives, table);

    foo::inject_io(types, natives, table);
    foo::inject_os(types, natives, table);
    foo::inject_rng(types, natives, table);
}

struct Prepared
{
    foo::RuntimeInput runtime_input;
    foo::CollectorOutput collector_output;
};

static std::optional<Prepared> prepare(const fs::path& path, bool show_warn = true)
{
    bool ok = true;

    auto co = foo::collect(path.parent_path(), path.filename().string());
    const auto& sources = co.resolver_input.sources;

    auto report = [&](auto file_index, const auto& item, Severity severity)
    {
        const auto& source = sources[file_index];
        print_diagnostics(item.message, source.text, item.span, severity, source.path);
    };

    bool first = true;
    for (const auto& err : co.errors)
    {
        if (!first)
            std::cout << std::endl;

        first = false;

        ok = false;

        if (err.file_id == -1)
        {
            std::cout << clr::bright_red << "error: " << err.message << clr::reset << std::endl;
            continue;
        }


        report(err.file_id, err, Severity::Error);
    }

    if (!ok)
        return {};

    foo::NativeTable ntable;
    inject(co.resolver_input.types, co.resolver_input.natives, ntable);

    auto ro = foo::resolve(co.resolver_input);

    first = true;
    if (show_warn)
        for (const auto& warning : ro.warnings)
        {
            if (!first)
                std::cout << std::endl;

            first = false;

            if (warning.file_index == -1)
            {
                std::cout << clr::bright_yellow << "error: " << warning.message << clr::reset << std::endl;
                continue;
            }

            report(warning.file_index, warning, Severity::Warning);
        }

    for (const auto& err : ro.errors)
    {
        if (!first)
            std::cout << std::endl;

        first = false;

        ok = false;
        report(err.file_index, err, Severity::Error);
    }

    if (!ok)
        return {};

    foo::RuntimeInput runtime_input;

    runtime_input.resolver_info = ro;
    runtime_input.native_table = ntable;

    return Prepared{runtime_input, std::move(co)};
}

static bool check_script(const fs::path& path)
{
    std::cout << clr::blue << "checking: " << path.string() << clr::reset << std::endl;

    auto r = prepare(path);

    if (r.has_value())
    {
        std::cout << clr::bright_green << "checking: ok" << clr::reset << std::endl;
        return true;
    } else
    {
        std::cout << clr::blue << "checking: failed" << clr::reset << std::endl;
        return false;
    }
}

static int setup_script(const std::string& script)
{
    const fs::path file_path = script + ".foo";

    if (fs::exists(file_path))
    {
        std::cout << "script " << script << " already exists" << std::endl;
        return 0;
    }

    std::ofstream file(file_path);
    if (!file)
    {
        std::cerr << "could not create " << file_path.string() << std::endl;
        return 1;
    }

    file << "fn main() -> void\n"
            "{\n"
            "    // this prints hello world\n"
            "    println(\"Hello, World!\")\n"
            "}\n";

    return 0;
}

static bool run_script(const fs::path& path)
{
    auto r = prepare(path, true);

    if (r)
    {
        int mainid = -1;
        for (const auto& [index, item] : std::views::enumerate(r->runtime_input.resolver_info.functions))
        {
            if (item.decl->name == "main")
            {
                mainid = static_cast<int>(index);
                break;
            }
        }

        if (mainid == -1)
        {
            std::cout << clr::bright_red << "error: no main()" << clr::reset << std::endl;
            return false;
        }

        foo::Runtime runtime{r->runtime_input};
        auto result = runtime.call(static_cast<foo::FunctionId>(mainid), {});
        if (result)
        {
            std::cout << "main returned: " << foo::to_string(*result.value()) << std::endl;
            return true;
        }
        else
        {
            std::cout << clr::bright_red << "runtime error" << clr::reset << std::endl;

            const auto& err = result.error();
            print_diagnostics(err.what, r->collector_output.resolver_input.sources[err.file_index].text, err.span, Severity::Error);
            return false;
        }
    }

    return false;
}

struct Example
{
    struct SingleFile
    {
        std::string name;
        std::string text;
    };

    std::string suite;
    std::vector<SingleFile> files;
};

static void create_example_files(const Example& e, std::filesystem::path root = "")
{
    std::filesystem::path path_to_folder = root / e.suite;
    std::filesystem::create_directories(path_to_folder);

    for (const auto& fileinfo : e.files)
    {
        std::filesystem::path path_to_file = path_to_folder / (fileinfo.name + ".foo");

        std::ofstream file(path_to_file);

        file << fileinfo.text;
        file.flush();

        file.close();
    }
}

static bool check_example(const Example& e, std::filesystem::path root = "")
{
    std::filesystem::path path_to_script = root / e.suite / "main";

    return check_script(path_to_script);
}

static std::vector<Example> create_examples()
{
    std::vector<Example> examples;
    int id = 0;
    auto id_string = [&](std::string_view sv)
    {
        return std::to_string(id++) + "_" + std::string{sv};
    };

    auto add = [&](const std::string& name, const std::vector<Example::SingleFile>& files)
    {
        std::vector<Example::SingleFile> copied_files = files;
        for (auto& file : copied_files)
        {
            file.text = std::string{foo::trim( file.text)};
        }

        examples.emplace_back(Example{
            id_string(name),
            copied_files
        });
    };

    add("hello_world",
{
    {"main",
R"AAA(
fn main() -> void
{
    println("Hello, World!")
}
)AAA"
        },
    });

    add("variables",
{
{"main",
R"AAA(
fn main() -> int
{
    var is_int = 5

    {
        var is_also_int = 5

        // < is_also_int ends here
    }

    // type annotations are needed when
    //  it can't infer the type from the initializer
    var variable_with_type: string[] = []

    // here it isn't needed as it can infer string[] from ["a","b"]
    var variable_without_type = ["a","b"]

    return is_int
    // < is_int ends here
}
)AAA"
},
});

    add("range",
{
    {"main",
R"AAA(
fn main() -> void
{
    for a in 0..=10
    {
        println(a)
    }
}
)AAA"
        },
    });

    add("returning",
{
    {"main",
R"AAA(
fn main() -> string
{

    return "string!"
}
)AAA"
        },
    });

    add("functions",
{
    {"main",
R"AAA(
fn my_function() -> int
{
    return 5
}

fn main() -> int
{
    return my_function()
}
)AAA"
        },
    });

    add("functions",
{
{"main",
R"AAA(
fn double(ref a: int) -> void
{
    a = a * 2
}

fn main() -> int
{
    var a = 5

    double(a)

    return a; // should return 10
}
)AAA"
    },
});



    add("if",
{
{"main",
R"AAA(
fn main() -> int
{
    var b = true

    if (b)
    {
        // ...
    }

    return 0
}
)AAA"
},
});

    add("optionals",
{
{"main",
R"AAA(
fn main() -> int
{
    // optional int
    var a: int? = 0

    check a => good_a
    {
        // a exists,
        //  good_a is of type int
        var new_a: int = good_a;

        return new_a
    } else
    {
        // a was null
        return -1
    }
}
)AAA"
},
});

    add("multiple_files",
{
{"main",
R"AAA(
import "a"

fn main() -> int
{
    return a::double(10)
}
)AAA"
}, {"a", R"AAA(
fn double(x: int) -> int
{
    return x * 2
}
)AAA"}
});

    add("arrays",
{
    {"main",
R"AAA(
fn main() -> void
{
    var a_0: int[] = [1, 2, 3]

    var a_1: int[][] = [ [1,2],
                         [3,4] ]

    a_0 = a_1[0]
}
)AAA"
        },
    });

    add("structs",
{
{"main",
R"AAA(
struct my_struct
{
    a: int
    b: int
}


fn main() -> void
{
    var a = #my_struct{ a = 0, b = -1 }
    a.a = 0

    a.a = a.b * a.a
}
)AAA"
    },
});

    add("methods",
{
{"main",
R"AAA(
struct my_struct
{
    a: int
    b: int

    // ref is used to modify the arg in place of caller or in order to save memory space.
    fn times(ref self: my_struct, other: my_struct) -> void
    {
        self.a = self.a * other.a
        self.b = self.b * other.b
    }
}


fn main() -> void
{
    var a = #my_struct{ a = 0, b = -1 }

    // times itself
    a.times(a)
}
)AAA"
},
});


    //     add("hello_world",
    // {
    //     {"main",
    // R"AAA(
    // fn main() -> void
    // {
    //     println("Hello, World!")
    // }
    // )AAA"
    //         },
    //     });


    add("any",
{
{"main",
R"AAA(
fn main() -> int
{
    var a: any = 5;

    check @int.unbox(a) => is_int
    {
        return is_int
    } else {
        return 0
    }
}
)AAA"
},
});


    add("should_error",
{
{"main",
R"AAA(
fn good_function() -> int
{
    return 420
}

fn needs_input(a: int, b: any) -> void
{

}

fn main() -> int
{
    var good_var = ["a","b"]

    // type mismatch
    var t_01: int       = 1.0
    var t_02: float     = ""
    var t_03: int[]     = ["",""]
    var t_04: bool      = good_var[0]
    var t_05: float     = good_function();

    // unknown variable
    var u_01 = IDONTEXIST



    // not bool
    if (5)          { }
    while (t_01)    { }
    while (1 + 1)   { }


    // hidden types
    var h_01: any = 1
    t_01 = t_01 + h_01

    // incorrect usage of function
    needs_input(1)
    needs_input()
    needs_input("", "")



    if (false)
    {
        // return incompatible type
        return ""
    }

    // not every path returns
}
)AAA"
},
});

    return examples;
}

static bool run_examples(const fs::path& root, bool check)
{
    fs::create_directories(root);

    const auto examples = create_examples();

    if (fs::is_empty(root))
    {
        for (const auto& e : examples)
            create_example_files(e, root);
    }
    else
    {
        std::cout << clr::yellow
                  << "no new examples were added, clear the directory or give a unique one"
                  << clr::reset << std::endl;
    }

    bool ok = true;
    if (check)
    {
        for (const auto& e : examples)
            ok = check_example(e, root) && ok;
    }
    return ok;
}

static void show_api()
{
    foo::TypeTable tt;
    foo::NativeRegistry nr;

    foo::NativeTable native_table;

    inject(tt, nr, native_table);

    for (const auto& [index, item] : std::views::enumerate(native_table.get_infos()))
    {
        std::cout << clr::magenta << item.api << clr::reset << std::endl;

        if (!item.explanation.empty())
            std::cout << "    " << item.explanation << clr::reset << std::endl;

        if (!item.example.empty())
            std::cout << clr::bright_black << "    example usage: ("
                << item.example
                << ")"
                << clr::reset << std::endl;
    }
}

static void show_syntax(const std::vector<std::string>& kinds)
{
    auto wants = [&](auto... names)
    {
        return kinds.empty()
            || ((std::ranges::find(kinds, std::string_view{names}) != kinds.end()) || ...);
    };

    const int row_width = 34;
    const int label_width = 10;

    auto indent = [](int depth) { foo::print(foo::str_repeat("  ", depth)); };

    bool first_header = true;
    bool first_sheader = true;
    auto header = [&](std::string_view h)
    {
        if (!first_header)
            foo::println("");

        first_header = false;
        first_sheader = true;

        foo::println(colored(std::string{h}, clr::bright_magenta));
    };

    auto sheader = [&](std::string_view h)
    {
        if (!first_sheader)
            foo::println("");

        first_sheader = false;

        indent(1);
        foo::println(colored(std::string{h}, clr::bright_cyan));
    };

    auto row = [&](std::string_view syntax, std::string_view text)
    {
        indent(2);
        foo::println(colored(std::format("{:<{}}", syntax, row_width), clr::green) + " " + std::string{text});
    };

    auto form = [&](std::string_view f)
    {
        indent(2);
        foo::println(colored(std::string{f}, clr::green));
    };

    auto part = [&](std::string_view label, std::string_view text)
    {
        indent(3);
        foo::println(std::format("{:<{}} {}", label, label_width, text));
    };

    auto more = [&](std::string_view text)
    {
        part("", text);
    };

    {
        header("");

        sheader("how to read this");
        row("<name>",                               "something you fill in");

        sheader("names");
        row("counter   _tmp   player_2",            "letters, digits and _, not starting with a digit");
        row("a::b::c",                              ":: reaches into the names of another file");

        sheader("comments (C style)");
        row("// text",                              "ignored until the end of the line");
        row("/* text */",                           "");

        sheader("semicolons");
        row(";",                                    "optional");
    }

    if (wants("types"))
    {
        header("types");

        row("int  float  bool  string  any",        "built-in types");
        row("void",                                 "only for functions");
        row("<type>[]",                             "array of <type>: int[]   int[][]");
        row("<type>?",                              "a <type> or nothing: int?   (int?? is not allowed)");
        row("[<key>, <value>]",                     "map from one type to another: [string, int]");
        row("<path>::<name>",                       "a named type from another file: lib_b::Point");
    }

    if (wants("decls"))
    {
        header("declarations");

        sheader("imports");
        form(R"AAA(import "<path>", ...)AAA");
        part(R"AAA("<path>")AAA",   "a file from the project root, written without .foo");
        part("then",                R"AAA(import "lib/b" gives you the names lib::b::<name>)AAA");
        part("e.g.",                R"AAA(import "lib/b")AAA");
        more(R"AAA(import "a", "b")AAA");

        sheader("functions");
        form("fn <name>(<params>) -> <type> { ... }");
        part("<params>",            "name: type, separated by commas");
        more("put ref in front of a name to let the function edit the caller's value");
        part("<type>",              "what it gives back, void for nothing");
        part("e.g.",                "fn add(a: int, b: int) -> int { ... }");
        more("fn double(ref a: int) -> void { ... }");

        sheader("structs");
        form("struct <name> { <fields> <methods> }");
        part("field",               "<name>: <type>");
        part("method",              "a function inside the struct");
        more("its first parameter is the struct itself, usually ref self: <name>");
        part("e.g.",                "struct Point");
        more("{");
        more("    x: int");
        more("    y: int");
        more("");
        more("    fn sum(ref self: Point) -> int { ... }");
        more("}");
    }

    if (wants("stmts"))
    {
        header("statements");

        sheader("variables");
        form("var <name>: <type> = <expr>");
        part(": <type>",            "can be left out when there is a value");
        part("= <expr>",            "can be left out when there is a type");
        part("note",                "you need at least one of the two");
        part("e.g.",                "var count = 5");
        more("var names: string[] = []");
        more("var total: int");

        sheader("if");
        form("if <expr> { ... } else if <expr> { ... } else { ... }");
        part("<expr>",              "must be true or false; parentheses around it are optional");
        part("else",                "can be left out, and else if can repeat");

        sheader("while");
        form("while <expr> { ... }");
        part("<expr>",              "the block repeats as long as it is true");

        sheader("for");
        form("for <name> in <expr> { ... }");
        part("<name>",              "holds the next value each time round");
        part("<expr>",              "what to loop over, such as the range 0..10");
        more("0..10 stops before 10, 0..=10 includes it");

        sheader("check");
        form("check <expr> => <name> { ... } else { ... }");
        part("<expr>",              "an optional value, such as a variable of type int?");
        part("<name>",              "what the value is called inside the first block");
        part("else",                "runs when there was no value, can be left out");

        sheader("return");
        form("return <expr>");
        part("<expr>",              "the value to give back");
        part("alone",               "return;  leaves without a value (the ; is needed here)");

        sheader("blocks and expressions");
        form("{ ... }");
        part("block",               "variables declared inside end at the }");
        part("alone",               "any expression can be a statement, e.g. println(a)");
    }

    if (wants("exprs"))
    {
        header("expressions");

        sheader("values");
        row(R"AAA(5   1.5   "text"   true)AAA",      "int, float, string, bool");
        row("[<expr>, ...]",                         "an array: [1, 2, 3]");
        row("#<type>{ <name> = <expr>, ... }",       "a struct: #Point{ x = 1, y = 2 }");
        row("",                                      "fields use =, types use :");

        sheader("using values");
        row("<expr>.<name>",                         "a field of a struct: p.x");
        row("<expr>(<args>)",                        "a call: add(1, 2)   p.sum()");
        row("<expr>[<expr>]",                        "an element of an array: list[0]");
        row("@<type>.<name>(<args>)",                "a function of a type: @int.from(a)");
        row("(<expr>)",                              "grouping: (a + b) * c");

        sheader("operators (strongest first)");
        row(". () []",                               "field, call, element");
        row("-a   !a",                               "negative, not");
        row("*  /  %",                               "multiply, divide, remainder");
        row("+  -",                                  "add, subtract");
        row("<  <=  >  >=",                          "compare");
        row("==  !=",                                "equal, not equal");
        row("&&",                                    "and");
        row("||",                                    "or");
        row("..  ..=",                               "a range of numbers");
        row("=",                                     "assign (weakest)");
    }
}

static void add_flag(argparse::ArgumentParser& program, const char* help, const char* name)
{
    program.add_argument(name).default_value(false).implicit_value(true).help(help);
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("foo", "0.1");
    std::vector<std::string> syntax_kinds;

    program.add_argument("script")
        .default_value(std::string{"main"})
        .help("script file to use");

    program.add_argument("-c", "--check")
        .default_value(false)
        .implicit_value(true)
        .help("checks if script is executable");

    program.add_argument("--syntax")
        .nargs(argparse::nargs_pattern::any)
        .choices("stmts", "decls", "types", "exprs")
        .action([&](const std::string& v)
        {
            if (!v.empty()) syntax_kinds.push_back(v);
        })
        .help("show syntax parts (bare --syntax = all)");

    add_flag(program, "sets up single file project",    "--setup");
    add_flag(program, "sets up single file project",    "--api");

    program.add_argument("--examples")
        .default_value(std::string{""})
        .implicit_value(std::string{"examples"})
        .help("creates an example folder with all the examples");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << '\n';
        return 2;
    }

    const std::string script    = program.get<std::string>("script");
    const std::string examples  = program.get<std::string>("--examples");
    const bool setup        = program.get<bool>("--setup");
    const bool check        = program.get<bool>("--check");
    const bool api          = program.get<bool>("--api");

    const bool want_syntax = program.is_used("--syntax");

    const bool has_examples = !examples.empty();
    const bool should_run   = !check && !has_examples && !setup && !api && !want_syntax;

    int exit_code = 0;

    if (has_examples && !run_examples(examples, check))
        exit_code = 1;

    if (setup)
        return setup_script(script);

    if (check && !has_examples && !check_script(script))
        exit_code = 1;

    if (should_run)
        run_script(script);

    if (api)
        show_api();

    if (want_syntax)
        show_syntax(syntax_kinds);


    return exit_code;
}