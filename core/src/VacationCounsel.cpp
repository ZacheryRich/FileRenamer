#include "finrenamer/VacationCounsel.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace finrenamer {

namespace {

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string key(const std::string& s)
{
    std::string k = trim(s);
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return k;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::string line;
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') {
            out.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    out.push_back(line);
    return out;
}

void appendLine(std::string& text, const std::string& line)
{
    const std::string t = trim(line);
    if (t.empty()) return;
    if (!text.empty()) text += '\n';
    text += t;
}

void appendBlock(std::string& text, const std::string& block)
{
    // An address may itself have several lines; blank lines inside it are dropped so that a
    // blank line only ever separates firms.
    for (const auto& l : lines(block)) appendLine(text, l);
}

const Firm* firmNamed(const std::vector<Firm>& firms, const std::string& name)
{
    const std::string k = key(name);
    if (k.empty()) return nullptr;
    for (const auto& f : firms)
        if (key(f.name) == k) return &f;
    return nullptr;
}

const Attorney* attorneyNamed(const std::vector<Attorney>& attorneys, std::int64_t firmId, const std::string& name)
{
    const std::string k = key(name);
    if (k.empty()) return nullptr;
    for (const auto& a : attorneys)
        if (a.firmId == firmId && key(a.name) == k) return &a;
    return nullptr;
}

}  // namespace

OpposingCounselText opposingCounselText(const std::vector<OpposingFirmChoice>& choices)
{
    OpposingCounselText out;
    std::set<std::string> seenEmails;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const auto& c = choices[i];
        auto addEmail = [&](const std::string& raw) {
            const std::string email = trim(raw);
            if (!email.empty() && seenEmails.insert(key(email)).second) {
                if (!out.emails.empty()) out.emails += "; ";
                out.emails += email;
            }
        };
        for (const auto& a : c.attorneys) addEmail(serviceOrContactEmail(a));
        if (i == 0) {
            for (const auto& a : c.attorneys) appendLine(out.names, a.name);
            out.firmName = trim(c.firm.name);
            appendBlock(out.address, c.firm.address);
            continue;
        }
        std::string block;
        for (const auto& a : c.attorneys) appendLine(block, a.name);
        appendLine(block, c.firm.name);
        appendBlock(block, c.firm.address);
        if (block.empty()) continue;
        if (!out.others.empty()) out.others += "\n\n";
        out.others += block;
    }
    return out;
}

OpposingCounselMatch matchOpposingCounsel(const std::string& names, const std::string& firmName,
                                          const std::string& others, const std::vector<Firm>& firms,
                                          const std::vector<Attorney>& attorneys)
{
    OpposingCounselMatch out;

    // The first firm: OPC Firm Name, with the OPC lines as its attorneys.
    const Firm* first = firmNamed(firms, firmName);
    if (!trim(firmName).empty() && !first) out.unmatched.push_back("OPC Firm Name: " + trim(firmName));
    if (first) out.choices.push_back({*first, {}});
    for (const auto& l : lines(names)) {
        if (trim(l).empty()) continue;
        const Attorney* a = first ? attorneyNamed(attorneys, first->id, l) : nullptr;
        if (a) out.choices.front().attorneys.push_back(*a);
        else out.unmatched.push_back("OPC: " + trim(l));
    }

    // Further firms: blocks of OPC 2 Address separated by blank lines. In each block the first
    // line that is a firm's name is the firm; the lines above it are its attorneys; the lines
    // below are its address (not compared: it may be typed differently).
    std::vector<std::vector<std::string>> blocks(1);
    for (const auto& l : lines(others)) {
        if (trim(l).empty()) {
            if (!blocks.back().empty()) blocks.emplace_back();
        } else {
            blocks.back().push_back(trim(l));
        }
    }
    for (const auto& block : blocks) {
        if (block.empty()) continue;
        std::size_t at = block.size();
        const Firm* firm = nullptr;
        for (std::size_t i = 0; i < block.size() && !firm; ++i) {
            firm = firmNamed(firms, block[i]);
            if (firm) at = i;
        }
        if (!firm) {
            out.unmatched.push_back("OPC 2 Address: " + block.front() + (block.size() > 1 ? " ..." : ""));
            continue;
        }
        OpposingFirmChoice choice{*firm, {}};
        for (std::size_t i = 0; i < at; ++i) {
            if (const Attorney* a = attorneyNamed(attorneys, firm->id, block[i])) choice.attorneys.push_back(*a);
            else out.unmatched.push_back("OPC 2 Address: " + block[i]);
        }
        out.choices.push_back(std::move(choice));
    }

    return out;
}

}  // namespace finrenamer
