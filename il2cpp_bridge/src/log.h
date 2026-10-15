#pragma once

#if defined(__ANDROID__)
#include <android/log.h>
#define MODRIX_TAG "ModrixBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, MODRIX_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, MODRIX_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MODRIX_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define MODRIX_LOG(level, ...)                     \
    do {                                           \
        std::fprintf(stderr, "[ModrixBridge] " level " "); \
        std::fprintf(stderr, __VA_ARGS__);         \
        std::fputc('\n', stderr);                  \
    } while (0)
#define LOGI(...) MODRIX_LOG("I", __VA_ARGS__)
#define LOGW(...) MODRIX_LOG("W", __VA_ARGS__)
#define LOGE(...) MODRIX_LOG("E", __VA_ARGS__)
#endif
