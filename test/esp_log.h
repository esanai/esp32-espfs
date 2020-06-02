#pragma once

#include <stdio.h>

#define ESP_LOGE(_tag, _fmt, ...) fprintf(stderr, "E: %s: " _fmt "\n", _tag, ##__VA_ARGS__)
#define ESP_LOGW(_tag, _fmt, ...) fprintf(stderr, "W: %s: " _fmt "\n", _tag, ##__VA_ARGS__)
#define ESP_LOGI(_tag, _fmt, ...) fprintf(stderr, "I: %s: " _fmt "\n", _tag, ##__VA_ARGS__)
#define ESP_LOGD(_tag, _fmt, ...) fprintf(stderr, "D: %s: " _fmt "\n", _tag, ##__VA_ARGS__)
#define ESP_LOGV(_tag, _fmt, ...) fprintf(stderr, "V: %s: " _fmt "\n", _tag, ##__VA_ARGS__)
