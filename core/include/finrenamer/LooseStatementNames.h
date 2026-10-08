#pragma once

#include <optional>
#include <string>
#include <vector>

#include "finrenamer/DateSpec.h"
#include "finrenamer/Models.h"

namespace finrenamer {

// A date found anywhere in a file name, read forgivingly: 2023.05.31, 2023-05-31,
// 20230531, 05-31-2023, 2023-05, 2023 Q2, May 2023, "May 31, 2023", or a period
// "2023.04.01 - 2023.06.30". A month alone (2023-05) counts as that month.
struct FoundDate {
    DateSpec date;
    std::size_t start = 0;  // where it sits in the text
    std::size_t end = 0;
};
std::optional<FoundDate> findLooseDate(const std::string& text);

// For statements named before this program, or by hand, in some other style
// ("Chase Checking x1234 May 2023", "2023-05 BofA 5678 statement"). A name
// matches an account when it has a readable date AND the account's last four
// digits AND the institution (its full name or its display name) or the account
// type, as whole words, in any order and letter case. Owners and everything
// else in the name are ignored. A combined statement needs the institution and
// all its accounts' numbers. If two accounts fit equally well, nothing matches
// (the file is left for the user to sort out; the program never guesses).
class LooseNameMatcher {
public:
    struct Match {
        const Account* account = nullptr;
        DateSpec date;
    };

    // `accounts` must outlive the matcher.
    explicit LooseNameMatcher(const std::vector<Account>& accounts);

    std::optional<Match> match(const std::string& stem) const;

private:
    struct Terms {
        const Account* account = nullptr;
        std::vector<std::vector<std::string>> institutions;  // each as words
        std::vector<std::string> type;                       // words
        std::vector<std::string> numbers;                    // lower-case
    };
    std::vector<Terms> terms_;
};

}  // namespace finrenamer
