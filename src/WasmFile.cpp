#include <MemoryStream.h>
#include <Opcode.h>
#include <Parser.h>
#include <Type.h>
#include <Util.h>
#include <Validator.h>
#include <WasmFile.h>
#include <iostream>

namespace WasmFile
{
    Ref<WasmFile> s_currentWasmFile;

    static std::vector<Instruction> parse_with_current_wasm_file(Stream& stream)
    {
        return parse(stream, s_currentWasmFile);
    }

    Limits Limits::read_from_stream(Stream& stream)
    {
        uint8_t type = stream.read_little_endian<uint8_t>();

        uint32_t min = stream.read_leb<uint32_t>();
        std::optional<uint32_t> max;

        if (type == 0x01)
            max = stream.read_leb<uint32_t>();
        else if (type != 0x00)
            throw InvalidWASMException();

        if (max && min > max)
            throw InvalidWASMException();

        return Limits {
            .min = min,
            .max = max,
        };
    }

    MemArg MemArg::read_from_stream(Stream& stream)
    {
        uint32_t align = stream.read_leb<uint32_t>();

        uint32_t memoryIndex = 0;
        if ((align & 0x40) != 0)
        {
            align &= ~0x40;
            memoryIndex = stream.read_leb<uint32_t>();
        }

        return MemArg {
            .align = align,
            .offset = stream.read_leb<uint32_t>(),
            .memoryIndex = memoryIndex,
        };
    }

    FunctionType FunctionType::read_from_stream(Stream& stream)
    {
        uint8_t byte = stream.read_leb<uint8_t>();
        if (byte != 0x60)
            throw InvalidWASMException();

        std::vector<Type> params = stream.read_vec_with_function<Type, read_type_from_stream>();
        std::vector<Type> returns = stream.read_vec_with_function<Type, read_type_from_stream>();

        return FunctionType {
            .params = params,
            .returns = returns,
        };
    }

    Import Import::read_from_stream(Stream& stream)
    {
        std::string environment = stream.read_typed<std::string>();
        std::string name = stream.read_typed<std::string>();

        uint8_t type = stream.read_leb<uint8_t>();
        switch (type)
        {
            case 0:
                return Import {
                    .type = ImportType::Function,
                    .environment = environment,
                    .name = name,
                    .functionTypeIndex = stream.read_leb<uint32_t>(),
                };
            case 1:
                return Import {
                    .type = ImportType::Table,
                    .environment = environment,
                    .name = name,
                    .tableRefType = read_type_from_stream(stream),
                    .tableLimits = stream.read_typed<Limits>(),
                };
            case 2:
                return Import {
                    .type = ImportType::Memory,
                    .environment = environment,
                    .name = name,
                    .memoryLimits = stream.read_typed<Limits>(),
                };
            case 3: {
                auto type = read_type_from_stream(stream);
                auto mut = (GlobalMutability)stream.read_little_endian<uint8_t>();

                if (mut != GlobalMutability::Constant && mut != GlobalMutability::Variable)
                    throw InvalidWASMException();

                return Import {
                    .type = ImportType::Global,
                    .environment = environment,
                    .name = name,
                    .globalType = type,
                    .globalMut = mut,
                };
            }
            default:
                std::println(std::cerr, "Error: Invalid import type: {}", type);
                throw InvalidWASMException();
        }
    }

    Table Table::read_from_stream(Stream& stream)
    {
        return Table {
            .refType = read_type_from_stream(stream),
            .limits = stream.read_typed<Limits>(),
        };
    }

    Memory Memory::read_from_stream(Stream& stream)
    {
        return Memory {
            .limits = stream.read_typed<Limits>(),
        };
    }

    Global Global::read_from_stream(Stream& stream)
    {
        auto type = read_type_from_stream(stream);
        auto mut = (GlobalMutability)stream.read_little_endian<uint8_t>();

        if (mut != GlobalMutability::Constant && mut != GlobalMutability::Variable)
            throw InvalidWASMException();

        return Global {
            .type = type,
            .mut = mut,
            .initCode = parse(stream, s_currentWasmFile),
        };
    }

    Export Export::read_from_stream(Stream& stream)
    {
        return Export {
            .name = stream.read_typed<std::string>(),
            .type = (ImportType)stream.read_little_endian<uint8_t>(), // FIXME: Don't rely on casts
            .index = stream.read_leb<uint32_t>(),
        };
    }

    Element Element::read_from_stream(Stream& stream)
    {
        uint32_t type = stream.read_leb<uint32_t>();

        if (type > 0x07)
            throw InvalidWASMException();

        bool is_passive_or_declarative = type & 0b1;
        bool has_table_index = type & 0b10;
        bool has_element_expressions = type & 0b100;

        Element element {};

        if (is_passive_or_declarative)
        {
            if (has_table_index)
                element.mode = ElementMode::Declarative;
            else
                element.mode = ElementMode::Passive;
        }
        else
        {
            element.mode = ElementMode::Active;
            element.table = has_table_index ? stream.read_leb<uint32_t>() : 0;
            element.expr = parse(stream, s_currentWasmFile);
        }

        element.valueType = Type::funcref;
        if (is_passive_or_declarative || has_table_index)
        {
            if (has_element_expressions)
            {
                element.valueType = (Type)stream.read_leb<uint8_t>();
                if (!is_reference_type(element.valueType))
                    throw InvalidWASMException();
            }
            else
            {
                if (stream.read_leb<uint8_t>() != 0)
                    throw InvalidWASMException();
            }
        }

        if (has_element_expressions)
            element.referencesExpr = stream.read_vec_with_function<std::vector<Instruction>, parse_with_current_wasm_file>();
        else
            element.functionIndexes = stream.read_vec<uint32_t>();

        return element;
    }

    Local Local::read_from_stream(Stream& stream)
    {
        return Local {
            .count = stream.read_leb<uint32_t>(),
            .type = read_type_from_stream(stream),
        };
    }

    Code Code::read_from_stream(Stream& stream)
    {
        // Skip size
        stream.read_leb<uint32_t>();

        std::vector<Local> locals = stream.read_vec<Local>();

        uint64_t count = 0;
        for (const auto& local : locals)
            count += local.count;

        if (count > UINT32_MAX)
            throw InvalidWASMException();

        std::vector<Type> localTypes;
        for (const auto& local : locals)
            for (size_t i = 0; i < local.count; i++)
                localTypes.push_back(local.type);

        return Code {
            .locals = localTypes,
            .instructions = parse(stream, s_currentWasmFile),
        };
    }

    Data Data::read_from_stream(Stream& stream)
    {
        uint32_t type = stream.read_leb<uint32_t>();
        switch (type)
        {
            case 0:
                return Data {
                    .type = type,
                    .memoryIndex = 0,
                    .expr = parse(stream, s_currentWasmFile),
                    .data = stream.read_vec<uint8_t>(),
                    .mode = ElementMode::Active,
                };
            case 1:
                return Data {
                    .type = type,
                    .memoryIndex = (uint32_t)-1,
                    .expr = {},
                    .data = stream.read_vec<uint8_t>(),
                    .mode = ElementMode::Passive,
                };
            case 2:
                return Data {
                    .type = type,
                    .memoryIndex = stream.read_leb<uint32_t>(),
                    .expr = parse(stream, s_currentWasmFile),
                    .data = stream.read_vec<uint8_t>(),
                    .mode = ElementMode::Active,
                };
            default:
                std::println(std::cerr, "Unsupported data type: {}", type);
                throw InvalidWASMException();
        }
    }

    Ref<WasmFile> WasmFile::read_from_stream(Stream& stream, bool runValidator)
    {
        try
        {
            Ref<WasmFile> wasm = MakeRef<WasmFile>();
            s_currentWasmFile = wasm;

            uint32_t signature = stream.read_little_endian<uint32_t>();
            uint32_t version = stream.read_little_endian<uint32_t>();

            if (signature != WASM_SIGNATURE)
            {
                std::println(std::cerr, "Not a WASM file!");
                throw InvalidWASMException();
            }

            if (version != 1)
                throw InvalidWASMException();

            std::vector<Section> foundSections;

            while (!stream.eof())
            {
                Section tag = (Section)stream.read_little_endian<uint8_t>();
                uint32_t size = stream.read_leb<uint32_t>();

                if (tag != Section::Custom && vector_contains(foundSections, tag))
                    throw InvalidWASMException();
                foundSections.push_back(tag);

                std::vector<uint8_t> section(size);
                stream.read((void*)section.data(), size);

                MemoryStream sectionStream((char*)section.data(), size);
                switch (tag)
                {
                    case Section::Custom:
                        sectionStream.read_typed<std::string>();
                        sectionStream.move_to(sectionStream.size());
                        break;
                    case Section::Type:
                        wasm->functionTypes = sectionStream.read_vec<FunctionType>();
                        break;
                    case Section::Import:
                        wasm->imports = sectionStream.read_vec<Import>();
                        break;
                    case Section::Function:
                        wasm->functionTypeIndexes = sectionStream.read_vec<uint32_t>();
                        break;
                    case Section::Table:
                        wasm->tables = sectionStream.read_vec<Table>();
                        break;
                    case Section::Memory:
                        wasm->memories = sectionStream.read_vec<Memory>();
                        break;
                    case Section::Global:
                        wasm->globals = sectionStream.read_vec<Global>();
                        break;
                    case Section::Export:
                        wasm->exports = sectionStream.read_vec<Export>();
                        break;
                    case Section::Start:
                        wasm->startFunction = sectionStream.read_leb<uint32_t>();
                        break;
                    case Section::Element:
                        wasm->elements = sectionStream.read_vec<Element>();
                        break;
                    case Section::Code:
                        wasm->codeBlocks = sectionStream.read_vec<Code>();
                        break;
                    case Section::Data:
                        wasm->dataBlocks = sectionStream.read_vec<Data>();
                        break;
                    case Section::DataCount: {
                        wasm->dataCount = sectionStream.read_leb<uint32_t>();
                        break;
                    }
                    default:
                        std::println(std::cerr, "Warning: Unknown section: {}", static_cast<int>(tag));
                        throw InvalidWASMException();
                }

                if (!sectionStream.eof())
                    throw InvalidWASMException();
            }

            if (wasm->functionTypeIndexes.size() != wasm->codeBlocks.size())
                throw InvalidWASMException();

            if (wasm->dataCount)
                if (wasm->dataBlocks.size() != wasm->dataCount)
                    throw InvalidWASMException();

            s_currentWasmFile = nullptr;

            if (runValidator)
                Validator validator = Validator(wasm);

            return wasm;
        }
        catch (StreamReadException e)
        {
            throw InvalidWASMException();
        }
    }

    Export WasmFile::find_export_by_name(const std::string& name)
    {
        for (const auto& exportValue : exports)
        {
            if (exportValue.name == name)
                return exportValue;
        }

        std::println(std::cerr, "Error: Tried to find a non existent export: {}", name);
        throw Trap();
    }

    uint32_t WasmFile::get_import_count_of_type(ImportType type)
    {
        uint32_t count = 0;
        for (const auto& import : imports)
            if (import.type == type)
                count++;
        return count;
    }

    BlockType BlockType::read_from_stream(Stream& stream)
    {
        Type type = (Type)stream.read_leb<uint32_t>();
        if (type == Type::empty)
            return { .type = {}, .index = UINT64_MAX };

        if (is_valid_type(type))
            return { .type = type, .index = UINT64_MAX };
        else
        {
            stream.skip(-1);
            // FIXME: This s33
            return { .type = {}, .index = stream.read_leb<uint64_t>() };
        }
    }

    std::vector<Type> BlockType::get_param_types(Ref<WasmFile> wasmFile) const
    {
        if (index == UINT64_MAX)
            return {};

        if (index >= wasmFile->functionTypes.size())
        {
            std::println(std::cerr, "Error: Invalid block type index: {}", index);
            throw InvalidWASMException();
        }

        const auto& functionType = wasmFile->functionTypes[index];
        return functionType.params;
    }

    std::vector<Type> BlockType::get_return_types(Ref<WasmFile> wasmFile) const
    {
        if (index == UINT64_MAX)
        {
            if (type.has_value())
                return { type.value() };
            else
                return {};
        }

        if (index >= wasmFile->functionTypes.size())
        {
            std::println(std::cerr, "Error: Invalid block type index: {}", index);
            throw InvalidWASMException();
        }

        const auto& functionType = wasmFile->functionTypes[index];
        return functionType.returns;
    }
}
