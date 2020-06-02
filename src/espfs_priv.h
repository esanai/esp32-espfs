#pragma once

#include <stdbool.h>
#include <stddef.h>

#if !defined(ESPFS_TEST)
#include "esp_spi_flash.h"
#endif

#include "espfs_format.h"

typedef struct EspFs {
#if !defined(ESPFS_TEST)
	spi_flash_mmap_handle_t mmapHandle;
#endif
	const EspFsHeader *header;
	EspFsHashTableEntry *hashTable;
	bool cacheHashTable;

	/* cached header data */
	uint32_t numFiles;
} EspFs;

typedef struct EspFsFile {
	const EspFsFileHeader *header;

	/* cached header data */
	uint8_t flags;
	uint8_t compress;
	uint16_t pathLen;
	uint32_t fsSize;
	uint32_t actualSize;

	int32_t decompPos;
	void *fsPtr;
	void *fsStartPtr;
	void *decompData;
} EspFsFile;
