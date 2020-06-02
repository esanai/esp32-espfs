#pragma once

/*
Stupid cpio-like tool to make read-only 'filesystems' that live on the flash SPI chip of the module.
Can (will) use lzf compression (when I come around to it) to make shit quicker. Aligns names, files,
headers on 4-byte boundaries so the SPI abstraction hardware in the ESP8266 doesn't crap on itself
when trying to do a <4byte or unaligned read.
*/

/*
The idea 'borrows' from cpio: it's basically a concatenation of {header, filename, file} data.
Header, filename and file data is 32-bit aligned. The last file is indicated by data-less header
with the FLAG_LASTFILE flag set.
*/

#include <stdint.h>

#define ESPFS_MAGIC 0x32736645

typedef struct {
	uint32_t magic;
	uint8_t majorVersion;
	uint8_t minorVersion;
	uint16_t reserved;
	uint32_t numFiles;
} __attribute__((packed)) EspFsHeader;

typedef struct {
	uint32_t hash;
	uint32_t offset;
} __attribute__((packed)) EspFsHashTableEntry;

typedef struct {
	uint8_t flags;
	uint8_t compress;
	uint16_t pathLen;
	uint32_t fsSize;
	uint32_t actualSize;
} __attribute__((packed)) EspFsFileHeader;
