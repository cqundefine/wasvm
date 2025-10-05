#pragma once

#include "Util/StringMap.h"
#include "VM/Module.h"

class SpecTestModule : public Module
{
public:
    SpecTestModule();

    virtual std::optional<ImportedObject> try_import(std::string_view name, WasmFile::ImportType type) const override;

private:
    StringMap<Ref<Global>> m_globals;
    Ref<Table> m_table;
    Ref<Table> m_table64;
    Ref<Memory> m_memory;
    StringMap<Ref<Function>> m_functions;
};
