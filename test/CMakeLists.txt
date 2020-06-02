cmake_minimum_required(VERSION 3.6)
project(espfs_test)
set(SOURCES "main.c"
            "../src/espfs.c"
            "../heatshrink/heatshrink_decoder.c")

include_directories("."
                    "../src"
                    "../include"
                    "../heatshrink")
add_executable(espfs_test ${SOURCES})
