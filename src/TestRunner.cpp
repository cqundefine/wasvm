#include <FileStream.h>
#include <TestRunner.h>
#include <VM.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>

TestStats run_tests(const char* path)
{
    TestStats stats {};

    chdir("tests");
    chdir(path);
    std::ifstream f(path + std::string(".json"));
    nlohmann::json data = nlohmann::json::parse(f);

    bool module_loaded = false;

    for (const auto& command : data["commands"])
    {
        std::string type = command["type"];
        uint32_t line = command["line"].get<uint32_t>();
        if (type == "module")
        {
            FileStream fileStream(command["filename"].get<std::string>().c_str());
            WasmFile file;
            try
            {
                WasmFile file = WasmFile::read_from_stream(fileStream);
                VM::bootup(file);
                module_loaded = true;
            }
            catch (InvalidWASMException e)
            {
                printf("%s/%u module failed to load\n", path, line);
                module_loaded = false;
            }
            catch (Trap e)
            {
                printf("%s/%u module failed to load\n", path, line);
                module_loaded = false;
            }
        }
        else if (type == "action")
        {
            if (!module_loaded)
            {
                printf("%s/%u action skipped: module not loaded\n", path, line);
                continue;
            }

            bool failed = false;

            const auto& action = command["action"];
            std::string actionType = action["type"];
            if (actionType == "invoke")
            {
                std::vector<Value> args;
                for (const auto& arg : action["args"])
                {
                    std::string argType = arg["type"];
                    std::string argValue = arg["value"];
                    if (argType == "i32")
                    {
                        args.push_back((uint32_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "i64")
                    {
                        args.push_back((uint64_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "f32")
                    {
                        uint32_t rawValue = (uint32_t)std::stoull(argValue.c_str());
                        args.push_back(*(float*)&rawValue);
                    }
                    else if (argType == "f64")
                    {
                        uint64_t rawValue = (uint64_t)std::stoull(argValue.c_str());
                        args.push_back(*(double*)&rawValue);
                    }
                    else
                    {
                        failed = true;
                        printf("%s/%u action failed: unsupported argument type: %s\n", path, line, argType.c_str());
                        break;
                    }
                }

                if (failed)
                    continue;

                std::string field = action["field"];

                std::vector<Value> returnValues;
                try
                {
                    returnValues = VM::run_function(field, args);
                }
                catch (Trap t)
                {
                    failed = true;
                    printf("%s/%u failed: unexpected trap\n", path, line);
                    continue;
                }

                if (returnValues.size() != 0)
                {
                    failed = true;
                    printf("%s/%u action failed: returned values: %zu\n", path, line, returnValues.size());
                    continue;
                }

                const auto& expectedValues = command["expected"];
                if (expectedValues.size() != 0)
                {
                    failed = true;
                    printf("%s/%u action failed: has expected return values: %zu\n", path, line, expectedValues.size());
                    continue;
                }
            }
            else
            {
                failed = true;
                printf("%s/%u action skipped: unsupported action type: %s\n", path, line, actionType.c_str());
                break;
            }
        }
        else if (type == "assert_return")
        {
            stats.total++;

            if (!module_loaded)
            {
                stats.failed_to_load++;
                printf("%s/%u skipped: module not loaded\n", path, line);
                continue;
            }

            bool failed = false;

            const auto& action = command["action"];
            std::string actionType = action["type"];
            if (actionType == "invoke")
            {
                std::vector<Value> args;
                for (const auto& arg : action["args"])
                {
                    std::string argType = arg["type"];
                    std::string argValue = arg["value"];
                    if (argType == "i32")
                    {
                        args.push_back((uint32_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "i64")
                    {
                        args.push_back((uint64_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "f32")
                    {
                        uint32_t rawValue = (uint32_t)std::stoull(argValue.c_str());
                        args.push_back(*(float*)&rawValue);
                    }
                    else if (argType == "f64")
                    {
                        uint64_t rawValue = (uint64_t)std::stoull(argValue.c_str());
                        args.push_back(*(double*)&rawValue);
                    }
                    else
                    {
                        stats.skipped++;
                        failed = true;
                        printf("%s/%u skipped: unsupported argument type: %s\n", path, line, argType.c_str());
                        break;
                    }
                }

                if (failed)
                    continue;

                std::string field = action["field"];

                std::vector<Value> returnValues;
                try
                {
                    returnValues = VM::run_function(field, args);
                }
                catch (Trap t)
                {
                    stats.failed++;
                    failed = true;
                    printf("%s/%u failed: unexpected trap\n", path, line);
                    continue;
                }

                const auto& expectedValues = command["expected"];
                if (expectedValues.size() != returnValues.size())
                {
                    stats.failed++;
                    failed = true;
                    printf("%s/%u failed: unexpected return value count %zu, expected %zu\n", path, line, returnValues.size(), expectedValues.size());
                    continue;
                }

                for (size_t i = 0; i < expectedValues.size(); i++)
                {
                    std::string expectedType = expectedValues[i]["type"];
                    std::string expectedValue = expectedValues[i]["value"];
                    try
                    {
                        if (expectedType == "i32")
                        {
                            if (!std::holds_alternative<uint32_t>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected type %s, expected i32\n", path, line, i, get_value_variant_name_by_index(returnValues[i].index()));
                                break;
                            }

                            if (std::stoull(expectedValue) != std::get<uint32_t>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected value %u, expected %s\n", path, line, i, std::get<uint32_t>(returnValues[i]), expectedValue.c_str());
                                break;
                            }
                        }
                        else if (expectedType == "i64")
                        {
                            if (!std::holds_alternative<uint64_t>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected type %s, expected i64\n", path, line, i, get_value_variant_name_by_index(returnValues[i].index()));
                                break;
                            }

                            if (std::stoull(expectedValue) != std::get<uint64_t>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected value %lu, expected %s\n", path, line, i, std::get<uint64_t>(returnValues[i]), expectedValue.c_str());
                                break;
                            }
                        }
                        else if (expectedType == "f32")
                        {
                            if (!std::holds_alternative<float>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected type %s, expected f32\n", path, line, i, get_value_variant_name_by_index(returnValues[i].index()));
                                break;
                            }

                            uint32_t value = (uint32_t)std::stoull(expectedValue);
                            if (*(float*)&value != std::get<float>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected value %f, expected %f\n", path, line, i, std::get<float>(returnValues[i]), *(float*)&value);
                                break;
                            }
                        }
                        else if (expectedType == "f64")
                        {
                            if (!std::holds_alternative<double>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected type %s, expected f64\n", path, line, i, get_value_variant_name_by_index(returnValues[i].index()));
                                break;
                            }

                            uint64_t value = (uint64_t)std::stoull(expectedValue);
                            if (*(double*)&value != std::get<double>(returnValues[i]))
                            {
                                stats.failed++;
                                failed = true;
                                printf("%s/%u failed: return value %zu has unexpected value %lf, expected %lf\n", path, line, i, std::get<double>(returnValues[i]), *(double*)&value);
                                break;
                            }
                        }
                        else
                        {
                            stats.skipped++;
                            failed = true;
                            printf("%s/%u skipped: unsupported return type: %s\n", path, line, expectedType.c_str());
                            break;
                        }
                    }
                    catch (const std::invalid_argument& e)
                    {
                        stats.skipped++;
                        failed = true;
                        printf("%s/%u skipped: failed to parse return value %zu: %s\n", path, line, i, expectedValue.c_str());
                        break;
                    }
                }

                if (!failed)
                {
                    stats.passed++;
                    printf("%s/%u passed\n", path, line);
                }
            }
            else
            {
                stats.skipped++;
                failed = true;
                printf("%s/%u skipped: unsupported action type: %s\n", path, line, actionType.c_str());
                break;
            }
        }
        else if (type == "assert_trap")
        {
            stats.total++;

            if (!module_loaded)
            {
                stats.failed_to_load++;
                printf("%s/%u skipped: module not loaded\n", path, line);
                continue;
            }

            bool failed = false;

            const auto& action = command["action"];
            std::string actionType = action["type"];
            if (actionType == "invoke")
            {
                std::vector<Value> args;
                for (const auto& arg : action["args"])
                {
                    std::string argType = arg["type"];
                    std::string argValue = arg["value"];
                    if (argType == "i32")
                    {
                        args.push_back((uint32_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "i64")
                    {
                        args.push_back((uint64_t)std::stoull(argValue.c_str()));
                    }
                    else if (argType == "f32")
                    {
                        uint32_t rawValue = (uint32_t)std::stoull(argValue.c_str());
                        args.push_back(*(float*)&rawValue);
                    }
                    else if (argType == "f64")
                    {
                        uint64_t rawValue = (uint64_t)std::stoull(argValue.c_str());
                        args.push_back(*(double*)&rawValue);
                    }
                    else
                    {
                        stats.skipped++;
                        failed = true;
                        printf("%s/%u skipped: unsupported argument type: %s\n", path, line, argType.c_str());
                        break;
                    }
                }

                std::string field = action["field"];
                try
                {
                    VM::run_function(field, args);
                    stats.failed++;
                    printf("%s/%u failed: expected trap, not trapped\n", path, line);
                }
                catch (Trap t)
                {
                    stats.passed++;
                    printf("%s/%u passed\n", path, line);
                }
            }
            else
            {
                stats.skipped++;
                failed = true;
                printf("%s/%u skipped: unsupported return type: %s\n", path, line, actionType.c_str());
                break;
            }
        }
        else
        {
            printf("command type unsupported: %s\n", type.c_str());
        }
    }

    return stats;
}
