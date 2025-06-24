#pragma once

#include "Util/StringMap.h"
#include "VM/Module.h"

class WASIModule : public Module
{
public:
    WASIModule();

    virtual std::optional<ImportedObject> try_import(std::string_view name, WasmFile::ImportType type) const override;

private:
    StringMap<Ref<Function>> m_functions;
};
