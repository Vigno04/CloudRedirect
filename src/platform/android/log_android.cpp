#include "log.h"
#include "xdg.h"

#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

static constexpr const char* kLogTag = "CloudRedirect";
static FILE* g_logFile = nullptr;
static std::mutex g_logMutex;
static constexpr long kMaxLogSize = 5 * 1024 * 1024;

static void EnsureDirRecursive(const std::string& path) {
    if (path.empty()) return;
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' || i + 1 == path.size()) {
            if (cur.size() > 1) mkdir(cur.c_str(), 0755);
        }
    }
}

static void WriteLine(int prio, const char* level, const char* fmt, va_list ap) {
    char msg[2048];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    __android_log_print(prio, kLogTag, "[%s] %s", level, msg);

    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logFile) return;

    long pos = ftell(g_logFile);
    if (pos > kMaxLogSize) {
        freopen(nullptr, "w", g_logFile);
    }

    time_t now = time(nullptr);
    struct tm tmNow {};
    localtime_r(&now, &tmNow);
    fprintf(g_logFile, "%04d-%02d-%02d %02d:%02d:%02d [%s] %s\n",
            tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
            tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec,
            level, msg);
    fflush(g_logFile);
}

void Log::Init() {
    std::string base = AndroidConfig::GetConfigDir() + "/CloudRedirect";
    EnsureDirRecursive(base);
    std::string logPath = base + "/cloud_redirect.log";

    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
    g_logFile = fopen(logPath.c_str(), "a");
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "=== CloudRedirect Android started ===");
}

void Log::Init(const char* path) {
    if (path && path[0]) {
        std::lock_guard<std::mutex> lk(g_logMutex);
        if (g_logFile) {
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        g_logFile = fopen(path, "a");
    } else {
        Init();
    }
}

void Log::Shutdown() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Log::Write(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteLine(ANDROID_LOG_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void Log::Info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteLine(ANDROID_LOG_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void Log::Warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteLine(ANDROID_LOG_WARN, "WARN", fmt, ap);
    va_end(ap);
}

void Log::Error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteLine(ANDROID_LOG_ERROR, "ERROR", fmt, ap);
    va_end(ap);
}

void Log::Debug(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteLine(ANDROID_LOG_DEBUG, "DEBUG", fmt, ap);
    va_end(ap);
}
