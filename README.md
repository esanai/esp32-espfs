EspFs
=====

EspFs is a read-only filesystem component for [ESP-IDF](https://github.com/espressif/esp-idf) that uses a sorted hash table to locate file and directory entries. It works with a block of data that is generated from a directory tree, using the mkespfsimage&#46;py tool. It currently supports [heatshrink](https://github.com/atomicobject/heatshrink) It was originally written to use with Sprite_tm's esphttpd, but it has been separated to be used for other uses.

Getting started
---------------

To use this component, make a components directory in your project directory and within that directory run:

`git clone --recursive https://github.com/jkent/esp32-espfs espfs`

Also within your project directory, you should create an espfs directory.  This directory should contain all the files and directories you wish to have in the espfs image.  By default, the filesystem blob will be linked with your project as `espfs_image_bin`.

You can generate a filesystem using `tools/mkespfsiage.py`.  The tool takes two arguments, ROOT, the directory containing the filesystem image, and IMAGE, the output file for the image.  The script references a yaml file in the root of your project.  That yaml file specifies various processes over the files in your filesystem.  Example:

```yaml
process:
  '*.css':
  - uglifycss

  '*.js':
  - babel-convert
  - babel-minify

  '*.html':
  - html-minifier

  '*':
  - heatshrink

  'some/file':
  - gzip
```
You can add your own processes.  Look at the espfs.yaml within the component as an example.

There are two ways you can use EspFs.  There is a raw interface and there is a vfs interface.  The vfs interface is the recommended way to use EspFs.

Common initialization
---------------------

Example initialization:

```C
EspFsConfig espfs_conf = {
    .memAddr = espfs_image_bin,
    .cacheHashTable = true,
};
EspFs* fs = espFsInit(&espfs_conf);
```

```C
espFsDeinit(fs);
```

You can also mount a filesystem from a partition.  Instead of specifying memAddr, you'd specify a partLabel.

VFS interface
-------------

```C
esp_vfs_espfs_conf_t vfs_espfs_conf = {
    .base_path = "/espfs",
    .fs = fs,
    .max_files = 5,
};
esp_vfs_espfs_register(&vfs_espfs_conf);
```

You can then use either the system (open, read, close) or stream (fopen, fread, fclose) to access files.

Raw interface
-------------

```C
int espFsFlags(EspFsFile *file);
EspFsFile *espFsOpen(EspFs *espFs, const char *path);
int espFsStat(EspFs* espFs, const char *path, EspFsStat *stat);
int espFsRead(EspFsFile *file, char *buf, int len);
int espFsSeek(EspFsFile *file, long offset, int mode);
int espFsAccess(EspFsFile *file, void **buf);
int espFsFilesize(EspFsFile *file);
void espFsClose(EspFsFile *file);
```