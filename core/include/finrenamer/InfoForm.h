#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "finrenamer/CaseDocument.h"  // CaseFile
#include "finrenamer/Models.h"

namespace finrenamer {

// The fillable fields of a PDF form: fully qualified field name -> value (text as typed; a check
// box or radio button gives its state name, e.g. "/Choice1", or "" / "/Off" when not set).
// Throws std::runtime_error with a message for the user if the file can't be read.
std::map<std::string, std::string> readPdfFormFields(const std::filesystem::path& pdf, const std::string& password = {});

// What the firm's client Information Form (Information Form Part I, fillable PDF) holds, in the
// case's terms. Every value may be blank.
struct InfoFormData {
    CaseParty client;    // name, DOB, POB, SSN, license, address, phone, email, employer, car, name change
    CaseParty opposing;
    std::string marriageDate, marriagePlace, separationDate;
    std::string insurance, insuranceCarrier, policyNumber, groupNumber;
    std::vector<CaseChild> children;
    std::string opposingAttorney;              // section 8, as typed
    std::vector<std::pair<std::string, std::string>> extras;  // everything else: label -> value, in form order
    int fieldsFound = 0;                        // how many of the form's known fields the PDF had
};

// Reads the form's fields (by the names the firm's PDF uses). `fieldsFound` == 0 means the PDF is
// not that form.
InfoFormData parseInfoForm(const std::map<std::string, std::string>& fields);

// One thing the import can put into the case, for the user to tick or untick.
struct FormChange {
    enum class Target { Case, Client, Opposing, Child, Notes, Attorney };
    Target target = Target::Case;
    std::string section;   // "Case", "Our client", "Opposing party", "Child 1", "Notes", "Attorneys"
    std::string label;     // "Date of marriage"
    std::string key;       // which field (see applyFormChanges); for children the field of the child
    int child = -1;        // index into InfoFormData::children
    std::string value;     // from the form
    std::string current;   // what the case has now
    bool apply = true;     // default: off when it would replace something different already there
    std::int64_t attorneyId = 0;  // Target::Attorney: the directory attorney matched by name
};

// Compares the form with the case (an empty CaseFile for a new case). Only values the form has are
// listed; ones equal to what the case already has are left out. The client is the party ticked
// "Our client" (else a party of the same name); the opposing party is a party of the client's other
// role (else of the same name); children match by name. `directory` is every attorney, to match
// the opposing attorney's name (exactly, ignoring case) -- otherwise that name goes in the notes.
std::vector<FormChange> planFormImport(const CaseFile& existing, const InfoFormData& form,
                                       const std::vector<Attorney>& directory, const std::string& notesHeading);

// Applies the ticked changes to `file` (parties and children are added when new: the client with
// `clientRole` and "Our client" ticked, the opposing party with the other role). Returns the
// attorneys to add to the case.
std::vector<std::int64_t> applyFormChanges(CaseFile& file, const InfoFormData& form,
                                           const std::vector<FormChange>& changes, PartyRole clientRole);

}  // namespace finrenamer
