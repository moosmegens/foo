#include <foo/natives.hpp>
#include <foo/runtime.hpp>
#include <foo/value.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <filesystem>
#include <thread>
#include <limits>
#include <chrono>
#include <cstdlib>


namespace foo
{
    namespace
    {
        namespace fs = std::filesystem;

        class FileHandle : public Native
        {
        public:
            std::fstream stream;
        };

        std::ios::openmode open_mode(const std::string& mode)
        {
            if (mode == "r")
                return std::ios::in | std::ios::binary;

            if (mode == "w")
                return std::ios::out | std::ios::trunc | std::ios::binary;

            if (mode == "a")
                return std::ios::out | std::ios::app | std::ios::binary;

            throw std::runtime_error(R"(io::open: mode must be "r", "w" or "a", got ")" + mode + "\"");
        }

        std::string errno_message()
        {
            return errno != 0 ? std::strerror(errno) : "error";
        }

        FileHandle& open_file(const ValueRef& v)
        {
            auto& handle = native_cast<FileHandle>(*v);

            if (!handle.stream.is_open())
            {
                throw std::runtime_error("file is closed");
            }

            return handle;
        }
    }

    void inject_io(TypeTable& types, NativeRegistry& natives, NativeTable& table)
    {
        NativeDef def(types, natives, table);

        const TypeId String = BaseTypes::String;
        const TypeId file_type = natives.type(types, "io::file");
        const TypeId file_opt = types.optional_of(file_type);
        const TypeId string_opt = types.optional_of(String);
        const TypeId strings_opt = types.optional_of(types.array_of(String));

        auto last_error = std::make_shared<std::string>();

        def.function("io::last_error", NativeDef::sig({}, String),
            [last_error](Runtime&, Args a, const auto&)
            {
                return make_string(*last_error);
            },
            {"io::last_error() -> string", "why the last failed io call failed"});

        def.function("io::open", NativeDef::sig({ParamSig{String, false}, ParamSig{String, false}}, file_opt),
            [last_error](Runtime&, Args a, const auto&)
            {
                auto handle = make<FileHandle>();

                errno = 0;
                handle->stream.open(as_string(a[0]), open_mode(as_string(a[1])));

                if (!handle->stream.is_open())
                {
                    *last_error = errno_message();
                    return make_void();
                }

                return make_value(Rc<Native>(handle));
            },
            {"io::open(path: string, mode: string) -> io::file", R"OO(mode: "r", "w" or "a")OO"});

        def.method("read_all", file_type, NativeDef::sig({}, String),
            [](Runtime&, Args a, const auto&)
            {
                auto& stream = open_file(a[0]).stream;

                stream.seekg(0, std::ios::end);
                std::string result(stream.tellg(), '\0');
                stream.seekg(0);
                stream.read(result.data(), result.size());

                if (stream.bad())
                    throw std::runtime_error("could not read from the file");

                return make_string(std::move(result));
            },
            {"(io::file).read_all() -> string"});

        def.method("read_line", file_type, NativeDef::sig({}, string_opt),
            [](Runtime&, Args a, const auto&)
            {
                std::string line;

                if (!std::getline(open_file(a[0]).stream, line))
                    return make_void();

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                return make_string(std::move(line));
            },
            {"(io::file).read_line() -> string?"});

        def.function("io::read_lines",
            NativeDef::sig({ParamSig{String, false}}, strings_opt),
            [last_error](Runtime&, Args a, const auto&)
            {
                errno = 0;
                std::ifstream in(as_string(a[0]), std::ios::binary);

                if (!in)
                {
                    *last_error = errno_message();
                    return make_void();
                }

                Array result;
                std::string line;

                while (std::getline(in, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                    result.items.push_back(make_string(std::move(line)));
                }

                if (in.bad())
                {
                    *last_error = "could not read from the file";
                    return make_void();
                }

                *last_error = "";
                return make_value(std::move(result));
            },
            {"io::read_lines(path: string) -> string[]?"});

        def.method("write", file_type, NativeDef::sig({ParamSig{String, false}}, BaseTypes::Void),
            [](Runtime&, Args a, const auto&)
            {
                std::fstream& stream = open_file(a[0]).stream;

                stream << as_string(a[1]);

                if (!stream)
                    throw std::runtime_error("could not write to the file");

                return make_void();
            },
            {"(io::file).write(string) -> void"});

        def.method("write_line", file_type,
            NativeDef::sig({ParamSig{String, false}}, BaseTypes::Void),
            [](Runtime&, Args a, const auto&)
            {
                auto& stream = open_file(a[0]).stream;

                stream << as_string(a[1]) << '\n';

                if (!stream)
                    throw std::runtime_error("could not write to the file");

                return make_void();
            },
            {"(io::file).write_line(string) -> void"});

        def.function("io::append_file",
            NativeDef::sig({ParamSig{String, false}, ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::ofstream out(as_string(a[0]), std::ios::binary | std::ios::app);

                if (!out)
                {
                    *last_error = errno_message();
                    return make_bool(false);
                }

                out << as_string(a[1]);

                if (!out)
                {
                    *last_error = errno_message();
                    return make_bool(false);
                }

                *last_error = "";
                return make_bool(true);
            },
            {"io::append_file(path: string, content: string) -> bool"});

        def.function("io::copy",
            NativeDef::sig({ParamSig{String, false}, ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                const bool copied = fs::copy_file(
                    as_string(a[0]),
                    as_string(a[1]),
                    fs::copy_options::none,
                    ec
                );

                if (ec)
                    *last_error = ec.message();
                else
                    *last_error = copied ? "" : "copy failed";

                return make_bool(copied);
            },
            {"io::copy(from: string, to: string) -> bool"});

        def.function("io::rename",
            NativeDef::sig({ParamSig{String, false}, ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                fs::rename(as_string(a[0]), as_string(a[1]), ec);

                if (ec)
                {
                    *last_error = ec.message();
                    return make_bool(false);
                }

                *last_error = "";
                return make_bool(true);
            },
            {"io::rename(from: string, to: string) -> bool"});

        def.method("close", file_type, NativeDef::sig({}, BaseTypes::Void),
            [](Runtime&, Args a, const auto&)
            {
                native_cast<FileHandle>(*a[0]).stream.close();
                return make_void();
            },
            {"(io::file).close() -> void"});

        def.function("io::read_file", NativeDef::sig({ParamSig{String, false}}, string_opt),
            [last_error](Runtime&, Args a, const auto&)
            {
                errno = 0;
                std::ifstream in(as_string(a[0]), std::ios::binary);

                if (!in)
                {
                    *last_error = errno_message();
                    return make_void();
                }

                return make_string(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
            },
            {"io::read_file(path: string) -> string?"});

        def.function("io::write_file",
            NativeDef::sig({ParamSig{String, false}, ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                errno = 0;
                std::ofstream out(as_string(a[0]), std::ios::binary | std::ios::trunc);

                if (!out)
                {
                    *last_error = errno_message();
                    return make_bool(false);
                }

                out << as_string(a[1]);
                return make_bool(static_cast<bool>(out));
            },
            {"io::write_file(path: string, content: string) -> bool"});

        def.function("io::exists", NativeDef::sig({ParamSig{String, false}}, BaseTypes::Bool),
            [](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                return make_bool(fs::exists(as_string(a[0]), ec));
            },
            {"io::exists(path: string) -> bool"});

        def.function("io::remove", NativeDef::sig({ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                const bool removed = fs::remove(as_string(a[0]), ec);
                *last_error = ec ? ec.message() : (removed ? "" : "no such file");

                return make_bool(removed);
            },
            {"io::remove(path: string) -> bool"});

        def.function("io::mkdir", NativeDef::sig({ParamSig{String, false}}, BaseTypes::Bool),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                fs::create_directories(as_string(a[0]), ec);

                if (ec)
                {
                    *last_error = ec.message();
                    return make_bool(false);
                }

                return make_bool(true);
            },
            {"io::mkdir(path: string) -> bool", "creates folders recursively"});

        def.function("io::list_dir", NativeDef::sig({ParamSig{String, false}}, strings_opt),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;

                std::vector<std::string> names;

                for (fs::directory_iterator it(as_string(a[0]), ec), end; !ec && it != end; it.increment(ec))
                    names.push_back(it->path().filename().string());

                if (ec)
                {
                    *last_error = ec.message();
                    return make_void();
                }

                std::sort(names.begin(), names.end());

                Array result;
                result.items.reserve(names.size());

                for (std::string& name : names)
                    result.items.push_back(make_string(std::move(name)));

                return make_value(std::move(result));
            },
            {"io::list_dir(path: string) -> string[]?"});

        def.function("io::join", NativeDef::variadic({{BaseTypes::String}}, BaseTypes::String),
            [](Runtime&, Args a, const auto&)
            {
                std::filesystem::path result;

                for (size_t i = 0; i < a.size(); i++)
                {
                    const std::filesystem::path part(as_string(a[i]));

                    if (i == 0)
                        result = part;
                    else
                        result /= part;
                }

                return make_string(result.string());
            },
            {"io::join(string...) -> string", "turns (folder, folder, file) into os specific path"});

        def.function("io::filename",
            NativeDef::sig({ParamSig{String, false}}, String),
            [](Runtime&, Args a, const auto&)
            {
                return make_string(
                    fs::path(as_string(a[0])).filename().string()
                );
            },
            {"io::filename(path: string) -> string"});

        def.function("io::extension",
            NativeDef::sig({ParamSig{String, false}}, String),
            [](Runtime&, Args a, const auto&)
            {
                return make_string(
                    fs::path(as_string(a[0])).extension().string()
                );
            },
            {"io::extension(path: string) -> string"});

        def.function("io::parent",
            NativeDef::sig({ParamSig{String, false}}, String),
            [](Runtime&, Args a, const auto&)
            {
                return make_string(
                    fs::path(as_string(a[0])).parent_path().string()
                );
            },
            {"io::parent(path: string) -> string"});

        def.function("io::absolute",
            NativeDef::sig({ParamSig{String, false}}, string_opt),
            [last_error](Runtime&, Args a, const auto&)
            {
                std::error_code ec;
                auto path = fs::absolute(as_string(a[0]), ec);

                if (ec)
                {
                    *last_error = ec.message();
                    return make_void();
                }

                *last_error = "";
                return make_string(path.string());
            },
            {"io::absolute(path: string) -> string?"});


        def.function("io::cwd", NativeDef::sig({}, BaseTypes::String),
            [](Runtime&, Args a, const auto&)
            {
                return make_string(std::filesystem::current_path().string());
            },
            {"io::cwd() -> string", "current working directory"});
    }

    void inject_os(TypeTable& types, NativeRegistry& natives, NativeTable& table)
    {
        NativeDef def(types, natives, table);

        const TypeId string_opt = types.optional_of(BaseTypes::String);
        const TypeId strings = types.array_of(BaseTypes::String);

        const auto start = std::chrono::steady_clock::now();

        def.function("os::getenv", NativeDef::sig({ParamSig{BaseTypes::String, false}}, string_opt),
            [](Runtime&, Args a, const auto&)
            {
                const char* value = std::getenv(as_string(a[0]).c_str());

                if (!value)
                    return make_void();

                return make_string(value);
            },
            {"os::getenv(name: string) -> string?"});

        def.function("os::time", NativeDef::sig({}, BaseTypes::Float),
            [](Runtime&, Args a, const auto&)
            {
                const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
                return make_float(std::chrono::duration<double>(since_epoch).count());
            },
            {"os::time() -> float", "seconds since 1970"});

        def.function("os::clock", NativeDef::sig({}, BaseTypes::Float),
            [start](Runtime&, Args a, const auto&)
            {
                return make_float(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            },
            {"os::clock() -> float", "monotonic seconds, for timing}"});

        def.function("os::sleep", NativeDef::sig({ParamSig{BaseTypes::Float, false}}, BaseTypes::Void),
            [](Runtime&, Args a, const auto&)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0, as_float(a[0]))));
                return make_void();
            },
            {"os::sleep(seconds: float) -> void"});

        def.function("os::cmd", NativeDef::sig({ParamSig{BaseTypes::String, false}}, BaseTypes::Void),
            [](Runtime&, Args a, const auto&)
            {
                system(as_string(a[0]).c_str());

                return make_void();
            },
            {"os::cmd(string)"});
    }

    void inject_rng(TypeTable& types, NativeRegistry& natives, NativeTable& table)
    {
        NativeDef def(types, natives, table);

        auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());

        def.function("rng::seed", NativeDef::sig({ParamSig{BaseTypes::Int, false}}, BaseTypes::Void),
            [engine](Runtime&, Args a, const auto&)
            {
                engine->seed(static_cast<uint64_t>(as_int(a[0])));
                return make_void();
            },
            {"rng::seed(int) -> void"});

        def.function("rng::range",
            NativeDef::sig({ParamSig{BaseTypes::Int, false}, ParamSig{BaseTypes::Int, false}}, BaseTypes::Int),
            [engine](Runtime&, Args a, const auto&)
            {
                const int64_t lo = as_int(a[0]);
                const int64_t hi = as_int(a[1]);

                if (lo > hi)
                    throw std::runtime_error(std::format("rng::range: lo ({}) is greater than hi ({})", lo, hi));

                std::uniform_int_distribution<int64_t> dist(lo, hi);
                return make_int(dist(*engine));
            },
            {"rng::range(lo: int, hi: int) -> int", "random range, inclusive (lo cannot be greater than hi)"});

        def.method("shuffle",
            [](TypeTable& ts, TypeId receiver) -> std::optional<FunctionSig>
            {
                if (ts.kind(receiver) != TypeKind::Array)
                    return std::nullopt;

                return NativeDef::sig({}, BaseTypes::Void, true);
            },
            [engine](Runtime&, Args a, const auto&)
            {
                auto& items = as_array(a[0]).items;

                std::shuffle(items.begin(), items.end(), *engine);
                return make_void();
            },
            {"(array).shuffle() -> void", "shuffles any array type inplace"});
    }
}
