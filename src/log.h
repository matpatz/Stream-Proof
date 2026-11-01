#pragma once

#include <string>

namespace se {

// Minimal logger: writes to a file next to the executable and to the debugger.
void LogInit(const std::string& path, bool echo_to_stdout);
void LogShutdown();
void LogLine(const char* fmt, ...);

}  // namespace se
