#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPFS_FLAG_DIR (1 << 0)
#define ESPFS_FLAG_GZIP (1 << 1)
#define ESPFS_COMPRESS_NONE 0
#define ESPFS_COMPRESS_HEATSHRINK 1

struct EspFsConfig {
    const void* memAddr;
    char* partLabel;
    bool cacheHashTable;
};

struct EspFsStat {
    uint8_t flags;
    uint8_t compress;
    uint32_t size;
};

typedef struct EspFsConfig EspFsConfig;
typedef struct EspFs EspFs;
typedef struct EspFsFile EspFsFile;
typedef struct EspFsStat EspFsStat;

EspFs* espFsInit(EspFsConfig* conf);
void espFsDeinit(EspFs* espFs);
EspFsFile* espFsOpen(EspFs* file, const char *fileName);
int espFsStat(EspFs *espFs, const char *fileName, EspFsStat *stat);
int espFsFlags(EspFsFile *file);
int espFsRead(EspFsFile *file, char *buf, int len);
int espFsSeek(EspFsFile *file, long offset, int mode);
int espFsAccess(EspFsFile *file, void **buf);
int espFsFilesize(EspFsFile *file);
void espFsClose(EspFsFile *file);

#ifdef __cplusplus
}
#endif
