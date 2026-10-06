#include "finrenamer/NameFixer.h"

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {
namespace {

std::string key(const std::string& utf8) { return caseFoldKey(pathFromUtf8(utf8)); }

struct FileLabelInfo {
    const Account* account = nullptr;
    std::string number;  // the number files with this label should use
};

bool sameFile(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

bool usesNumber(const Account& a, const std::string& number)
{
    if (key(number) == key(a.lastFour)) return true;
    for (const auto& n : a.previousLastFour)
        if (key(n) == key(number)) return true;
    return false;
}

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

// Deepest folder containing every root (used as the batch's root in history).
fs::path commonAncestor(const std::vector<fs::path>& roots)
{
    if (roots.empty()) return {};
    fs::path common = roots.front();
    for (const fs::path& r : roots) {
        fs::path candidate;
        auto a = common.begin(), b = r.begin();
        for (; a != common.end() && b != r.end() && caseFoldKey(*a) == caseFoldKey(*b); ++a, ++b)
            candidate /= *a;
        common = candidate;
    }
    return common;
}

}  // namespace

RenamePlan planNameFixes(const std::vector<fs::path>& rootsIn, const std::vector<Account>& accounts,
                         const std::vector<OldAccountName>& oldNames, const NameFixOptions& options)
{
    RenamePlan plan;
    std::error_code ec;

    // ---- Roots: resolved, de-duplicated, nested ones dropped when searching subfolders.
    std::vector<fs::path> roots;
    for (const fs::path& r : rootsIn) {
        const fs::path p = resolve(r);
        if (!fs::is_directory(p, ec)) continue;
        if (std::any_of(roots.begin(), roots.end(), [&](const fs::path& q) { return caseFoldKey(q) == caseFoldKey(p); }))
            continue;
        roots.push_back(p);
    }
    if (options.includeSubfolders) {
        std::vector<fs::path> outer;
        for (const fs::path& r : roots)
            if (std::none_of(roots.begin(), roots.end(), [&](const fs::path& q) { return isInside(q, r); }))
                outer.push_back(r);
        roots = std::move(outer);
    }
    plan.root = commonAncestor(roots);

    // ---- Names we recognise.
    std::unordered_map<std::int64_t, const Account*> byId;
    for (const Account& a : accounts) byId[a.id] = &a;

    // Label (as used in file names) -> account and number. Current labels first,
    // so a name that is current for one account is never treated as old.
    std::unordered_map<std::string, FileLabelInfo> fileLabels;
    for (const Account& a : accounts) {
        fileLabels.emplace(key(accountLabel(a)), FileLabelInfo{&a, a.lastFour});
        for (const auto& n : a.previousLastFour)
            fileLabels.emplace(key(accountLabel(a, n)), FileLabelInfo{&a, n});
    }
    for (const OldAccountName& old : oldNames) {
        if (old.isFolder || !byId.count(old.accountId)) continue;
        fileLabels.emplace(key(old.name), FileLabelInfo{byId[old.accountId], old.lastFour});
    }

    // Folder name -> account. Any label an account has ever had, used as a
    // folder name, is that account's folder.
    std::unordered_map<std::string, const Account*> folderNames;
    for (const Account& a : accounts) folderNames.emplace(key(accountFolderLabel(a)), &a);
    for (const auto& [k, info] : fileLabels) folderNames.emplace(k, info.account);
    for (const OldAccountName& old : oldNames)
        if (old.isFolder && byId.count(old.accountId)) folderNames.emplace(key(old.name), byId[old.accountId]);

    // ---- Walk the folders.
    std::vector<fs::path> dirs;   // every folder searched, roots included
    std::vector<fs::path> files;  // PDFs found
    auto isPdf = [](const fs::path& p) { return key(utf8FromPath(p.extension())) == key(".pdf"); };
    for (const fs::path& root : roots) {
        dirs.push_back(root);
        if (options.includeSubfolders) {
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_directory(ec)) dirs.push_back(it->path());
                else if (it->is_regular_file(ec) && isPdf(it->path())) files.push_back(it->path());
            }
        } else {
            for (const auto& e : fs::directory_iterator(root, ec))
                if (e.is_regular_file(ec) && isPdf(e.path())) files.push_back(e.path());
        }
    }
    // Parents before children, so renamed parents are applied to children's paths.
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        const auto da = std::distance(a.begin(), a.end()), db = std::distance(b.begin(), b.end());
        return da != db ? da < db : a < b;
    });
    std::sort(files.begin(), files.end());

    std::unordered_set<std::string> claimed;            // destinations handed out
    std::vector<std::pair<fs::path, fs::path>> renamed;  // folder renames: old -> new

    // Where a path will be once the folder renames have happened.
    auto mapped = [&](const fs::path& p) {
        fs::path out = p;
        for (const auto& [from, to] : renamed) {
            if (out == from) { out = to; continue; }
            const fs::path rel = out.lexically_relative(from);
            if (!rel.empty() && *rel.begin() != "..") out = to / rel;
        }
        return out;
    };
    auto tooLong = [&](const fs::path& p) { return utf16Length(utf8FromPath(p)) > options.maxPathLength; };

    // ---- 1. Account folders.
    if (options.renameFolders) {
        for (const fs::path& dir : dirs) {
            const auto found = folderNames.find(caseFoldKey(dir.filename()));
            if (found == folderNames.end()) continue;
            const std::string wanted = accountFolderLabel(*found->second);
            if (utf8FromPath(dir.filename()) == wanted) continue;

            PlannedMove move;
            move.source = mapped(dir);
            move.destination = move.source.parent_path() / pathFromUtf8(wanted);
            move.message = kFolderRenameTag;

            // Another folder already has the new name: leave this one alone.
            const bool takenOnDisk = fs::exists(dir.parent_path() / pathFromUtf8(wanted), ec) &&
                                     !sameFile(dir.parent_path() / pathFromUtf8(wanted), dir);
            if (takenOnDisk || claimed.count(caseFoldKey(move.destination))) {
                move.status = MoveStatus::Collision;
                plan.moves.push_back(std::move(move));
                continue;
            }
            if (tooLong(move.destination)) {
                move.status = MoveStatus::Error;
                plan.moves.push_back(std::move(move));
                continue;
            }
            move.status = MoveStatus::Ready;
            claimed.insert(caseFoldKey(move.destination));
            renamed.push_back({move.source, move.destination});
            plan.moves.push_back(std::move(move));
        }
    }

    // ---- 2. File names: "<date> <old label>[ (n)].pdf" -> "<date> <current label>.pdf".
    static const std::regex datePrefix(
        R"(^(\d{4}\.(?:Q[1-4]|\d{2}\.\d{2}(?: - \d{4}\.\d{2}\.\d{2})?)) (.+)$)");
    static const std::regex numberSuffix(R"(^(.*) \((\d+)\)$)");

    for (const fs::path& file : files) {
        const std::string stem = utf8FromPath(file.stem());
        const std::string ext = utf8FromPath(file.extension());
        std::smatch m;
        if (!std::regex_match(stem, m, datePrefix)) continue;

        const std::string date = m[1];
        std::string rest = m[2];
        auto found = fileLabels.find(key(rest));
        if (found == fileLabels.end()) {
            std::smatch s;
            if (std::regex_match(rest, s, numberSuffix)) {
                rest = s[1];  // "label (2)" -> "label"
                found = fileLabels.find(key(rest));
            }
        }
        if (found == fileLabels.end()) continue;

        const Account& a = *found->second.account;
        // Keep a number the account still has; one it no longer has -> current.
        const std::string number = usesNumber(a, found->second.number) ? found->second.number : a.lastFour;
        const std::string label = accountLabel(a, key(number) == key(a.lastFour) ? std::string() : number);
        if (label == rest) continue;  // already current

        // Same folder; first free name. Names in a renamed folder are the same
        // names that are in it now, so check the folder as it is on disk.
        const fs::path source = mapped(file);
        const std::string newStem = date + ' ' + label;
        fs::path dest;
        bool free = false;
        for (int n = 1; n < 1000 && !free; ++n) {
            const std::string name = n == 1 ? newStem + ext : newStem + " (" + std::to_string(n) + ")" + ext;
            dest = source.parent_path() / pathFromUtf8(name);
            const fs::path onDiskNow = file.parent_path() / pathFromUtf8(name);
            free = !claimed.count(caseFoldKey(dest)) &&
                   !(fs::exists(onDiskNow, ec) && !sameFile(onDiskNow, file));
        }

        PlannedMove move;
        move.source = source;
        move.destination = dest;
        if (!free) {
            move.status = MoveStatus::Collision;
        } else if (tooLong(dest)) {
            move.status = MoveStatus::Error;
            move.message = "Full path is longer than Windows allows.";
        } else {
            move.status = MoveStatus::Ready;
            claimed.insert(caseFoldKey(dest));
        }
        plan.moves.push_back(std::move(move));
    }
    return plan;
}

}  // namespace finrenamer
