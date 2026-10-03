#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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
