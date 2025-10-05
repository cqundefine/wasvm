#include "WasmFile.h"
#include "Parser.h"
#include "Stream/MemoryStream.h"
#include "Validator.h"

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

        uint64_t min = stream.read_leb<uint64_t>();
        std::optional<uint64_t> max;

        if (type != 0x00 && type != 0x01 && type != 0x04 && type != 0x05)
            throw InvalidWASMException("Invalid limits type");

        bool has_max = type & 0b001;
        bool addr64 = type & 0b100;

        if (has_max)
            max = stream.read_leb<uint64_t>();

        if (max && min > max)
            throw InvalidWASMException("Invalid limits");

        return Limits {
            .min = min,
            .max = max,
            .address_type = addr64 ? AddressType::i64 : AddressType::i32,
        };
    }

    bool Limits::fits_within(const Limits& other) const
    {
        return min >= other.min && (!other.max.has_value() || (max.has_value() && max.value() <= other.max.value())) && address_type == other.address_type;
    }

    MemArg MemArg::read_from_stream(Stream& stream)
    {
        uint32_t align = stream.read_leb<uint32_t>();

        uint32_t memory_index = 0;
        if ((align & 0x40) != 0)
        {
            align &= ~0x40;
            memory_index = stream.read_leb<uint32_t>();
        }

        return MemArg {
            .align = align,
            .offset = stream.read_leb<uint64_t>(),
            .memory_index = memory_index,
        };
    }

    FunctionType FunctionType::read_from_stream(Stream& stream)
    {
        uint8_t byte = stream.read_leb<uint8_t>();
        if (byte != 0x60)
            throw InvalidWASMException("Invalid function type byte");

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
                    throw InvalidWASMException("Invalid global mutability of import");

                return Import {
                    .type = ImportType::Global,
                    .environment = environment,
                    .name = name,
                    .globalType = type,
                    .globalMutability = mut,
                };
            }
            default:
                throw InvalidWASMException(std::format("Invalid import type: {}", type));
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
            throw InvalidWASMException("Invalid global mutability");

        return Global {
            .type = type,
            .mutability = mut,
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
            throw InvalidWASMException("Invalid element type");

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
                    throw InvalidWASMException("Invalid element reference type");
            }
            else
            {
                if (stream.read_leb<uint8_t>() != 0)
                    throw InvalidWASMException("Invalid element byte");
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
            throw InvalidWASMException("Too many locals");

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
                throw InvalidWASMException(std::format("Unsupported data type: {}", type));
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
                throw InvalidWASMException("Not a WASM file!");

            if (version != 1)
                throw InvalidWASMException("Invalid WASM version");

            std::vector<Section> foundSections;

            while (!stream.eof())
            {
                Section tag = (Section)stream.read_little_endian<uint8_t>();
                uint32_t size = stream.read_leb<uint32_t>();

                if (tag != Section::Custom && vector_contains(foundSections, tag))
                    throw InvalidWASMException("Duplicate sections");
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
                        throw InvalidWASMException(std::format("Unknown section: {}", static_cast<int>(tag)));
                }

                if (!sectionStream.eof())
                    throw InvalidWASMException("Extra data at the end of a section");
            }

            if (wasm->functionTypeIndexes.size() != wasm->codeBlocks.size())
                throw InvalidWASMException("Function count doesnt match code count");

            if (wasm->dataCount)
                if (wasm->dataBlocks.size() != wasm->dataCount)
                    throw InvalidWASMException("Data counts do not match");

            s_currentWasmFile = nullptr;

            if (runValidator)
                Validator validator = Validator(wasm);

            return wasm;
        }
        catch (StreamReadException e)
        {
            throw InvalidWASMException("Stream read failed");
        }
    }

    std::optional<Export> WasmFile::find_export_by_name(std::string_view name)
    {
        for (const auto& exportValue : exports)
        {
            if (exportValue.name == name)
                return exportValue;
        }

        return {};
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
            throw InvalidWASMException(std::format("Invalid block type index: {}", index));

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
            throw InvalidWASMException(std::format("Invalid block type index: {}", index));

        const auto& functionType = wasmFile->functionTypes[index];
        return functionType.returns;
    }
}
