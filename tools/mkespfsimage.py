#!/usr/bin/env python

from argparse import ArgumentParser
from collections import OrderedDict
from sortedcontainers import SortedList
from fnmatch import fnmatch
import gzip
import heatshrink2
import os
from struct import Struct
import subprocess
import sys
import yaml

script_dir = os.path.dirname(os.path.realpath(__file__))

# magic, version_major, version_minor, reserved, num_files
fs_header_struct = Struct('<IBBHI')
ESPFS_MAGIC = 0x32736645 # Efs2
VERSION_MAJOR = 1
VERSION_MINOR = 0

# hash, offset
hashtable_strcut = Struct('<II')

# flags, compression, filename_length, fs_size, actual_size
file_header_struct = Struct('<BBHII')
FLAG_DIR = 1 << 0
FLAG_GZIP_ENCAP = 1 << 1
COMPRESS_NONE = 0
COMPRESS_HEATSHRINK = 1

def filename_hasher(s):
    hash = 5381
    for c in s.encode('utf8'):
        hash = ((hash << 5) + hash + c) & 0xFFFFFFFF
    return hash

def make_fs_header(num_files):
    return fs_header_struct.pack(ESPFS_MAGIC, VERSION_MAJOR, VERSION_MINOR, 0, num_files)

def make_hashlist(filenames):
    hashlist = SortedList()
    for i, filename in enumerate(filenames):
        hash = filename_hasher(filename)
        hashlist.add((hash, filename))
    return hashlist

def make_dir_entry(filename):
    filename = filename.replace('\\', '/')
    print(f'{filename}: directory')
    filename = filename.encode('utf8') + b'\0'

    file_header = file_header_struct.pack(FLAG_DIR, COMPRESS_NONE, len(filename), 0, 0)
    return file_header + filename

def make_file_entry(config, filepath, filename, actions):
    with open(filepath, 'rb') as f:
        initial_data = f.read()

    processed_data = initial_data

    for action in actions:
        if action in ['gzip', 'heatshrink']:
            pass
        elif action in config['tools']:
            tool = config['tools'][action]
            command = tool['command']
            p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
            processed_data = p.communicate(input=processed_data)[0]
        else:
            print(f'Unknown action: {action}', file=sys.stderr)
            sys.exit(1)

    flags = 0
    if 'gzip' in actions:
        flags |= FLAG_GZIP_ENCAP
        tool = config['tools']['gzip']
        level = min(max(tool.get('level', 9), 0), 9)
        processed_data = gzip.compress(processed_data, level)

    if 'heatshrink' in actions:
        compression = COMPRESS_HEATSHRINK
        tool = config['tools']['heatshrink']
        level = min(max(tool.get('level', 9), 0), 9) // 2
        window_sizes, lookahead_sizes = [5, 6, 8, 11, 13],  [3, 3, 4, 4, 4]
        window_sz2, lookahead_sz2 = window_sizes[level], lookahead_sizes[level]
        header = bytes([window_sz2 << 4 | lookahead_sz2])
        compressed_data = header + heatshrink2.compress(processed_data, window_sz2=window_sz2, lookahead_sz2=lookahead_sz2)
    else:
        compression = COMPRESS_NONE
        compressed_data = processed_data

    compressed_len = len(compressed_data)
    processed_len = len(processed_data)

    if compressed_len >= processed_len:
        compression = COMPRESS_NONE
        compressed_data = processed_data
        compressed_len = processed_len

    initial_len, processed_len, compressed_len = len(initial_data), len(processed_data), len(compressed_data)

    if initial_len < 1024:
        initial = f'{initial_len} B'
        compressed = f'{compressed_len} B'
    elif initial_len < 1024 * 1024:
        initial = f'{initial_len / 1024:.1f} KiB'
        compressed = f'{compressed_len / 1024:.1f} KiB'
    elif initial_len < 1024 * 1024 * 1024:
        initial = f'{initial_len / 1024 / 1024:.1f} MiB'
        compressed = f'{compressed_len / 1024 / 1024:.1f} MiB'

    percent = 100.0
    if initial_len > 0:
        percent = compressed_len / initial_len * 100

    filename = filename.replace('\\', '/')
    print(f'{filename}: {initial} -> {compressed} ({percent:.1f}%)')
    filename = filename.encode('utf8') + b'\0'
    compressed_data = compressed_data.ljust((compressed_len + 3) // 4 * 4, b'\0')

    file_header = file_header_struct.pack(flags, compression, len(filename), compressed_len, processed_len)
    return file_header + filename + compressed_data

def main():
    parser = ArgumentParser()
    parser.add_argument('ROOT')
    parser.add_argument('IMAGE')
    args = parser.parse_args()

    with open(os.path.join(script_dir, '..', 'espfs.yaml')) as f:
        config = yaml.load(f.read(), Loader=yaml.SafeLoader)

    user_config = None
    user_config_file = os.path.join(os.getenv('PROJECT_DIR', '.'), 'espfs.yaml')
    if os.path.exists(user_config_file):
        with open(user_config_file) as f:
            user_config = yaml.load(f.read(), Loader=yaml.SafeLoader)

    if user_config:
        for k, v in user_config.items():
            if k == 'tools':
                if 'tools' not in config:
                    config['tools'] = {}
                for k2, v2 in v.items():
                    config['tools'][k2] = v2
            elif k == 'process':
                if 'process' not in config:
                    config['process'] = {}
                for k2, v2 in v.items():
                    config['process'][k2] = v2
            elif k == 'skip':
                if 'tools' not in config:
                    config['tools'] = {}
                config['skip'] = v

    file_ops = OrderedDict()
    for subdir, _, files in os.walk(args.ROOT):
        dirname = os.path.relpath(subdir, args.ROOT)
        if dirname != '.':
            file_ops[dirname] = []
        for file in files:
            filename = os.path.relpath(os.path.join(subdir, file), args.ROOT)
            if filename not in file_ops:
                file_ops[filename] = []
            if 'process' in config:
                for pattern, actions in config['process'].items():
                    if fnmatch(filename, pattern):
                        file_ops[filename].extend(actions)

    if 'skip' in config:
        for pattern in config['skip']:
            for filename in file_ops.copy().keys():
                if fnmatch(filename, pattern):
                    file_ops[filename] = []

    all_tools = set()
    for filename, tools in file_ops.items():
        all_tools.update(tools)

    for tool in all_tools:
        if tool in ['gzip', 'heatshrink']:
            continue
        if 'npm' in config['tools'][tool]:
            npm = config['tools'][tool]['npm']
            npms = npm if type(npm) == list else [npm]
            for npm in npms:
                if not os.path.exists(os.path.join('node_modules', npm)):
                    subprocess.check_call(f'npm install {npm}', shell=True)

        elif 'setup' in config[tools][tool]:
            setup = config['tools'][tool]['setup']
            setups = setup if type(setup) == list else [setup]
            for setup in setups:
                subprocess.check_call(setup, shell=True)

    fs_header = make_fs_header(len(file_ops))
    fs_header_size = len(fs_header)

    hashlist = make_hashlist(file_ops.keys())
    hashtable_size = hashtable_strcut.size * len(hashlist)
    hashtable_data = b''
    
    file_data = b''
    for hash, filename in hashlist:
        actions = file_ops[filename]
        offset = fs_header_size + hashtable_size + len(file_data)
        hashtable_data += hashtable_strcut.pack(hash, offset)
        path = os.path.join(args.ROOT, filename)
        if os.path.isdir(path):
            file_data += make_dir_entry(filename)
        else:
            file_data += make_file_entry(config, path, filename, file_ops[filename])

    with open(args.IMAGE, 'wb') as f:
        f.write(fs_header + hashtable_data + file_data)

if __name__ == '__main__':
    main()