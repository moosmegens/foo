#include "vendor/argparse.hpp"

#include <foo/dynamic_runner.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <ranges>

#include <foo/lexer.hpp>
#include <foo/util/scoped_timer.hpp>
#include <foo/parser.hpp>
#include <foo/source.hpp>
#include <foo/ast_printer.hpp>

#include <foo/collector.hpp>

#include "foo/runtime.hpp"
#include "foo/util/str.hpp"

#define CLR_BLACK   "\033[1;30m"
#define CLR_RED     "\033[1;31m"
#define CLR_GREEN   "\033[1;32m"
#define CLR_YELLOW  "\033[1;33m"
#define CLR_BLUE    "\033[1;34m"
#define CLR_MAGENTA "\033[1;35m"
#define CLR_CYAN    "\033[1;36m"
#define CLR_WHITE   "\033[1;37m"
#define CLR_RESET   "\033[0m"

#define CLR_BRIGHT_BLACK   "\033[1;90m"
#define CLR_BRIGHT_RED     "\033[1;91m"
#define CLR_BRIGHT_GREEN   "\033[1;92m"
#define CLR_BRIGHT_YELLOW  "\033[1;93m"
#define CLR_BRIGHT_BLUE    "\033[1;94m"
#define CLR_BRIGHT_MAGENTA "\033[1;95m"
#define CLR_BRIGHT_CYAN    "\033[1;96m"
#define CLR_BRIGHT_WHITE   "\033[1;97m"

static void print_diagnostics(const std::string& error, std::string_view src, foo::SourceSpan span, bool is_error = true, const std::string file = "")
{
    auto lines = foo::get_diagnostic_lines(src, span, is_error ? 2 : 1);

    std::string_view col = is_error ? CLR_RED : CLR_YELLOW;

    if (is_error)
        std::cout << col << "error" << CLR_RESET << ": " << error << "\n";
    else
        std::cout << col << "warning" << CLR_RESET << ": " << error << "\n";

    if (!file.empty())
    {
        std::cout << file << "\n";
    }

    if (lines.empty())
    {
        std::cout << "NO LINES FOUND\n";
        return;
    }

    int max_line = lines.back().line_number;
    int pad_len = std::to_string(max_line).length();

    std::string pad(pad_len, ' ');

    for (const auto& dl : lines)
    {
        std::string num_str = std::to_string(dl.line_number);
        std::string num_pad(pad_len - num_str.length(), ' ');

        std::cout << CLR_CYAN << num_str << num_pad
                  << " |" << CLR_BRIGHT_WHITE << " "
                  << dl.text << CLR_RESET << '\n';

        if (dl.underline_length > 0)
        {
            std::cout << pad << " " << CLR_CYAN << "|" << CLR_RESET << " ";
            std::cout << std::string(dl.underline_start, ' ');
            std::cout << col << std::string(dl.underline_length, '^');
            std::cout << " " << error << CLR_RESET << '\n';
        }
    }
}

void check_script(std::filesystem::path path)
{
    bool check_is_ok = true;

    std::cout << CLR_BLUE << "checking: " << path.string() <<CLR_RESET << std::endl;

    auto co = foo::collect(path.parent_path(), path.filename().string());

    for (const auto& err : co.errors)
    {
        check_is_ok = false;
        if (err.file_id == -1)
        {
            std::cout << CLR_BRIGHT_RED << "error: " << err.message << CLR_RESET << std::endl;
            continue;
        }

        print_diagnostics(err.message, co.resolver_input.sources[err.file_id].text, err.span, true,
            co.resolver_input.sources[err.file_id].path.string());
    }


    foo::Runtime::inject(co.resolver_input.types, co.resolver_input.natives);

    auto ro = foo::resolve(co.resolver_input);

    for (const auto& wrng : ro.warnings)
    {
        print_diagnostics(wrng.message, co.resolver_input.sources[wrng.file_index].text, wrng.span, false,
            co.resolver_input.sources[wrng.file_index].path.string());

    }

    for (const auto& err : ro.errors)
    {
        check_is_ok = false;
        print_diagnostics(err.message, co.resolver_input.sources[err.file_index].text, err.span, true,
            co.resolver_input.sources[err.file_index].path.string());
    }

    if (check_is_ok)
    {
        std::cout << CLR_GREEN << "check: is ok" << CLR_RESET << std::endl;
    } else
    {
        std::cout << CLR_RED << "check: not ok" << CLR_RESET << std::endl;
    }
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

static void check_example(const Example& e, std::filesystem::path root = "")
{
    std::filesystem::path path_to_script = root / e.suite / "main";

    check_script(path_to_script);
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
    // optional bool
    var a: bool? = true

    check a => good_a
    {
        // a exists,
        //  good_a is of type bool

        var new_a: bool = good_a;
    } else
    {
        // a was null
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


int main(int argc, char** argv)
{
    argparse::ArgumentParser program("foo", "0.0");

    program.add_argument("script")
        .default_value("main")
        .help("Script file to execute");

    program.add_argument("--ast")
        .default_value(false)
        .implicit_value(true)
        .help("Print the AST");

    program.add_argument("-c", "--check")
        .default_value(false)
        .implicit_value(true)
        .help("Check if script is executable");

    program.add_argument("--setup")
        .default_value(false)
        .implicit_value(true)
        .help("Set up single file project");

    program.add_argument("--examples")
        .default_value(false)
        .implicit_value(true)
        .help("Create an example folder with all the examples");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << '\n';
        return 2;
    }

    const auto script = program.get<std::string>("script");
    const bool ast = program.get<bool>("--ast");
    const bool setup = program.get<bool>("--setup");
    const bool check = program.get<bool>("--check");
    const bool examples = program.get<bool>("--examples");

    bool should_run = !check && !examples && !setup && !ast;

    if (examples)
    {
        auto example_list = create_examples();
        for (const auto& e : example_list)
        {
            create_example_files(e,"examples");
        }

        if (check)
        {
            for (const auto& e : example_list)
            {
                check_example(e, "examples");
            }
        }
    }

    if (ast)
    {
        std::filesystem::path path = script;

        auto co = foo::collect(path.parent_path(), path.filename().string());

        for (const auto& file : co.resolver_input.files)
        {
            std::cout << CLR_YELLOW << "[" << file.path << "]" << CLR_RESET <<'\n';

            std::cout << CLR_GREEN;
            for (const auto& decl : file.decls)
            {
                std::cout << foo::to_string(decl, 1) << '\n';
            }
            std::cout << CLR_RESET;

            std::cout << std::endl;
        }
    }

    if (setup)
    {
        std::filesystem::path path = script;

        auto co = foo::collect(path.parent_path(), path.filename().string());

        if (std::filesystem::exists(script + ".foo"))
        {
            std::cout << "script " << script << " already exists" << std::endl;
        } else
        {
            std::ofstream file(script + ".foo");

            file << "fn main() -> void" << std::endl;
            file << "{" << std::endl;
            file << "    // this prints hello world" << std::endl;
            file << "    io::println(\"Hello, World!\")" << std::endl;
            file << "    " << std::endl;
            file << "    return \"ok\" " << std::endl;
            file << "}" << std::endl;

            file.close();
        }

        return 0;
    }

    if (check && !examples)
    {
        std::filesystem::path path = script;

        check_script(path);
    }

    if (should_run)
    {
        std::filesystem::path path = script;

        auto r = foo::collect(path.parent_path(), path.filename().string());
    }
}