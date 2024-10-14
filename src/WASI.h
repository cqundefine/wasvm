#pragma once

#include <Value.h>
#include <cstdint>
#include <string>
#include <sys/stat.h>
#include <vector>

#define ENUMERATE_WASI_CALL(x)                             \
    x(args_get, uint32_t, uint32_t)                        \
    x(args_sizes_get, uint32_t, uint32_t)                  \
    x(clock_time_get, uint32_t, uint64_t, uint32_t)        \
    x(environ_get, uint32_t, uint32_t)                     \
    x(environ_sizes_get, uint32_t, uint32_t)               \
    x(fd_fdstat_get, uint32_t, uint32_t)                   \
    x(fd_prestat_dir_name, uint32_t, uint32_t, uint32_t)   \
    x(fd_prestat_get, uint32_t, uint32_t)                  \
    x(fd_write, uint32_t, uint32_t, uint32_t, uint32_t)    \
    x(poll_oneoff, uint32_t, uint32_t, uint32_t, uint32_t) \
    x(proc_exit, uint32_t)                                 \
    x(random_get, uint32_t, uint32_t)

namespace WASI
{
    enum class ClockID : uint32_t
    {
        Realtime,
        Monotonic,
        ProcessCPUTimeID,
        ThreadCPUTimeID,
    };

    struct IOVector
    {
        uint32_t pointer;
        uint32_t length;
    };
    static_assert(sizeof(IOVector) == 8);

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

    FileType file_type_from_stat(const struct stat& statBuffer);

    union FDFlags
    {
        uint16_t value;
        struct
        {
            uint16_t append : 1;
            uint16_t dsync : 1;
            uint16_t nonblock : 1;
            uint16_t rsync : 1;
            uint16_t sync : 1;
            uint16_t unused : 11;
        };
    };
    static_assert(sizeof(FDFlags) == 2);

    union Rights
    {
        uint64_t value;
        struct
        {
            uint64_t fd_datasync : 1;
            uint64_t fd_read : 1;
            uint64_t fd_seek : 1;
            uint64_t fd_fdstat_set_flags : 1;
            uint64_t fd_sync : 1;
            uint64_t fd_tell : 1;
            uint64_t fd_write : 1;
            uint64_t fd_advice : 1;
            uint64_t fd_allocate : 1;
            uint64_t path_create_directory : 1;
            uint64_t path_create_file : 1;
            uint64_t path_link_source : 1;
            uint64_t path_link_target : 1;
            uint64_t path_open : 1;
            uint64_t fd_readdir : 1;
            uint64_t path_readlink : 1;
            uint64_t path_rename_source : 1;
            uint64_t path_rename_target : 1;
            uint64_t path_filestat_get : 1;
            uint64_t path_filestat_set_size : 1;
            uint64_t path_filestat_set_times : 1;
            uint64_t fd_filestat_get : 1;
            uint64_t fd_filestat_set_size : 1;
            uint64_t fd_filestat_set_times : 1;
            uint64_t path_symlink : 1;
            uint64_t path_remove_directory : 1;
            uint64_t path_unlink_file : 1;
            uint64_t poll_fd_readwrite : 1;
            uint64_t sock_shutdown : 1;
            uint64_t sock_accept : 1;
            uint64_t unused : 34;
        };
    };
    static_assert(sizeof(Rights) == 8);

    struct FDStat
    {
        FileType fs_filetype;
        // a byte of padding
        FDFlags fs_flags;
        // a byte of padding
        Rights fs_rights_base;
        Rights fs_rights_inheriting;
    };
    static_assert(sizeof(FDStat) == 24);

    enum class PreOpenType : uint8_t
    {
        Dir,
    };

    struct PreStatDir
    {
        uint32_t pr_name_len;
    };
    static_assert(sizeof(PreStatDir) == 4);

    struct PreStat
    {
        PreOpenType pr_type;
        PreStatDir u;
    };
    static_assert(sizeof(PreStat) == 8);

    std::vector<Value> run_wasi_call(const std::string& name, const std::vector<Value>& args);
}
