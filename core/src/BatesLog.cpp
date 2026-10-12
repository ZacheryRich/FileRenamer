#include "finrenamer/BatesLog.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "finrenamer/Utf8Path.h"

namespace finrenamer {

namespace {

void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string fromUtf16(const std::string& bytes, std::size_t start, bool littleEndian)
{
    std::string out;
    auto unit = [&](std::size_t i) -> std::uint32_t {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        return littleEndian ? (b << 8 | a) : (a << 8 | b);
    };
    for (std::size_t i = start; i + 1 < bytes.size(); i += 2) {
        std::uint32_t u = unit(i);
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < bytes.size()) {
            const std::uint32_t low = unit(i + 2);
            if (low >= 0xDC00 && low < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(out, u);
    }
    return out;
}

bool validUtf8(const std::string& s)
{
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 99;
        if (extra == 99) return false;
        for (std::size_t k = 1; k <= extra; ++k)
            if (i + k >= s.size() || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

// Windows "ANSI" text -> UTF-8 (Latin-1 is close enough for file names).
std::string fromAnsi(const std::string& s)
{
    std::string out;
    for (unsigned char c : s) appendUtf8(out, c);
    return out;
}

std::string toUtf8(const std::string& bytes)
{
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE)
        return fromUtf16(bytes, 2, true);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF)
        return fromUtf16(bytes, 2, false);
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
        return bytes.substr(3);
    return validUtf8(bytes) ? bytes : fromAnsi(bytes);
}

std::string trimmed(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (static_cast<unsigned char>(s[a]) <= 32)) ++a;
    while (b > a && (static_cast<unsigned char>(s[b - 1]) <= 32)) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string matchKey(const std::string& utf8Name)
{
    std::string clean;
    for (unsigned char c : utf8Name)
        if (c >= 32 && c != 127) clean += static_cast<char>(c);
    clean = trimmed(clean);
    if (clean.size() >= 4) {
        std::string tail = clean.substr(clean.size() - 4);
        for (char& c : tail)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (tail == ".pdf") clean.resize(clean.size() - 4);
    }
    return caseFoldKey(pathFromUtf8(trimmed(clean)));
}

BatesLog parseBatesLogText(const std::string& text)
{
    BatesLog log;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find_first_of("\r\n", pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;

        std::string fields[3];
        std::size_t start = 0;
        for (int i = 0; i < 3; ++i) {
            const std::size_t tab = line.find('\t', start);
            fields[i] = trimmed(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (fields[0].empty() || (fields[1].empty() && fields[2].empty())) continue;

        const std::string key = matchKey(fields[0]);
        if (key.empty()) continue;
        ++log.lines;
        if (log.entries.count(key)) ++log.duplicates;
        log.entries[key] = BatesNumbers{fields[1], fields[2]};
        log.displayNames[key] = fields[0];
    }
    return log;
}

BatesLog parseBatesLog(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Couldn't open the Bates log file.");
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseBatesLogText(toUtf8(bytes));
}

}  // namespace finrenamer
