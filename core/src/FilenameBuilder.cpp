#include "finrenamer/FilenameBuilder.h"

#include <algorithm>
#include <iterator>

namespace finrenamer {

std::string sanitizeComponent(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;

    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        char mapped = ch;

        if (c == '/' || c == '\\' || c == ':' || c == '|') mapped = '-';
        else if (c == '<' || c == '>' || c == '"' || c == '?' || c == '*') continue;
        else if (c < 0x20 || c == 0x7F) mapped = ' ';  // tabs, newlines, etc.

        if (mapped == ' ') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += mapped;
    }

    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out;
}

namespace {

std::string buildLabel(const Account& account, std::string_view number, bool withPrevious, bool xBeforeNumber = true)
{
    std::string label;
    auto append = [&label](std::string_view part) {
        const std::string clean = sanitizeComponent(part);
        if (clean.empty()) return;
        if (!label.empty()) label += ' ';
        label += clean;
    };

    append(account.institutionDisplay.empty() ? account.institution : account.institutionDisplay);
    if (account.isCombined()) {
        // "Chk x1111, Sav x2222 (W), Chk x3333 (H)": accounts with the same owners are grouped
        // (in the order each group first appears) and the owners follow the group.
        struct Group {
            std::vector<std::string> owners;
            std::string text;
        };
        auto cleanOwners = [](const std::vector<std::string>& names) {
            std::vector<std::string> out;
            for (const auto& n : names) {
                const std::string clean = sanitizeComponent(n);
                if (!clean.empty()) out.push_back(clean);
            }
            return out;
        };
        bool anyPartOwners = false;
        for (const CombinedPart& p : account.combined) anyPartOwners = anyPartOwners || !cleanOwners(p.owners).empty();

        std::vector<Group> groups;
        for (const CombinedPart& p : account.combined) {
            std::string part = sanitizeComponent(p.accountType);
            const std::string n = sanitizeComponent(p.lastFour);
            if (!n.empty()) part += (part.empty() ? "x" : " x") + n;
            if (part.empty()) continue;
            // A statement whose parts have no owners of their own uses the statement's owners.
            const auto owners = cleanOwners(anyPartOwners ? p.owners : account.owners);
            auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.owners == owners; });
            if (it == groups.end()) {
                groups.push_back({owners, {}});
                it = std::prev(groups.end());
            }
            if (!it->text.empty()) it->text += ", ";
            it->text += part;
        }
        std::string parts;
        for (const Group& g : groups) {
            if (!parts.empty()) parts += ", ";
            parts += g.text;
            std::string owners;
            for (const auto& o : g.owners) owners += (owners.empty() ? "" : "; ") + o;
            if (!owners.empty()) parts += " (" + owners + ')';
        }
        append(parts);
    } else {
        append(account.accountType);
        const std::string digits = sanitizeComponent(number.empty() ? std::string_view(account.lastFour) : number);
        append(!digits.empty() && xBeforeNumber ? "x" + digits : digits);
    }

    if (withPrevious && !account.isCombined()) {
        std::string was;
        for (const auto& n : account.previousLastFour) {
            const std::string clean = sanitizeComponent(n);
            if (clean.empty()) continue;
            was += was.empty() ? "(was x" : ", x";
            was += clean;
        }
        if (!was.empty()) append(was + ")");
    }

    std::string owners;
    for (const auto& owner : account.owners) {
        if (account.isCombined()) break;  // its owners are already in the name, per group
        const std::string clean = sanitizeComponent(owner);
        if (clean.empty()) continue;
        if (!owners.empty()) owners += "; ";
        owners += clean;
    }
    if (!owners.empty()) {
        if (!label.empty()) label += ' ';
        label += '(' + owners + ')';
    }
    return label;
}

}  // namespace

std::string accountLabel(const Account& account, std::string_view number)
{
    return buildLabel(account, number, false);
}

std::string accountFolderLabel(const Account& account)
{
    return buildLabel(account, {}, true);
}

std::string legacyAccountLabel(const Account& account, std::string_view number, bool forFolder)
{
    return buildLabel(account, number, forFolder, false);
}

std::string buildFilename(const Account& account, const DateSpec& date,
                          std::string_view extension, std::string_view number)
{
    return formatDateSpec(date) + ' ' + accountLabel(account, number) + std::string(extension);
}

std::optional<std::string> validateAccount(const Account& account)
{
    if (sanitizeComponent(account.institution).empty()) return "Account has no institution.";
    if (account.isCombined()) {
        if (account.combined.size() < 2) return "A combined statement needs at least two accounts.";
        for (const CombinedPart& p : account.combined)
            if (sanitizeComponent(p.lastFour).empty())  // the type may be blank
                return "An account on the combined statement has no number.";
        return std::nullopt;
    }
    // The account type is optional (it can be left blank when it isn't known).
    if (sanitizeComponent(account.lastFour).empty()) return "Account has no last four digits.";
    return std::nullopt;
}

}  // namespace finrenamer

namespace finrenamer {

bool isReservedWindowsName(std::string_view fileName)
{
    // Windows looks only at the part before the first dot, ignoring trailing spaces.
    std::string base(fileName.substr(0, fileName.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    for (char& c : base)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
    return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) &&
           base[3] >= '1' && base[3] <= '9';
}

}  // namespace finrenamer
