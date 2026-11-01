#include "sha1.h"

#include <cstdint>
#include <cstring>

namespace se {
namespace {

inline uint32_t RotL(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

}  // namespace

std::string Sha1Raw(const void* data, size_t length) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const uint64_t bit_length = static_cast<uint64_t>(length) * 8;

    // Message + 0x80 + padding + 8 byte length, in 64 byte blocks.
    const size_t padded_length = ((length + 8) / 64 + 1) * 64;
    std::string buffer(padded_length, '\0');
    if (length) memcpy(&buffer[0], bytes, length);
    buffer[length] = static_cast<char>(0x80);
    for (int i = 0; i < 8; ++i) {
        buffer[padded_length - 1 - i] = static_cast<char>((bit_length >> (8 * i)) & 0xFF);
    }

    for (size_t block = 0; block < padded_length; block += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&buffer[block + i * 4]);
            w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = RotL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t temp = RotL(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotL(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::string digest(20, '\0');
    for (int i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = static_cast<char>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<char>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<char>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<char>(h[i] & 0xFF);
    }
    return digest;
}

std::string Base64Encode(const void* data, size_t length) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = (i + 1 < length) ? bytes[i + 1] : 0;
        const uint32_t b2 = (i + 2 < length) ? bytes[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < length ? kTable[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < length ? kTable[triple & 0x3F] : '=');
    }
    return out;
}

std::string Sha1Base64(const std::string& input) {
    const std::string digest = Sha1Raw(input.data(), input.size());
    return Base64Encode(digest.data(), digest.size());
}

}  // namespace se
