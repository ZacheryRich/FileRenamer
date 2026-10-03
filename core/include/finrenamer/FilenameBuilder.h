#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "finrenamer/DateSpec.h"
#include "finrenamer/Models.h"

namespace finrenamer {

// Makes one piece of a name safe for Windows: "/ \ : |" become "-",
// "< > \" ? *" and control characters are removed, runs of whitespace are
// collapsed, and leading/trailing spaces and trailing dots are trimmed.
std::string sanitizeComponent(std::string_view text);

// "Chase Checking 1234 (John Smith; Jane Smith)"
// Also used as the account subfolder name.
std::string accountLabel(const Account& account);

// "2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf"
std::string buildFilename(const Account& account, const DateSpec& date,
                          std::string_view extension = ".pdf");

// Institution, account type and last four are required; owners are optional
// (the parenthesised part is omitted when there are none).
std::optional<std::string> validateAccount(const Account& account);

}  // namespace finrenamer
