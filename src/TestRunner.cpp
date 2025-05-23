#include <FileStream.h>
#include <SIMD.h>
#include <TestRunner.h>
#include <Util.h>
#include <VM.h>
#include <Value.h>
#include <bit>
#include <concepts>
#include <fstream>
#include <math.h>
#include <nlohmann/json.hpp>
#include <print>
#include <unistd.h>
#include <utility>
#include <variant>

struct ArithmeticNaN
{
    uint8_t bits;

    bool operator==(Value value) const
    {
        if (bits == 32)
        {
            if (!value.holds_alternative<float>())
                return false;
            return std::isnan(value.get<float>());
        }

        if (bits == 64)
        {
            if (!value.holds_alternative<double>())
                return false;
            return std::isnan(value.get<double>());
        }

        UNREACHABLE();
    }
};

struct CanonicalNaN
{
    uint8_t bits;

    bool operator==(Value value) const
    {
        if (bits == 32)
        {
            if (!value.holds_alternative<float>())
                return false;
            return std::bit_cast<uint32_t>(value.get<float>()) == 0x7FC00000 || std::bit_cast<uint32_t>(value.get<float>()) == 0xFFC00000;
        }

        if (bits == 64)
        {
            if (!value.holds_alternative<double>())
                return false;
            return std::bit_cast<uint64_t>(value.get<double>()) == 0x7FF8000000000000 || std::bit_cast<uint64_t>(value.get<double>()) == 0xFFF8000000000000;
        }

        UNREACHABLE();
    }
};

static uint128_t getLane(uint128_t vector, uint8_t laneSize, uint8_t lane)
{
    switch (laneSize)
    {
        case 8:
            return std::bit_cast<uint8x16_t>(vector)[lane];
        case 16:
            return std::bit_cast<uint16x8_t>(vector)[lane];
        case 32:
            return std::bit_cast<uint32x4_t>(vector)[lane];
        case 64:
            return std::bit_cast<uint64x2_t>(vector)[lane];
        default:
            UNREACHABLE();
    }
}

struct TestVector
{
    std::vector<std::variant<uint128_t, ArithmeticNaN, CanonicalNaN>> lanes;

    Value to_value() const
    {
        // FIXME: Handle NaN
        uint8_t laneSize = 128 / lanes.size();
        uint128_t valueInt = 0;
        for (size_t i = 0; i < lanes.size(); i++)
            if (std::holds_alternative<uint128_t>(lanes[i]))
                valueInt |= std::get<uint128_t>(lanes[i]) << (laneSize * i);

        return valueInt;
    }

    bool operator==(Value value) const
    {
        if (!value.holds_alternative<uint128_t>())
            return false;
        auto vector = value.get<uint128_t>();

        // return to_value() == value;

        uint8_t laneSize = 128 / lanes.size();

        for (size_t i = 0; i < lanes.size(); i++)
            if (std::holds_alternative<uint128_t>(lanes[i]))
            {
                if (std::get<uint128_t>(lanes[i]) != getLane(vector, laneSize, i))
                    return false;
            }
            else if (std::holds_alternative<ArithmeticNaN>(lanes[i]))
            {
                if (laneSize == 32)
                {
                    if (std::get<ArithmeticNaN>(lanes[i]) != std::bit_cast<float32_t>(static_cast<uint32_t>(getLane(vector, laneSize, i))))
                        return false;
                }
                else
                {
                    if (std::get<ArithmeticNaN>(lanes[i]) != std::bit_cast<float64_t>(static_cast<uint64_t>(getLane(vector, laneSize, i))))
                        return false;
                }
            }
            else if (std::holds_alternative<CanonicalNaN>(lanes[i]))
            {
                if (laneSize == 32)
                {
                    if (std::get<CanonicalNaN>(lanes[i]) != std::bit_cast<float32_t>(static_cast<uint32_t>(getLane(vector, laneSize, i))))
                        return false;
                }
                else
                {
                    if (std::get<CanonicalNaN>(lanes[i]) != std::bit_cast<float64_t>(static_cast<uint64_t>(getLane(vector, laneSize, i))))
                        return false;
                }
            }

        return true;
    }
};

class TestValue
{
public:
    friend std::formatter<TestValue>;

    template <IsValueType T>
    TestValue(T value)
        : value(value)
    {
    }

    template <IsAnyOf<ArithmeticNaN, CanonicalNaN, TestVector, Value> T>
    TestValue(T value)
        : value(value)
    {
    }

    bool operator==(const Value& other) const
    {
        if (std::holds_alternative<ArithmeticNaN>(value))
            return std::get<ArithmeticNaN>(value) == other;
        if (std::holds_alternative<CanonicalNaN>(value))
            return std::get<CanonicalNaN>(value) == other;
        if (std::holds_alternative<Value>(value))
            return std::get<Value>(value) == other;
        if (std::holds_alternative<TestVector>(value))
            return std::get<TestVector>(value) == other;

        UNREACHABLE();
    }

    Value get_value() const
    {
        if (std::holds_alternative<Value>(value))
            return std::get<Value>(value);
        if (std::holds_alternative<TestVector>(value))
            return std::get<TestVector>(value).to_value();
        UNREACHABLE();
    }

private:
    std::variant<ArithmeticNaN, CanonicalNaN, TestVector, Value> value;
};

template <>
struct std::formatter<TestValue>
{
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return std::cbegin(ctx);
    }

    auto format(const TestValue& obj, std::format_context& ctx) const
    {
        if (std::holds_alternative<ArithmeticNaN>(obj.value))
            return std::format_to(ctx.out(), "nan:arithmetic");
        if (std::holds_alternative<CanonicalNaN>(obj.value))
            return std::format_to(ctx.out(), "nan:canonical");
        if (std::holds_alternative<Value>(obj.value) || std::holds_alternative<TestVector>(obj.value))
            return std::format_to(ctx.out(), "{}", obj.get_value());

        UNREACHABLE();
    }
};

std::optional<TestValue> parse_value(nlohmann::json json)
{
    // FIXME: This still doesn't parse NaNs entirely properly
    try
    {
        std::string type = json["type"];
        if (type == "v128")
        {
            std::string laneType = json["lane_type"];

            std::vector<std::variant<uint128_t, ArithmeticNaN, CanonicalNaN>> lanes;
            for (size_t i = 0; i < json["value"].size(); i++)
            {
                auto lane = json["value"][i];
                if (lane == "nan:arithmetic")
                    lanes.push_back(ArithmeticNaN { laneType == "f32" ? uint8_t { 32 } : uint8_t { 64 } });
                else if (lane == "nan:canonical")
                    lanes.push_back(CanonicalNaN { laneType == "f32" ? uint8_t { 32 } : uint8_t { 64 } });
                else
                    lanes.push_back(static_cast<uint128_t>(std::stoull(lane.get<std::string>())));
            }
            return TestVector { lanes };
        }

        std::string value = json["value"];

        if (type == "i32")
            return static_cast<uint32_t>(std::stoull(value));
        if (type == "i64")
            return static_cast<uint64_t>(std::stoull(value));

        if (type == "f32")
        {
            if (value == "nan")
                return typed_nan<float>();
            if (value == "nan:arithmetic")
                return ArithmeticNaN { 32 };
            if (value == "nan:canonical")
                return CanonicalNaN { 32 };

            uint32_t rawValue = static_cast<uint32_t>(std::stoull(value));
            return std::bit_cast<float>(rawValue);
        }
        if (type == "f64")
        {
            if (value == "nan")
                return typed_nan<float>();
            if (value == "nan:arithmetic")
                return ArithmeticNaN { 64 };
            if (value == "nan:canonical")
                return CanonicalNaN { 64 };

            uint64_t rawValue = static_cast<uint64_t>(std::stoull(value));
            return std::bit_cast<double>(rawValue);
        }

        if (type == "funcref")
            return Reference { ReferenceType::Function, value == "null" ? UINT32_MAX : static_cast<uint32_t>(std::stoull(value)), nullptr };
        if (type == "externref")
            return Reference { ReferenceType::Extern, value == "null" ? UINT32_MAX : static_cast<uint32_t>(std::stoull(value)), nullptr };

        return {};
    }
    catch (const std::invalid_argument& e)
    {
        return {};
    }
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

            args.push_back(value.value().get_value());
        }

        if (failed)
            return {};

        std::string field = action["field"];
        if (action.contains("module"))
            return VM::run_function(action["module"], field, args);
        else
            return VM::run_function(field, args);
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
                if (expectedValue != returnValues[i])
                {
                    stats.failed++;
                    failed = true;
                    std::println("{}/{} failed: return value {} has unexpected value {}, expected {}", path, line, i, returnValues[i], expectedValue);
                    break;
                }
            }

            if (!failed)
            {
                stats.passed++;
                std::println("{}/{} passed", path, line);
            }
        }
        else if (type == "assert_trap" || type == "assert_exhaustion")
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
        else
        {
            std::println("command type unsupported: {}", type);
            stats.total++;
            stats.skipped++;
        }
    }

    return stats;
}
