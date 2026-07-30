#pragma once
// Enough of Controllers::FS to exercise ActivityLogController on the host: one file kept in a
// vector, so a save followed by a load goes through the same bytes the watch would write.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define LFS_ERR_OK 0
#define LFS_O_RDONLY 1
#define LFS_O_WRONLY 2
#define LFS_O_CREAT 4
#define LFS_O_TRUNC 8

struct lfs_file_t {
  int flags = 0;
  size_t pos = 0;
};
struct lfs_dir {
  int unused = 0;
};

namespace Pinetime {
  namespace Controllers {
    class FS {
    public:
      std::vector<uint8_t> contents;
      bool exists = false;
      bool dirExists = false;

      int FileOpen(lfs_file_t* file, const char*, int flags) {
        if ((flags & LFS_O_RDONLY) && !exists) {
          return -1;
        }
        file->flags = flags;
        file->pos = 0;
        if (flags & LFS_O_TRUNC) {
          contents.clear();
          exists = true;
        }
        return LFS_ERR_OK;
      }

      int FileRead(lfs_file_t* file, uint8_t* out, uint32_t size) {
        const size_t available = contents.size() - std::min(contents.size(), file->pos);
        const size_t n = std::min<size_t>(size, available);
        std::memcpy(out, contents.data() + file->pos, n);
        file->pos += n;
        return static_cast<int>(n);
      }

      int FileWrite(lfs_file_t* file, const uint8_t* in, uint32_t size) {
        contents.insert(contents.end(), in, in + size);
        file->pos = contents.size();
        return static_cast<int>(size);
      }

      int FileClose(lfs_file_t*) {
        return LFS_ERR_OK;
      }

      int DirOpen(const char*, lfs_dir*) {
        return dirExists ? LFS_ERR_OK : -1;
      }

      int DirClose(lfs_dir*) {
        return LFS_ERR_OK;
      }

      int DirCreate(const char*) {
        dirExists = true;
        return LFS_ERR_OK;
      }
    };
  }
}
