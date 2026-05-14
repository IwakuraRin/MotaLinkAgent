// 作用：C 语言版 OmniRoam HostPC 后端，负责静态前端托管、登录会话、设置保存、串口枚举、文件浏览和主 WebSocket 通道。

#include "hostpc_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <pty.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ==================== 常量配置 ====================
// 作用：集中定义 HTTP 缓冲区、会话时长、默认账号和 WebSocket 协议常量。
// ==================================================
#define HOSTPC_HEADER_LIMIT 32768
#define HOSTPC_BODY_LIMIT (2 * 1024 * 1024)
#define HOSTPC_SESSION_SECONDS (7 * 24 * 60 * 60)
#define HOSTPC_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// ==================== 运行状态 ====================
// 作用：保存当前服务配置、账号信息和内存会话列表。
// ==================================================
typedef struct session_node {
    char token[65];
    char username[128];
    time_t expires_at;
    struct session_node *next;
} session_node_t;

typedef struct {
    char username[128];
    char salt_hex[65];
    char password_hash_hex[65];
    int must_change_password;
} user_record_t;

static hostpc_config_t g_config;
static user_record_t g_user;
static session_node_t *g_sessions = NULL;
static pthread_mutex_t g_session_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_user_mu = PTHREAD_MUTEX_INITIALIZER;

// ==================== 字符串工具 ====================
// 作用：提供安全复制、大小写比较、路径检查和 JSON 转义等通用能力。
// ==================================================
static void copy_text(char *dst, size_t size, const char *src) {
    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, size, "%s", src);
}

static bool starts_with(const char *value, const char *prefix) {
    return value != NULL && prefix != NULL && strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool equals_ignore_case(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void trim_in_place(char *text) {
    if (text == NULL) {
        return;
    }
    char *start = text;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static bool contains_parent_path(const char *path) {
    return strstr(path, "../") != NULL || strstr(path, "/..") != NULL || strcmp(path, "..") == 0;
}

static void hex_encode(const unsigned char *bytes, size_t len, char *out, size_t out_size) {
    static const char table[] = "0change-me-on-first-login789abcdef";
    if (out_size < len * 2 + 1) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = table[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = table[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static char *json_escape_alloc(const char *text) {
    if (text == NULL) {
        return strdup("");
    }
    size_t extra = 1;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        extra += (c < 32 || c == '"' || c == '\\') ? 6 : 1;
    }
    char *out = calloc(extra, 1);
    if (out == NULL) {
        return NULL;
    }
    char *w = out;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            *w++ = '\\';
            *w++ = (char)c;
        } else if (c == '\n') {
            *w++ = '\\';
            *w++ = 'n';
        } else if (c == '\r') {
            *w++ = '\\';
            *w++ = 'r';
        } else if (c == '\t') {
            *w++ = '\\';
            *w++ = 't';
        } else if (c < 32) {
            snprintf(w, 7, "\\u%04x", c);
            w += 6;
        } else {
            *w++ = (char)c;
        }
    }
    *w = '\0';
    return out;
}

static void url_decode(char *text) {
    char *r = text;
    char *w = text;
    while (*r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], '\0'};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

// ==================== HTTP 响应工具 ====================
// 作用：统一发送 JSON、文本、错误、Cookie 和静态文件响应。
// ==================================================
static const char *status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "OK";
    }
}

static void send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void send_response_raw(int fd, int status, const char *content_type, const char *body, size_t body_len, const char *extra_headers) {
    char header[4096];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status,
        status_text(status),
        content_type == NULL ? "text/plain; charset=utf-8" : content_type,
        body_len,
        extra_headers == NULL ? "" : extra_headers);
    if (n > 0) {
        send_all(fd, header, (size_t)n);
    }
    if (body != NULL && body_len > 0) {
        send_all(fd, body, body_len);
    }
}

static void send_text(int fd, int status, const char *body) {
    send_response_raw(fd, status, "text/plain; charset=utf-8", body, strlen(body), NULL);
}

static void send_json(int fd, int status, const char *body) {
    send_response_raw(fd, status, "application/json; charset=utf-8", body, strlen(body), NULL);
}

static void send_json_error(int fd, int status, const char *error) {
    char body[512];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error);
    send_json(fd, status, body);
}

static const char *mime_type_for_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (equals_ignore_case(ext, ".html")) return "text/html; charset=utf-8";
    if (equals_ignore_case(ext, ".js")) return "application/javascript; charset=utf-8";
    if (equals_ignore_case(ext, ".css")) return "text/css; charset=utf-8";
    if (equals_ignore_case(ext, ".json")) return "application/json; charset=utf-8";
    if (equals_ignore_case(ext, ".svg")) return "image/svg+xml";
    if (equals_ignore_case(ext, ".png")) return "image/png";
    if (equals_ignore_case(ext, ".jpg") || equals_ignore_case(ext, ".jpeg")) return "image/jpeg";
    if (equals_ignore_case(ext, ".ico")) return "image/x-icon";
    if (equals_ignore_case(ext, ".woff2")) return "font/woff2";
    return "application/octet-stream";
}

// ==================== HTTP 解析 ====================
// 作用：从 socket 中读取 HTTP 请求头、请求体，并解析方法、路径和查询参数。
// ==================================================
static const char *header_value(const http_request_t *req, const char *name) {
    static char value[4096];
    value[0] = '\0';
    size_t name_len = strlen(name);
    const char *p = req->headers;
    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > name_len && p[name_len] == ':' && strncasecmp(p, name, name_len) == 0) {
            size_t n = line_len - name_len - 1;
            if (n >= sizeof(value)) {
                n = sizeof(value) - 1;
            }
            memcpy(value, p + name_len + 1, n);
            value[n] = '\0';
            trim_in_place(value);
            return value;
        }
        if (line_end == NULL) {
            break;
        }
        p = line_end + 2;
    }
    return "";
}

static bool read_http_request(int fd, http_request_t *req) {
    memset(req, 0, sizeof(*req));
    req->client_fd = fd;

    char *buf = calloc(HOSTPC_HEADER_LIMIT + 1, 1);
    if (buf == NULL) {
        return false;
    }
    size_t used = 0;
    char *header_end = NULL;
    while (used < HOSTPC_HEADER_LIMIT) {
        ssize_t n = recv(fd, buf + used, HOSTPC_HEADER_LIMIT - used, 0);
        if (n <= 0) {
            free(buf);
            return false;
        }
        used += (size_t)n;
        buf[used] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end != NULL) {
            break;
        }
    }
    if (header_end == NULL) {
        free(buf);
        return false;
    }

    char *line_end = strstr(buf, "\r\n");
    if (line_end == NULL) {
        free(buf);
        return false;
    }
    *line_end = '\0';
    char url[3072] = {0};
    if (sscanf(buf, "%15s %3071s", req->method, url) != 2) {
        free(buf);
        return false;
    }
    char *question = strchr(url, '?');
    if (question != NULL) {
        *question = '\0';
        copy_text(req->query, sizeof(req->query), question + 1);
    }
    copy_text(req->path, sizeof(req->path), url);
    url_decode(req->path);

    char *headers_start = line_end + 2;
    size_t headers_len = (size_t)(header_end - headers_start);
    if (headers_len >= sizeof(req->headers)) {
        headers_len = sizeof(req->headers) - 1;
    }
    memcpy(req->headers, headers_start, headers_len);
    req->headers[headers_len] = '\0';

    size_t header_bytes = (size_t)(header_end + 4 - buf);
    size_t body_already = used - header_bytes;
    long content_length = atol(header_value(req, "Content-Length"));
    if (content_length < 0 || content_length > HOSTPC_BODY_LIMIT) {
        free(buf);
        return false;
    }
    req->body_len = (size_t)content_length;
    if (req->body_len > 0) {
        req->body = calloc(req->body_len + 1, 1);
        if (req->body == NULL) {
            free(buf);
            return false;
        }
        size_t copy_len = body_already > req->body_len ? req->body_len : body_already;
        memcpy(req->body, buf + header_bytes, copy_len);
        size_t got = copy_len;
        while (got < req->body_len) {
            ssize_t n = recv(fd, req->body + got, req->body_len - got, 0);
            if (n <= 0) {
                free(req->body);
                req->body = NULL;
                free(buf);
                return false;
            }
            got += (size_t)n;
        }
        req->body[req->body_len] = '\0';
    }
    free(buf);
    return true;
}

static void free_http_request(http_request_t *req) {
    free(req->body);
    req->body = NULL;
}

// ==================== JSON 输入解析 ====================
// 作用：从前端 JSON 请求中提取简单字符串字段，避免页面逻辑直接依赖服务端内部结构。
// ==================================================
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    if (out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (json == NULL || key == NULL) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    char *w = out;
    size_t left = out_size - 1;
    while (*p && *p != '"' && left > 0) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
            if (*p == 'n') {
                *w++ = '\n';
            } else if (*p == 'r') {
                *w++ = '\r';
            } else if (*p == 't') {
                *w++ = '\t';
            } else {
                *w++ = *p;
            }
        } else {
            *w++ = *p;
        }
        p++;
        left--;
    }
    *w = '\0';
    return true;
}

// ==================== 账号存储 ====================
// 作用：用 C 后端自己的轻量账号文件保存用户名、盐值、密码哈希和是否必须改密。
// ==================================================
static void hash_password(const char *salt_hex, const char *password, char out_hex[65]) {
    SHA256_CTX ctx;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, salt_hex, strlen(salt_hex));
    SHA256_Update(&ctx, ":", 1);
    SHA256_Update(&ctx, password, strlen(password));
    SHA256_Final(digest, &ctx);
    hex_encode(digest, sizeof(digest), out_hex, 65);
}

static bool write_user_file(void) {
    FILE *fp = fopen(g_config.user_path, "w");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "username=%s\nsalt=%s\nhash=%s\nmust_change_password=%d\n",
        g_user.username,
        g_user.salt_hex,
        g_user.password_hash_hex,
        g_user.must_change_password);
    fclose(fp);
    chmod(g_config.user_path, 0600);
    return true;
}

static bool load_user_file(void) {
    FILE *fp = fopen(g_config.user_path, "r");
    if (fp == NULL) {
        const char *default_user = getenv("HOSTPC_USER");
        const char *default_password = getenv("HOSTPC_PASSWORD");
        if (default_user == NULL || default_user[0] == '\0') {
            default_user = "user";
        }
        if (default_password == NULL || default_password[0] == '\0') {
            default_password = "change-me-on-first-login";
        }
        unsigned char salt[16];
        if (RAND_bytes(salt, sizeof(salt)) != 1) {
            return false;
        }
        copy_text(g_user.username, sizeof(g_user.username), default_user);
        hex_encode(salt, sizeof(salt), g_user.salt_hex, sizeof(g_user.salt_hex));
        hash_password(g_user.salt_hex, default_password, g_user.password_hash_hex);
        g_user.must_change_password = 1;
        return write_user_file();
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        trim_in_place(line);
        if (starts_with(line, "username=")) {
            copy_text(g_user.username, sizeof(g_user.username), line + 9);
        } else if (starts_with(line, "salt=")) {
            copy_text(g_user.salt_hex, sizeof(g_user.salt_hex), line + 5);
        } else if (starts_with(line, "hash=")) {
            copy_text(g_user.password_hash_hex, sizeof(g_user.password_hash_hex), line + 5);
        } else if (starts_with(line, "must_change_password=")) {
            g_user.must_change_password = atoi(line + 21);
        }
    }
    fclose(fp);
    return g_user.username[0] != '\0' && g_user.salt_hex[0] != '\0' && g_user.password_hash_hex[0] != '\0';
}

static bool verify_password(const char *username, const char *password) {
    char hash_hex[65];
    pthread_mutex_lock(&g_user_mu);
    bool same_user = strcmp(username, g_user.username) == 0;
    hash_password(g_user.salt_hex, password, hash_hex);
    bool ok = same_user && strcmp(hash_hex, g_user.password_hash_hex) == 0;
    pthread_mutex_unlock(&g_user_mu);
    return ok;
}

static bool update_password(const char *password) {
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        return false;
    }
    pthread_mutex_lock(&g_user_mu);
    hex_encode(salt, sizeof(salt), g_user.salt_hex, sizeof(g_user.salt_hex));
    hash_password(g_user.salt_hex, password, g_user.password_hash_hex);
    g_user.must_change_password = 0;
    bool ok = write_user_file();
    pthread_mutex_unlock(&g_user_mu);
    return ok;
}

// ==================== 会话管理 ====================
// 作用：签发、验证和清理 HostSession Cookie，保护需要登录的 API。
// ==================================================
static void cleanup_sessions_locked(void) {
    time_t now = time(NULL);
    session_node_t **pp = &g_sessions;
    while (*pp != NULL) {
        session_node_t *cur = *pp;
        if (cur->expires_at <= now) {
            *pp = cur->next;
            free(cur);
        } else {
            pp = &cur->next;
        }
    }
}

static bool issue_session(const char *username, char token_out[65]) {
    unsigned char raw[32];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        return false;
    }
    hex_encode(raw, sizeof(raw), token_out, 65);
    session_node_t *node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return false;
    }
    copy_text(node->token, sizeof(node->token), token_out);
    copy_text(node->username, sizeof(node->username), username);
    node->expires_at = time(NULL) + HOSTPC_SESSION_SECONDS;

    pthread_mutex_lock(&g_session_mu);
    cleanup_sessions_locked();
    node->next = g_sessions;
    g_sessions = node;
    pthread_mutex_unlock(&g_session_mu);
    return true;
}

static bool extract_session_cookie(const http_request_t *req, char token[65]) {
    token[0] = '\0';
    const char *cookie = header_value(req, "Cookie");
    const char *p = strstr(cookie, "HostSession=");
    if (p == NULL) {
        return false;
    }
    p += strlen("HostSession=");
    size_t n = 0;
    while (isxdigit((unsigned char)p[n]) && n < 64) {
        token[n] = p[n];
        n++;
    }
    token[n] = '\0';
    return n == 64;
}

static bool request_is_authenticated(const http_request_t *req, char username[128]) {
    char token[65];
    if (!extract_session_cookie(req, token)) {
        return false;
    }
    bool ok = false;
    pthread_mutex_lock(&g_session_mu);
    cleanup_sessions_locked();
    for (session_node_t *cur = g_sessions; cur != NULL; cur = cur->next) {
        if (strcmp(cur->token, token) == 0) {
            copy_text(username, 128, cur->username);
            cur->expires_at = time(NULL) + HOSTPC_SESSION_SECONDS;
            ok = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_session_mu);
    return ok;
}

static void clear_session(const http_request_t *req) {
    char token[65];
    if (!extract_session_cookie(req, token)) {
        return;
    }
    pthread_mutex_lock(&g_session_mu);
    session_node_t **pp = &g_sessions;
    while (*pp != NULL) {
        session_node_t *cur = *pp;
        if (strcmp(cur->token, token) == 0) {
            *pp = cur->next;
            free(cur);
            break;
        }
        pp = &cur->next;
    }
    pthread_mutex_unlock(&g_session_mu);
}

static bool require_auth_or_401(const http_request_t *req, char username[128]) {
    if (request_is_authenticated(req, username)) {
        return true;
    }
    send_json_error(req->client_fd, 401, "unauthorized");
    return false;
}

// ==================== 鉴权 API ====================
// 作用：处理登录、登出、当前用户和修改密码接口，保持前端调用路径不变。
// ==================================================
static void handle_auth_login(const http_request_t *req) {
    if (strcmp(req->method, "POST") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    char username[128];
    char password[256];
    if (!json_get_string(req->body, "username", username, sizeof(username)) ||
        !json_get_string(req->body, "password", password, sizeof(password))) {
        send_json_error(req->client_fd, 400, "missing credentials");
        return;
    }
    trim_in_place(username);
    if (!verify_password(username, password)) {
        send_json_error(req->client_fd, 401, "invalid username or password");
        return;
    }
    char token[65];
    if (!issue_session(username, token)) {
        send_json_error(req->client_fd, 500, "server");
        return;
    }
    char headers[256];
    snprintf(headers, sizeof(headers), "Set-Cookie: HostSession=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax\r\n",
        token,
        HOSTPC_SESSION_SECONDS);
    char body[512];
    pthread_mutex_lock(&g_user_mu);
    snprintf(body, sizeof(body), "{\"ok\":true,\"username\":\"%s\",\"must_change_password\":%s}",
        g_user.username,
        g_user.must_change_password ? "true" : "false");
    pthread_mutex_unlock(&g_user_mu);
    send_response_raw(req->client_fd, 200, "application/json; charset=utf-8", body, strlen(body), headers);
}

static void handle_auth_logout(const http_request_t *req) {
    if (strcmp(req->method, "POST") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    clear_session(req);
    send_response_raw(req->client_fd, 200, "application/json; charset=utf-8",
        "{\"ok\":true}", strlen("{\"ok\":true}"),
        "Set-Cookie: HostSession=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
}

static void handle_auth_me(const http_request_t *req) {
    if (strcmp(req->method, "GET") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    char *safe_username = json_escape_alloc(username);
    char body[512];
    pthread_mutex_lock(&g_user_mu);
    snprintf(body, sizeof(body), "{\"ok\":true,\"username\":\"%s\",\"must_change_password\":%s}",
        safe_username == NULL ? "" : safe_username,
        g_user.must_change_password ? "true" : "false");
    pthread_mutex_unlock(&g_user_mu);
    free(safe_username);
    send_json(req->client_fd, 200, body);
}

static void handle_auth_change_password(const http_request_t *req) {
    if (strcmp(req->method, "POST") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    char new_password[256];
    char confirm[256];
    char current_password[256];
    if (!json_get_string(req->body, "new_password", new_password, sizeof(new_password)) ||
        !json_get_string(req->body, "new_password_confirm", confirm, sizeof(confirm))) {
        send_json_error(req->client_fd, 400, "missing password");
        return;
    }
    if (strcmp(new_password, confirm) != 0) {
        send_json_error(req->client_fd, 400, "passwords do not match");
        return;
    }
    if (strlen(new_password) < 8) {
        send_json_error(req->client_fd, 400, "password too short");
        return;
    }
    pthread_mutex_lock(&g_user_mu);
    int must_change = g_user.must_change_password;
    pthread_mutex_unlock(&g_user_mu);
    if (!must_change) {
        if (!json_get_string(req->body, "current_password", current_password, sizeof(current_password)) || current_password[0] == '\0') {
            send_json_error(req->client_fd, 400, "current password required");
            return;
        }
        if (!verify_password(username, current_password)) {
            send_json_error(req->client_fd, 401, "current password incorrect");
            return;
        }
    }
    if (!update_password(new_password)) {
        send_json_error(req->client_fd, 500, "server");
        return;
    }
    send_json(req->client_fd, 200, "{\"ok\":true}");
}

// ==================== 设置 API ====================
// 作用：读写前端共享配置文件 hostpc-settings.json，供 ROS 和脚本复用。
// ==================================================
static char *read_entire_file(const char *path, size_t max_bytes, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0 || (size_t)len > max_bytes) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = calloc((size_t)len + 1, 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[got] = '\0';
    if (out_len != NULL) {
        *out_len = got;
    }
    return buf;
}

static bool write_entire_file(const char *path, const char *body, size_t len, mode_t mode) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    bool ok = fwrite(body, 1, len, fp) == len;
    fclose(fp);
    chmod(path, mode);
    return ok;
}

static void handle_settings(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (strcmp(req->method, "GET") == 0) {
        size_t len = 0;
        char *body = read_entire_file(g_config.settings_path, HOSTPC_BODY_LIMIT, &len);
        if (body == NULL) {
            send_json(req->client_fd, 200, "{\"camera_url\":\"\",\"serial_roles\":{}}");
            return;
        }
        send_response_raw(req->client_fd, 200, "application/json; charset=utf-8", body, len, NULL);
        free(body);
        return;
    }
    if (strcmp(req->method, "POST") == 0) {
        if (req->body == NULL || req->body_len == 0 || req->body[0] != '{') {
            send_json_error(req->client_fd, 400, "invalid json");
            return;
        }
        if (!write_entire_file(g_config.settings_path, req->body, req->body_len, 0600)) {
            send_json_error(req->client_fd, 500, "write");
            return;
        }
        send_response_raw(req->client_fd, 200, "application/json; charset=utf-8", req->body, req->body_len, NULL);
        return;
    }
    send_text(req->client_fd, 405, "method not allowed");
}

// ==================== 串口 API ====================
// 作用：扫描 Linux 上的 USB 串口路径，并返回给前端设置面板绑定 ESP32。
// ==================================================
typedef struct {
    char path[PATH_MAX];
    char target[PATH_MAX];
    char kind[32];
} serial_entry_t;

static bool serial_target_seen(serial_entry_t *items, size_t count, const char *target) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(items[i].target, target) == 0) {
            return true;
        }
    }
    return false;
}

static void add_serial_entry(serial_entry_t *items, size_t *count, size_t max_count, const char *path, const char *target, const char *kind) {
    if (*count >= max_count || path == NULL || target == NULL || serial_target_seen(items, *count, target)) {
        return;
    }
    copy_text(items[*count].path, sizeof(items[*count].path), path);
    const char *base = strrchr(target, '/');
    copy_text(items[*count].target, sizeof(items[*count].target), base == NULL ? target : base + 1);
    copy_text(items[*count].kind, sizeof(items[*count].kind), kind);
    (*count)++;
}

static void scan_serial_glob(serial_entry_t *items, size_t *count, const char *pattern, const char *kind) {
    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob(pattern, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            char resolved[PATH_MAX];
            const char *target = realpath(g.gl_pathv[i], resolved) == NULL ? g.gl_pathv[i] : resolved;
            add_serial_entry(items, count, 128, g.gl_pathv[i], target, kind);
        }
    }
    globfree(&g);
}

static int compare_serial_entries(const void *a, const void *b) {
    const serial_entry_t *ea = (const serial_entry_t *)a;
    const serial_entry_t *eb = (const serial_entry_t *)b;
    return strcmp(ea->path, eb->path);
}

static void handle_serial_devices(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (strcmp(req->method, "GET") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    serial_entry_t items[128];
    size_t count = 0;
    memset(items, 0, sizeof(items));
    scan_serial_glob(items, &count, "/dev/serial/by-id/*", "by-id");
    scan_serial_glob(items, &count, "/dev/ttyUSB*", "tty");
    scan_serial_glob(items, &count, "/dev/ttyACM*", "tty");
    qsort(items, count, sizeof(items[0]), compare_serial_entries);

    size_t cap = 8192 + count * 512;
    char *body = calloc(cap, 1);
    if (body == NULL) {
        send_json_error(req->client_fd, 500, "server");
        return;
    }
    snprintf(body, cap, "{\"os\":\"linux\",\"devices\":[");
    for (size_t i = 0; i < count; i++) {
        char *path = json_escape_alloc(items[i].path);
        char *target = json_escape_alloc(items[i].target);
        char *kind = json_escape_alloc(items[i].kind);
        char item[1024];
        snprintf(item, sizeof(item), "%s{\"path\":\"%s\",\"target\":\"%s\",\"kind\":\"%s\"}",
            i == 0 ? "" : ",",
            path == NULL ? "" : path,
            target == NULL ? "" : target,
            kind == NULL ? "" : kind);
        strncat(body, item, cap - strlen(body) - 1);
        free(path);
        free(target);
        free(kind);
    }
    strncat(body, "]}", cap - strlen(body) - 1);
    send_json(req->client_fd, 200, body);
    free(body);
}

// ==================== 文件列表 API ====================
// 作用：提供只读目录浏览能力，供前端桌面文件窗口查看上位机 Linux 文件系统。
// ==================================================
static void query_get_param(const char *query, const char *key, char *out, size_t out_size) {
    out[0] = '\0';
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    const char *p = strstr(query, pattern);
    if (p == NULL) {
        return;
    }
    p += strlen(pattern);
    size_t n = 0;
    while (p[n] && p[n] != '&' && n + 1 < out_size) {
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
    url_decode(out);
}

static int compare_dirent_names(const struct dirent **a, const struct dirent **b) {
    if ((*a)->d_type == DT_DIR && (*b)->d_type != DT_DIR) return -1;
    if ((*a)->d_type != DT_DIR && (*b)->d_type == DT_DIR) return 1;
    return strcasecmp((*a)->d_name, (*b)->d_name);
}

static void handle_fs_list(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (strcmp(req->method, "GET") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    char raw_path[PATH_MAX];
    query_get_param(req->query, "path", raw_path, sizeof(raw_path));
    if (raw_path[0] == '\0') {
        copy_text(raw_path, sizeof(raw_path), "/");
    }
    if (raw_path[0] != '/' || strchr(raw_path, '\0') == NULL) {
        send_json_error(req->client_fd, 400, "path must be absolute");
        return;
    }
    char clean_path[PATH_MAX];
    if (realpath(raw_path, clean_path) == NULL) {
        send_json_error(req->client_fd, errno == EACCES ? 403 : 404, errno == EACCES ? "permission denied" : "not found");
        return;
    }
    struct stat st;
    if (stat(clean_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        send_json_error(req->client_fd, 400, "not a directory");
        return;
    }
    struct dirent **names = NULL;
    int n = scandir(clean_path, &names, NULL, compare_dirent_names);
    if (n < 0) {
        send_json_error(req->client_fd, errno == EACCES ? 403 : 500, errno == EACCES ? "permission denied" : "read directory failed");
        return;
    }

    size_t cap = 65536;
    char *body = calloc(cap, 1);
    char *safe_path = json_escape_alloc(clean_path);
    char parent[PATH_MAX];
    copy_text(parent, sizeof(parent), clean_path);
    char *slash = strrchr(parent, '/');
    if (slash != NULL && slash != parent) {
        *slash = '\0';
    } else {
        copy_text(parent, sizeof(parent), "/");
    }
    char *safe_parent = json_escape_alloc(parent);
    snprintf(body, cap, "{\"path\":\"%s\",\"parent\":\"%s\",\"entries\":[", safe_path == NULL ? "" : safe_path, safe_parent == NULL ? "/" : safe_parent);
    free(safe_path);
    free(safe_parent);

    int emitted = 0;
    bool truncated = false;
    for (int i = 0; i < n; i++) {
        const char *name = names[i]->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            free(names[i]);
            continue;
        }
        if (emitted >= 2048) {
            truncated = true;
            free(names[i]);
            continue;
        }
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", clean_path, name);
        struct stat ent_st;
        bool stat_ok = stat(full, &ent_st) == 0;
        char *safe_name = json_escape_alloc(name);
        char item[2048];
        snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"is_dir\":%s,\"size\":%lld,\"mod_time\":\"\",\"mode\":%u}",
            emitted == 0 ? "" : ",",
            safe_name == NULL ? "" : safe_name,
            stat_ok && S_ISDIR(ent_st.st_mode) ? "true" : "false",
            stat_ok ? (long long)ent_st.st_size : 0LL,
            stat_ok ? (unsigned int)(ent_st.st_mode & 0777) : 0U);
        if (strlen(body) + strlen(item) + 64 >= cap) {
            truncated = true;
        } else {
            strncat(body, item, cap - strlen(body) - 1);
            emitted++;
        }
        free(safe_name);
        free(names[i]);
    }
    free(names);
    char tail[128];
    snprintf(tail, sizeof(tail), "],\"truncated\":%s}", truncated ? "true" : "false");
    strncat(body, tail, cap - strlen(body) - 1);
    send_json(req->client_fd, 200, body);
    free(body);
}

// ==================== 更新 API ====================
// 作用：保留前端自更新接口；在没有配置仓库凭据时返回明确状态，避免页面报错。
// ==================================================
static void handle_updates_status(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (strcmp(req->method, "GET") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    if (g_config.repo_root[0] == '\0') {
        send_json(req->client_fd, 200,
            "{\"enabled\":false,\"update_available\":false,\"branch\":\"MainScript\",\"changelog_ok\":false,\"reason\":\"repo root not configured\"}");
        return;
    }
    char *safe_repo = json_escape_alloc(g_config.repo_root);
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"enabled\":true,\"update_available\":false,\"branch\":\"MainScript\",\"changelog_ok\":false,\"reason\":\"C backend does not run unattended git updates\",\"script_path\":\"\",\"repo_root\":\"%s\"}",
        safe_repo == NULL ? "" : safe_repo);
    free(safe_repo);
    send_json(req->client_fd, 200, body);
}

static void handle_updates_apply(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (strcmp(req->method, "POST") != 0) {
        send_text(req->client_fd, 405, "method not allowed");
        return;
    }
    send_json(req->client_fd, 400, "{\"error\":\"no update available\"}");
}

// ==================== 静态资源 ====================
// 作用：托管 Vue 构建产物，找不到具体文件时回退到 index.html。
// ==================================================
static void send_file_response(int fd, const char *path) {
    size_t len = 0;
    char *body = read_entire_file(path, 64 * 1024 * 1024, &len);
    if (body == NULL) {
        send_text(fd, 404, "not found");
        return;
    }
    send_response_raw(fd, 200, mime_type_for_path(path), body, len, NULL);
    free(body);
}

static void handle_static_file(const http_request_t *req) {
    char rel[PATH_MAX];
    if (strcmp(req->path, "/") == 0) {
        copy_text(rel, sizeof(rel), "index.html");
    } else {
        copy_text(rel, sizeof(rel), req->path + 1);
    }
    if (contains_parent_path(rel)) {
        send_text(req->client_fd, 403, "forbidden");
        return;
    }
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", g_config.static_dir, rel);
    struct stat st;
    if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) {
        snprintf(full, sizeof(full), "%s/index.html", g_config.static_dir);
    }
    send_file_response(req->client_fd, full);
}

// ==================== WebSocket 工具 ====================
// 作用：完成浏览器 WebSocket 握手、文本帧发送和主控制通道消息读取。
// ==================================================
static bool websocket_accept_value(const char *key, char *out, size_t out_size) {
    char source[256];
    unsigned char sha[SHA_DIGEST_LENGTH];
    snprintf(source, sizeof(source), "%s%s", key, HOSTPC_WS_GUID);
    SHA1((const unsigned char *)source, strlen(source), sha);
    int n = EVP_EncodeBlock((unsigned char *)out, sha, SHA_DIGEST_LENGTH);
    if (n <= 0 || (size_t)n >= out_size) {
        return false;
    }
    out[n] = '\0';
    return true;
}

static void ws_send_text(int fd, const char *text) {
    size_t len = strlen(text);
    unsigned char header[10];
    size_t header_len = 0;
    header[0] = 0x81;
    if (len < 126) {
        header[1] = (unsigned char)len;
        header_len = 2;
    } else if (len <= 65535) {
        header[1] = 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    } else {
        return;
    }
    send_all(fd, header, header_len);
    send_all(fd, text, len);
}

static void ws_send_binary(int fd, const unsigned char *data, size_t len) {
    unsigned char header[10];
    size_t header_len = 0;
    header[0] = 0x82;
    if (len < 126) {
        header[1] = (unsigned char)len;
        header_len = 2;
    } else if (len <= 65535) {
        header[1] = 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    } else {
        return;
    }
    send_all(fd, header, header_len);
    send_all(fd, data, len);
}

static bool ws_read_frame_ex(int fd, unsigned char *payload, size_t payload_size, size_t *out_len, int *out_opcode) {
    unsigned char h[2];
    if (recv(fd, h, 2, MSG_WAITALL) != 2) {
        return false;
    }
    unsigned char opcode = h[0] & 0x0F;
    bool masked = (h[1] & 0x80) != 0;
    uint64_t len = h[1] & 0x7F;
    if (opcode == 0x8) {
        return false;
    }
    if (len == 126) {
        unsigned char ext[2];
        if (recv(fd, ext, 2, MSG_WAITALL) != 2) {
            return false;
        }
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        return false;
    }
    if (len + 1 > payload_size) {
        return false;
    }
    unsigned char mask[4] = {0};
    if (masked && recv(fd, mask, 4, MSG_WAITALL) != 4) {
        return false;
    }
    if (recv(fd, payload, len, MSG_WAITALL) != (ssize_t)len) {
        return false;
    }
    if (masked) {
        for (uint64_t i = 0; i < len; i++) {
            payload[i] = (char)((unsigned char)payload[i] ^ mask[i % 4]);
        }
    }
    payload[len] = '\0';
    if (out_len != NULL) {
        *out_len = (size_t)len;
    }
    if (out_opcode != NULL) {
        *out_opcode = opcode;
    }
    return opcode == 0x1 || opcode == 0x2 || opcode == 0x9;
}

static bool ws_read_frame(int fd, char *payload, size_t payload_size) {
    size_t len = 0;
    int opcode = 0;
    return ws_read_frame_ex(fd, (unsigned char *)payload, payload_size, &len, &opcode);
}

static bool send_websocket_handshake(const http_request_t *req) {
    const char *key = header_value(req, "Sec-WebSocket-Key");
    if (key[0] == '\0') {
        send_json_error(req->client_fd, 400, "missing websocket key");
        return false;
    }
    char accept[128];
    if (!websocket_accept_value(key, accept, sizeof(accept))) {
        send_json_error(req->client_fd, 500, "websocket");
        return false;
    }
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept);
    send_all(req->client_fd, header, strlen(header));
    return true;
}

static void handle_main_websocket(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (!send_websocket_handshake(req)) {
        return;
    }
    ws_send_text(req->client_fd, "{\"type\":\"log\",\"line\":\"INFO  C HostPC WebSocket connected\",\"edge\":\"e_ws\"}");

    char payload[4096];
    while (ws_read_frame(req->client_fd, payload, sizeof(payload))) {
        if (strstr(payload, "\"type\"") != NULL && strstr(payload, "\"key\"") != NULL) {
            ws_send_text(req->client_fd, "{\"type\":\"ack\",\"msg\":\"key command received by C backend\",\"edge\":\"e_ws\"}");
        }
    }
}

// ==================== Shell WebSocket ====================
// 作用：用 C 的 forkpty 启动真实 shell，并把浏览器二进制帧与 PTY 双向转发。
// ==================================================
static void handle_shell_websocket(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (!send_websocket_handshake(req)) {
        return;
    }

    int pty_fd = -1;
    pid_t child = forkpty(&pty_fd, NULL, NULL, NULL);
    if (child < 0) {
        ws_send_text(req->client_fd, "\r\n[host] failed to start shell\r\n");
        return;
    }
    if (child == 0) {
        const char *shell = getenv("SHELL");
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/bash";
        }
        execl(shell, shell, "-l", (char *)NULL);
        execl("/bin/bash", "/bin/bash", "-l", (char *)NULL);
        execl("/bin/sh", "/bin/sh", (char *)NULL);
        _exit(127);
    }

    ws_send_binary(req->client_fd, (const unsigned char *)"\r\n[host] C shell connected\r\n", strlen("\r\n[host] C shell connected\r\n"));
    unsigned char buf[8192];
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(req->client_fd, &rfds);
        FD_SET(pty_fd, &rfds);
        int maxfd = req->client_fd > pty_fd ? req->client_fd : pty_fd;
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) <= 0) {
            break;
        }
        if (FD_ISSET(pty_fd, &rfds)) {
            ssize_t n = read(pty_fd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            ws_send_binary(req->client_fd, buf, (size_t)n);
        }
        if (FD_ISSET(req->client_fd, &rfds)) {
            size_t len = 0;
            int opcode = 0;
            if (!ws_read_frame_ex(req->client_fd, buf, sizeof(buf) - 1, &len, &opcode)) {
                break;
            }
            if (opcode == 0x2 && len > 0) {
                if (write(pty_fd, buf, len) < 0) {
                    break;
                }
            }
        }
    }
    close(pty_fd);
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
}

// ==================== VNC WebSocket ====================
// 作用：把浏览器 noVNC 的 WebSocket 二进制流转发到本机 127.0.0.1:5900。
// ==================================================
static int connect_local_vnc(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5900);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void handle_vnc_websocket(const http_request_t *req) {
    char username[128];
    if (!require_auth_or_401(req, username)) {
        return;
    }
    if (!send_websocket_handshake(req)) {
        return;
    }
    int vnc_fd = connect_local_vnc();
    if (vnc_fd < 0) {
        ws_send_text(req->client_fd, "{\"error\":\"vnc target 127.0.0.1:5900 unavailable\"}");
        return;
    }

    unsigned char buf[32768];
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(req->client_fd, &rfds);
        FD_SET(vnc_fd, &rfds);
        int maxfd = req->client_fd > vnc_fd ? req->client_fd : vnc_fd;
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) <= 0) {
            break;
        }
        if (FD_ISSET(vnc_fd, &rfds)) {
            ssize_t n = recv(vnc_fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            ws_send_binary(req->client_fd, buf, (size_t)n);
        }
        if (FD_ISSET(req->client_fd, &rfds)) {
            size_t len = 0;
            int opcode = 0;
            if (!ws_read_frame_ex(req->client_fd, buf, sizeof(buf) - 1, &len, &opcode)) {
                break;
            }
            if (opcode == 0x2 && len > 0) {
                send_all(vnc_fd, buf, len);
            }
        }
    }
    close(vnc_fd);
}

// ==================== 路由分发 ====================
// 作用：保持前端原有路径不变，把请求分发到 C 后端对应模块。
// ==================================================
static void handle_health(const http_request_t *req) {
    (void)req;
    send_json(req->client_fd, 200,
        "{\"ok\":true,\"service\":\"omniroam-host-c\",\"mysql\":{\"enabled\":false},\"users_backend\":\"c_file\"}");
}

static void route_request(const http_request_t *req) {
    if (strcmp(req->path, "/api/health") == 0) {
        handle_health(req);
    } else if (strcmp(req->path, "/api/auth/login") == 0) {
        handle_auth_login(req);
    } else if (strcmp(req->path, "/api/auth/logout") == 0) {
        handle_auth_logout(req);
    } else if (strcmp(req->path, "/api/auth/me") == 0) {
        handle_auth_me(req);
    } else if (strcmp(req->path, "/api/auth/change-password") == 0) {
        handle_auth_change_password(req);
    } else if (strcmp(req->path, "/api/settings") == 0) {
        handle_settings(req);
    } else if (strcmp(req->path, "/api/serial/devices") == 0) {
        handle_serial_devices(req);
    } else if (strcmp(req->path, "/api/fs/list") == 0) {
        handle_fs_list(req);
    } else if (strcmp(req->path, "/api/updates/status") == 0) {
        handle_updates_status(req);
    } else if (strcmp(req->path, "/api/updates/apply") == 0) {
        handle_updates_apply(req);
    } else if (strcmp(req->path, "/ws") == 0) {
        handle_main_websocket(req);
    } else if (strcmp(req->path, "/ws/shell") == 0) {
        handle_shell_websocket(req);
    } else if (strcmp(req->path, "/ws/vnc") == 0) {
        handle_vnc_websocket(req);
    } else if (starts_with(req->path, "/api/")) {
        send_json_error(req->client_fd, 404, "not found");
    } else {
        handle_static_file(req);
    }
}

// ==================== 客户端线程 ====================
// 作用：每个连接独立解析请求、调用路由并释放连接资源。
// ==================================================
typedef struct {
    int fd;
} client_job_t;

static void *client_thread_main(void *arg) {
    client_job_t *job = (client_job_t *)arg;
    int fd = job->fd;
    free(job);
    http_request_t req;
    if (read_http_request(fd, &req)) {
        route_request(&req);
        free_http_request(&req);
    }
    close(fd);
    return NULL;
}

// ==================== 参数解析 ====================
// 作用：解析 -addr、-static、-settings、-users、-repo-root 等启动参数。
// ==================================================
static void parse_addr(const char *addr, char *host, size_t host_size, int *port) {
    const char *colon = strrchr(addr, ':');
    if (colon == NULL) {
        copy_text(host, host_size, "0.0.0.0");
        *port = atoi(addr);
        return;
    }
    size_t host_len = (size_t)(colon - addr);
    if (host_len == 0) {
        copy_text(host, host_size, "0.0.0.0");
    } else {
        if (host_len >= host_size) {
            host_len = host_size - 1;
        }
        memcpy(host, addr, host_len);
        host[host_len] = '\0';
    }
    *port = atoi(colon + 1);
    if (*port <= 0) {
        *port = 8080;
    }
}

static void parse_args(int argc, char **argv, hostpc_config_t *config) {
    copy_text(config->listen_host, sizeof(config->listen_host), "0.0.0.0");
    config->listen_port = 8080;
    copy_text(config->static_dir, sizeof(config->static_dir), "../frontend/dist");
    copy_text(config->settings_path, sizeof(config->settings_path), "hostpc-settings.json");
    copy_text(config->user_path, sizeof(config->user_path), "hostpc-users.cauth");
    config->repo_root[0] = '\0';

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-addr") == 0 && i + 1 < argc) {
            parse_addr(argv[++i], config->listen_host, sizeof(config->listen_host), &config->listen_port);
        } else if (strcmp(argv[i], "-static") == 0 && i + 1 < argc) {
            copy_text(config->static_dir, sizeof(config->static_dir), argv[++i]);
        } else if (strcmp(argv[i], "-settings") == 0 && i + 1 < argc) {
            copy_text(config->settings_path, sizeof(config->settings_path), argv[++i]);
        } else if (strcmp(argv[i], "-users") == 0 && i + 1 < argc) {
            copy_text(config->user_path, sizeof(config->user_path), argv[++i]);
        } else if (strcmp(argv[i], "-repo-root") == 0 && i + 1 < argc) {
            copy_text(config->repo_root, sizeof(config->repo_root), argv[++i]);
        }
    }
}

// ==================== 服务启动 ====================
// 作用：创建 TCP 监听 socket，绑定端口，并持续接收浏览器连接。
// ==================================================
int hostpc_run_server(const hostpc_config_t *config) {
    g_config = *config;
    if (!load_user_file()) {
        fprintf(stderr, "failed to load or create user file: %s\n", g_config.user_path);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_config.listen_port);
    if (inet_pton(AF_INET, g_config.listen_host, &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 128) != 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("OmniRoam C HostPC listening on %s:%d static=%s\n", g_config.listen_host, g_config.listen_port, g_config.static_dir);
    printf("Default web account is stored in %s\n", g_config.user_path);
    while (true) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        client_job_t *job = calloc(1, sizeof(*job));
        if (job == NULL) {
            close(fd);
            continue;
        }
        job->fd = fd;
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread_main, job) != 0) {
            close(fd);
            free(job);
            continue;
        }
        pthread_detach(tid);
    }
    close(server_fd);
    return 0;
}

// ==================== 主入口 ====================
// 作用：屏蔽 SIGPIPE，读取命令行参数，然后启动 C 后端服务。
// ==================================================
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    hostpc_config_t config;
    parse_args(argc, argv, &config);
    return hostpc_run_server(&config);
}
