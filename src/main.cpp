#include <FileStream.h>
#include <Proposals.h>
#include <TestRunner.h>
#include <VM.h>
#include <WasmFile.h>
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

    if (parser["--load-test-module"] == true)
    {
        FileStream test("spectest.wasm");
        auto testModule = VM::load_module(WasmFile::WasmFile::read_from_stream(test), true);
        VM::register_module("spectest", testModule);
    }

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
        {
            FileStream test("test_data/spectest.wasm");
            auto testModule = VM::load_module(WasmFile::WasmFile::read_from_stream(test), true);
            VM::register_module("spectest", testModule);
        }

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
            std::println("Trapped");
        }
        catch (const WasmFile::InvalidWASMException& e)
        {
            std::println("Invalid WASM");
        }
        catch (...)
        {
            std::println("Unknown exception");
        }
    }
}
