#pragma once

#include <cstddef>
#include <string>

namespace se {

// SHA-1 of raw bytes, returned as 20 binary bytes.
std::string Sha1Raw(const void* data, size_t length);

// Standard base64 with '=' padding.
std::string Base64Encode(const void* data, size_t length);

// SHA-1 then base64, as the WebSocket handshake requires.
std::string Sha1Base64(const std::string& input);

}  // namespace se
