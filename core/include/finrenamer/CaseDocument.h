#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "finrenamer/Models.h"
#include "finrenamer/WordTemplate.h"

namespace finrenamer {

// Everything known about a case, for filling a document template (see caseMergeData).
struct CaseAttorney {
    Attorney attorney;
    Firm firm;  // firm.ours: our attorney; otherwise opposing counsel
};

struct CaseFile {
    ClientCase info;
    std::vector<CaseParty> parties;
    std::vector<CaseChild> children;
    std::vector<CaseContact> contacts;     // other people: prior counsel, third parties, experts...
    std::vector<CaseAttorney> attorneys;   // ours and opposing, as on the case
};

// The fields and repeating groups a case document template can use (see caseFieldReference for
// the list). `signerId` is the attorney of ours who signs (0 = the first of ours): the Our...
// fields, SignOff and Initials are theirs, and they come first in the "Our" group.
// Every value may be blank; nothing is required.
MergeData caseMergeData(const CaseFile& file, std::int64_t signerId, std::chrono::year_month_day today);

// For the "Field Names" help: every field and group, with what it holds.
struct FieldHelp {
    std::string name;         // as to type it in Word, e.g. "OPC_BarNumber" or "Begin_OPC ... End_OPC"
    std::string description;
    std::string section;      // heading it is listed under
};
std::vector<FieldHelp> caseFieldReference();

// Helpers (public for tests).
std::string initialsOf(const std::string& name);                    // "Scott A. Nyitray" -> "SAN"
std::string joinNames(const std::vector<std::string>& names);        // "A", "A and B", "A, B, and C"
std::string ageOn(const std::string& dob, std::chrono::year_month_day today);  // "" if the date can't be read
// The court for the caption's second line under a fixed "IN THE DISTRICT COURT": the court as typed
// without a leading "In the District Court" ("In the District Court, 345th Judicial District" ->
// "345th Judicial District"); a court typed some other way is returned as typed (the user edits
// those few documents by hand). Blank -> "____ Judicial District", as on the firm's blank sheet.
std::string courtDistrict(const std::string& court);
std::string longDate(std::chrono::year_month_day d);                // "November 10, 2026"
// The last line of a multi-line address (city, state, ZIP) and the lines before it.
std::string addressStreet(const std::string& address);
std::string addressCityLine(const std::string& address);

}  // namespace finrenamer
