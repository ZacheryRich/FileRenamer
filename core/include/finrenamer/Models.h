#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace finrenamer {

// All strings in the core library are UTF-8.

struct ClientCase {
    std::int64_t id = 0;
    std::string clientName;
    std::string notes;
};

struct Person {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string fullName;
};

// An account as the renaming code sees it. The database layer resolves the
// account's linked Person rows into `owners`, already in display order.
struct Account {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string institution;   // "Chase"
    std::string accountType;   // free text: "Checking", "Roth IRA", ...
    std::string lastFour;      // "1234" -- never store the full number
    std::vector<std::string> owners;  // {"John Smith", "Jane Smith"}
};

}  // namespace finrenamer
