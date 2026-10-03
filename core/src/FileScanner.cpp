#include "finrenamer/FileScanner.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "finrenamer/Utf8Path.h"

namespace finrenamer {

std::vector<std::filesystem::path> listPdfFiles(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = utf8FromPath(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".pdf") result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return utf8FromPath(a.filename()) < utf8FromPath(b.filename());
    });
    return result;
}

}  // namespace finrenamer
