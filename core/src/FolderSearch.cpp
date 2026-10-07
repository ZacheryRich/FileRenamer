#include "finrenamer/FolderSearch.h"

#include <algorithm>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {
namespace {

fs::path resolve(const fs::path& p)
{
    std::error_code ec;
    fs::path r = fs::weakly_canonical(fs::absolute(p), ec);
    return ec ? fs::absolute(p).lexically_normal() : r;
}

bool isInside(const fs::path& parent, const fs::path& p)
{
    const fs::path rel = p.lexically_relative(parent);
    return !rel.empty() && *rel.begin() != ".." && rel != ".";
}

bool isPdf(const fs::path& p)
{
    return caseFoldKey(p.extension()) == caseFoldKey(fs::path(".pdf"));
}

}  // namespace

std::vector<fs::path> searchRoots(const std::vector<fs::path>& chosen, bool includeSubfolders)
{
    std::error_code ec;
    std::vector<fs::path> roots;
    for (const fs::path& r : chosen) {
        const fs::path p = resolve(r);
        if (!fs::is_directory(p, ec)) continue;
        if (std::any_of(roots.begin(), roots.end(), [&](const fs::path& q) { return caseFoldKey(q) == caseFoldKey(p); }))
            continue;
        roots.push_back(p);
    }
    if (includeSubfolders) {
        std::vector<fs::path> outer;
        for (const fs::path& r : roots)
            if (std::none_of(roots.begin(), roots.end(), [&](const fs::path& q) { return isInside(q, r); }))
                outer.push_back(r);
        roots = std::move(outer);
    }
    return roots;
}

std::vector<fs::path> findPdfs(const std::vector<fs::path>& chosen, bool includeSubfolders)
{
    std::error_code ec;
    std::vector<fs::path> files;
    for (const fs::path& root : searchRoots(chosen, includeSubfolders)) {
        if (includeSubfolders) {
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && isPdf(it->path())) files.push_back(it->path());
        } else {
            for (const auto& e : fs::directory_iterator(root, ec))
                if (e.is_regular_file(ec) && isPdf(e.path())) files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace finrenamer
