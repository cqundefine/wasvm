#include "WASI.h"
#include "VM/Module.h"
#include "VM/VM.h"
#include "VM/Value.h"
#include <cassert>
#include <cstring>
#include <functional>
#include <print>
#include <sys/stat.h>
#include <vector>

enum class FileType : uint8_t
{
    Unknown,
    BlockDevice,
    CharacterDevice,
    Directory,
    RegularFile,
    SocketDGram,
    SocketStream,
    SymbolicLink,
};

FileType file_type_from_stat(const struct stat& statBuffer)
{
    switch (statBuffer.st_mode & S_IFMT)
    {
        case S_IFDIR:
            return FileType::Directory;
        case S_IFCHR:
            return FileType::CharacterDevice;
        case S_IFBLK:
            return FileType::BlockDevice;
        case S_IFREG:
            return FileType::RegularFile;
        case S_IFLNK:
            return FileType::SymbolicLink;
        case S_IFSOCK:
            return FileType::SocketStream;
        case S_IFIFO:
        default:
            return FileType::Unknown;
    }
}

struct FDStat
{
    FileType fs_filetype;
    // a byte of padding
    uint16_t fs_flags;
    // a byte of padding
    uint64_t fs_rights_base;
    uint64_t fs_rights_inheriting;
};
static_assert(sizeof(FDStat) == 24);

struct IOVector
{
    uint32_t pointer;
    uint32_t length;
};
static_assert(sizeof(IOVector) == 8);

class NativeFunction : public Function
{
public:
    using FunctionType = std::function<std::vector<Value>(std::span<const Value>)>;

    NativeFunction(FunctionType function, const std::vector<Type>& params, std::optional<Type> returnType)
        : m_function(function)
        , m_type(WasmFile::FunctionType {
              .params = params,
              .returns = returnType ? std::vector<Type> { returnType.value() } : std::vector<Type> {} })
    {
    }

    virtual const WasmFile::FunctionType& type() const override
    {
        return m_type;
    }

    virtual std::vector<Value> run(std::span<const Value> args) const override
    {
        return { m_function(args) };
    }

private:
    FunctionType m_function;
    WasmFile::FunctionType m_type;
};

WASIModule::WASIModule()
{
    m_functions["clock_time_get"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value> args) {
            if (args[0].get<uint32_t>() != 0)
                std::println("unsupported clock_time_get");
            if (args[1].get<uint64_t>() != 1000)
                std::println("unsupported clock_time_get");

            struct timespec ts;
            assert(clock_gettime(0, &ts) == 0);
            uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
            memcpy(VM::get_current_frame_memory_0()->data() + args[2].get<uint32_t>(), &nanos, sizeof(nanos));

            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32, Type::i64, Type::i32 }, Type::i32);

    m_functions["fd_close"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value>) {
            std::println("fd_close not supported");
            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32 }, Type::i32);

    m_functions["fd_fdstat_get"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value> args) {
            struct stat statBuffer;
            fstat(args[0].get<uint32_t>(), &statBuffer);

            FDStat fdStat {
                .fs_filetype = file_type_from_stat(statBuffer),
                .fs_flags = 0,
                .fs_rights_base = 0,
                .fs_rights_inheriting = 0,
            };

            memcpy(VM::get_current_frame_memory_0()->data() + args[1].get<uint32_t>(), &fdStat, sizeof(fdStat));

            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32, Type::i32 }, Type::i32);

    m_functions["fd_seek"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value>) {
            std::println("fd_seek not supported");
            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32, Type::i64, Type::i32, Type::i32 }, Type::i32);

    m_functions["fd_write"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value> args) {
            uint32_t written = 0;
            const auto memory = VM::get_current_frame_memory_0();

            IOVector* iov = (IOVector*)(memory->data() + args[1].get<uint32_t>());

            for (uint32_t i = 0; i < args[2].get<uint32_t>(); i++)
                written += (uint32_t)write(args[0].get<uint32_t>(), (memory->data() + iov[i].pointer), iov[i].length);

            memcpy(memory->data() + args[3].get<uint32_t>(), &written, sizeof(written));

            return std::vector<Value> { written };
        }),
        std::vector { Type::i32, Type::i32, Type::i32, Type::i32 }, Type::i32);

    m_functions["poll_oneoff"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value>) {
            std::println("poll_oneoff not supported");
            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32, Type::i32, Type::i32, Type::i32 }, Type::i32);

    m_functions["proc_exit"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value>) {
            std::println("proc_exit not supported");
            return std::vector<Value> {};
        }),
        std::vector { Type::i32 }, std::optional<Type> {});

    m_functions["random_get"] = MakeRef<NativeFunction>(
        NativeFunction::FunctionType([](std::span<const Value> args) {
            fill_buffer_with_random_data(VM::get_current_frame_memory_0()->data() + args[0].get<uint32_t>(), args[1].get<uint32_t>());
            return std::vector<Value> { (uint32_t)0 };
        }),
        std::vector { Type::i32, Type::i32 }, Type::i32);
}

std::optional<ImportedObject> WASIModule::try_import(std::string_view name, WasmFile::ImportType type) const
{
    switch (type)
    {
        using enum WasmFile::ImportType;
        case Function: {
            auto it = m_functions.find(name);
            return it != m_functions.end() ? it->second : std::optional<ImportedObject> {};
        }
        default:
            return {};
    }
}
