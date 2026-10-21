#include <iostream>
#include <emscripten/bind.h>
#include <string>

#include <foo/dynamic_runner.hpp>
#include <foo/parser.hpp>

#include "foo/lexer.hpp"

struct WebDiagnostic {
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

std::string run_code(const std::string& source)
{
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
            return tkns.errors[0].error;
        }

        auto r = foo::parse(tkns.tokens);
        if (!r.errors.empty())
        {
            restore();
            return r.errors[0].error;
        }

        auto runtime_error = foo::run(r.decls);
        if (runtime_error)
        {
            std::cout.flush();
            restore();
            return output.str() + runtime_error->message;
        }
    }
    catch (const std::exception& e)
    {
        std::cout.flush();
        restore();
        return output.str() + std::string("Error: ") + e.what();
    }

    std::cout.flush();
    restore();
    return output.str();
}

EMSCRIPTEN_BINDINGS(foo_web)
{
    emscripten::function("runCode", &run_code);

    emscripten::value_object<WebDiagnostic>("WebDiagnostic")
        .field("message", &WebDiagnostic::message)
        .field("start", &WebDiagnostic::start)
        .field("end", &WebDiagnostic::end);

    emscripten::register_vector<WebDiagnostic>("DiagnosticVector");

    emscripten::function("checkCode", &check_code);
}