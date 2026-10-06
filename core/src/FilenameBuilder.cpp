#include "finrenamer/FilenameBuilder.h"

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

std::string buildLabel(const Account& account, std::string_view number, bool withPrevious)
{
    std::string label;
    auto append = [&label](std::string_view part) {
        const std::string clean = sanitizeComponent(part);
        if (clean.empty()) return;
        if (!label.empty()) label += ' ';
        label += clean;
    };

    append(account.institutionDisplay.empty() ? account.institution : account.institutionDisplay);
    append(account.accountType);
    append(number.empty() ? std::string_view(account.lastFour) : number);

    if (withPrevious) {
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

std::string buildFilename(const Account& account, const DateSpec& date,
                          std::string_view extension, std::string_view number)
{
    return formatDateSpec(date) + ' ' + accountLabel(account, number) + std::string(extension);
}

std::optional<std::string> validateAccount(const Account& account)
{
    if (sanitizeComponent(account.institution).empty()) return "Account has no institution.";
    if (sanitizeComponent(account.accountType).empty()) return "Account has no account type.";
    if (sanitizeComponent(account.lastFour).empty()) return "Account has no last four digits.";
    return std::nullopt;
}

}  // namespace finrenamer
