#include <emscripten/bind.h>

#include <iostream>
#include <string>
#include <sstream>

#include <foo/dynamic_runner.hpp>
#include <foo/parser.hpp>
#include <foo/lexer.hpp>

struct WebDiagnostic
{
    std::string message;
    unsigned start;
    unsigned end;
};

std::vector<WebDiagnostic> check_code(const std::string& source)
{
    std::vector<WebDiagnostic> result;

    auto tkns = foo::tokenize(source);

    for (const auto& error : tkns.errors)
    {
        result.push_back({
            error.error,
            static_cast<unsigned>(error.source.start),
            static_cast<unsigned>(error.source.end())
        });
    }

    if (!result.empty())
        return result;

    auto parsed = foo::parse(tkns.tokens);

    for (const auto& error : parsed.errors)
    {
        result.push_back({
            error.error,
            static_cast<unsigned>(error.source.start),
            static_cast<unsigned>(error.source.end())
        });
    }

    return result;
}

struct RanCode
{
    std::string output;
    std::vector<WebDiagnostic> runtime_errors{};
};

RanCode run_code(const std::string& source)
{
    std::vector<WebDiagnostic> runtime_errors;

    std::ostringstream output;
    auto* old_buf = std::cout.rdbuf(output.rdbuf());

    auto restore = [&]()
    {
        std::cout.rdbuf(old_buf);
    };

    try {
        auto tkns = foo::tokenize(source);
        if (!tkns.errors.empty())
        {
            restore();
            return {"",};
        }

        auto r = foo::parse(tkns.tokens);
        if (!r.errors.empty())
        {
            restore();
            return {"",};
        }

        auto runtime_error = foo::run(r.decls);
        if (runtime_error)
        {
            runtime_errors.push_back({
                runtime_error->message,
                static_cast<unsigned>(runtime_error->span.start),
                static_cast<unsigned>(runtime_error->span.end())
            });

            std::cout.flush();
            restore();
            return {output.str() + "\nRUNTIME ERROR: " + runtime_error->message, runtime_errors};
        }
    }
    catch (const std::exception& e)
    {
        std::cout.flush();
        restore();
        return {output.str() + std::string("Error: ") + e.what()};
    }

    std::cout.flush();
    restore();
    return {output.str()};
}

EMSCRIPTEN_BINDINGS(foo_web)
{
    emscripten::function("runCode", &run_code);

    emscripten::value_object<WebDiagnostic>("WebDiagnostic")
        .field("message", &WebDiagnostic::message)
        .field("start", &WebDiagnostic::start)
        .field("end", &WebDiagnostic::end);

    emscripten::register_vector<WebDiagnostic>("DiagnosticVector");

    emscripten::value_object<RanCode>("RanCode")
        .field("output", &RanCode::output)
        .field("errors", &RanCode::runtime_errors);

    emscripten::function("checkCode", &check_code);
}