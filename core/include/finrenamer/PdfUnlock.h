#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace finrenamer {

// PDF Unlocker: makes an unprotected copy of a PDF.
//   - Restrictions ("owner" protection: no printing / copying / editing ...) are removed; no
//     password is needed for that.
//   - A PDF that asks for a password to open needs that password (the one you were given).
//     This does NOT guess or crack passwords.
// The original is never changed, and nothing is ever overwritten.

enum class PdfLock {
    NotProtected,  // no encryption at all
    Restricted,    // opens without a password, but is encrypted (usually with restrictions)
    NeedsPassword, // can't be opened without a password (or the password given was wrong)
    Unreadable     // not a PDF, or too damaged to read
};

struct PdfInspection {
    PdfLock lock = PdfLock::Unreadable;
    std::vector<std::string> restrictions;  // what the PDF forbids, e.g. "Printing", "Copying"
    bool wrongPassword = false;             // lock == NeedsPassword and a password was tried
    std::string message;                    // for the user when Unreadable
};

// Looks at the file without changing anything. `password` is tried when given (it may be a user
// or owner password); an empty one means "none".
PdfInspection inspectPdf(const std::filesystem::path& pdf, const std::string& password = {});

enum class UnlockOutcome {
    Unlocked,       // the copy was written
    NotProtected,   // nothing to remove; no copy was made
    NeedsPassword,
    WrongPassword,
    Failed          // see message
};

struct UnlockResult {
    UnlockOutcome outcome = UnlockOutcome::Failed;
    std::string message;
    bool repaired = false;  // the PDF had small structural problems that were repaired in the copy
};

// Writes an unprotected copy of `source` to `destination`. `destination` must not exist (it is
// never overwritten) and must not be the source. Written through a temporary file, so a failure
// leaves nothing behind. Everything else in the PDF is kept.
UnlockResult unlockPdf(const std::filesystem::path& source, const std::filesystem::path& destination,
                       const std::string& password = {});

// A name in `folder` for the copy of `source`: its own file name, or "name (2).pdf", "name (3).pdf" ...
// if taken.
std::filesystem::path unlockedCopyPath(const std::filesystem::path& folder, const std::filesystem::path& source);

}  // namespace finrenamer
