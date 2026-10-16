#pragma once

#include <filesystem>
#include <fstream>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "finrenamer/Models.h"
#include "finrenamer/Utf8Path.h"

namespace testing {

namespace fs = std::filesystem;

// A scratch folder that deletes itself when the test ends.
class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("finrenamer_test_" + std::to_string(rd()));
        fs::create_directories(path_);
        path_ = fs::canonical(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

    fs::path touch(const std::string& relativeUtf8, const std::string& contents = "pdf") const
    {
        const fs::path p = path_ / finrenamer::pathFromUtf8(relativeUtf8);
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << contents;
        return p;
    }

private:
    fs::path path_;
};

inline std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Writes a zip of stored (uncompressed) entries: enough to build .xlsx / .docx test files by hand.
inline void writeZip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries)
{
    auto crc32 = [](const std::string& data) {
        static const auto table = [] {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t i = 0; i < 256; ++i) {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }();
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const char ch : data) crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    };
    auto put16 = [](std::string& o, std::uint16_t v) {
        o += static_cast<char>(v & 0xFF);
        o += static_cast<char>(v >> 8);
    };
    auto put32 = [&](std::string& o, std::uint32_t v) {
        put16(o, static_cast<std::uint16_t>(v & 0xFFFF));
        put16(o, static_cast<std::uint16_t>(v >> 16));
    };
    std::string body, central;
    for (const auto& [name, data] : entries) {
        const std::uint32_t crc = crc32(data);
        const auto offset = static_cast<std::uint32_t>(body.size());
        put32(body, 0x04034b50); put16(body, 20); put16(body, 0); put16(body, 0); put16(body, 0); put16(body, 0x21);
        put32(body, crc); put32(body, static_cast<std::uint32_t>(data.size())); put32(body, static_cast<std::uint32_t>(data.size()));
        put16(body, static_cast<std::uint16_t>(name.size())); put16(body, 0);
        body += name; body += data;
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0x21); put32(central, crc);
        put32(central, static_cast<std::uint32_t>(data.size())); put32(central, static_cast<std::uint32_t>(data.size()));
        put16(central, static_cast<std::uint16_t>(name.size())); put16(central, 0); put16(central, 0); put16(central, 0);
        put16(central, 0); put32(central, 0); put32(central, offset);
        central += name;
    }
    std::string end;
    put32(end, 0x06054b50); put16(end, 0); put16(end, 0);
    put16(end, static_cast<std::uint16_t>(entries.size())); put16(end, static_cast<std::uint16_t>(entries.size()));
    put32(end, static_cast<std::uint32_t>(central.size())); put32(end, static_cast<std::uint32_t>(body.size()));
    put16(end, 0);
    std::ofstream(file, std::ios::binary) << body << central << end;
}

inline finrenamer::Account chaseJoint()
{
    finrenamer::Account a;
    a.id = 1;
    a.institution = "Chase";
    a.accountType = "Checking";
    a.lastFour = "1234";
    a.owners = {"John Smith", "Jane Smith"};
    return a;
}

inline finrenamer::Account fidelitySingle()
{
    finrenamer::Account a;
    a.id = 2;
    a.institution = "Fidelity";
    a.accountType = "Brokerage";
    a.lastFour = "5678";
    a.owners = {"Jane Smith"};
    return a;
}

}  // namespace testing
