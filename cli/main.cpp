#include <emscripten/bind.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <foo/collector.hpp>
#include <foo/natives.hpp>
#include <foo/runtime.hpp>

namespace fs = std::filesystem;

std::string hello(const std::string& name)
{
    return "Hello, " + name + "!";
}

struct WebDiagnostic
{
    std::string message;
    unsigned start;
    unsigned end;
    bool is_error;
};

struct RunResult
{
    bool ok = false;
    std::string output;     // everything printed via std::cout
    std::string returned;   // what main() returned
    std::vector<WebDiagnostic> diagnostics;
};

namespace
{
    // redirects std::cout into a string for as long as it lives
    struct CoutCapture
    {
        std::ostringstream buffer;
        std::streambuf* old;

        CoutCapture() : old(std::cout.rdbuf(buffer.rdbuf())) {}
        ~CoutCapture() { std::cout.rdbuf(old); }

        std::string str()
        {
            std::cout.flush();
            return buffer.str();
        }
    };

    WebDiagnostic diag(const std::string& message, foo::SourceSpan span, bool is_error)
    {
        return {message,
                static_cast<unsigned>(span.start),
                static_cast<unsigned>(span.end()),
                is_error};
    }

    void run_impl(const std::string& source, RunResult& out)
    {
        const fs::path dir = "/tmp/foo_web";
        fs::create_directories(dir);

        {
            std::ofstream file(dir / "main.foo");
            file << source;
        }

        // collect
        auto co = foo::collect(dir, "main");

        bool failed = false;
        for (const auto& err : co.errors)
        {
            failed = true;
            if (err.file_id == -1)
                out.diagnostics.push_back({err.message, 0, 0, true});
            else
                out.diagnostics.push_back(diag(err.message, err.span, true));
        }
        if (failed)
            return;

        // natives + resolve
        foo::NativeTable ntable;
        foo::Runtime::inject(co.resolver_input.types, co.resolver_input.natives, ntable);
        foo::inject_io(co.resolver_input.types, co.resolver_input.natives, ntable);
        foo::inject_os(co.resolver_input.types, co.resolver_input.natives, ntable);
        foo::inject_rng(co.resolver_input.types, co.resolver_input.natives, ntable);

        auto ro = foo::resolve(co.resolver_input);

        for (const auto& w : ro.warnings)
        {
            if (w.file_index == -1)
                out.diagnostics.push_back({w.message, 0, 0, false});
            else
                out.diagnostics.push_back(diag(w.message, w.span, false));
        }

        for (const auto& err : ro.errors)
        {
            failed = true;
            out.diagnostics.push_back(diag(err.message, err.span, true));
        }
        if (failed)
            return;

        // find main
        foo::RuntimeInput input;
        input.resolver_info = ro;
        input.native_table = ntable;

        const auto& fns = input.resolver_info.functions;
        int main_id = -1;
        for (size_t i = 0; i < fns.size(); ++i)
        {
            if (fns[i].decl->name == "main")
            {
                main_id = static_cast<int>(i);
                break;
            }
        }

        if (main_id == -1)
        {
            out.diagnostics.push_back({"no main()", 0, 0, true});
            return;
        }

        // run
        foo::Runtime runtime{input};
        auto result = runtime.call(static_cast<foo::FunctionId>(main_id), {});

        if (result)
        {
            out.ok = true;
            out.returned = foo::to_string(*result.value());
        }
        else
        {
            const auto& err = result.error();
            out.diagnostics.push_back(diag(err.what, err.span, true));
        }
    }
}

RunResult run(const std::string& source)
{
    RunResult result;
    CoutCapture capture;

    try
    {
        run_impl(source, result);
    }
    catch (const std::exception& e)
    {
        result.ok = false;
        result.diagnostics.push_back({e.what(), 0, 0, true});
    }

    result.output = capture.str();
    return result;
}

EMSCRIPTEN_BINDINGS(foo_web)
{
    emscripten::function("hello", &hello);

    emscripten::value_object<WebDiagnostic>("WebDiagnostic")
        .field("message", &WebDiagnostic::message)
        .field("start", &WebDiagnostic::start)
        .field("end", &WebDiagnostic::end)
        .field("isError", &WebDiagnostic::is_error);

    emscripten::register_vector<WebDiagnostic>("DiagnosticVector");

    emscripten::value_object<RunResult>("RunResult")
        .field("ok", &RunResult::ok)
        .field("output", &RunResult::output)
        .field("returned", &RunResult::returned)
        .field("diagnostics", &RunResult::diagnostics);

    emscripten::function("run", &run);
}