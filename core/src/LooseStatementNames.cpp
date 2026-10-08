#include "finrenamer/LooseStatementNames.h"

#include <algorithm>
#include <cctype>

namespace finrenamer {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isSep(char c) { return c == '.' || c == '-' || c == '_' || c == '/' || c == ' '; }

struct Cursor {
    const std::string* ps;
    std::size_t i;
    Cursor(const std::string& text, std::size_t pos) : ps(&text), i(pos) {}
    bool digits(int n, int& out) {
        const std::string& s = *ps;
        if (i + static_cast<std::size_t>(n) > s.size()) return false;
        int v = 0;
        for (int k = 0; k < n; ++k) {
            const char c = s[i + static_cast<std::size_t>(k)];
            if (!isDigit(c)) return false;
            v = v * 10 + (c - '0');
        }
        i += static_cast<std::size_t>(n);
        out = v;
        return true;
    }
    bool sep() {
        const std::string& s = *ps;
        if (i < s.size() && isSep(s[i])) {
            ++i;
            return true;
        }
        return false;
    }
    bool atDigit() const { return i < ps->size() && isDigit((*ps)[i]); }
};

bool validYear(int y) { return y >= 1900 && y <= 2100; }

std::optional<std::chrono::year_month_day> ymd(int y, int m, int d)
{
    if (!validYear(y) || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    const auto date = makeDate(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    if (!date.ok()) return std::nullopt;
    return date;
}

// 2023.05.31 / 2023-05-31 / 20230531 (separators optional, mixed allowed)
std::optional<FoundDate> yearMonthDay(const std::string& s, std::size_t at)
{
    Cursor c{s, at};
    int y, m, d;
    if (!c.digits(4, y)) return std::nullopt;
    c.sep();
    if (!c.digits(2, m)) return std::nullopt;
    c.sep();
    if (!c.digits(2, d) || c.atDigit()) return std::nullopt;
    const auto first = ymd(y, m, d);
    if (!first) return std::nullopt;
    FoundDate found{SingleDate{*first}, at, c.i};

    // "2023.04.01 - 2023.06.30"
    Cursor p{s, c.i};
    while (p.i < s.size() && s[p.i] == ' ') ++p.i;
    if (p.i < s.size() && s[p.i] == '-') {
        ++p.i;
        while (p.i < s.size() && s[p.i] == ' ') ++p.i;
        int y2, m2, d2;
        if (p.digits(4, y2)) {
            p.sep();
            if (p.digits(2, m2)) {
                p.sep();
                if (p.digits(2, d2) && !p.atDigit()) {
                    const auto second = ymd(y2, m2, d2);
                    if (second && *second >= *first) {
                        found.date = Period{*first, *second};
                        found.end = p.i;
                    }
                }
            }
        }
    }
    return found;
}

// 05-31-2023 (US order; separators required and the same kind)
std::optional<FoundDate> monthDayYear(const std::string& s, std::size_t at)
{
    Cursor c{s, at};
    int m, d, y;
    auto oneOrTwo = [&](int& out) {
        Cursor t = c;
        if (t.digits(2, out)) {
            c = t;
            return true;
        }
        t = c;
        if (t.digits(1, out)) {
            c = t;
            return true;
        }
        return false;
    };
    if (!oneOrTwo(m)) return std::nullopt;
    if (c.i >= s.size() || !isSep(s[c.i]) || s[c.i] == ' ') return std::nullopt;
    const char sepChar = s[c.i++];
    if (!oneOrTwo(d)) return std::nullopt;
    if (c.i >= s.size() || s[c.i] != sepChar) return std::nullopt;
    ++c.i;
    if (!c.digits(4, y) || c.atDigit()) return std::nullopt;
    const auto date = ymd(y, m, d);
    if (!date) return std::nullopt;
    return FoundDate{SingleDate{*date}, at, c.i};
}

// 2023 Q2, 2023.Q2, 2023Q2
std::optional<FoundDate> quarter(const std::string& s, std::size_t at)
{
    Cursor c{s, at};
    int y, q;
    if (!c.digits(4, y) || !validYear(y)) return std::nullopt;
    c.sep();
    if (c.i >= s.size() || (s[c.i] != 'Q' && s[c.i] != 'q')) return std::nullopt;
    ++c.i;
    if (!c.digits(1, q) || q < 1 || q > 4) return std::nullopt;
    if (c.i < s.size() && (isDigit(s[c.i]) || isAlpha(s[c.i]))) return std::nullopt;
    return FoundDate{Quarter{y, q}, at, c.i};
}

// 2023-05 (a month alone)
std::optional<FoundDate> yearMonth(const std::string& s, std::size_t at)
{
    Cursor c{s, at};
    int y, m;
    if (!c.digits(4, y) || !validYear(y)) return std::nullopt;
    if (!c.sep() || !c.digits(2, m) || m < 1 || m > 12) return std::nullopt;
    if (c.atDigit()) return std::nullopt;
    // "2023-05-" followed by a digit is a day we couldn't read: not a month alone.
    if (c.i + 1 < s.size() && isSep(s[c.i]) && isDigit(s[c.i + 1])) return std::nullopt;
    const auto date = ymd(y, m, 1);
    if (!date) return std::nullopt;
    return FoundDate{SingleDate{*date}, at, c.i};
}

int monthNumber(std::string word)
{
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char ch) { return std::tolower(ch); });
    static const char* const full[12] = {"january", "february", "march",     "april",   "may",      "june",
                                         "july",    "august",   "september", "october", "november", "december"};
    static const char* const abbr[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                         "jul", "aug", "sep", "oct", "nov", "dec"};
    for (int m = 0; m < 12; ++m)
        if (word == full[m] || word == abbr[m]) return m + 1;
    if (word == "sept") return 9;
    return 0;
}

// "May 2023", "Sept. 2023", "May 31, 2023", "May 31st 2023"
std::optional<FoundDate> monthName(const std::string& s, std::size_t at)
{
    std::size_t i = at;
    while (i < s.size() && isAlpha(s[i])) ++i;
    const int month = monthNumber(s.substr(at, i - at));
    if (!month) return std::nullopt;
    Cursor c{s, i};
    if (c.i < s.size() && s[c.i] == '.') ++c.i;
    bool any = false;
    while (c.i < s.size() && (isSep(s[c.i]) || s[c.i] == ',')) {
        ++c.i;
        any = true;
    }
    if (!any) return std::nullopt;

    int day = 1, year;
    Cursor t = c;
    int d;
    if ((t.digits(2, d) || t.digits(1, d)) && !t.atDigit()) {
        // optional ordinal suffix, then separators, then a 4 digit year
        if (t.i + 1 < s.size() && isAlpha(s[t.i]) && isAlpha(s[t.i + 1])) t.i += 2;
        std::size_t j = t.i;
        while (j < s.size() && (isSep(s[j]) || s[j] == ',')) ++j;
        Cursor y{s, j};
        int yy;
        if (j > t.i && y.digits(4, yy) && !y.atDigit() && validYear(yy) && ymd(yy, month, d)) {
            return FoundDate{SingleDate{*ymd(yy, month, d)}, at, y.i};
        }
    }
    if (!c.digits(4, year) || c.atDigit() || !validYear(year)) return std::nullopt;
    return FoundDate{SingleDate{*ymd(year, month, day)}, at, c.i};
}

// ---- words ----

std::vector<std::string> words(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (isDigit(ch) || isAlpha(ch) || u >= 0x80) {
            cur += (u < 0x80) ? static_cast<char>(std::tolower(u)) : ch;
        } else if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool containsRun(const std::vector<std::string>& hay, const std::vector<std::string>& needle)
{
    if (needle.empty() || needle.size() > hay.size()) return false;
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

// "1234", "x1234", "xxxx1234" (a word that is the number, with only x's before it)
bool hasNumber(const std::vector<std::string>& hay, const std::string& number)
{
    if (number.empty()) return false;
    for (const std::string& w : hay) {
        if (w == number) return true;
        if (w.size() > number.size() && w.compare(w.size() - number.size(), number.size(), number) == 0 &&
            std::all_of(w.begin(), w.end() - static_cast<std::ptrdiff_t>(number.size()),
                        [](char ch) { return ch == 'x'; }))
            return true;
    }
    return false;
}

std::string lower(std::string t)
{
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return t;
}

}  // namespace

std::optional<FoundDate> findLooseDate(const std::string& text)
{
    std::optional<FoundDate> best;
    auto consider = [&](std::optional<FoundDate> found) {
        if (!found) return;
        if (!best || found->start < best->start || (found->start == best->start && found->end > best->end))
            best = std::move(found);
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool boundary = i == 0 || !(isDigit(text[i - 1]) || isAlpha(text[i - 1]));
        if (isDigit(text[i]) && (i == 0 || !isDigit(text[i - 1]))) {
            consider(yearMonthDay(text, i));
            consider(quarter(text, i));
            consider(monthDayYear(text, i));
            consider(yearMonth(text, i));
        } else if (isAlpha(text[i]) && boundary) {
            consider(monthName(text, i));
        }
        if (best && best->start <= i) break;  // can't get earlier
    }
    return best;
}

LooseNameMatcher::LooseNameMatcher(const std::vector<Account>& accounts)
{
    for (const Account& a : accounts) {
        Terms t;
        t.account = &a;
        for (const std::string& name : {a.institution, a.institutionDisplay}) {
            auto w = words(name);
            if (!w.empty() && std::find(t.institutions.begin(), t.institutions.end(), w) == t.institutions.end())
                t.institutions.push_back(std::move(w));
        }
        if (a.isCombined()) {
            for (const CombinedPart& part : a.combined) t.numbers.push_back(lower(part.lastFour));
        } else {
            t.type = words(a.accountType);
            t.numbers.push_back(lower(a.lastFour));
            for (const auto& n : a.previousLastFour) t.numbers.push_back(lower(n));
        }
        t.numbers.erase(std::remove(t.numbers.begin(), t.numbers.end(), std::string()), t.numbers.end());
        if (!t.numbers.empty()) terms_.push_back(std::move(t));
    }
}

std::optional<LooseNameMatcher::Match> LooseNameMatcher::match(const std::string& stem) const
{
    const auto found = findLooseDate(stem);
    if (!found) return std::nullopt;
    const auto hay = words(stem.substr(0, found->start) + " " + stem.substr(found->end));

    auto institutionFits = [&](const Terms& t) {
        for (const auto& inst : t.institutions)
            if (containsRun(hay, inst)) return true;
        return false;
    };

    // A combined statement needs the institution and every one of its numbers.
    const Terms* combined = nullptr;
    bool combinedTie = false;
    const Terms* single = nullptr;
    int singleScore = 0;
    bool singleTie = false;

    for (const Terms& t : terms_) {
        if (t.account->isCombined()) {
            if (!institutionFits(t)) continue;
            if (!std::all_of(t.numbers.begin(), t.numbers.end(), [&](const std::string& n) { return hasNumber(hay, n); }))
                continue;
            if (combined) combinedTie = true;
            combined = &t;
            continue;
        }
        const bool number = std::any_of(t.numbers.begin(), t.numbers.end(), [&](const std::string& n) { return hasNumber(hay, n); });
        if (!number) continue;
        const bool inst = institutionFits(t);
        const bool type = containsRun(hay, t.type);
        if (!inst && !type) continue;
        const int score = 1 + (inst ? 2 : 0) + (type ? 2 : 0);
        if (score > singleScore) {
            single = &t;
            singleScore = score;
            singleTie = false;
        } else if (score == singleScore) {
            singleTie = true;
        }
    }

    if (combined) {
        if (combinedTie) return std::nullopt;
        return Match{combined->account, found->date};
    }
    if (!single || singleTie) return std::nullopt;
    return Match{single->account, found->date};
}

}  // namespace finrenamer
