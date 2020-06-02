/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This is a read-only filesystem that uses a sorted hash table to locate
 * file and directory entries.  It uses a block of data that comes from the
 * mkespfsimage.py tool.  It was originally written to use with esphttpd
 * but it has been separated to be used for other uses. */ 

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#if !defined(ESPFS_TEST)
#include "esp_partition.h"
#include "esp_spi_flash.h"
#endif

#if defined(CONFIG_ESPFS_USE_HEATSHRINK)
#include "heatshrink_config_custom.h"
#include "heatshrink_decoder.h"
#endif

#include "espfs_priv.h"
#include "espfs.h"

const static char* TAG = "espfs";

EspFs* espFsInit(EspFsConfig* conf)
{
	const void *espFsAddr = conf->memAddr;
#if !defined(ESPFS_TEST)
	spi_flash_mmap_handle_t mmapHandle = 0;

	if (espFsAddr == NULL) {
		esp_partition_subtype_t subtype = conf->partLabel ?
				ESP_PARTITION_SUBTYPE_ANY : ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD;
		const esp_partition_t* partition = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA, subtype, conf->partLabel);
		if (!partition) {
			return NULL;
		}

		esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
				SPI_FLASH_MMAP_DATA, (const void **)&espFsAddr, &mmapHandle);
		if (err) {
			return NULL;
		}
	}
#endif 

	const EspFsHeader *espFsHeader = espFsAddr;
	if (espFsHeader->magic != ESPFS_MAGIC) {
		ESP_LOGE(TAG, "Invalid magic at %p", espFsAddr);
#if !defined(ESPFS_TEST)
		if (mmapHandle) {
			spi_flash_munmap(mmapHandle);
		}
#endif
		return NULL;
	}

	EspFs* espFs = calloc(1, sizeof(EspFs));
	if (espFs == NULL) {
		ESP_LOGE(TAG, "Unable to allocate EspFs data");
#if !defined(ESPFS_TEST)
		if (mmapHandle) {
			spi_flash_munmap(mmapHandle);
		}
#endif
		return NULL;
	}

#if !defined(ESPFS_TEST)
	espFs->mmapHandle = mmapHandle;
#endif
	espFs->header = espFsAddr;

	/* cache */
	espFs->numFiles = espFs->header->numFiles;

	if (conf->cacheHashTable) {
		uint32_t hashTableSize = sizeof(EspFsHashTableEntry) * espFs->numFiles;
		espFs->hashTable = malloc(hashTableSize);
		if (espFs->hashTable == NULL) {
			ESP_LOGW(TAG, "Unable to allocate hashTable.");
			espFs->hashTable = (void *)espFs->header + sizeof(EspFsHeader);
		} else {
			espFs->cacheHashTable = true;
			memcpy(espFs->hashTable, (void *)espFs->header + sizeof(EspFsHeader), hashTableSize);
		}
	} else {
		espFs->hashTable = (void *)espFs->header + sizeof(EspFsHeader);
	}

	return espFs;
}

void espFsDeinit(EspFs* espFs)
{
	if (espFs->cacheHashTable) {
		free(espFs->hashTable);
	}
#if !defined(ESPFS_TEST)
	if (espFs->mmapHandle) {
		spi_flash_munmap(espFs->mmapHandle);
	}
#endif
	free(espFs);
}

/* Returns flags of an open file. */
int espFsFlags(EspFsFile *file)
{
	assert(file != NULL);

	return file->header->flags;
}

static uint32_t hashPath(const char *s)
{
	uint32_t hash = 5381;
	uint8_t c;

	while ((c = (uint8_t)*s++)) {
		/* hash = hash * 33 + c */
		hash = ((hash << 5) + hash) + c;
	}

	return hash;
}

static void *espFsFindObject(EspFs *espFs, const char *path)
{
	assert(espFs != NULL);

	/* Strip initial slash. It would be an error to strip more than one slash,
	 * and has security implications in esphttpd with auth handlers. */
	if (*path == '/') {
		path++;
	};

	ESP_LOGD(TAG, "Looking for object '%s'", path);
	
	uint32_t pathHash = hashPath(path);
	int32_t first = 0;
	int32_t last = espFs->numFiles - 1;
	int32_t middle = last / 2;
	EspFsHashTableEntry *entry;

	while (first <= last) {
		entry = espFs->hashTable + middle;
		if (entry->hash == pathHash) {
			break;
		} else if (entry->hash < pathHash) {
			first = middle + 1;
		} else {
			last = middle - 1;
		}
		middle = first + (last - first) / 2;
	}
	if (first > last) {
		ESP_LOGI(TAG, "Hash not found.");
		return NULL;
	}

	ESP_LOGI(TAG, "Hash match at index %d.", middle);

	/* be optimistic and test the first match */
	EspFsFileHeader *espFsFileHeader = (void *)espFs->header + entry->offset;
	if (strcmp(path, (char *)espFsFileHeader + sizeof(EspFsFileHeader)) == 0) {
		return espFsFileHeader;
	}
	int32_t skip = middle;

	/* okay, now we have a hash collision, move entry to the first match */
	while (middle > 0) {
		if ((espFs->hashTable + 1)->hash != pathHash) {
			break;
		}
		entry--;
		middle--;
	}

	/* walk through canidates and look for a match */
	do {
		if (middle != skip) {
			espFsFileHeader = (void *)espFs->header + entry->offset;
			if (strcmp(path, (char *)espFsFileHeader + sizeof(EspFsFileHeader)) == 0) {
				return espFsFileHeader;
			}
		}
		entry++;
		middle++;

	} while (middle <= last && entry->hash == pathHash);

	return NULL;
}

/* Open a file and return a pointer a new EspFsFile object. */
EspFsFile *espFsOpen(EspFs *espFs, const char *path)
{
	EspFsFileHeader *header = espFsFindObject(espFs, path);
	if (header == NULL) {
		ESP_LOGD(TAG, "Unable to find file");
	}

	if (header->flags & ESPFS_FLAG_DIR) {
		ESP_LOGD(TAG, "Attempted to open directory as a file");
		return NULL;
	}

	EspFsFile *file = malloc(sizeof(EspFsFile));
	if (file == NULL) {
		ESP_LOGE(TAG, "Unable to allocate EspFsFile data");
		return NULL;
	}

	file->header = header;

	/* cache some frequently used things */
	file->flags = header->flags;
	file->compress = header->compress;
	file->pathLen = header->pathLen;
	file->fsSize = header->fsSize;
	file->actualSize = header->actualSize;

	file->fsStartPtr = file->fsPtr = (void *)header + sizeof(EspFsHeader) + file->pathLen;
	file->decompPos = 0;
	if (file->compress == ESPFS_COMPRESS_NONE) {
		file->decompData = NULL;
#if defined(CONFIG_ESPFS_USE_HEATSHRINK)
	} else if (file->compress == ESPFS_COMPRESS_HEATSHRINK) {
		uint8_t param = *((uint8_t *)(file->fsStartPtr));
		file->fsStartPtr++;
		file->fsPtr++;
		file->fsSize--;
		ESP_LOGD(TAG, "Heatshrink compressed file; decode params %02X", param);
		file->decompData = heatshrink_decoder_alloc(16, param >> 4, param & 0x0F);
#endif
	} else {
		ESP_LOGE(TAG, "Invalid compress type %d", file->compress);
		free(file);
		return NULL;
	}
	return file;
}

/* Return information about a file or directory. */
int espFsStat(EspFs* espFs, const char *path, EspFsStat *stat)
{
	char *path2 = NULL;
	size_t pathLen = strlen(path);

	if (path[pathLen - 1] == '/') {
		path2 = malloc(pathLen);
		if (path2 == NULL) {
			ESP_LOGE(TAG, "Unable to allocate data for path2");
			return false;
		}

		strncpy(path2, path, pathLen - 1);
		path2[pathLen - 1] = '\0';
	}
	EspFsFileHeader *header = espFsFindObject(espFs, path2 ? path2 : path);
	if (path2) {
		free(path2);
	}

	if (header == NULL) {
		return false;
	}

	if (stat == NULL) {
		return true;
	}
	
	stat->flags = header->flags;
	stat->size = header->actualSize;

	if (header->flags & ESPFS_FLAG_DIR) {
		stat->compress = 0;
	} else {
		stat->compress = header->compress;
	}

	return true;
}

/* Read len bytes from the given file into buf.
 * Returns the actual amount of bytes read. */
int espFsRead(EspFsFile *file, char *buf, int len)
{
	assert(file != NULL);
	assert(len >= 0);

	if (file->compress == ESPFS_COMPRESS_NONE) {
		int fsRemain = file->fsSize - (file->fsPtr - file->fsStartPtr);
		if (len > fsRemain) {
			 len = fsRemain;
		}
		ESP_LOGV(TAG, "Reading %d bytes, fsPtr=%p", len, file->fsPtr);
		memcpy(buf, file->fsPtr, len);
		file->decompPos += len;
		file->fsPtr += len;
		ESP_LOGV(TAG, "Finished reading %d bytes, fsPtr=%p", len, file->fsPtr);
		return len;
#if defined(CONFIG_ESPFS_USE_HEATSHRINK)
	} else if (file->compress == ESPFS_COMPRESS_HEATSHRINK) {
		int decoded = 0;
		size_t rlen;

		heatshrink_decoder *decoder = (heatshrink_decoder *)file->decompData;
		if (file->decompPos == file->actualSize) {
			return 0;
		}

		/* We must ensure that whole file is decompressed and written to output bufer.
		 * This means even when there is no input data (fsRemain==0) try to poll decoder until
		 * decompPos equals decompressed file length */
		while (decoded < len) {
			/* Feed data into the decoder */
			size_t fsRemain = file->fsSize - (file->fsPtr - file->fsStartPtr);
			if (fsRemain > 0) {
				HSD_sink_res res = heatshrink_decoder_sink(decoder, file->fsPtr, (fsRemain > 16) ? 16 : fsRemain, &rlen);
				if (res < 0) {
					return -1;
				}
				file->fsPtr += rlen;
			}

			/* Grab decompressed data and put into buf */
			HSD_poll_res res = heatshrink_decoder_poll(decoder, (uint8_t *)buf, len - decoded, &rlen);
			if (res < 0) {
				return -1;
			}
			file->decompPos += rlen;
			buf += rlen;
			decoded += rlen;

			ESP_LOGV(TAG, "fsRemain=%d rlen=%d decoded=%d decompPos=%d actualSize=%d", fsRemain, rlen, decoded, file->decompPos, file->actualSize);

			if (fsRemain == 0) {
				if (file->decompPos == file->actualSize) {
					ESP_LOGD(TAG, "Heatshrink finished");
					HSD_finish_res res = heatshrink_decoder_finish(decoder);
					if (res < 0) {
						return -1;
					}
				}
				return decoded;
			}
		}
		return len;
#endif
	}
	return 0;
}

/* Seek in the file. */
int espFsSeek(EspFsFile *file, long offset, int mode)
{
	assert(file != NULL);

	if (file->compress == ESPFS_COMPRESS_NONE) {
		if (mode == SEEK_SET) {
			if (offset < 0) {
				return -1;
			} else if (offset == 0) {
				file->fsPtr = file->fsStartPtr;
				file->decompPos = 0;
			} else {
				if (offset > file->fsSize) {
					offset = file->fsSize;
				}
				file->fsPtr = file->fsStartPtr + offset;
				file->decompPos = offset;
			}
		} else if (mode == SEEK_CUR) {
			if (offset < 0) {
				if (file->decompPos + offset < 0) {
					file->fsPtr = file->fsStartPtr;
					file->decompPos = 0;
				} else {
					file->fsPtr = file->fsPtr + offset;
					file->decompPos = file->decompPos + offset;
				}
			} else if (offset == 0) {
				return file->decompPos;
			} else {
				if (file->decompPos + offset > file->fsSize) {
					file->fsPtr = file->fsStartPtr + file->fsSize;
					file->decompPos = file->fsSize;
				} else {
					file->fsPtr = file->fsPtr + offset;
					file->decompPos = file->decompPos + offset;
				}
			}
		} else if (mode == SEEK_END) {
			if (offset < 0) {
				if ((int32_t)file->fsSize + offset < 0) {
					file->fsPtr = file->fsStartPtr;
					file->decompPos = 0;
				} else {
					file->fsPtr = file->fsStartPtr + file->fsSize + offset;
					file->decompPos = file->fsSize + offset;
				}
			} else if (offset == 0) {
				file->fsPtr = file->fsStartPtr + file->fsSize;
				file->decompPos = file->fsSize;
			} else {
				return -1;
			}
		} else {
			return -1;
		}
#if defined(CONFIG_ESPFS_USE_HEATSHRINK)
	} else if (file->compress == ESPFS_COMPRESS_HEATSHRINK) {
		if (mode == SEEK_SET) {
			if (offset < 0) {
				return -1;
			} else if (offset == 0) {
				file->fsPtr = file->fsStartPtr;
				file->decompPos = 0;
			} else {
				return -1;
			}
		} else if (mode == SEEK_CUR) {
			if (offset < 0) {
				return -1;
			} else if (offset == 0) {
				return file->decompPos;
			} else {
				return -1;
			}
		} else if (mode == SEEK_END) {
			if (offset < 0) {
				return -1;
			} else if (offset == 0) {
				file->fsPtr = file->fsStartPtr + file->fsSize;
				file->decompPos = file->fsSize;
			} else {
				return -1;
			}
		} else {
			return -1;
		}
#endif
	}

	return file->decompPos;
}

/* Provide access to the underlying memory of a file. */
int espFsAccess(EspFsFile *file, void **buf)
{
	if (file->compress != ESPFS_COMPRESS_NONE) {
		return -1;
	}

	*buf = file->fsStartPtr;
	return file->fsSize;
}

int espFsFilesize(EspFsFile *file)
{
	return file->actualSize;
}

/* Close the file. */
void espFsClose(EspFsFile *file)
{
	assert(file != NULL);

#if defined(CONFIG_ESPFS_USE_HEATSHRINK)
	if (file->compress == ESPFS_COMPRESS_HEATSHRINK) {
		heatshrink_decoder *decoder = (heatshrink_decoder *)file->decompData;
		heatshrink_decoder_free(decoder);
		ESP_LOGV(TAG, "Freed heatshrink decoder %p", decoder);
	}
#endif

	free(file);
	ESP_LOGV(TAG, "Freed file %p", file);
}
