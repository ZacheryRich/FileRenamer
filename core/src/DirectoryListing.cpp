#include "finrenamer/DirectoryListing.h"

#include <algorithm>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

bool isIgnoredFileName(const std::string& name)
{
    if (name.rfind("~$", 0) == 0) return true;  // Word/Excel "owner" lock files
    std::string lower = name;
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return lower == "thumbs.db" || lower == "desktop.ini" || lower == ".ds_store";
}

namespace {

void walk(const fs::path& dir, const std::string& label, const std::string& skipKey, std::vector<FolderListing>& out)
{
    std::vector<std::string> files;
    std::vector<std::pair<std::string, fs::path>> subfolders;

    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code ec2;
        const std::string name = utf8FromPath(it->path().filename());
        if (it->is_directory(ec2)) {
            if (!ec2 && !it->is_symlink(ec2)) subfolders.emplace_back(name, it->path());
        } else if (it->is_regular_file(ec2) && !isIgnoredFileName(name) &&
                   (skipKey.empty() || caseFoldKey(it->path()) != skipKey)) {
            files.push_back(name);
        }
    }

    std::sort(files.begin(), files.end());
    std::sort(subfolders.begin(), subfolders.end());
    if (!files.empty()) out.push_back(FolderListing{label, std::move(files)});
    for (const auto& [name, path] : subfolders) walk(path, (label == "/" ? "" : label) + "/" + name, skipKey, out);
}

}  // namespace

std::vector<FolderListing> listDirectoryTree(const fs::path& root, const fs::path& skip)
{
    std::vector<FolderListing> out;
    std::error_code ec;
    const fs::path skipPath = skip.empty() ? fs::path() : fs::absolute(skip, ec);
    walk(fs::absolute(root, ec), "/", skip.empty() ? std::string() : caseFoldKey(skipPath), out);
    return out;
}

}  // namespace finrenamer
