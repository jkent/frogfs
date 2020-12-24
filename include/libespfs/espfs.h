#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct EspFsConfig {
    const void* memAddr;
    const char* partLabel;
};

enum EspFsStatType {
    ESPFS_TYPE_MISSING,
    ESPFS_TYPE_FILE,
    ESPFS_TYPE_DIR,
};

struct EspFsStat {
    enum EspFsStatType type;
    int32_t size;
    int8_t flags;
};

typedef struct EspFsConfig EspFsConfig;
typedef struct EspFs EspFs;
typedef struct EspFsFile EspFsFile;
typedef struct EspFsStat EspFsStat;

EspFs* espFsInit(EspFsConfig* conf);
void espFsDeinit(EspFs* fs);
EspFsFile* espFsOpen(EspFs* fs, const char *fileName);
int espFsStat(EspFs *fs, const char *fileName, EspFsStat *s);
int espFsFlags(EspFsFile *fh);
int espFsRead(EspFsFile *fh, char *buff, int len);
int espFsSeek(EspFsFile *fh, long offset, int mode);
bool espFsIsCompressed(EspFsFile *fh);
int espFsAccess(EspFsFile *fh, void **buf);
int espFsFilesize(EspFsFile *fh);
void espFsClose(EspFsFile *fh);

#ifdef __cplusplus
}
#endif
