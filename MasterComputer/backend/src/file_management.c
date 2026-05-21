/*
|--------------------------------------------------------------------------
| AmseokBot C 文件管理模块
|--------------------------------------------------------------------------
| 提供文件下载读取、上传写入、删除、移动、复制等底层文件系统操作。
| Go API 负责 HTTP、登录鉴权和参数校验，C 层只执行已经授权的本机动作。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
|--------------------------------------------------------------------------
| 错误与路径工具
|--------------------------------------------------------------------------
| 统一检查绝对路径，避免空路径、相对路径和根目录破坏性操作。
|--------------------------------------------------------------------------
*/
static void set_error(char *error, size_t error_size, const char *message) {
    if (error_size == 0) return;
    snprintf(error, error_size, "%s", message);
}

static void set_errno_error(char *error, size_t error_size, const char *prefix) {
    if (error_size == 0) return;
    snprintf(error, error_size, "%s: %s", prefix, strerror(errno));
}

static bool require_absolute_path(const char *path, char *error, size_t error_size) {
    if (path == NULL || path[0] == '\0') {
        set_error(error, error_size, "path is empty");
        return false;
    }
    if (path[0] != '/') {
        set_error(error, error_size, "path must be absolute");
        return false;
    }
    if (strlen(path) >= PATH_MAX) {
        set_error(error, error_size, "path is too long");
        return false;
    }
    return true;
}

static bool require_not_root_path(const char *path, char *error, size_t error_size) {
    if (!require_absolute_path(path, error, error_size)) return false;
    if (strcmp(path, "/") == 0) {
        set_error(error, error_size, "refuse to operate on root directory");
        return false;
    }
    return true;
}

static const char *path_basename(const char *path) {
    const char *p = strrchr(path, '/');
    if (p == NULL) return path;
    if (*(p + 1) == '\0') return path;
    return p + 1;
}

static bool join_child_path(const char *parent, const char *name, char *out, size_t out_size, char *error, size_t error_size) {
    int n = snprintf(out, out_size, "%s/%s", parent, name);
    if (n < 0 || (size_t)n >= out_size) {
        set_error(error, error_size, "joined path is too long");
        return false;
    }
    return true;
}

/*
|--------------------------------------------------------------------------
| 文件信息模块
|--------------------------------------------------------------------------
| 读取文件/目录元信息，给 Go API 下载前判断类型和大小。
|--------------------------------------------------------------------------
*/
bool amseokbot_fs_stat(const char *path, amseokbot_fs_info_t *info, char *error, size_t error_size) {
    if (info == NULL) {
        set_error(error, error_size, "info is null");
        return false;
    }
    if (!require_absolute_path(path, error, error_size)) return false;

    struct stat st;
    if (lstat(path, &st) != 0) {
        set_errno_error(error, error_size, "stat failed");
        return false;
    }

    memset(info, 0, sizeof(*info));
    snprintf(info->path, sizeof(info->path), "%s", path);
    snprintf(info->name, sizeof(info->name), "%s", path_basename(path));
    info->is_dir = S_ISDIR(st.st_mode);
    info->size = (long long)st.st_size;
    info->mode = (unsigned int)(st.st_mode & 0777U);

    struct tm tmv;
    if (localtime_r(&st.st_mtime, &tmv) != NULL) {
        strftime(info->mod_time, sizeof(info->mod_time), "%Y-%m-%d %H:%M:%S", &tmv);
    }
    return true;
}

/*
|--------------------------------------------------------------------------
| 删除模块
|--------------------------------------------------------------------------
| 支持递归删除文件夹，权限完全由运行 C 核心的 Linux 用户决定。
|--------------------------------------------------------------------------
*/
static bool delete_recursive(const char *path, char *error, size_t error_size) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        set_errno_error(error, error_size, "stat before delete failed");
        return false;
    }

    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        if (unlink(path) != 0) {
            set_errno_error(error, error_size, "unlink failed");
            return false;
        }
        return true;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        set_errno_error(error, error_size, "open directory failed");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[PATH_MAX];
        if (!join_child_path(path, entry->d_name, child, sizeof(child), error, error_size)) {
            closedir(dir);
            return false;
        }
        if (!delete_recursive(child, error, error_size)) {
            closedir(dir);
            return false;
        }
    }
    closedir(dir);

    if (rmdir(path) != 0) {
        set_errno_error(error, error_size, "rmdir failed");
        return false;
    }
    return true;
}

bool amseokbot_fs_delete(const char *path, char *error, size_t error_size) {
    if (!require_not_root_path(path, error, error_size)) return false;
    return delete_recursive(path, error, error_size);
}

/*
|--------------------------------------------------------------------------
| 复制模块
|--------------------------------------------------------------------------
| 支持普通文件、目录和符号链接；目标已存在时拒绝覆盖，避免误伤。
|--------------------------------------------------------------------------
*/
static bool copy_file(const char *src, const char *dst, mode_t mode, char *error, size_t error_size) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        set_errno_error(error, error_size, "open source failed");
        return false;
    }

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (out_fd < 0) {
        close(in_fd);
        set_errno_error(error, error_size, "open destination failed");
        return false;
    }

    char buffer[1024 * 64];
    for (;;) {
        ssize_t n = read(in_fd, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) {
            close(in_fd);
            close(out_fd);
            set_errno_error(error, error_size, "read source failed");
            return false;
        }
        char *p = buffer;
        ssize_t left = n;
        while (left > 0) {
            ssize_t w = write(out_fd, p, (size_t)left);
            if (w < 0) {
                close(in_fd);
                close(out_fd);
                set_errno_error(error, error_size, "write destination failed");
                return false;
            }
            p += w;
            left -= w;
        }
    }

    if (close(in_fd) != 0) {
        close(out_fd);
        set_errno_error(error, error_size, "close source failed");
        return false;
    }
    if (close(out_fd) != 0) {
        set_errno_error(error, error_size, "close destination failed");
        return false;
    }
    return true;
}

static bool copy_symlink(const char *src, const char *dst, char *error, size_t error_size) {
    char target[PATH_MAX];
    ssize_t n = readlink(src, target, sizeof(target) - 1);
    if (n < 0) {
        set_errno_error(error, error_size, "readlink failed");
        return false;
    }
    target[n] = '\0';
    if (symlink(target, dst) != 0) {
        set_errno_error(error, error_size, "symlink failed");
        return false;
    }
    return true;
}

static bool copy_recursive(const char *src, const char *dst, char *error, size_t error_size) {
    struct stat st;
    if (lstat(src, &st) != 0) {
        set_errno_error(error, error_size, "stat source failed");
        return false;
    }

    if (S_ISREG(st.st_mode)) return copy_file(src, dst, st.st_mode, error, error_size);
    if (S_ISLNK(st.st_mode)) return copy_symlink(src, dst, error, error_size);

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & 0777) != 0) {
            set_errno_error(error, error_size, "mkdir destination failed");
            return false;
        }
        DIR *dir = opendir(src);
        if (dir == NULL) {
            set_errno_error(error, error_size, "open source directory failed");
            return false;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char src_child[PATH_MAX];
            char dst_child[PATH_MAX];
            if (!join_child_path(src, entry->d_name, src_child, sizeof(src_child), error, error_size) ||
                !join_child_path(dst, entry->d_name, dst_child, sizeof(dst_child), error, error_size)) {
                closedir(dir);
                return false;
            }
            if (!copy_recursive(src_child, dst_child, error, error_size)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
        return true;
    }

    set_error(error, error_size, "unsupported file type");
    return false;
}

bool amseokbot_fs_copy(const char *src, const char *dst, char *error, size_t error_size) {
    if (!require_not_root_path(src, error, error_size)) return false;
    if (!require_not_root_path(dst, error, error_size)) return false;
    if (access(dst, F_OK) == 0) {
        set_error(error, error_size, "destination already exists");
        return false;
    }
    return copy_recursive(src, dst, error, error_size);
}

/*
|--------------------------------------------------------------------------
| 移动模块
|--------------------------------------------------------------------------
| 同分区用 rename；跨分区时复制后删除，保持前端移动操作一致。
|--------------------------------------------------------------------------
*/
bool amseokbot_fs_move(const char *src, const char *dst, char *error, size_t error_size) {
    if (!require_not_root_path(src, error, error_size)) return false;
    if (!require_not_root_path(dst, error, error_size)) return false;
    if (access(dst, F_OK) == 0) {
        set_error(error, error_size, "destination already exists");
        return false;
    }
    if (rename(src, dst) == 0) return true;
    if (errno != EXDEV) {
        set_errno_error(error, error_size, "rename failed");
        return false;
    }
    if (!copy_recursive(src, dst, error, error_size)) return false;
    return delete_recursive(src, error, error_size);
}

/*
|--------------------------------------------------------------------------
| 上传写入模块
|--------------------------------------------------------------------------
| 从 stdin 接收 Go API 传入的数据，先写临时文件，完成后原子替换目标。
|--------------------------------------------------------------------------
*/
bool amseokbot_fs_write_stream(const char *path, bool overwrite, char *error, size_t error_size) {
    if (!require_not_root_path(path, error, error_size)) return false;
    if (!overwrite && access(path, F_OK) == 0) {
        set_error(error, error_size, "destination already exists");
        return false;
    }

    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        set_error(error, error_size, "temporary path is too long");
        return false;
    }

    int out_fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) {
        set_errno_error(error, error_size, "open upload target failed");
        return false;
    }

    char buffer[1024 * 64];
    for (;;) {
        ssize_t r = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (r == 0) break;
        if (r < 0) {
            close(out_fd);
            unlink(tmp);
            set_errno_error(error, error_size, "read upload stream failed");
            return false;
        }
        char *p = buffer;
        ssize_t left = r;
        while (left > 0) {
            ssize_t w = write(out_fd, p, (size_t)left);
            if (w < 0) {
                close(out_fd);
                unlink(tmp);
                set_errno_error(error, error_size, "write upload stream failed");
                return false;
            }
            p += w;
            left -= w;
        }
    }

    if (close(out_fd) != 0) {
        unlink(tmp);
        set_errno_error(error, error_size, "close upload target failed");
        return false;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        set_errno_error(error, error_size, "commit upload failed");
        return false;
    }
    return true;
}

/*
|--------------------------------------------------------------------------
| 下载读取模块
|--------------------------------------------------------------------------
| 把文件原始字节写到 stdout，由 Go API 直接转发给浏览器下载。
|--------------------------------------------------------------------------
*/
bool amseokbot_fs_read_stream(const char *path, char *error, size_t error_size) {
    if (!require_not_root_path(path, error, error_size)) return false;
    struct stat st;
    if (lstat(path, &st) != 0) {
        set_errno_error(error, error_size, "stat download target failed");
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        set_error(error, error_size, "download target is not a regular file");
        return false;
    }

    int in_fd = open(path, O_RDONLY);
    if (in_fd < 0) {
        set_errno_error(error, error_size, "open download target failed");
        return false;
    }

    char buffer[1024 * 64];
    for (;;) {
        ssize_t r = read(in_fd, buffer, sizeof(buffer));
        if (r == 0) break;
        if (r < 0) {
            close(in_fd);
            set_errno_error(error, error_size, "read download target failed");
            return false;
        }
        char *p = buffer;
        ssize_t left = r;
        while (left > 0) {
            ssize_t w = write(STDOUT_FILENO, p, (size_t)left);
            if (w < 0) {
                close(in_fd);
                set_errno_error(error, error_size, "write download stream failed");
                return false;
            }
            p += w;
            left -= w;
        }
    }
    if (close(in_fd) != 0) {
        set_errno_error(error, error_size, "close download target failed");
        return false;
    }
    return true;
}
