#include <FileStream.h>
#include <TestRunner.h>
#include <VM.h>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>
#include <stdio.h>

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

    parser.add_argument("path")
        .help("path of module/test to run");

    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::exception& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    if (parser["-t"] == true)
    {
        TestStats stats = run_tests(parser.get("path").c_str());

        nlohmann::json j;
        j["total"] = stats.total;
        j["passed"] = stats.passed;
        j["failed"] = stats.failed;
        j["skipped"] = stats.skipped;
        j["failed_to_load"] = stats.failed_to_load;
        printf("%s\n", j.dump().c_str());
    }
    else
    {
        FileStream fileStream(argv[1]);
        auto file = WasmFile::WasmFile::read_from_stream(fileStream);

        VM::load_module(file);

        std::vector<Value> returnValues = VM::run_function(parser.get("-f"), {});
    }
}
