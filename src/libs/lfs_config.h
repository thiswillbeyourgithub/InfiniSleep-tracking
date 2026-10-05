#pragma once

// littlefs keeps one lfs_t, with one read and one program cache, for every caller, so two tasks
// inside it at once corrupt whatever either is writing. The BLE host task writes the bond when
// the phone reconnects, and the system task flushes the logs on the very wake that the bond write
// asks for, so the two do meet. With this defined littlefs takes FS's lock around every call.
#define LFS_THREADSAFE

#include <libraries/log/nrf_log.h>

#ifndef LFS_TRACE
#ifdef LFS_YES_TRACE
#define LFS_TRACE_(fmt, ...) \
    NRF_LOG_DEBUG("[LFS] %s:%d:trace: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_TRACE(...) LFS_TRACE_(__VA_ARGS__, "")
#else
#define LFS_TRACE(...)
#endif
#endif

#ifndef LFS_DEBUG
#ifndef LFS_NO_DEBUG
#define LFS_DEBUG_(fmt, ...) \
    NRF_LOG_DEBUG("[LFS] %s:%d:debug: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_DEBUG(...) LFS_DEBUG_(__VA_ARGS__, "")
#else
#define LFS_DEBUG(...)
#endif
#endif

#ifndef LFS_WARN
#ifndef LFS_NO_WARN
#define LFS_WARN_(fmt, ...) \
    NRF_LOG_WARNING("[LFS] %s:%d:warn: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_WARN(...) LFS_WARN_(__VA_ARGS__, "")
#else
#define LFS_WARN(...)
#endif
#endif

#ifndef LFS_ERROR
#ifndef LFS_NO_ERROR
#define LFS_ERROR_(fmt, ...) \
    NRF_LOG_ERROR("[LFS] %s:%d:error: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS_ERROR(...) LFS_ERROR_(__VA_ARGS__, "")
#else
#define LFS_ERROR(...)
#endif
#endif

// This is required in order for the CRC implementation in littlefs/lfs_util.c to be compiled
#undef LFS_CONFIG

#undef LFS_UTIL_H
#include <littlefs/lfs_util.h>
