#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <foo/collector.hpp>
#include <foo/natives.hpp>
#include <foo/runtime.hpp>

namespace fs = std::filesystem;
using emscripten::val;

namespace
{
    constexpr std::string_view kWorkDir = "/tmp/foo_web";
    constexpr std::string_view kEntry   = "main";


    struct Span
    {
        unsigned start{};
        unsigned end{};
    };

    struct Problem
    {
        std::string message;
        bool is_error{true};
        std::optional<Span> span{};
    };

    [[nodiscard]] Problem problem(std::string message, bool is_error)
    {
        return {.message = std::move(message), .is_error = is_error};
    }

    [[nodiscard]] Problem problem(std::string message, foo::SourceSpan span, bool is_error)
    {
        return {
            .message  = std::move(message),
            .is_error = is_error,
            .span     = Span{static_cast<unsigned>(span.start), static_cast<unsigned>(span.end())},
        };
    }


    class CoutCapture
    {
    public:
        CoutCapture() : old_{std::cout.rdbuf(buffer_.rdbuf())} {}
        ~CoutCapture() { std::cout.rdbuf(old_); }

        CoutCapture(const CoutCapture&) = delete;
        CoutCapture& operator=(const CoutCapture&) = delete;

        [[nodiscard]] std::string take()
        {
            std::cout.flush();
            return buffer_.str();
        }

    private:
        std::ostringstream buffer_;
        std::streambuf* old_;
    };


    struct Prepared
    {
        foo::RuntimeInput input;
        foo::CollectorOutput collected;
    };

    struct Analysis
    {
        std::vector<Problem> problems;
        std::optional<Prepared> prepared;
    };

    [[nodiscard]] Analysis analyze(std::string_view source)
    {
        Analysis result;
        auto& problems = result.problems;

        const fs::path dir{kWorkDir};
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
        {
            problems.push_back(problem(std::format("could not create {}: {}", dir.string(), ec.message()), true));
            return result;
        }

        {
            std::ofstream file{dir / std::format("{}.foo", kEntry)};
            file << source;
            file.close();
            if (file.fail())
            {
                problems.push_back(problem("could not write the source file", true));
                return result;
            }
        }

        auto collected = foo::collect(dir, std::string{kEntry});

        for (const auto& err : collected.errors)
            problems.push_back(err.file_id == -1 ? problem(err.message, true)
                                                 : problem(err.message, err.span, true));
        if (!problems.empty())
            return result;

        auto& input = collected.resolver_input;
        foo::NativeTable natives;
        foo::inject_std(input.types, input.natives, natives);
        foo::inject_io(input.types, input.natives, natives);
        foo::inject_os(input.types, input.natives, natives);
        foo::inject_rng(input.types, input.natives, natives);

        auto resolved = foo::resolve(input);

        for (const auto& warning : resolved.warnings)
            problems.push_back(warning.file_index == -1 ? problem(warning.message, false)
                                                        : problem(warning.message, warning.span, false));

        for (const auto& err : resolved.errors)
            problems.push_back(problem(err.message, err.span, true));

        if (!resolved.errors.empty())
            return result;

        foo::RuntimeInput runtime_input;
        runtime_input.resolver_info = std::move(resolved);
        runtime_input.native_table  = std::move(natives);

        result.prepared = Prepared{std::move(runtime_input), std::move(collected)};
        return result;
    }

    [[nodiscard]] std::optional<foo::FunctionId> find_main(const Prepared& prepared)
    {
        const auto& functions = prepared.input.resolver_info.functions;

        const auto it = std::ranges::find_if(functions, [](const auto& fn)
        {
            return fn.decl->name == kEntry;
        });

        if (it == functions.end())
            return std::nullopt;

        return static_cast<foo::FunctionId>(it - functions.begin());
    }


    struct RunOutcome
    {
        bool ok{};

        std::string output;
        std::string returned;

        std::vector<Problem> problems;
    };

    [[nodiscard]] RunOutcome execute(std::string_view source)
    {
        RunOutcome outcome;

        auto analysis = analyze(source);
        outcome.problems = std::move(analysis.problems);
        if (!analysis.prepared)
            return outcome;

        auto& prepared = *analysis.prepared;

        const auto main_id = find_main(prepared);
        if (!main_id)
        {
            outcome.problems.push_back(problem("no main()", true));
            return outcome;
        }

        foo::Runtime runtime{prepared.input};
        auto result = runtime.call(*main_id, {});

        if (result)
        {
            outcome.ok = true;
            outcome.returned = foo::to_string(*result.value());
        }
        else
        {
            const auto& err = result.error();
            outcome.problems.push_back(problem(err.what, err.span, true));
        }

        return outcome;
    }


    [[nodiscard]] val to_js(const Problem& p)
    {
        auto object = val::object();
        object.set("message", p.message);
        object.set("severity", std::string{p.is_error ? "error" : "warning"});
        object.set("start", p.span ? val{p.span->start} : val::null());
        object.set("end",   p.span ? val{p.span->end}   : val::null());
        return object;
    }

    [[nodiscard]] val to_js(const std::vector<Problem>& problems)
    {
        std::vector<val> items;
        items.reserve(problems.size());
        std::ranges::transform(problems, std::back_inserter(items), [](const Problem& p) { return to_js(p); });
        return val::array(items);
    }

    [[nodiscard]] val to_js(const RunOutcome& outcome)
    {
        auto object = val::object();
        object.set("ok", outcome.ok);
        object.set("output", outcome.output);
        object.set("returned", outcome.returned);
        object.set("diagnostics", to_js(outcome.problems));
        return object;
    }
}


val run_source(const std::string& source)
{
    CoutCapture capture;
    RunOutcome outcome;

    try
    {
        outcome = execute(source);
    }
    catch (const std::exception& e)
    {
        outcome.problems.push_back(problem(std::format("internal error: {}", e.what()), true));
    }
    catch (...)
    {
        outcome.problems.push_back(problem("internal error", true));
    }

    outcome.output = capture.take();
    return to_js(outcome);
}


val check_source(const std::string& source)
{
    try
    {
        return to_js(analyze(source).problems);
    }
    catch (const std::exception& e)
    {
        return to_js(std::vector{problem(std::format("internal error: {}", e.what()), true)});
    }
    catch (...)
    {
        return to_js(std::vector{problem("internal error", true)});
    }
}

EMSCRIPTEN_BINDINGS(foo_web)
{
    emscripten::function("runSource", &run_source);
    emscripten::function("checkSource", &check_source);
}