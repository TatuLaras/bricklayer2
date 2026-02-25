#ifndef _LOG
#define _LOG

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LOG_PREFIX
#define LOG_PREFIX ""
#endif

#ifdef LOG_DISABLE_ERROR
#define ERROR(...)
#else
#ifdef DEBUG
#define ERROR(message, ...)                                                    \
    fprintf(stderr, LOG_PREFIX "[%s() at %s:%u] ERROR: " message "\n",         \
            __func__, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define ERROR(message, ...)                                                    \
    fprintf(stderr, LOG_PREFIX "ERROR: " message "\n", ##__VA_ARGS__)
#endif
#endif

// Exit on `cond` and print an error `message`.
#define ERR(cond, message)                                                     \
    if (cond) {                                                                \
        ERROR(message ": %s", strerror(errno));                                \
        exit(EXIT_FAILURE);                                                    \
    }

// Return -1 on `cond` and print an error `message`.
#define ERR_RETURN(cond, message)                                              \
    if (cond) {                                                                \
        ERROR(message ": %s", strerror(errno));                                \
        return -1;                                                             \
    }

#ifdef LOG_DISABLE_WARNING
#define WARNING(...)
#else
#ifdef DEBUG
#define WARNING(message, ...)                                                  \
    fprintf(stderr, LOG_PREFIX "[%s() at %s:%u] WARNING: " message "\n",       \
            __func__, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define WARNING(message, ...)                                                  \
    fprintf(stderr, LOG_PREFIX "WARNING: " message "\n", ##__VA_ARGS__)
#endif
#endif

#ifdef LOG_DISABLE_INFO
#define INFO(...)
#else
#ifdef DEBUG
#define INFO(message, ...)                                                     \
    printf(LOG_PREFIX "[%s() at %s:%u] INFO: " message "\n", __func__,         \
           __FILE__, __LINE__, ##__VA_ARGS__)

#else
#define INFO(message, ...)                                                     \
    fprintf(stderr, LOG_PREFIX "INFO: " message "\n", ##__VA_ARGS__)
#endif
#endif

#ifdef LOG_DISABLE_HINT
#define HINT(...)
#else
#define HINT(message, ...)                                                     \
    printf(LOG_PREFIX "HINT: " message "\n", ##__VA_ARGS__)
#endif

#endif
