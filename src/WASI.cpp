#include <WASI.h>

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
}
