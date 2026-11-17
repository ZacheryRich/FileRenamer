#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace finrenamer {

// All strings in the core library are UTF-8.

struct ClientCase {
    std::int64_t id = 0;
    std::string clientName;
    std::string notes;
    // Court details (all optional).
    std::string causeNumber;
    std::string county;
    std::string court;      // free text as it should read in the caption
    // More case information (all optional; typed as it should print).
    std::string caseType;
    std::string marriageDate;
    std::string marriagePlace;
    std::string separationDate;
    std::string insurance;
    std::string insuranceCarrier;
    std::string policyNumber;
    std::string groupNumber;
};

// "Client name" if there is one, else "Cause No. ...", else "(unnamed case)": every case field is optional.
inline std::string caseDisplayName(const ClientCase& c)
{
    auto blank = [](const std::string& t) { return t.find_first_not_of(" \t\r\n") == std::string::npos; };
    if (!blank(c.clientName)) return c.clientName;
    if (!blank(c.causeNumber)) return "Cause No. " + c.causeNumber;
    return "(unnamed case)";
}

// A party to the case (separate from the People list, which is only for account owners).
enum class PartyRole { Petitioner, Respondent };

struct CaseParty {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string name;
    PartyRole role = PartyRole::Petitioner;
    // Everything below is optional and typed as it should print.
    bool ourClient = false;
    std::string address;
    std::string cityStateZip;
    std::string phone;
    std::string dob;
    std::string pob;
    std::string ssn;
    std::string driversLicense;
    std::string car;
    std::string employer;
    std::string email;
    std::string nameChange;  // "Name Change to:"
};

// A child of the case (all fields optional, typed as they should print).
struct CaseChild {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string name;
    std::string age;     // blank = worked out from the date of birth when a document is made
    std::string sex;
    std::string dob;
    std::string pob;
    std::string school;
    std::string ssn;
};

// Anyone else involved: prior counsel, a third party, an expert... (all optional).
struct CaseContact {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string role;    // e.g. "Prior Counsel", "Third Party", "Expert"
    std::string name;
    std::string firm;    // firm or organization
    std::string barNumber;
    std::string address; // may have line breaks
    std::string phone;
    std::string fax;
    std::string email;
};

inline const char* partyRoleName(PartyRole r) { return r == PartyRole::Respondent ? "Respondent" : "Petitioner"; }

struct Person {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string fullName;     // "John Smith" -- shown in the app
    std::string displayName;  // "H" -- what appears in filenames
};

// One account on a combined statement, in the order it appears in the name. Just typed
// text: it doesn't have to be an account saved in the case. Each has its own owners; the
// name groups accounts that have the same owners: "Chase Chk x1111, Sav x2222 (W), Chk x3333 (H)".
struct CombinedPart {
    std::string accountType;  // "Checking" (may be blank)
    std::string lastFour;     // "1111"
    // Display names, in name order -- what the name uses. Filled in by the database layer
    // (loadAccounts) from ownerIds; set directly when a part is built by hand.
    std::vector<std::string> owners;
    // Person ids, in name order -- what is stored (AccountRecord::parts).
    std::vector<std::int64_t> ownerIds;
};

// An account as the renaming code sees it. The database layer resolves the
// account's linked Person rows into `owners` (their display names), already
// in the order they should appear in filenames.
struct Account {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string institution;         // "Bank of America" -- shown in the app
    std::string institutionDisplay;  // "BofA" -- used in filenames; empty = use `institution`
    std::string accountType;   // free text: "Checking", "Roth IRA", ...
    std::string lastFour;      // "1234" -- never store the full number
    std::vector<std::string> owners;  // {"John Smith", "Jane Smith"}
    // Numbers this same account had before (a replaced card), newest first.
    // Shown in the account folder name as "(was x5678, x1234)"; a file can use
    // one of them instead of lastFour (see PlanInput::number).
    std::vector<std::string> previousLastFour;
    // Non-empty for a combined statement (one PDF covering several accounts at
    // the same institution): "Chase Checking x1111, Savings x2222 (H)". Then
    // accountType/lastFour/previousLastFour are empty, `institution`/
    // `institutionDisplay` are the statement's own and each part has its own
    // owners (`owners` is then just everyone on the statement; the name uses the parts' owners).
    std::vector<CombinedPart> combined;
    // Optional. When the account was opened / closed; the Deficiency List doesn't
    // expect statements before the opening month or after the closing month.
    std::optional<std::chrono::year_month_day> openedOn;
    std::optional<std::chrono::year_month_day> closedOn;

    bool isCombined() const { return !combined.empty(); }
};

// A name an account used to have, before an edit changed it. Lets the
// "Update File Names" tool recognise files and folders named the old way.
struct OldAccountName {
    std::int64_t accountId = 0;
    bool isFolder = false;   // account folder name, or the label part of file names
    std::string name;        // e.g. "Chsae Checking 1234 (H)"
    std::string lastFour;    // for file names: the number files with that name should
                             // now use (the same number, or its corrected text)
};

// The attorney directory: law firms (ours and opposing counsel) and the attorneys in them.
// Only the firm name and the attorney's name are required.
struct Firm {
    std::int64_t id = 0;
    std::string name;
    std::string phone;
    std::string fax;
    std::string address;   // may have line breaks
    bool ours = false;     // true = our firm, false = opposing counsel
};

struct Attorney {
    std::int64_t id = 0;
    std::int64_t firmId = 0;
    std::string name;
    std::string barNumber;
    std::string email;
    std::string signOff;  // how their letters close, e.g. "Very truly yours,"; blank = "Sincerely,"
};

}  // namespace finrenamer
