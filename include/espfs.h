#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ESPFS_INIT_RESULT_OK,
	ESPFS_INIT_RESULT_NO_IMAGE,
	ESPFS_INIT_RESULT_BAD_ALIGN,
} EspFsInitResult;

typedef struct EspFsFile EspFsFile;

EspFsInitResult espFsInit(void *flashAddress);
EspFsFile *espFsOpen(const char *fileName);
int espFsFlags(EspFsFile *fh);
int espFsRead(EspFsFile *fh, char *buff, int len);
int espFsSeek(EspFsFile *fh, long offset, int mode);
void espFsClose(EspFsFile *fh);

#ifdef __cplusplus
}
#endif
