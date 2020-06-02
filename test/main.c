#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "espfs.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s IMAGE PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fputs("open failed\n", stderr);
        return EXIT_FAILURE;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        fputs("fstat failed\n", stderr);
    }

    void *image = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (image == MAP_FAILED) {
        fputs("mmap failed\n", stderr);
        close(fd);
        return EXIT_FAILURE;
    }

    EspFsConfig config = {
        .memAddr = image,
        .cacheHashTable = true,
    };

    EspFs *espFs = espFsInit(&config);
    if (espFs == NULL) {
        fputs("espFsInit failed\n", stderr);
        munmap(image, sb.st_size);
        close(fd);
        return EXIT_FAILURE;
    
    }

    EspFsStat stat;
    if (espFsStat(espFs, argv[2], &stat)) {
        if (stat.flags & ESPFS_FLAG_DIR) {
            fprintf(stderr, "Object '%s' is a directory.\n", argv[2]);
        } else {
            fprintf(stderr, "Object '%s' is a file.\n", argv[2]);
            if (stat.compress == ESPFS_COMPRESS_HEATSHRINK) {
                fprintf(stderr, "File is compressed with heatsrhink.\n");
            } else if (stat.compress != ESPFS_COMPRESS_NONE) {
                fprintf(stderr, "File is compressed wth unknown.\n");
            }
            if (stat.flags & ESPFS_FLAG_GZIP) {
                fprintf(stderr, "File is gzip encapsulated.\n");
            }
            fprintf(stderr, "File is %d bytes.\n", stat.size);
            EspFsFile *f = espFsOpen(espFs, argv[2]);
            if (f == NULL) {
                fprintf(stderr, "Error opening file.\n", argv[2]);
            } else {
                char buf[16];
                int bytes;
                fputs("File contents:\n", stderr);
                while ((bytes = espFsRead(f, buf, sizeof(buf))) == sizeof(buf)) {
                    fwrite(buf, bytes, 1, stdout);
                    fflush(stdout);
                }
            }
        }
    } else {
        fprintf(stderr, "Object '%s' does not exist.\n", argv[2]);
    }

    espFsDeinit(espFs);
    munmap(image, sb.st_size);
    close(fd);
    return EXIT_SUCCESS;
}
