#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

namespace se {
namespace {

std::mutex g_mutex;
std::ofstream g_file;
bool g_echo = false;
bool g_initialised = false;

std::string Timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof buf, "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
    return buf;
}

}  // namespace

void LogInit(const std::string& path, bool echo_to_stdout) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_echo = echo_to_stdout;
    if (g_file.is_open()) g_file.close();
    g_file.open(path, std::ios::out | std::ios::trunc);
    g_initialised = true;
    if (echo_to_stdout) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) g_file.flush();
    g_initialised = false;
}

void LogLine(const char* fmt, ...) {
    char message[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    const std::string line = Timestamp() + "  " + message;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialised && g_file.is_open()) {
        g_file << line << '\n';
        g_file.flush();
    }
    OutputDebugStringA((line + '\n').c_str());
    if (g_echo) {
        fputs((line + '\n').c_str(), stdout);
        fflush(stdout);
    }
}

}  // namespace se
