#include <Util.h>
#include <VM.h>
#include <WASI.h>
#include <cassert>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace WASI
{
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

#define DEFINE_WASI_CALL(x) std::vector<Value> wasi$##x(const std::vector<Value>& args)

    DEFINE_WASI_CALL(args_get)
    {
        fprintf(stderr, "Warning: args_get not implemented\n");
        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(args_sizes_get)
    {
        uint32_t count = 0;
        uint32_t size = 0;

        fprintf(stderr, "Warning: args_sizes_get not implemented\n");

        // for (char **s = argv; *s; s++)
        // {
        //     count++;
        //     size += strlen(*s) + 1;
        // }

        memcpy(VM::memory() + args[0].get<uint32_t>(), &count, sizeof(count));
        memcpy(VM::memory() + args[1].get<uint32_t>(), &size, sizeof(size));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(clock_time_get)
    {
        assert(args[0].get<uint32_t>() == 0);
        assert(args[1].get<uint64_t>() == 1000);

        struct timespec ts;
        assert(clock_gettime(0, &ts) == 0);
        uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
        memcpy(VM::memory() + args[2].get<uint32_t>(), &nanos, sizeof(nanos));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(environ_get)
    {
        fprintf(stderr, "Warning: environ_get not implemented\n");
        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(environ_sizes_get)
    {
        uint32_t count = 0;
        uint32_t size = 0;

        fprintf(stderr, "Warning: environ_sizes_get not implemented\n");

        // for (char **s = environ; *s; s++)
        // {
        //     count++;
        //     size += strlen(*s) + 1;
        // }

        memcpy(VM::memory() + args[0].get<uint32_t>(), &count, sizeof(count));
        memcpy(VM::memory() + args[1].get<uint32_t>(), &size, sizeof(size));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(fd_fdstat_get)
    {
        struct stat statBuffer;
        fstat(args[0].get<uint32_t>(), &statBuffer);

        FDStat fdStat {
            .fs_filetype = file_type_from_stat(statBuffer),
            .fs_flags = { 0 },
            .fs_rights_base = { 0 },
            .fs_rights_inheriting = { 0 },
        };

        memcpy(VM::memory() + args[1].get<uint32_t>(), &fdStat, sizeof(fdStat));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(fd_prestat_dir_name)
    {
        fprintf(stderr, "Warning: fd_prestat_dir_name not implemented: %d\n", args[0].get<uint32_t>());

        if (args[0].get<uint32_t>() != 3)
            return { (uint32_t)8 };

        assert(args[2].get<uint32_t>() >= 1);

        memcpy(VM::memory() + args[1].get<uint32_t>(), "/", 1);

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(fd_prestat_get)
    {
        fprintf(stderr, "Warning: fd_prestat_get not implemented: %d\n", args[0].get<uint32_t>());

        if (args[0].get<uint32_t>() != 3)
            return { (uint32_t)8 };

        PreStat preStat {
            .pr_type = PreOpenType::Dir,
            .u = {
                .pr_name_len = 1, // "/"
            }
        };

        memcpy(VM::memory() + args[1].get<uint32_t>(), &preStat, sizeof(preStat));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(fd_write)
    {
        uint32_t written = 0;

        IOVector* iov = (IOVector*)(VM::memory() + args[1].get<uint32_t>());

        for (uint32_t i = 0; i < args[2].get<uint32_t>(); i++)
            written += (uint32_t)write(args[0].get<uint32_t>(), (VM::memory() + iov[i].pointer), iov[i].length);

        memcpy(VM::memory() + args[3].get<uint32_t>(), &written, sizeof(written));

        return { written };
    }

    DEFINE_WASI_CALL(poll_oneoff)
    {
        uint32_t count = 0;

        // fprintf(stderr, "Warning: poll_oneoff not implemented\n");

        memcpy(VM::memory() + args[3].get<uint32_t>(), &count, sizeof(count));

        return { (uint32_t)0 };
    }

    DEFINE_WASI_CALL(proc_exit)
    {
        exit(args[0].get<uint32_t>());
        assert(false);
    }

    DEFINE_WASI_CALL(random_get)
    {
        fill_buffer_with_random_data(VM::memory() + args[0].get<uint32_t>(), args[1].get<uint32_t>());
        return { (uint32_t)0 };
    }

    template <typename... T>
    void verify_args(const std::vector<Value>& args)
    {
        // Thanks ChatGPT

        // Check if the number of elements matches the number of template arguments
        if (args.size() != sizeof...(T))
            throw Trap();

        // Helper lambda to verify types
        auto verify_type = [](const auto& arg, auto type_tag) {
            using ExpectedType = decltype(type_tag);
            return arg.template holds_alternative<ExpectedType>();
        };

        // Using a tuple to iterate through template arguments
        std::tuple<T...> types;

        // Check types for each element in the vector
        size_t index = 0;
        bool result = true;
        auto check_types = [&](auto&&... types) {
            (void)std::initializer_list<int> { ((result = result && verify_type(args[index], types)), ++index, 0)... };
        };

        std::apply(check_types, types);

        if (!result)
            throw Trap();
    }

    std::vector<Value> run_wasi_call(const std::string& name, const std::vector<Value>& args)
    {
#define WASI_CALL(x, ...)               \
    if (name == #x)                     \
    {                                   \
        verify_args<__VA_ARGS__>(args); \
        return wasi$##x(args);          \
    }

        ENUMERATE_WASI_CALL(WASI_CALL)

#undef WASI_CALL

        fprintf(stderr, "Error: Invalid WASI call: %s\n", name.c_str());
        throw Trap();
    }
}
