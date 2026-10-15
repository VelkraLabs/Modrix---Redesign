#pragma once

#if defined(__ANDROID__)
#include <android/log.h>
#define TLMOD_TAG "TLModBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TLMOD_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TLMOD_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TLMOD_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define TLMOD_LOG(level, ...)                     \
    do {                                           \
        std::fprintf(stderr, "[TLModBridge] " level " "); \
        std::fprintf(stderr, __VA_ARGS__);         \
        std::fputc('\n', stderr);                  \
    } while (0)
#define LOGI(...) TLMOD_LOG("I", __VA_ARGS__)
#define LOGW(...) TLMOD_LOG("W", __VA_ARGS__)
#define LOGE(...) TLMOD_LOG("E", __VA_ARGS__)
#endif
