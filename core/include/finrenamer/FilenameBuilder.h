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

// The label used in file names: "Chase Checking x1234 (John Smith; Jane Smith)".
// A combined statement: "Chase Checking x1111, Savings x2222 (H)".
// `number` picks one of the account's previous numbers instead of the current
// one (empty = current).
std::string accountLabel(const Account& account, std::string_view number = {});

// The account folder name: the current number, then any previous numbers:
// "Chase Credit Card 9012 (was x5678, x1234) (H)". Without previous numbers it
// is the same as accountLabel().
std::string accountFolderLabel(const Account& account);

// The label as this program wrote it before account numbers got their "x"
// ("Chase Checking 1234 (H)"). Only for recognising files and folders named
// that way; never used to name anything. Same as accountLabel() for a combined
// statement, whose numbers always had the x.
std::string legacyAccountLabel(const Account& account, std::string_view number = {}, bool forFolder = false);

// "2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf"
std::string buildFilename(const Account& account, const DateSpec& date,
                          std::string_view extension = ".pdf", std::string_view number = {});

// Institution, account type and last four are required; owners are optional
// (the parenthesised part is omitted when there are none). A combined
// statement needs at least two accounts, each with a type and a number.
std::optional<std::string> validateAccount(const Account& account);

}  // namespace finrenamer
