#include "Stream/FileStream.h"
#include "Tests/SpecTestModule.h"
#include "Tests/TestRunner.h"
#include "VM/Proposals.h"
#include "VM/Trap.h"
#include "VM/VM.h"
#include "WASI.h"
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <print>

int main(int argc, char** argv)
{
    argparse::ArgumentParser parser("wasvm");

    auto& group = parser.add_mutually_exclusive_group();

    group.add_argument("-t")
        .help("run tests")
        .flag();

    group.add_argument("-f")
        .help("which function of a module to run")
        .default_value(std::string("_start"));

    parser.add_argument("-n", "--no-wasm-validator")
        .help("disable validation of WASM module")
        .flag();

    parser.add_argument("--load-test-module")
        .help("load the spectest module")
        .flag();

    parser.add_argument("-w", "--enable-wasi")
        .help("enable support for WASI")
        .flag();

    parser.add_argument("--enable-multi-memory")
        .help("enable multi-memory proposal")
        .flag();

    parser.add_argument("--enable-extended-const")
        .help("enable extended-const proposal")
        .flag();

    parser.add_argument("path")
        .help("path of module/test to run");

    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::exception& err)
    {
        std::cerr << err.what() << '\n';
        std::cerr << parser;
        return 1;
    }

    g_enable_multi_memory = parser["--enable-multi-memory"] == true;
    g_enable_extended_const = parser["--enable-extended-const"] == true;

    if (parser["-t"] == true)
    {
        TestStats stats = run_tests(parser.get("path"));

        nlohmann::json j;
        j["vm_error"] = stats.vm_error;
        j["total"] = stats.total;
        j["passed"] = stats.passed;
        j["failed"] = stats.failed;
        j["skipped"] = stats.skipped;
        j["failed_to_load"] = stats.failed_to_load;
        std::println("{}", j.dump());
    }
    else
    {
        if (parser["--load-test-module"] == true)
            VM::register_module("spectest", MakeRef<SpecTestModule>());

        if (parser["-w"] == true)
            VM::register_module("wasi_snapshot_preview1", MakeRef<WASIModule>());

        try
        {
            FileStream fileStream(parser.get("path"));
            auto file = WasmFile::WasmFile::read_from_stream(fileStream, parser["-n"] == false);

            VM::load_module(file);

            std::vector<Value> returnValues = VM::run_function(parser.get("-f"), {});
            for (const auto value : returnValues)
                std::println("{}", value);
        }
        catch (const Trap& trap)
        {
            std::println(std::cerr, "Trapped ({})", trap.reason());
        }
        catch (const WasmFile::InvalidWASMException& e)
        {
            std::println(std::cerr, "Invalid WASM ({})", e.reason());
        }
        catch (...)
        {
            std::println("Unknown exception");
        }
    }
}
