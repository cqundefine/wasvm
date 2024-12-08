#pragma once

#include <WasmFile.h>

class Validator
{
public:
    static void validate(Ref<WasmFile::WasmFile> wasmFile);

private:
    static void validate_function(Ref<WasmFile::WasmFile> wasmFile, const WasmFile::FunctionType& type, const WasmFile::Code& code);
};
