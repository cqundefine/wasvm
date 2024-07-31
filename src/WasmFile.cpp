#include <MemoryStream.h>
#include <Opcode.h>
#include <Parser.h>
#include <Type.h>
#include <Util.h>
#include <WasmFile.h>

WasmFile* s_currentWasmFile;

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
    return {
        .align = stream.read_leb<uint32_t>(),
        .offset = stream.read_leb<uint32_t>(),
    };
}

FunctionType FunctionType::read_from_stream(Stream& stream)
{
    uint8_t byte = stream.read_leb<uint8_t>();
    if (byte != 0x60)
        throw InvalidWASMException();

    std::vector<uint8_t> params = stream.read_vec<uint8_t>();
    std::vector<uint8_t> returns = stream.read_vec<uint8_t>();

    return {
        .params = params,
        .returns = returns,
    };
}

Import Import::read_from_stream(Stream& stream)
{
    std::string environment = stream.read_typed<std::string>();
    std::string name = stream.read_typed<std::string>();

    uint8_t byte = stream.read_leb<uint8_t>();
    if (byte != 0)
    {
        fprintf(stderr, "Error: Invalid import type: %d\n", byte);
        throw InvalidWASMException();
    }

    return {
        .environment = environment,
        .name = name,
        .index = stream.read_leb<uint32_t>(),
    };
}

Table Table::read_from_stream(Stream& stream)
{
    return {
        .refType = stream.read_little_endian<uint8_t>(),
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
        .type = stream.read_leb<uint8_t>(),
        .mut = stream.read_little_endian<uint8_t>(),
        .initCode = parse(stream, *s_currentWasmFile),
    };
}

Export Export::read_from_stream(Stream& stream)
{
    return {
        .name = stream.read_typed<std::string>(),
        .type = stream.read_little_endian<uint8_t>(),
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
            .expr = parse(stream, *s_currentWasmFile),
            .functionIndexes = stream.read_vec<uint32_t>(),
        };
    }
    else if (type == 2)
    {
        uint32_t table = stream.read_leb<uint32_t>();

        stream.read_little_endian<uint8_t>();

        return {
            .type = type,
            .table = table,
            .expr = parse(stream, *s_currentWasmFile),
            .functionIndexes = stream.read_vec<uint32_t>(),
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
        .type = stream.read_leb<uint8_t>(),
    };
}

Code Code::read_from_stream(Stream& stream)
{
    uint32_t size = stream.read_leb<uint32_t>();

    size_t beforeLocals = stream.offset();
    std::vector<Local> locals = stream.read_vec<Local>();

    return {
        .locals = locals,
        .instructions = parse(stream, *s_currentWasmFile),
    };
}

Data Data::read_from_stream(Stream& stream)
{
    uint32_t type = stream.read_leb<uint32_t>();
    if (type != 0)
    {
        fprintf(stderr, "Unsupported data type: %d\n", type);
        throw InvalidWASMException();
    }

    return {
        .expr = parse(stream, *s_currentWasmFile),
        .data = stream.read_vec<uint8_t>(),
    };
}

WasmFile WasmFile::read_from_stream(Stream& stream)
{
    WasmFile wasm;
    s_currentWasmFile = &wasm;

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
            wasm.functionTypes = sectionStream.read_vec<FunctionType>();
        }
        else if (tag == ImportSection)
        {
            wasm.imports = sectionStream.read_vec<Import>();
        }
        else if (tag == FunctionSection)
        {
            wasm.functionTypeIndexes = vector_to_map_offset(sectionStream.read_vec<uint32_t>(), wasm.imports.size());
        }
        else if (tag == TableSection)
        {
            wasm.tables = sectionStream.read_vec<Table>();
        }
        else if (tag == MemorySection)
        {
            wasm.memories = sectionStream.read_vec<Memory>();
            if (wasm.memories.size() > 1)
                throw InvalidWASMException();
        }
        else if (tag == GlobalSection)
        {
            wasm.globals = sectionStream.read_vec<Global>();
        }
        else if (tag == ExportSection)
        {
            wasm.exports = sectionStream.read_vec<Export>();
        }
        else if (tag == ElementSection)
        {
            wasm.elements = sectionStream.read_vec<Element>();
        }
        else if (tag == CodeSection)
        {
            wasm.codeBlocks = vector_to_map_offset(sectionStream.read_vec<Code>(), wasm.imports.size());
        }
        else if (tag == DataSection)
        {
            wasm.dataBlocks = sectionStream.read_vec<Data>();
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

BlockType BlockType::read_from_stream(Stream& stream)
{
    uint8_t type = stream.read_little_endian<uint8_t>();
    if (type == EMPTY_TYPE)
        return { .type = {}, .index = UINT64_MAX };

    if (is_valid_type(type))
        return { .type = type, .index = UINT64_MAX };
    else
    {
        stream.skip(-1);
        return { .type = {}, .index = stream.read_leb<uint64_t>() };
    }
}

std::vector<uint8_t> BlockType::get_param_types(const WasmFile& wasmFile) const
{
    if (index == UINT64_MAX)
        return {};

    if (index >= wasmFile.functionTypes.size())
    {
        printf("Error: Invalid block type index: %d\n", index);
        throw InvalidWASMException();
    }

    const auto& functionType = wasmFile.functionTypes[index];
    return functionType.params;
}

std::vector<uint8_t> BlockType::get_return_types(const WasmFile& wasmFile) const
{
    if (index == UINT64_MAX)
    {
        if (type.has_value())
            return { type.value() };
        else
            return {};
    }

    if (index >= wasmFile.functionTypes.size())
    {
        printf("Error: Invalid block type index: %d\n", index);
        throw InvalidWASMException();
    }

    const auto& functionType = wasmFile.functionTypes[index];
    return functionType.returns;
}
