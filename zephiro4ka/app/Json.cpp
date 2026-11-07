#include "Json.h"

#include <cstdint>
#include <cstdio>

namespace {
    int HexValue(char d) {
        if (d >= '0' && d <= '9') return d - '0';
        if (d >= 'a' && d <= 'f') return d - 'a' + 10;
        if (d >= 'A' && d <= 'F') return d - 'A' + 10;
        return -1;
    }

    bool ReadHex4(const std::string& json, size_t& pos, uint32_t& out) {
        if (pos + 4 > json.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = HexValue(json[pos++]);
            if (h < 0) return false;
            out = (out << 4) | static_cast<uint32_t>(h);
        }
        return true;
    }

    void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
}

std::string Json::Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", ch);
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
    }
    return out;
}

bool Json::ReadString(const std::string& json, size_t& pos, std::string& value) {
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    value.clear();
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') return true;
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= json.size()) return false;
        switch (const char e = json[pos++]) {
        case '"': case '\\': case '/': value.push_back(e); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
            uint32_t cp = 0;
            if (!ReadHex4(json, pos, cp)) return false;
            const bool highSurrogate = cp >= 0xd800 && cp <= 0xdbff;
            if (highSurrogate && pos + 6 <= json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                pos += 2;
                uint32_t low = 0;
                if (!ReadHex4(json, pos, low)) return false;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            }
            AppendUtf8(value, cp);
            break;
        }
        default: return false;
        }
    }
    return false;
}

std::vector<std::string> Json::FindStrings(const std::string& json, const char* key, size_t limit) {
    std::vector<std::string> out;
    const std::string needle = std::string("\"") + key + "\"";
    for (size_t pos = json.find(needle); pos != std::string::npos && out.size() < limit; pos = json.find(needle, pos)) {
        pos += needle.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) ++pos;
        std::string value;
        if (pos < json.size() && json[pos] == '"' && ReadString(json, pos, value)) out.push_back(std::move(value));
    }
    return out;
}
