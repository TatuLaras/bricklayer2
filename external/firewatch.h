//  TODO: Function that takes in c source filepath, compiles when changed and
//  calls callback with a dlopen handle.

#ifndef FIREWATCH_MAX_DIRECTORIES
#define FIREWATCH_MAX_DIRECTORIES 32
#endif

#ifndef _FIREWATCH
#define _FIREWATCH

/*
   (c) Tatu Laras 2025

   firewatch - A single-header library to simplify the implementation of
   hot-reload capablilities.

   This is a single-header library. Similar to other libraries of this kind, you
   need to define FIREWATCH_IMPLEMENTATION (followed by inclusion of this file)
   in exactly one source file.

   #define FIREWATCH_IMPLEMENTATION
   #include "firewatch.h"

   See README.md for usage instructions and examples.

   Depends on inotify and some standard library functions. Exact dependencies
   are listed as includes below this comment.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef FIREWATCH_NO_RELOAD
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

#define PATH_MAX 4096

typedef void (*FileRefreshFunction)(const char *file_path, uint64_t cookie);

typedef struct {
    char filepath[PATH_MAX + 1];
    // True if updates to this file should be pushed to the updated files stack
    // instead of calling the on_change_callback.
    int using_stack;
    size_t filename_offset;
    uint64_t cookie;
    FileRefreshFunction on_change_callback;
} FileInfo;

typedef struct {
    FileInfo *data;
    size_t data_allocated;
    size_t data_used;
} FileInfoVector;

// Registers a new file at `filepath` to be watched by firewatch.
//
// `filepath`: The file to watch. Must be a file, not a directory. The parent
// directory of the file must exist, the file itself doesn't need to.
// Relative paths are allowed.
//
// `cookie`: Any arbitrary integer that will be passed to the callback as an
// argument.
//
// `on_change_callback`: A function pointer for a function that takes two
// parameters, matching the first two parameters of this function.
//
// `load_instantly`: If 1, the `on_change_callback` will immediately be called
// from a separate thread when the file changes. If for some reason the file
// needs to be loaded from the current thread, put 0 here. In that case,
// `on_change_callback` will be called from the current thread when the
// firewatch_check function is called.
void firewatch_new_file(const char *filepath, uint64_t cookie,
                        FileRefreshFunction on_change_callback,
                        int load_instantly);

// Check for file change events and call callbacks if necessary (only applies to
// when `load_instantly` of firewatch_new_file is set to 0).
void firewatch_check(void);

// Restores all internal state to its initial state.
void firewatch_reset(void);

#endif // _FIREWATCH

#ifdef LSP_EDITOR
#define FIREWATCH_IMPLEMENTATION
#endif

#ifdef FIREWATCH_IMPLEMENTATION

#define _BUF_SIZE (1024 * (sizeof(struct inotify_event) + 16))

// --- Dynamic array for file info structs ---
FileInfoVector fileinfovec_init(void) {
    FileInfoVector vec = {
        .data = malloc(4 * sizeof(FileInfo)),
        .data_allocated = 4,
    };
    return vec;
}

size_t fileinfovec_append(FileInfoVector *vec, FileInfo data) {
    if (vec->data_used >= vec->data_allocated) {
        vec->data_allocated *= 2;
        vec->data = realloc(vec->data, vec->data_allocated * sizeof(FileInfo));
        if (!vec->data)
            abort();
    }
    vec->data[vec->data_used++] = data;
    return vec->data_used - 1;
}

FileInfo *fileinfovec_get(FileInfoVector *vec, size_t index) {
    if (index >= vec->data_used)
        return 0;
    return vec->data + index;
}

void fileinfovec_free(FileInfoVector *vec) {
    if (vec->data) {
        free(vec->data);
        vec->data = 0;
    }
}

// --- Firewatch implementation starts ---

#ifndef FIREWATCH_NO_RELOAD

static FileInfoVector fw_file_info_lists[FIREWATCH_MAX_DIRECTORIES];
static int fw_inotify_fd = -1;
static int pipe_fds[2] = {0};
static pthread_t fw_thread_id = 0;
static pthread_mutex_t fw_lock;
static FileInfoVector fw_needs_refresh_queue = {0};

// Last occurrence of character '/' in `string` plus one.
// Returns 0 if no slashes in `string`.
static inline int fw_basename_start_index(const char *string) {
    size_t last = 0;
    size_t i = 0;
    while (string[i]) {
        if (string[i] == '/')
            last = i + 1;
        i++;
    }

    return last;
}

// Will run in a thread and read inotify events, calling the callback if
// necessary.
static void *fw_watch_for_changes(void *_) {
    assert(fw_inotify_fd > 0);
    assert(pipe_fds[0] > 0);

    // Waiting on the inotify fd, as well as the other end of a pipe to allow
    // for cancellation in firewatch_deinit().
    struct pollfd poll_fds[] = {
        {
            .fd = fw_inotify_fd,
            .events = POLLIN,
        },
        {
            .fd = pipe_fds[0],
            .events = POLLIN,
        },
    };

    char buf[_BUF_SIZE] = {0};
    size_t size = 0;

    while (1) {
        int result =
            poll(poll_fds, ((sizeof poll_fds) / sizeof(*poll_fds)), -1);
        if (result == -1) {
            perror("FIREWATCH poll call failed");
            return 0;
        }

        if (result == 0)
            continue;
        if (poll_fds[1].revents != 0)
            return 0;
        if (poll_fds[0].revents == 0)
            continue;

        size = read(poll_fds[0].fd, buf, _BUF_SIZE);

        size_t i = 0;
        while (i < size) {
            struct inotify_event *event = (struct inotify_event *)buf + i;
            i += sizeof(struct inotify_event) + event->len;
            if (!event->mask || !event->len || event->wd <= 0)
                continue;
            if (!fw_file_info_lists[event->wd].data)
                continue;

            pthread_mutex_lock(&fw_lock);

            for (size_t j = 0; j < fw_file_info_lists[event->wd].data_used;
                 j++) {
                FileInfo *file_info =
                    fileinfovec_get(fw_file_info_lists + event->wd, j);

                if (strcmp(file_info->filepath + file_info->filename_offset,
                           event->name))
                    continue;

                // Two methods, stack and callback
                if (file_info->using_stack) {
                    fileinfovec_append(&fw_needs_refresh_queue, *file_info);
                } else {
                    (file_info->on_change_callback)(file_info->filepath,
                                                    file_info->cookie);
                }
            }

            pthread_mutex_unlock(&fw_lock);
        }
    }

    return 0;
    (void)_;
}

// Ensure all necassary resources have been initialized.
static inline void fw_ensure_init(void) {
    if (fw_inotify_fd <= 0)
        fw_inotify_fd = inotify_init();
    assert((fw_inotify_fd > 0));

    // Create watch thread and pipe for cancellation of that thread
    if (!fw_thread_id) {
        if (pipe(pipe_fds)) {
            perror("FIREWATCH pipe creation failed");
            abort();
        }

        if (pthread_create(&fw_thread_id, NULL, &fw_watch_for_changes, 0)) {
            perror("FIREWATCH thread creation failed");
            abort();
        }
    }

    if (!fw_needs_refresh_queue.data)
        fw_needs_refresh_queue = fileinfovec_init();
}
#endif // !FIREWATCH_NO_RELOAD

//  TODO: Be able to cancel watch
void firewatch_new_file(const char *filepath, uint64_t cookie,
                        FileRefreshFunction on_change_callback,
                        int load_instantly) {
#ifdef FIREWATCH_NO_RELOAD
    (*on_change_callback)(filepath, cookie);
#else
    fw_ensure_init();

    FileInfo file_info = {.on_change_callback = on_change_callback,
                          .using_stack = !load_instantly,
                          .cookie = cookie};

    strncpy(file_info.filepath, filepath, PATH_MAX);

    size_t filename_start = fw_basename_start_index(filepath);
    file_info.filename_offset = filename_start;

    char directory[PATH_MAX] = {0};
    if (filename_start == 0)
        directory[0] = '.';
    else
        memcpy(directory, filepath, filename_start);

    // Create watch
    int wd = inotify_add_watch(fw_inotify_fd, directory,
                               IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd <= 0) {
        fprintf(stderr,
                "FIREWATCH ERROR: could not begin watching changes on file %s, "
                "maybe "
                "the parent directory of the file does not exist?\n",
                filepath);
        return;
    }

    pthread_mutex_lock(&fw_lock);

    if (!fw_file_info_lists[wd].data)
        fw_file_info_lists[wd] = fileinfovec_init();
    fileinfovec_append(fw_file_info_lists + wd, file_info);

    pthread_mutex_unlock(&fw_lock);

    (*on_change_callback)(filepath, cookie);
#endif
}

void firewatch_check(void) {
#ifndef FIREWATCH_NO_RELOAD
    pthread_mutex_lock(&fw_lock);

    FileInfo *file_info = 0;

    while (fw_needs_refresh_queue.data_used > 0) {
        fw_needs_refresh_queue.data_used--;
        file_info =
            fw_needs_refresh_queue.data + fw_needs_refresh_queue.data_used;

        assert(file_info->on_change_callback);

        (file_info->on_change_callback)(file_info->filepath, file_info->cookie);
    }

    pthread_mutex_unlock(&fw_lock);
#endif
}

void firewatch_reset(void) {
#ifndef FIREWATCH_NO_RELOAD
    if (!pipe_fds[1])
        return;

    char buf = 0;
    write(pipe_fds[1], &buf, 1);
    pthread_join(fw_thread_id, 0);

    close(pipe_fds[0]);
    close(pipe_fds[1]);
#endif
}

#endif // FIREWATCH_IMPLEMENTATION
