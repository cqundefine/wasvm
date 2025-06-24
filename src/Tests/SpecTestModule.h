#include "Util/StringMap.h"
#include "VM/Module.h"

class SpecTestModule : public Module
{
public:
    SpecTestModule();
    std::optional<ImportedObject> try_import(std::string_view name, WasmFile::ImportType type) const;

    StringMap<Ref<Global>> m_globals;
    Ref<Table> m_table;
    Ref<Memory> m_memory;
    StringMap<Ref<Function>> m_functions;
};
