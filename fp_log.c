#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#if _WIN32
#include <Windows.h>
#include <share.h>
#else
#include <unistd.h>
#endif
#include "fp_api.h"
#include "libusb.h"
#include "fp_log.h"
#include "fp_time.h"

#define FP_LOG_FILE_LENGTH 256
#define FP_LOG_MODE_LENGTH 10

#define FP_LOG_ROTATION_FILE_SIZE (1 * 1024 * 1024)
#define FP_LOG_ROTATION_POOL_SIZE 5

struct fp_option {
    int log_level;
    char log_file[FP_LOG_FILE_LENGTH];
    char mode[FP_LOG_MODE_LENGTH];
    FILE* log_stream;
    unsigned int rotation_file_size;
    unsigned int rotation_pool_size;
};

static struct fp_option g_fp_option = {
    .log_level          = DEBUG_LEVEL_D,
    .log_stream         = NULL,
    .rotation_file_size = FP_LOG_ROTATION_FILE_SIZE,
    .rotation_pool_size = FP_LOG_ROTATION_POOL_SIZE,
};

struct fp_option* OBTAION_LOG_OPTION()
{
    return &g_fp_option;
}

int fp_log_set_level(int level)
{
    g_fp_option.log_level = level;
    return 0;
}

int fp_log_get_level(void)
{
    return g_fp_option.log_level;
}

int fp_log_open(const char* filename, const char* mode)
{
    FILE* f                  = NULL;
    struct fp_option* option = OBTAION_LOG_OPTION();

    if (filename == NULL)
        return -1;
#ifdef _WIN32
    f = _fsopen(filename, mode, _SH_DENYNO);
#else
    f = fopen(filename, mode);
#endif
    if (f == NULL)
        return -1;

    memset(option->log_file, 0, FP_LOG_FILE_LENGTH);
    memset(option->mode, 0, FP_LOG_MODE_LENGTH);
    memcpy(option->log_file, filename, strlen(filename));
    memcpy(option->mode, mode, strlen(mode));
    option->log_stream = f;

    return 0;
}

int fp_log_close()
{
    struct fp_option* option = OBTAION_LOG_OPTION();
    if (option->log_stream) {
        fclose(option->log_stream);
        option->log_stream = NULL;
        memset(option->log_file, 0, 1024);
    }
    return 0;
}

int fp_log_printf(const char* format, ...)
{
    struct fp_option* option = OBTAION_LOG_OPTION();
    FILE* f                  = stdout;

    if (option->log_stream == NULL && strlen(option->log_file) != 0) {
#ifdef _WIN32
        option->log_stream = _fsopen(option->log_file, option->mode, _SH_DENYNO);
#else
        option->log_stream = fopen(option->log_file, option->mode);
#endif
        if (option->log_stream == NULL) {
            // Failed to open the file
            fprintf(stderr, "Failed to open log file\n");
        }
    }

    struct stat sb;
    if (option->log_stream != NULL) {
        f = option->log_stream;
        fstat(fileno(option->log_stream), &sb);
    }

    // Get the current time
    char time[256] = {0};
    fp_timestamp(time, 256);

    // Format the log message into a buffer with variable arguments
    char buffer[2048] = {0};
    va_list args;
    va_start(args, format);

    int len = snprintf(buffer, sizeof(buffer), "[%s]", time);
    if (len > 0) {
        int l = vsnprintf(buffer + len, sizeof(buffer) - len - 1, format, args);
        if (l > 0)
            len += l;
    }

    va_end(args);

    if (len > 0) {
        // Write the buffer to the file and add a newline
        fwrite(buffer, 1, len, f);
        if (buffer[len] != '\n')
            fputc('\n', f);
        fflush(f);
    }
    // #ifndef _WIN32
    //     fsync(fileno(f));  // Force the file to be written to the disk
    // #endif
    return 0;
}

void
#ifdef _WIN32
    WINAPI
#endif
    log_callback(libusb_context* ctx, enum libusb_log_level level, const char* str)

{
    fp_log_printf("%s", str);
}

int __log_rotaion(void)
{
    int rotation             = 0;
    struct fp_option* option = OBTAION_LOG_OPTION();
    if (option->log_stream != NULL) {
        struct stat sb;
        fstat(fileno(option->log_stream), &sb);
        // printf(" sb.size = %lu \r\n", sb.st_size);
        /* check file's size threshold */
        if (sb.st_size > option->rotation_file_size) {
            /* 1. close log file */
            fclose(option->log_stream);
            option->log_stream = NULL;

            /* 2. remove filname Extension name, like *.txt or *.log ... */
            char rot[256] = {0};
            memcpy(rot, option->log_file, strlen(option->log_file));
            char* dot = strrchr(rot, '.');
            if (dot) {
                *dot = '\0';
            }
            if (dot == NULL || rot[0] == '\0') {
                // snprintf(new, 300, "%s-%d.log", option->log_file, (int)1);
                memcpy(rot, option->log_file, 256);
            }

            /* 3. Rotation */
            char rot1[300] = {0};
            char rot2[300] = {0};
            for (int i = option->rotation_pool_size; i > 0; i--) {
                snprintf(rot1, 300, "%s-%d.log", rot, i);

                int exist = stat(rot1, &sb);
                if (exist == 0) { /* log file exist */
                    if (i == option->rotation_pool_size) {
                        printf("remove: %s \r\n", rot1);
                        remove(rot1);
                    } else {
                        snprintf(rot2, 300, "%s-%d.log", rot, i + 1);
                        printf("rename: %s -> %s\r\n", rot1, rot2);
                        int r = rename(rot1, rot2);
                        if (r != 0) {
                            remove(rot1);
                            printf("rename: %s -> %s\r\n", rot1, rot2);
                        }
                    }
                }
            }

            int try = 2;
            do {
                /* backup */
                if (rename(option->log_file, rot1) == 0) {
                    /* Nothing to do */
                    break;
                } else {
                    fprintf(stderr, "backup log file fail!");
                    // remove(option->log_file);
                }
            } while (try-- > 0);

            rotation = 1;
        }
    }
    return rotation;
}