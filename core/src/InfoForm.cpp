#include "finrenamer/InfoForm.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "finrenamer/Utf8Path.h"
#include "finrenamer/VacationSheet.h"  // normalizeHeader

namespace fs = std::filesystem;

namespace finrenamer {

// ---- reading the PDF ------------------------------------------------------------------------------

std::map<std::string, std::string> readPdfFormFields(const fs::path& pdf, const std::string& password)
{
    std::error_code ec;
    if (!fs::exists(pdf, ec)) throw std::runtime_error("The file was not found:\n" + utf8FromPath(pdf));
    std::map<std::string, std::string> out;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(utf8FromPath(pdf).c_str(), password.empty() ? nullptr : password.c_str());
        QPDFAcroFormDocumentHelper form(q);
        for (QPDFFormFieldObjectHelper& f : form.getFormFields()) {
            const std::string name = f.getFullyQualifiedName();
            if (name.empty()) continue;
            const QPDFObjectHandle v = f.getValue();
            std::string value = v.isName() ? v.getName() : f.getValueAsString();  // radio buttons / check boxes: a name
            if (value == "/Off") value.clear();
            out[name] = value;
        }
    } catch (const QPDFExc& e) {
        if (e.getErrorCode() == qpdf_e_password)
            throw std::runtime_error("This PDF is protected with a password. Unlock it first (PDF Unlocker).");
        throw std::runtime_error("Couldn't read the PDF's form:\n" + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("Couldn't read the PDF's form:\n" + std::string(e.what()));
    }
    return out;
}

// ---- the firm's Information Form ------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

class Fields {
public:
    explicit Fields(const std::map<std::string, std::string>& raw) : exact_(raw)
    {
        for (const auto& [k, v] : raw) values_.emplace(normalizeHeader(k), trim(v));
    }
    // The value of a field by its PDF name. The exact name first: the form has names differing only in
    // capitals ("Phone numbers Cell" for the client, "Phone Numbers Cell" for the other party). Then the
    // name compared without case/spaces/punctuation. Counts it as found.
    std::string get(const std::string& name)
    {
        if (const auto e = exact_.find(name); e != exact_.end()) {
            ++found_;
            return trim(e->second);
        }
        const auto it = values_.find(normalizeHeader(name));
        if (it == values_.end()) return {};
        ++found_;
        return it->second;
    }
    // A radio group: the label of the chosen button ("/Choice1" -> labels[0], ...).
    std::string choice(const std::string& name, std::initializer_list<const char*> labels)
    {
        const std::string v = get(name);
        std::size_t i = 0;
        for (const char* l : labels) {
            ++i;
            if (v == "/Choice" + std::to_string(i) || lower(v) == "/" + lower(l) || lower(v) == lower(l)) return l;
        }
        return v.empty() || v[0] != '/' ? v : v.substr(1);
    }
    int found() const { return found_; }

private:
    std::map<std::string, std::string> exact_;
    std::map<std::string, std::string> values_;
    int found_ = 0;
};

std::string joinWords(std::initializer_list<std::string> parts, const char* sep = " ")
{
    std::string out;
    for (const auto& p : parts) {
        if (trim(p).empty()) continue;
        if (!out.empty()) out += sep;
        out += trim(p);
    }
    return out;
}

std::string phones(std::initializer_list<std::pair<const char*, std::string>> numbers)
{
    std::vector<std::pair<std::string, std::string>> given;
    for (const auto& [label, n] : numbers)
        if (!trim(n).empty()) given.push_back({label, trim(n)});
    if (given.size() == 1) return given[0].second;
    std::string out;
    for (const auto& [label, n] : given) out += (out.empty() ? "" : "; ") + label + ": " + n;
    return out;
}

std::string license(const std::string& number, const std::string& state)
{
    if (trim(number).empty()) return {};
    return trim(state).empty() ? trim(number) : trim(number) + " (" + trim(state) + ")";
}

}  // namespace

InfoFormData parseInfoForm(const std::map<std::string, std::string>& raw)
{
    Fields f(raw);
    InfoFormData d;
    auto extra = [&](const std::string& label, const std::string& value) {
        if (!trim(value).empty()) d.extras.push_back({label, trim(value)});
    };

    // Part 1-3: the client.
    CaseParty& c = d.client;
    c.ourClient = true;
    c.name = f.get("YOUR FULL NAME");
    extra("Client's maiden name", f.get("MAIDEN NAME"));
    c.dob = f.get("Date of Birth");
    c.pob = f.get("Place of Birth");
    c.ssn = f.get("Social Security");
    c.driversLicense = license(f.get("Drivers License"), f.get("State"));
    c.address = f.get("Street Address");
    c.cityStateZip = f.get("CityStateZip");
    extra("Client's county of residence", f.get("County of Residence"));
    extra("Client's years in state", f.get("Years in State"));
    extra("Client's years in county", f.get("Years in County"));
    extra("Is this the client's mailing address", f.choice("Group2", {"Yes", "No"}));
    extra("Client's mailing address", joinWords({f.get("Street Address_2"), f.get("CityStateZip_2")}, ", "));
    c.phone = phones({{"Cell", f.get("Phone numbers Cell")}, {"Home", f.get("Home")}, {"Work", f.get("Work")}});
    extra("Client's best number to call", f.get("Best number to call"));
    extra("OK to leave messages", f.get("Yes/No"));
    c.email = f.get("Email");
    extra("Client's hair color", f.get("Hair color"));
    extra("Client's eye color", f.get("Eye color"));
    extra("Client's height", f.get("Height"));
    extra("Client's weight", f.get("Weight"));
    extra("Client's race", f.get("Race"));
    extra("Client's ethnicity", f.get("Ethnicity"));
    c.car = joinWords({f.get("Your vehicle Year"), f.get("Make"), f.get("Model")});
    c.employer = f.get("Employer");
    extra("Client's job title", f.get("Job Title"));
    extra("Client's employer's address", joinWords({f.get("Street Address_3"), f.get("CityStateZip_3")}, ", "));
    extra("Client's employer's phone", f.get("Telephone Number"));
    {
        const std::string salary = f.get("Gross Salary");
        const std::string per = f.choice("Group11", {"Monthly", "Annually"});
        extra("Client's gross salary", salary.empty() ? salary : joinWords({salary, per.empty() ? "" : "(" + lower(per) + ")"}));
    }
    extra("Client's length of employment", f.get("Length of employment"));
    extra("Client's highest level of education", f.get("Highest level of education"));
    c.nameChange = joinWords({f.get("First"), f.get("Middle"), f.get("Last")});

    // Part 4-7: the opposing party.
    CaseParty& o = d.opposing;
    o.name = f.get("Full Name");
    extra("Opposing party's maiden name", f.get("Maiden Name"));
    o.dob = f.get("Date of Birth_2");
    o.pob = f.get("Place of Birth_2");
    o.ssn = f.get("Social Security_2");
    o.driversLicense = license(f.get("Drivers License_2"), f.get("State_2"));
    o.address = f.get("Street Address_4");
    o.cityStateZip = f.get("CityStateZip_4");
    extra("Opposing party's county of residence", f.get("County of Residence_2"));
    extra("Opposing party's years in state", f.get("Years in State_2"));
    extra("Opposing party's years in county", f.get("Years in County_2"));
    o.phone = phones({{"Cell", f.get("Phone Numbers Cell")}, {"Home", f.get("Home_2")}, {"Work", f.get("Work_2")}});
    o.email = f.get("Email_2");
    o.employer = f.get("Employer_2");
    extra("Opposing party's job title", f.get("Job Title_2"));
    extra("Opposing party's employer's address", joinWords({f.get("Street Address_5"), f.get("CityStateZip_5")}, ", "));
    extra("Opposing party's employer's phone", f.get("Telephone Number_2"));
    {
        const std::string salary = f.get("Gross Salary_2");
        const std::string per = f.choice("Group12", {"Monthly", "Annually"});
        extra("Opposing party's gross salary", salary.empty() ? salary : joinWords({salary, per.empty() ? "" : "(" + lower(per) + ")"}));
    }
    extra("Opposing party's length of employment", f.get("Length of employment_2"));
    extra("Opposing party's highest level of education", f.get("Highest level of education_2"));
    extra("Opposing party's hair color", f.get("Hair color_2"));
    extra("Opposing party's eye color", f.get("Eye color_2"));
    extra("Opposing party's height", f.get("Height_2"));
    extra("Opposing party's weight", f.get("Weight_2"));
    extra("Opposing party's race", f.get("Race_2"));
    extra("Opposing party's ethnicity", f.get("Ethnicity_2"));
    o.car = joinWords({f.get("Automobile Year"), f.get("Make_2"), f.get("Model_2")});

    // Part 8-9.
    d.opposingAttorney = f.get("OPPOSING PARTYS ATTORNEY if known");
    d.marriageDate = f.get("Date");
    d.marriagePlace = f.get("City State");
    extra("Separated", f.get("Yes/No_2"));
    d.separationDate = f.get("If separated date of separation");

    // Part 10: children (six rows; the PDF names rows 3-6 "...LastRowe3").
    for (int i = 1; i <= 6; ++i) {
        const std::string n = std::to_string(i);
        CaseChild k;
        k.name = f.get("Name First Middle Last Row" + n);
        if (k.name.empty()) k.name = f.get("Name First Middle Last Rowe" + n);
        k.sex = f.get("Sex MFRow" + n);
        k.dob = f.get("DOBRow" + n);
        k.pob = f.get("Place of Birth City County State Row" + n);
        k.ssn = f.get("SSNRow" + n);
        if (!k.name.empty() || !k.sex.empty() || !k.dob.empty() || !k.pob.empty() || !k.ssn.empty())
            d.children.push_back(k);
    }
    extra("Children's present residential address",
          joinWords({f.get("Childrens Present Residential Address 1"), f.get("Childrens Present Residential Address 2")}, ", "));
    extra("Do any of the children have serious disabilities",
          joinWords({f.get("Do any of the children have serious disabilities 1"),
                     f.get("Do any of the children have serious disabilities 2")}));
    extra("Property owned by the children", f.get("children's property"));

    // Part 12: children's health insurance.
    const std::string carrier = f.get("Person who carries insurance");
    d.insurance = carrier.empty() ? std::string() : "Carried by " + carrier;
    extra("Insurance through an employer", f.choice("Group14", {"Yes", "No"}));
    d.insuranceCarrier = f.get("Name of Insurance Company");
    d.policyNumber = f.get("Policy Number");
    d.groupNumber = f.get("Group Number");
    {
        std::string paid = f.choice("Group14a", {"Monthly", "Annual"});
        const std::string other = f.get("Other");
        if (!other.empty()) paid = joinWords({paid, "Other: " + other}, "; ");
        extra("Insurance premium paid", paid);
    }
    extra("Premium deducted from the provider's paycheck", f.choice("Group15", {"Yes", "No"}));

    d.fieldsFound = f.found();
    return d;
}

// ---- planning the import --------------------------------------------------------------------------------

namespace {

using PartyField = std::string CaseParty::*;
const std::vector<std::tuple<const char*, const char*, PartyField>>& partyFields()
{
    static const std::vector<std::tuple<const char*, const char*, PartyField>> fields = {
        {"name", "Name", &CaseParty::name},
        {"dob", "Date of birth", &CaseParty::dob},
        {"pob", "Place of birth", &CaseParty::pob},
        {"ssn", "SSN", &CaseParty::ssn},
        {"driversLicense", "Driver's license", &CaseParty::driversLicense},
        {"address", "Address", &CaseParty::address},
        {"cityStateZip", "City, State, ZIP", &CaseParty::cityStateZip},
        {"phone", "Phone", &CaseParty::phone},
        {"email", "Email", &CaseParty::email},
        {"employer", "Employer", &CaseParty::employer},
        {"car", "Car", &CaseParty::car},
        {"nameChange", "Name change to", &CaseParty::nameChange},
    };
    return fields;
}

using CaseField = std::string ClientCase::*;
const std::vector<std::tuple<const char*, const char*, CaseField>>& caseFields()
{
    static const std::vector<std::tuple<const char*, const char*, CaseField>> fields = {
        {"clientName", "Client name (Case List)", &ClientCase::clientName},
        {"marriageDate", "Date of marriage", &ClientCase::marriageDate},
        {"marriagePlace", "Place of marriage", &ClientCase::marriagePlace},
        {"separationDate", "Date of separation", &ClientCase::separationDate},
        {"insurance", "Insurance", &ClientCase::insurance},
        {"insuranceCarrier", "Insurance carrier", &ClientCase::insuranceCarrier},
        {"policyNumber", "Policy no.", &ClientCase::policyNumber},
        {"groupNumber", "Group no.", &ClientCase::groupNumber},
    };
    return fields;
}

using ChildField = std::string CaseChild::*;
const std::vector<std::tuple<const char*, const char*, ChildField>>& childFields()
{
    static const std::vector<std::tuple<const char*, const char*, ChildField>> fields = {
        {"sex", "Sex", &CaseChild::sex},
        {"dob", "Date of birth", &CaseChild::dob},
        {"pob", "Place of birth", &CaseChild::pob},
        {"ssn", "SSN", &CaseChild::ssn},
    };
    return fields;
}

bool sameText(const std::string& a, const std::string& b) { return lower(trim(a)) == lower(trim(b)); }

int findClient(const CaseFile& file, const CaseParty& form)
{
    for (std::size_t i = 0; i < file.parties.size(); ++i)
        if (file.parties[i].ourClient) return static_cast<int>(i);
    if (!trim(form.name).empty())
        for (std::size_t i = 0; i < file.parties.size(); ++i)
            if (sameText(file.parties[i].name, form.name)) return static_cast<int>(i);
    return -1;
}

int findOpposing(const CaseFile& file, const CaseParty& form, int client)
{
    if (!trim(form.name).empty())
        for (std::size_t i = 0; i < file.parties.size(); ++i)
            if (static_cast<int>(i) != client && sameText(file.parties[i].name, form.name)) return static_cast<int>(i);
    if (client >= 0)
        for (std::size_t i = 0; i < file.parties.size(); ++i)
            if (static_cast<int>(i) != client && !file.parties[i].ourClient &&
                file.parties[i].role != file.parties[static_cast<std::size_t>(client)].role)
                return static_cast<int>(i);
    return -1;
}

int findChild(const CaseFile& file, const CaseChild& form)
{
    if (trim(form.name).empty()) return -1;
    for (std::size_t i = 0; i < file.children.size(); ++i)
        if (sameText(file.children[i].name, form.name)) return static_cast<int>(i);
    return -1;
}

void addChange(std::vector<FormChange>& out, FormChange c)
{
    if (trim(c.value).empty() || sameText(c.value, c.current)) return;  // nothing new
    c.apply = trim(c.current).empty();
    out.push_back(std::move(c));
}

std::string childSummary(const CaseChild& k)
{
    return joinWords({k.name.empty() ? std::string("(no name)") : k.name, k.sex, k.dob.empty() ? "" : "born " + k.dob,
                      k.pob.empty() ? "" : "in " + k.pob, k.ssn.empty() ? "" : "SSN " + k.ssn},
                     ", ");
}

}  // namespace

std::vector<FormChange> planFormImport(const CaseFile& existing, const InfoFormData& form,
                                       const std::vector<Attorney>& directory, const std::string& notesHeading)
{
    std::vector<FormChange> out;
    using T = FormChange::Target;

    // The case.
    ClientCase formCase;
    formCase.clientName = form.client.name;
    formCase.marriageDate = form.marriageDate;
    formCase.marriagePlace = form.marriagePlace;
    formCase.separationDate = form.separationDate;
    formCase.insurance = form.insurance;
    formCase.insuranceCarrier = form.insuranceCarrier;
    formCase.policyNumber = form.policyNumber;
    formCase.groupNumber = form.groupNumber;
    for (const auto& [key, label, member] : caseFields()) {
        if (std::string(key) == "clientName" && !trim(existing.info.clientName).empty()) continue;  // keep the case's name
        addChange(out, {T::Case, "Case", label, key, -1, formCase.*member, existing.info.*member});
    }

    // The parties.
    const int client = findClient(existing, form.client);
    const int opposing = findOpposing(existing, form.opposing, client);
    for (const auto& [key, label, member] : partyFields())
        addChange(out, {T::Client, "Our client", label, key, -1, form.client.*member,
                        client >= 0 ? existing.parties[static_cast<std::size_t>(client)].*member : std::string()});
    for (const auto& [key, label, member] : partyFields())
        if (std::string(key) != "nameChange")
            addChange(out, {T::Opposing, "Opposing party", label, key, -1, form.opposing.*member,
                            opposing >= 0 ? existing.parties[static_cast<std::size_t>(opposing)].*member : std::string()});

    // The children.
    for (std::size_t i = 0; i < form.children.size(); ++i) {
        const CaseChild& k = form.children[i];
        const int at = findChild(existing, k);
        const std::string section = "Child: " + (k.name.empty() ? "(no name)" : k.name);
        if (at < 0) {
            FormChange add{T::Child, section, "Add this child", "add", static_cast<int>(i), childSummary(k), ""};
            add.apply = true;
            out.push_back(add);
            continue;
        }
        for (const auto& [key, label, member] : childFields())
            addChange(out, {T::Child, section, label, key, static_cast<int>(i), k.*member,
                            existing.children[static_cast<std::size_t>(at)].*member});
    }

    // The opposing party's attorney: only someone already in the Attorney Directory is added.
    std::vector<std::pair<std::string, std::string>> extras = form.extras;
    if (!trim(form.opposingAttorney).empty()) {
        const Attorney* match = nullptr;
        for (const Attorney& a : directory)
            if (sameText(a.name, form.opposingAttorney)) {
                if (match) {  // two attorneys of that name: don't guess
                    match = nullptr;
                    break;
                }
                match = &a;
            }
        const bool onCase = match && std::any_of(existing.attorneys.begin(), existing.attorneys.end(),
                                                 [&](const CaseAttorney& a) { return a.attorney.id == match->id; });
        if (match && !onCase) {
            FormChange a{T::Attorney, "Attorneys", "Add to the case (from the Attorney Directory)", "attorney", -1,
                         match->name, ""};
            a.attorneyId = match->id;
            a.apply = true;
            out.push_back(a);
        } else if (!match) {
            extras.insert(extras.begin(), {"Opposing party's attorney (not in the Attorney Directory)", form.opposingAttorney});
        }
    }

    // Everything else, as notes.
    if (!extras.empty()) {
        std::string block = notesHeading;
        for (const auto& [label, value] : extras) block += "\n" + label + ": " + value;
        FormChange n{T::Notes, "Notes", "Added to the case's notes", "notes", -1, block,
                     trim(existing.info.notes).empty() ? std::string() : "(goes after the notes already there)"};
        n.apply = true;
        out.push_back(n);
    }
    return out;
}

std::vector<std::int64_t> applyFormChanges(CaseFile& file, const InfoFormData& form,
                                           const std::vector<FormChange>& changes, PartyRole clientRole)
{
    using T = FormChange::Target;
    std::vector<std::int64_t> attorneys;
    int client = findClient(file, form.client);
    int opposing = findOpposing(file, form.opposing, client);
    const PartyRole otherRole = clientRole == PartyRole::Petitioner ? PartyRole::Respondent : PartyRole::Petitioner;
    auto ensureClient = [&]() -> CaseParty& {
        if (client < 0) {
            CaseParty p;
            p.caseId = file.info.id;
            p.role = clientRole;
            file.parties.push_back(p);
            client = static_cast<int>(file.parties.size()) - 1;
        }
        CaseParty& p = file.parties[static_cast<std::size_t>(client)];
        p.ourClient = true;
        return p;
    };
    auto ensureOpposing = [&]() -> CaseParty& {
        if (opposing < 0) {
            CaseParty p;
            p.caseId = file.info.id;
            p.role = client >= 0 ? (file.parties[static_cast<std::size_t>(client)].role == PartyRole::Petitioner
                                        ? PartyRole::Respondent
                                        : PartyRole::Petitioner)
                                 : otherRole;
            file.parties.push_back(p);
            opposing = static_cast<int>(file.parties.size()) - 1;
        }
        return file.parties[static_cast<std::size_t>(opposing)];
    };

    // The client first, so a new opposing party takes the other side.
    for (const FormChange& c : changes) {
        if (!c.apply || c.target != T::Client) continue;
        for (const auto& [key, label, member] : partyFields())
            if (c.key == key) ensureClient().*member = c.value;
    }
    for (const FormChange& c : changes) {
        if (!c.apply) continue;
        switch (c.target) {
        case T::Case:
            for (const auto& [key, label, member] : caseFields())
                if (c.key == key) file.info.*member = c.value;
            break;
        case T::Client:
            break;
        case T::Opposing:
            for (const auto& [key, label, member] : partyFields())
                if (c.key == key) ensureOpposing().*member = c.value;
            break;
        case T::Child: {
            if (c.child < 0 || c.child >= static_cast<int>(form.children.size())) break;
            const CaseChild& k = form.children[static_cast<std::size_t>(c.child)];
            if (c.key == "add") {
                CaseChild add = k;
                add.caseId = file.info.id;
                file.children.push_back(add);
                break;
            }
            const int at = findChild(file, k);
            if (at < 0) break;
            for (const auto& [key, label, member] : childFields())
                if (c.key == key) file.children[static_cast<std::size_t>(at)].*member = c.value;
            break;
        }
        case T::Notes:
            file.info.notes = trim(file.info.notes).empty() ? c.value : trim(file.info.notes) + "\n\n" + c.value;
            break;
        case T::Attorney:
            if (c.attorneyId) attorneys.push_back(c.attorneyId);
            break;
        }
    }
    return attorneys;
}

}  // namespace finrenamer
