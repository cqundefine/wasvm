#include <Compiler.h>
#include <FileStream.h>
#include <TestRunner.h>
#include <VM.h>
#include <bit>
#include <fstream>
#include <math.h>
#include <nlohmann/json.hpp>
#include <print>
#include <unistd.h>

bool float_equals(float a, float b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    return a == b;
}

bool double_equals(double a, double b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    return a == b;
}

#define ENABLE_INVALID_TESTS
// #define COMPLAIN_ABOUT_EXHAUSTION

std::optional<Value> parse_value(nlohmann::json json)
{
    try
    {
        std::string type = json["type"];
        if (type == "v128")
        {
            std::string laneType = json["lane_type"];
            uint128_t value = 0;
            uint8_t shiftCount;
            if (laneType == "i8")
                shiftCount = 8;
            else if (laneType == "i16")
                shiftCount = 16;
            else if (laneType == "i32" || laneType == "f32")
                shiftCount = 32;
            else if (laneType == "i64" || laneType == "f64")
                shiftCount = 64;
            else
                return {};

            for (size_t i = 0; i < json["value"].size(); i++)
            {
                auto lane = json["value"][i];
                uint128_t laneInt = static_cast<uint128_t>(std::stoull(lane.get<std::string>()));
                value |= laneInt << (shiftCount * i);
            }
            return value;
        }

        std::string value = json["value"];

        if (type == "i32")
            return static_cast<uint32_t>(std::stoull(value));
        if (type == "i64")
            return static_cast<uint64_t>(std::stoull(value));

        if (type == "f32")
        {
            if (value.starts_with("nan"))
                return typed_nan<float>();
            uint32_t rawValue = static_cast<uint32_t>(std::stoull(value));
            return std::bit_cast<float>(rawValue);
        }
        if (type == "f64")
        {
            if (value.starts_with("nan"))
                return typed_nan<double>();
            uint64_t rawValue = static_cast<uint64_t>(std::stoull(value));
            return std::bit_cast<double>(rawValue);
        }

        if (type == "funcref")
            return Reference { ReferenceType::Function, value == "null" ? UINT32_MAX : static_cast<uint32_t>(std::stoull(value)) };
        if (type == "externref")
            return Reference { ReferenceType::Extern, value == "null" ? UINT32_MAX : static_cast<uint32_t>(std::stoull(value)) };

        return {};
    }
    catch (const std::invalid_argument& e)
    {
        return {};
    }
}

bool compare_values(Value a, Value b)
{
    if (a.get_type() != b.get_type())
        return false;

    if (a.holds_alternative<uint32_t>())
        return a.get<uint32_t>() == b.get<uint32_t>();
    if (a.holds_alternative<uint64_t>())
        return a.get<uint64_t>() == b.get<uint64_t>();

    if (a.holds_alternative<float>())
        return float_equals(a.get<float>(), b.get<float>());
    if (a.holds_alternative<double>())
        return double_equals(a.get<double>(), b.get<double>());

    if (a.holds_alternative<uint128_t>())
        return a.get<uint128_t>() == b.get<uint128_t>();

    if (a.holds_alternative<Reference>())
    {
        auto refA = a.get<Reference>();
        auto refB = b.get<Reference>();

        if (refA.type != refB.type)
            return false;

        return refA.index == refB.index;
    }

    assert(false);
    std::unreachable();
}

std::vector<Value> run_action(TestStats& stats, bool& failed, const std::string& path, uint32_t line, nlohmann::json action)
{
    std::string actionType = action["type"];
    if (actionType == "invoke")
    {
        std::vector<Value> args;
        for (const auto& arg : action["args"])
        {
            auto value = parse_value(arg);
            if (!value.has_value())
            {
                stats.skipped++;
                failed = true;
                std::println("{}/{} skipped: failed to parse argument of type: {}", path, line, arg["type"].get<std::string>());
                break;
            }
            else
            {
                args.push_back(value.value());
            }
        }

        if (failed)
            return {};

        try
        {
            std::string field = action["field"];
            if (action.contains("module"))
                return VM::run_function(action["module"], field, args);
            else
                return VM::run_function(field, args);
        }
        catch (JITCompilationException e)
        {
            stats.skipped++;
            failed = true;
            std::println("{}/{} skipped: failed to compile JIT code", path, line);
            return {};
        }
    }
    else if (actionType == "get")
    {
        Ref<Module> mod;
        if (action.contains("module"))
            mod = VM::get_registered_module(action["module"]);
        else
            mod = VM::current_module();

        WasmFile::Export exp = mod->wasmFile->find_export_by_name(action["field"]);
        assert(exp.type == WasmFile::ImportType::Global);
        return { mod->get_global(exp.index)->value };
    }
    else
    {
        stats.skipped++;
        failed = true;
        std::println("{}/{} skipped: unsupported action type: {}", path, line, actionType);
        return {};
    }
}

TestStats run_tests(const std::string& path)
{
    TestStats stats {};

    try
    {
        FileStream specTestStream("test_data/spectest.wasm");
        auto specTest = WasmFile::WasmFile::read_from_stream(specTestStream);
        VM::register_module("spectest", VM::load_module(specTest, true));
    }
    catch (...)
    {
        std::println("Failed to load spectest wasm");
        stats.vm_error = true;
        return stats;
    }

    chdir("test_data/testsuite-processed");
    chdir(path.c_str());

    std::filesystem::path fsPath(path);
    std::ifstream f(fsPath.filename().string() + ".json");
    nlohmann::json data = nlohmann::json::parse(f);

    bool module_loaded = false;

    for (const auto& command : data["commands"])
    {
        std::string type = command["type"];
        uint32_t line = command["line"].get<uint32_t>();
        if (type == "module")
        {
            stats.total++;
            FileStream fileStream(command["filename"].get<std::string>());
            try
            {
                auto file = WasmFile::WasmFile::read_from_stream(fileStream);
                VM::load_module(file);
                if (command.contains("name"))
                    VM::register_module(command["name"], VM::current_module());
                std::println("{}/{} module loaded", path, line);
                module_loaded = true;
                stats.passed++;
            }
            catch (WasmFile::InvalidWASMException e)
            {
                std::println("{}/{} module failed to load", path, line);
                module_loaded = false;
                stats.failed_to_load++;
            }
            catch (Trap e)
            {
                std::println("{}/{} module failed to load", path, line);
                module_loaded = false;
                stats.failed_to_load++;
            }
            catch (JITCompilationException e)
            {
                std::println("{}/{} module failed to load", path, line);
                module_loaded = false;
                stats.failed_to_load++;
            }
        }
        else if (type == "register")
        {
            if (command.contains("name"))
                VM::register_module(command["as"], VM::get_registered_module(command["name"]));
            else
                VM::register_module(command["as"], VM::current_module());
        }
        else if (type == "action")
        {
            if (!module_loaded)
            {
                std::println("{}/{} action skipped: module not loaded", path, line);
                continue;
            }

            bool failed = false;

            std::vector<Value> returnValues;
            try
            {
                returnValues = run_action(stats, failed, path, line, command["action"]);
            }
            catch (Trap t)
            {
                failed = true;
                std::println("{}/{} failed: unexpected trap", path, line);
                continue;
            }

            if (failed)
                continue;

            if (returnValues.size() != 0)
            {
                failed = true;
                std::println("{}/{} action failed: returned values: {}", path, line, returnValues.size());
                continue;
            }

            const auto& expectedValues = command["expected"];
            if (expectedValues.size() != 0)
            {
                failed = true;
                std::println("{}/{} action failed: has expected return values: {}", path, line, expectedValues.size());
                continue;
            }
        }
        else if (type == "assert_return")
        {
            stats.total++;

            if (!module_loaded)
            {
                stats.failed_to_load++;
                std::println("{}/{} skipped: module not loaded", path, line);
                continue;
            }

            bool failed = false;

            std::vector<Value> returnValues;
            try
            {
                returnValues = run_action(stats, failed, path, line, command["action"]);
            }
            catch (Trap t)
            {
                stats.failed++;
                failed = true;
                std::println("{}/{} failed: unexpected trap", path, line);
                continue;
            }

            if (failed)
                continue;

            const auto& expectedValues = command["expected"];
            if (expectedValues.size() != returnValues.size())
            {
                stats.failed++;
                failed = true;
                std::println("{}/{} failed: unexpected return value count {}, expected {}", path, line, returnValues.size(), expectedValues.size());
                continue;
            }

            for (size_t i = 0; i < expectedValues.size(); i++)
            {
                auto maybeExpectedValue = parse_value(expectedValues[i]);
                if (!maybeExpectedValue.has_value())
                {
                    stats.skipped++;
                    failed = true;
                    std::println("{}/{} skipped: failed to parse return value of type: {}", path, line, expectedValues[i]["type"].get<std::string>());
                    break;
                }
                auto expectedValue = maybeExpectedValue.value();
                if (!compare_values(returnValues[i], expectedValue))
                {
                    stats.failed++;
                    failed = true;
                    std::println("{}/{} failed: return value {} has unexpected value {}, expected {}", path, line, i, value_to_string(returnValues[i]), value_to_string(expectedValue));
                    break;
                }
            }

            if (!failed)
            {
                stats.passed++;
                std::println("{}/{} passed", path, line);
            }
        }
        else if (type == "assert_trap")
        {
            stats.total++;

            if (!module_loaded)
            {
                stats.failed_to_load++;
                std::println("{}/{} skipped: module not loaded", path, line);
                continue;
            }

            bool failed = false;

            try
            {
                run_action(stats, failed, path, line, command["action"]);

                if (failed)
                    continue;

                stats.failed++;
                std::println("{}/{} failed: expected trap, not trapped", path, line);
            }
            catch (Trap t)
            {
                stats.passed++;
                std::println("{}/{} passed", path, line);
            }
        }
#ifdef COMPLAIN_ABOUT_EXHAUSTION
        else if (type == "assert_exhaustion")
        {
            std::println("{}/{} failed: exhaustion not implemented", path, line);
            stats.total++;
            stats.failed++;
        }
#endif
#ifdef ENABLE_INVALID_TESTS
        else if (type == "assert_invalid" || type == "assert_malformed")
        {
            if (command["module_type"] != "binary")
            {
                // We are a VM, not a compiler
                continue;
            }

            stats.total++;

            FileStream fileStream(command["filename"].get<std::string>());
            try
            {
                auto file = WasmFile::WasmFile::read_from_stream(fileStream);

                stats.failed++;
                std::println("{}/{} expected to not load, loaded", path, line);
            }
            catch (WasmFile::InvalidWASMException e)
            {
                stats.passed++;
                std::println("{}/{} passed", path, line);
            }
        }
        else if (type == "assert_uninstantiable" || type == "assert_unlinkable")
        {
            if (command["module_type"] != "binary")
            {
                // We are a VM, not a compiler
                continue;
            }

            stats.total++;

            FileStream fileStream(command["filename"].get<std::string>());
            try
            {
                auto file = WasmFile::WasmFile::read_from_stream(fileStream);
                VM::load_module(file, true);

                stats.failed++;
                std::println("{}/{} expected to not instantiate, instantiated", path, line);
            }
            catch (WasmFile::InvalidWASMException e)
            {
                stats.failed++;
                std::println("{}/{} failed: module is invalid", path, line);
            }
            catch (Trap e)
            {
                stats.passed++;
                std::println("{}/{} passed", path, line);
            }
        }
#endif
        else
        {
#if defined(ENABLE_INVALID_TESTS) && defined(COMPLAIN_ABOUT_EXHAUSTION)
            std::println("command type unsupported: {}", type);
            stats.total++;
            stats.skipped++;
#endif
        }
    }

    return stats;
}
