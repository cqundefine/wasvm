#include <MemoryStream.h>
#include <Opcode.h>
#include <Parser.h>
#include <Type.h>
#include <Util.h>
#include <WasmFile.h>

namespace WasmFile
{
    Ref<WasmFile> s_currentWasmFile;

    Limits Limits::read_from_stream(Stream& stream)
    {
        uint8_t type = stream.read_little_endian<uint8_t>();
        if (type == 0x00)
        {
            return {
                .min = stream.read_leb<uint32_t>(),
                .max = UINT32_MAX,
            };
        }
        else if (type == 0x01)
        {
            return {
                .min = stream.read_leb<uint32_t>(),
                .max = stream.read_leb<uint32_t>(),
            };
        }

        throw InvalidWASMException();
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
        
        return {
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

        return {
            .params = params,
            .returns = returns,
        };
    }

    Import Import::read_from_stream(Stream& stream)
    {
        std::string environment = stream.read_typed<std::string>();
        std::string name = stream.read_typed<std::string>();

        uint8_t type = stream.read_leb<uint8_t>();
        if (type == 0)
        {
            return Import {
                .type = ImportType::Function,
                .environment = environment,
                .name = name,
                .functionIndex = stream.read_leb<uint32_t>(),
            };
        }
        else if (type == 1)
        {
            return Import {
                .type = ImportType::Table,
                .environment = environment,
                .name = name,
                .tableRefType = read_type_from_stream(stream),
                .tableLimits = stream.read_typed<Limits>(),
            };
        }
        else if (type == 2)
        {
            return Import {
                .type = ImportType::Memory,
                .environment = environment,
                .name = name,
                .memoryLimits = stream.read_typed<Limits>(),
            };
        }
        else if (type == 3)
        {
            return Import {
                .type = ImportType::Global,
                .environment = environment,
                .name = name,
                .globalType = read_type_from_stream(stream),
                .globalMut = stream.read_leb<uint8_t>(),
            };
        }
        else
        {
            fprintf(stderr, "Error: Invalid import type: %d\n", type);
            throw InvalidWASMException();
        }
    }

    Table Table::read_from_stream(Stream& stream)
    {
        return {
            .refType = read_type_from_stream(stream),
            .limits = stream.read_typed<Limits>(),
        };
    }

    Memory Memory::read_from_stream(Stream& stream)
    {
        return {
            .limits = stream.read_typed<Limits>(),
        };
    }

    Global Global::read_from_stream(Stream& stream)
    {
        return {
            .type = read_type_from_stream(stream),
            .mut = stream.read_little_endian<uint8_t>(),
            .initCode = parse(stream, s_currentWasmFile),
        };
    }

    Export Export::read_from_stream(Stream& stream)
    {
        return {
            .name = stream.read_typed<std::string>(),
            .type = (ImportType)stream.read_little_endian<uint8_t>(), // FIXME: Don't rely on casts
            .index = stream.read_leb<uint32_t>(),
        };
    }

    Element Element::read_from_stream(Stream& stream)
    {
        uint32_t type = stream.read_leb<uint32_t>();

        if (type == 0)
        {
            return {
                .type = type,
                .table = 0,
                .expr = parse(stream, s_currentWasmFile),
                .functionIndexes = stream.read_vec<uint32_t>(),
                .mode = ElementMode::Active,
            };
        }
        else if (type == 1)
        {
            stream.read_leb<uint8_t>();
            return Element {
                .type = type,
                .functionIndexes = stream.read_vec<uint32_t>(),
                .mode = ElementMode::Passive,
            };
        }
        else if (type == 2)
        {
            uint32_t table = stream.read_leb<uint32_t>();
            std::vector<Instruction> expr = parse(stream, s_currentWasmFile);
            stream.read_little_endian<uint8_t>();
            return {
                .type = type,
                .table = table,
                .expr = expr,
                .functionIndexes = stream.read_vec<uint32_t>(),
                .mode = ElementMode::Active,
            };
        }
        else if (type == 3)
        {
            stream.read_leb<uint8_t>();
            return Element {
                .type = type,
                .functionIndexes = stream.read_vec<uint32_t>(),
                .mode = ElementMode::Declarative,
            };
        }
        else if (type == 4)
        {
            std::vector<Instruction> expr = parse(stream, s_currentWasmFile);
            std::vector<std::vector<Instruction>> references;
            uint32_t size = stream.read_leb<uint32_t>();
            for (uint32_t i = 0; i < size; i++)
            {
                std::vector<Instruction> index = parse(stream, s_currentWasmFile);
                references.push_back(index);
            }
            return {
                .type = type,
                .table = 0,
                .expr = expr,
                .referencesExpr = references,
                .mode = ElementMode::Active,
            };
        }
        else if (type == 5)
        {
            stream.read_leb<uint8_t>();
            std::vector<std::vector<Instruction>> references;
            uint32_t size = stream.read_leb<uint32_t>();
            for (uint32_t i = 0; i < size; i++)
            {
                std::vector<Instruction> index = parse(stream, s_currentWasmFile);
                references.push_back(index);
            }
            return Element {
                .type = type,
                .referencesExpr = references,
                .mode = ElementMode::Passive,
            };
        }
        else if (type == 6)
        {
            uint32_t table = stream.read_leb<uint32_t>();
            std::vector<Instruction> expr = parse(stream, s_currentWasmFile);
            stream.read_leb<uint8_t>();

            std::vector<std::vector<Instruction>> references;
            uint32_t size = stream.read_leb<uint32_t>();
            for (uint32_t i = 0; i < size; i++)
            {
                std::vector<Instruction> index = parse(stream, s_currentWasmFile);
                references.push_back(index);
            }

            return Element {
                .type = type,
                .table = table,
                .expr = expr,
                .referencesExpr = references,
                .mode = ElementMode::Active,
            };
        }
        else if (type == 7)
        {
            stream.read_leb<uint8_t>();

            std::vector<std::vector<Instruction>> references;
            uint32_t size = stream.read_leb<uint32_t>();
            for (uint32_t i = 0; i < size; i++)
            {
                std::vector<Instruction> index = parse(stream, s_currentWasmFile);
                references.push_back(index);
            }

            return Element {
                .type = type,
                .referencesExpr = references,
                .mode = ElementMode::Declarative,
            };
        }
        else
        {
            fprintf(stderr, "Error: Unknown element type: %d\n", type);
            throw InvalidWASMException();
        }
    }

    Local Local::read_from_stream(Stream& stream)
    {
        return {
            .count = stream.read_leb<uint32_t>(),
            .type = read_type_from_stream(stream),
        };
    }

    Code Code::read_from_stream(Stream& stream)
    {
        uint32_t size = stream.read_leb<uint32_t>();

        size_t beforeLocals = stream.offset();
        std::vector<Local> locals = stream.read_vec<Local>();

        return {
            .locals = locals,
            .instructions = parse(stream, s_currentWasmFile),
        };
    }

    Data Data::read_from_stream(Stream& stream)
    {
        uint32_t type = stream.read_leb<uint32_t>();
        if (type == 0)
        {
            return {
                .type = type,
                .memoryIndex = 0,
                .expr = parse(stream, s_currentWasmFile),
                .data = stream.read_vec<uint8_t>(),
                .mode = ElementMode::Active,
            };
        }
        else if (type == 1)
        {
            return {
                .type = type,
                .memoryIndex = (uint32_t)-1,
                .expr = {},
                .data = stream.read_vec<uint8_t>(),
                .mode = ElementMode::Passive,
            };
        }
        else if (type == 2)
        {
            return {
                .type = type,
                .memoryIndex = stream.read_leb<uint32_t>(),
                .expr = parse(stream, s_currentWasmFile),
                .data = stream.read_vec<uint8_t>(),
                .mode = ElementMode::Active,
            };
        }
        else
        {
            fprintf(stderr, "Unsupported data type: %d\n", type);
            throw InvalidWASMException();
        }

    }

    Ref<WasmFile> WasmFile::read_from_stream(Stream& stream)
    {
        Ref<WasmFile> wasm = MakeRef<WasmFile>();
        s_currentWasmFile = wasm;

        uint32_t signature = stream.read_little_endian<uint32_t>();
        uint32_t version = stream.read_little_endian<uint32_t>();

        if (signature != WASM_SIGNATURE)
        {
            fprintf(stderr, "Not a WASM file!\n");
            throw InvalidWASMException();
        }

        if (version != 1)
        {
            fprintf(stderr, "Invalid WASM version!\n");
            throw InvalidWASMException();
        }

        while (!stream.eof())
        {
            uint8_t tag = stream.read_little_endian<uint8_t>();
            uint32_t size = stream.read_leb<uint32_t>();

            // printf("Section 0x%x: 0x%x bytes\n", tag, size);

            std::vector<uint8_t> section(size);
            stream.read((void*)section.data(), size);

            MemoryStream sectionStream((char*)section.data(), size);
            if (tag == CustomSection)
            {
                // Silently ignore...
            }
            else if (tag == TypeSection)
            {
                wasm->functionTypes = sectionStream.read_vec<FunctionType>();
            }
            else if (tag == ImportSection)
            {
                wasm->imports = sectionStream.read_vec<Import>();
            }
            else if (tag == FunctionSection)
            {
                wasm->functionTypeIndexes = vector_to_map_offset(sectionStream.read_vec<uint32_t>(), wasm->get_import_count_of_type(ImportType::Function));
            }
            else if (tag == TableSection)
            {
                wasm->tables = sectionStream.read_vec<Table>();
            }
            else if (tag == MemorySection)
            {
                wasm->memories = sectionStream.read_vec<Memory>();
            }
            else if (tag == GlobalSection)
            {
                wasm->globals = sectionStream.read_vec<Global>();
            }
            else if (tag == ExportSection)
            {
                wasm->exports = sectionStream.read_vec<Export>();
            }
            else if (tag == StartSection)
            {
                wasm->startFunction = sectionStream.read_leb<uint32_t>();
            }
            else if (tag == ElementSection)
            {
                wasm->elements = sectionStream.read_vec<Element>();
            }
            else if (tag == CodeSection)
            {
                wasm->codeBlocks = vector_to_map_offset(sectionStream.read_vec<Code>(), wasm->get_import_count_of_type(ImportType::Function));
            }
            else if (tag == DataSection)
            {
                wasm->dataBlocks = sectionStream.read_vec<Data>();
            }
            else if (tag == DataCountSection)
            {
                // Ignore it...
            }
            else
            {
                fprintf(stderr, "Warning: Unknown section: %d\n", tag);
            }
        }

        s_currentWasmFile = nullptr;
        return wasm;
    }

    Export WasmFile::find_export_by_name(const std::string& name)
    {
        for (const auto& exportValue : exports)
        {
            if (exportValue.name == name)
                return exportValue;
        }

        fprintf(stderr, "Error: Tried to find a non existent export: %s\n", name.c_str());
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
            return { .type = {}, .index = stream.read_leb<uint64_t>() };
        }
    }

    std::vector<Type> BlockType::get_param_types(Ref<WasmFile> wasmFile) const
    {
        if (index == UINT64_MAX)
            return {};

        if (index >= wasmFile->functionTypes.size())
        {
            fprintf(stderr, "Error: Invalid block type index: %lu\n", index);
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
            fprintf(stderr, "Error: Invalid block type index: %lu\n", index);
            throw InvalidWASMException();
        }

        const auto& functionType = wasmFile->functionTypes[index];
        return functionType.returns;
    }
}
