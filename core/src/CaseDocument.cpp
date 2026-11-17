#include "finrenamer/CaseDocument.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "finrenamer/DateSpec.h"
#include "finrenamer/VacationSheet.h"  // normalizeHeader
#include "finrenamer/WordTemplate.h"   // toLowerText

namespace finrenamer {

namespace {

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::string line;
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (!trim(line).empty()) out.push_back(trim(line));
            line.clear();
        } else {
            line += c;
        }
    }
    if (!trim(line).empty()) out.push_back(trim(line));
    return out;
}

void put(MergeRecord& r, std::initializer_list<const char*> names, const std::string& value)
{
    for (const char* n : names) r[normalizeHeader(n)] = trim(value);
}

std::string prefixed(const std::string& prefix, const char* suffix) { return prefix + "_" + suffix; }

// The details every person-like record has, under "<Prefix>_<Field>"; the name also as "<Prefix>".
void putContact(MergeRecord& r, const std::string& prefix, const std::string& nameKey, const std::string& role,
                const std::string& name, const std::string& firm, const std::string& bar, const std::string& address,
                const std::string& phone, const std::string& fax, const std::string& email)
{
    r[normalizeHeader(nameKey)] = trim(name);
    r[normalizeHeader(prefixed(prefix, "Name"))] = trim(name);
    r[normalizeHeader(prefixed(prefix, "Role"))] = trim(role);
    r[normalizeHeader(prefixed(prefix, "Firm"))] = trim(firm);
    r[normalizeHeader(prefixed(prefix, "BarNumber"))] = trim(bar);
    r[normalizeHeader(prefixed(prefix, "SBN"))] = trim(bar);
    r[normalizeHeader(prefixed(prefix, "Address"))] = trim(address);
    r[normalizeHeader(prefixed(prefix, "Street"))] = addressStreet(address);
    r[normalizeHeader(prefixed(prefix, "CityStateZip"))] = addressCityLine(address);
    r[normalizeHeader(prefixed(prefix, "Phone"))] = trim(phone);
    r[normalizeHeader(prefixed(prefix, "Fax"))] = trim(fax);
    r[normalizeHeader(prefixed(prefix, "Email"))] = trim(email);
}

}  // namespace

std::string courtDistrict(const std::string& court)
{
    std::string s = trim(court);
    if (s.empty()) return "____ Judicial District";
    static const std::string lead = "in the district court";
    if (s.size() >= lead.size() && toLowerText(s.substr(0, lead.size())) == lead) {
        std::size_t i = lead.size();
        while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == ';' || s[i] == '-' || s[i] == '\t')) ++i;
        s = trim(s.substr(i));
        if (s.empty()) return "____ Judicial District";
        // "of the 345th..." / "for the 345th..." after the lead: keep from the district
        for (const char* w : {"of the ", "for the "})
            if (toLowerText(s.substr(0, std::strlen(w))) == w) s = s.substr(std::strlen(w));
    }
    return s;
}

namespace {

std::string signOffOf(const Attorney& a) { return trim(a.signOff).empty() ? std::string("Sincerely,") : trim(a.signOff); }

void putAttorney(MergeRecord& r, const std::string& prefix, const std::string& nameKey, const std::string& role,
                 const CaseAttorney& a)
{
    putContact(r, prefix, nameKey, role, a.attorney.name, a.firm.name, a.attorney.barNumber, a.firm.address,
               a.firm.phone, a.firm.fax, serviceOrContactEmail(a.attorney));
    // _Email / _ServiceEmail: for signature blocks and service (service email, else contact email);
    // _ContactEmail: for letters (contact email, else service email). In the Players list _Email is
    // every address the attorney has, one per line (user's choice: no "contact"/"service" labels).
    const std::string contact = trim(a.attorney.email), service = trim(a.attorney.serviceEmail);
    r[normalizeHeader(prefixed(prefix, "ServiceEmail"))] = service.empty() ? contact : service;
    r[normalizeHeader(prefixed(prefix, "ContactEmail"))] = contact.empty() ? service : contact;
    if (prefix == "Player")
        r[normalizeHeader(prefixed(prefix, "Email"))] =
            contact.empty() || service.empty() || toLowerText(contact) == toLowerText(service)
                ? (contact.empty() ? service : contact)
                : contact + "\n" + service;
    r[normalizeHeader(prefixed(prefix, "SignOff"))] = signOffOf(a.attorney);
    r[normalizeHeader(prefixed(prefix, "Initials"))] = initialsOf(a.attorney.name);
}

void putParty(MergeRecord& r, const std::string& prefix, const CaseParty& p)
{
    r[normalizeHeader(prefix)] = trim(p.name);
    const std::string role = partyRoleName(p.role);
    const std::pair<const char*, std::string> fields[] = {
        {"Name", p.name},     {"Role", role},           {"Address", p.address}, {"CityStateZip", p.cityStateZip},
        {"Phone", p.phone},   {"DOB", p.dob},           {"POB", p.pob},         {"SSN", p.ssn},
        {"DriversLicense", p.driversLicense}, {"DL", p.driversLicense}, {"TxDL", p.driversLicense},
        {"Car", p.car},       {"Employer", p.employer}, {"Email", p.email},     {"NameChange", p.nameChange}};
    for (const auto& [suffix, value] : fields) r[normalizeHeader(prefixed(prefix, suffix))] = trim(value);
}

void putChild(MergeRecord& r, const CaseChild& c, std::chrono::year_month_day today)
{
    const std::string age = trim(c.age).empty() ? ageOn(c.dob, today) : trim(c.age);
    r["child"] = trim(c.name);
    const std::pair<const char*, std::string> fields[] = {{"Child_Name", c.name}, {"Child_Age", age},
                                                          {"Child_Sex", c.sex},   {"Child_DOB", c.dob},
                                                          {"Child_POB", c.pob},   {"Child_School", c.school},
                                                          {"Child_SSN", c.ssn}};
    for (const auto& [name, value] : fields) r[normalizeHeader(name)] = trim(value);
}

}  // namespace

std::string initialsOf(const std::string& name)
{
    std::string out;
    bool start = true;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (c == ' ' || c == '-' || c == '\t') {
            start = true;
        } else if (start) {
            if (std::isalpha(u)) out += static_cast<char>(std::toupper(u));
            start = false;
        }
    }
    return out;
}

std::string joinNames(const std::vector<std::string>& names)
{
    std::vector<std::string> n;
    for (const auto& s : names)
        if (!trim(s).empty()) n.push_back(trim(s));
    if (n.empty()) return {};
    if (n.size() == 1) return n[0];
    if (n.size() == 2) return n[0] + " and " + n[1];
    std::string out;
    for (std::size_t i = 0; i + 1 < n.size(); ++i) out += n[i] + ", ";
    return out + "and " + n.back();
}

std::string ageOn(const std::string& dob, std::chrono::year_month_day today)
{
    const auto born = parseUserDate(trim(dob));
    if (!born || *born > today) return {};
    int years = static_cast<int>(today.year()) - static_cast<int>(born->year());
    if (today.month() < born->month() || (today.month() == born->month() && today.day() < born->day())) --years;
    return years >= 0 ? std::to_string(years) : std::string();
}

std::string longDate(std::chrono::year_month_day d)
{
    static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
    const unsigned m = static_cast<unsigned>(d.month());
    if (m < 1 || m > 12) return {};
    return std::string(months[m - 1]) + " " + std::to_string(static_cast<unsigned>(d.day())) + ", " +
           std::to_string(static_cast<int>(d.year()));
}

std::string addressStreet(const std::string& address)
{
    const auto l = lines(address);
    if (l.size() < 2) return l.empty() ? std::string() : l[0];
    std::string out;
    for (std::size_t i = 0; i + 1 < l.size(); ++i) out += (i ? "\n" : "") + l[i];
    return out;
}

std::string addressCityLine(const std::string& address)
{
    const auto l = lines(address);
    return l.size() < 2 ? std::string() : l.back();
}

MergeData caseMergeData(const CaseFile& file, std::int64_t signerId, std::chrono::year_month_day today)
{
    MergeData d;
    MergeRecord& f = d.fields;
    const ClientCase& c = file.info;

    // ---- the case ----
    put(f, {"ClientName", "CaseName"}, c.clientName);
    put(f, {"CauseNumber", "CauseNo"}, c.causeNumber);
    put(f, {"County"}, c.county);
    put(f, {"Court"}, c.court);
    put(f, {"CourtDistrict"}, courtDistrict(c.court));
    put(f, {"CaseType", "Type"}, c.caseType);
    put(f, {"MarriageDate", "DateOfMarriage"}, c.marriageDate);
    put(f, {"MarriagePlace", "PlaceOfMarriage"}, c.marriagePlace);
    put(f, {"SeparationDate", "DateOfSeparation"}, c.separationDate);
    put(f, {"Insurance"}, c.insurance);
    put(f, {"InsuranceCarrier"}, c.insuranceCarrier);
    put(f, {"PolicyNumber"}, c.policyNumber);
    put(f, {"GroupNumber"}, c.groupNumber);
    put(f, {"Notes"}, c.notes);
    put(f, {"Today"}, longDate(today));
    put(f, {"Year"}, std::to_string(static_cast<int>(today.year())));

    // ---- parties ----
    std::vector<const CaseParty*> clients, opposing;
    for (const auto& p : file.parties)
        if (p.ourClient) clients.push_back(&p);
    for (const auto& p : file.parties)
        if (!p.ourClient && !clients.empty() && p.role != clients.front()->role) opposing.push_back(&p);
    auto names = [](const std::vector<const CaseParty*>& v) {
        std::vector<std::string> n;
        for (const auto* p : v) n.push_back(p->name);
        return joinNames(n);
    };
    std::vector<const CaseParty*> petitioners, respondents;
    for (const auto& p : file.parties) (p.role == PartyRole::Respondent ? respondents : petitioners).push_back(&p);
    put(f, {"Petitioner"}, names(petitioners));
    put(f, {"Respondent"}, names(respondents));
    if (!clients.empty()) putParty(f, "Client", *clients.front());
    if (!opposing.empty()) putParty(f, "OpposingParty", *opposing.front());
    put(f, {"Client"}, names(clients));
    put(f, {"ClientRole"}, clients.empty() ? std::string() : partyRoleName(clients.front()->role));
    put(f, {"OpposingParty"}, names(opposing));
    put(f, {"OpposingPartyRole"}, opposing.empty() ? std::string() : partyRoleName(opposing.front()->role));
    for (const auto* p : clients) {
        MergeRecord r;
        putParty(r, "Client", *p);
        r["clientrole"] = partyRoleName(p->role);
        d.groups["client"].push_back(r);
    }
    for (const auto* p : opposing) {
        MergeRecord r;
        putParty(r, "OpposingParty", *p);
        r["opposingpartyrole"] = partyRoleName(p->role);
        d.groups["opposingparty"].push_back(r);
    }
    for (const auto& p : file.parties) {
        MergeRecord r;
        putParty(r, "Party", p);
        d.groups["parties"].push_back(r);
    }
    d.groups.try_emplace("client");
    d.groups.try_emplace("opposingparty");
    d.groups.try_emplace("parties");

    // ---- children ----
    std::vector<std::string> childNames;
    auto& kids = d.groups["children"];
    for (const auto& k : file.children) {
        MergeRecord r;
        putChild(r, k, today);
        kids.push_back(r);
        childNames.push_back(k.name);
    }
    put(f, {"ChildrenNames", "Children"}, joinNames(childNames));
    put(f, {"ChildCount"}, std::to_string(file.children.size()));
    put(f, {"ChildOrChildren"}, file.children.empty() ? std::string() : file.children.size() == 1 ? "A CHILD" : "CHILDREN");
    d.groups["ifchildren"] = file.children.empty() ? std::vector<MergeRecord>{} : std::vector<MergeRecord>{MergeRecord{}};
    d.groups["ifnochildren"] = file.children.empty() ? std::vector<MergeRecord>{MergeRecord{}} : std::vector<MergeRecord>{};

    // ---- attorneys ----
    std::vector<const CaseAttorney*> ours, theirs;
    for (const auto& a : file.attorneys) (a.firm.ours ? ours : theirs).push_back(&a);
    // The signer goes first.
    const auto signer = std::find_if(ours.begin(), ours.end(), [&](const CaseAttorney* a) { return a->attorney.id == signerId; });
    if (signer != ours.end()) std::rotate(ours.begin(), signer, signer + 1);
    if (!ours.empty()) {
        putAttorney(f, "Our", "OurAttorney", "Attorney", *ours.front());
        put(f, {"SignOff"}, signOffOf(ours.front()->attorney));
        put(f, {"Initials"}, initialsOf(ours.front()->attorney.name));
        put(f, {"Signer"}, ours.front()->attorney.name);
    } else {
        put(f, {"SignOff"}, "Sincerely,");
    }
    for (const auto* a : ours) {
        MergeRecord r;
        putAttorney(r, "Our", "OurAttorney", "Attorney", *a);
        d.groups["our"].push_back(r);
    }
    d.groups.try_emplace("our");
    if (!theirs.empty()) putAttorney(f, "OPC", "OPC", "Opposing Counsel", *theirs.front());
    auto& opc = d.groups["opc"];
    auto& players = d.groups["players"];
    for (const auto* a : theirs) {
        MergeRecord r;
        putAttorney(r, "OPC", "OPC", "Opposing Counsel", *a);
        opc.push_back(r);
        MergeRecord pl;
        putAttorney(pl, "Player", "Player", "Opposing Counsel", *a);
        players.push_back(pl);
    }
    d.groups["ifopc"] = theirs.empty() ? std::vector<MergeRecord>{} : std::vector<MergeRecord>{MergeRecord{}};
    d.groups["ifnoopc"] = theirs.empty() ? std::vector<MergeRecord>{MergeRecord{}} : std::vector<MergeRecord>{};

    // ---- other people ----
    auto& others = d.groups["other"];
    for (const auto& o : file.contacts) {
        MergeRecord r;
        putContact(r, "Other", "Other", o.role, o.name, o.firm, o.barNumber, o.address, o.phone, o.fax, o.email);
        others.push_back(r);
        MergeRecord pl;
        putContact(pl, "Player", "Player", o.role, o.name, o.firm, o.barNumber, o.address, o.phone, o.fax, o.email);
        players.push_back(pl);
    }
    if (!file.contacts.empty()) {
        const auto& o = file.contacts.front();
        putContact(f, "Other", "Other", o.role, o.name, o.firm, o.barNumber, o.address, o.phone, o.fax, o.email);
    }
    return d;
}

std::vector<FieldHelp> caseFieldReference()
{
    std::vector<FieldHelp> out;
    auto add = [&](const std::string& section, const std::string& name, const std::string& what) {
        out.push_back({name, what, section});
    };
    const std::string kCase = "The case";
    add(kCase, "ClientName", "The case's client name (as in the Case List)");
    add(kCase, "CauseNumber", "Cause number");
    add(kCase, "County", "County (add \\* Upper to the field for capitals)");
    add(kCase, "Court", "Court, as typed for the caption");
    add(kCase, "CourtDistrict",
        "The court without a leading \"In the District Court\" (for a caption whose first line says that), "
        "e.g. 345th Judicial District; \"____ Judicial District\" when the case has no court");
    add(kCase, "CaseType", "Type");
    add(kCase, "MarriageDate", "Date of marriage");
    add(kCase, "MarriagePlace", "Place of marriage");
    add(kCase, "SeparationDate", "Date of separation");
    add(kCase, "Insurance", "Insurance");
    add(kCase, "InsuranceCarrier", "Insurance carrier");
    add(kCase, "PolicyNumber", "Policy number");
    add(kCase, "GroupNumber", "Group number");
    add(kCase, "Notes", "The case's notes");
    add(kCase, "Today", "Today's date, e.g. November 10, 2026");

    const std::string kParties = "Parties";
    add(kParties, "Petitioner", "Name(s) of the petitioner(s)");
    add(kParties, "Respondent", "Name(s) of the respondent(s)");
    add(kParties, "Client", "Name(s) of our client(s) (the party ticked \"Our client\")");
    add(kParties, "ClientRole", "Petitioner or Respondent (our client's side)");
    add(kParties, "OpposingParty", "Name(s) of the party on the other side");
    add(kParties, "OpposingPartyRole", "Petitioner or Respondent (the other side)");
    for (const char* who : {"Client", "OpposingParty"})
        for (const auto& [suffix, what] : std::vector<std::pair<std::string, std::string>>{
                 {"Address", "street address"}, {"CityStateZip", "city, state, ZIP"}, {"Phone", "phone"},
                 {"DOB", "date of birth"}, {"POB", "place of birth"}, {"SSN", "SSN"},
                 {"DriversLicense", "driver's license (also _DL or _TxDL)"}, {"Car", "car"},
                 {"Employer", "employer"}, {"Email", "email"}, {"NameChange", "name change to"}})
            add(kParties, std::string(who) + "_" + suffix,
                std::string(who == std::string("Client") ? "Our client's " : "The other party's ") + what);
    add(kParties, "Begin_Client ... End_Client", "Repeats for each of our clients (Client, Client_Address, ...)");
    add(kParties, "Begin_OpposingParty ... End_OpposingParty", "Repeats for each party on the other side");
    add(kParties, "Begin_Parties ... End_Parties", "Repeats for every party (Party, Party_Role, Party_Address, ...)");

    const std::string kKids = "Children";
    add(kKids, "ChildrenNames", "All the children's names: \"A\", \"A and B\", \"A, B, and C\"");
    add(kKids, "ChildOrChildren", "A CHILD or CHILDREN (blank if none)");
    add(kKids, "ChildCount", "How many children");
    add(kKids, "Begin_Children ... End_Children",
        "Repeats for each child: Child_Name, Child_Age (worked out from the date of birth if blank), Child_Sex, "
        "Child_DOB, Child_POB, Child_School, Child_SSN");
    add(kKids, "Begin_IfChildren ... End_IfChildren", "Shown only if the case has children");
    add(kKids, "Begin_IfNoChildren ... End_IfNoChildren", "Shown only if it has none");

    const std::string kOurs = "Our attorneys";
    add(kOurs, "OurAttorney", "The attorney who signs (chosen when the document is made)");
    add(kOurs, "OurBarNumber", "Their bar number (also OurSBN)");
    add(kOurs, "OurEmail", "Their email for signature blocks: the service email, or the contact email if none "
                           "(also OurServiceEmail)");
    add(kOurs, "OurContactEmail", "Their email for letters: the contact email, or the service email if none");
    add(kOurs, "OurFirm, OurAddress, OurStreet, OurCityStateZip, OurPhone, OurFax", "Our firm's details");
    add(kOurs, "SignOff", "Their sign-off (Attorney Directory); \"Sincerely,\" if blank");
    add(kOurs, "Initials", "Their initials, e.g. SAN");
    add(kOurs, "Signature", "Their signature picture (from Vacation Letters' Signatures list), if any");
    add(kOurs, "Begin_Our ... End_Our", "Repeats for each of our attorneys on the case (the signer first)");

    const std::string kOpc = "Opposing counsel";
    add(kOpc, "OPC", "Opposing counsel's name (the first one, outside a block)");
    add(kOpc, "OPC_BarNumber", "Bar number (also OPC_SBN)");
    add(kOpc, "OPC_Email", "Email for signature blocks and service: the service email, or the contact email if none "
                           "(also OPC_ServiceEmail)");
    add(kOpc, "OPC_ContactEmail", "Email for letters: the contact email, or the service email if none");
    add(kOpc, "OPC_Firm", "Firm name");
    add(kOpc, "OPC_Address", "The firm's whole address (all lines)");
    add(kOpc, "OPC_Street, OPC_CityStateZip", "The address without its last line / just its last line");
    add(kOpc, "OPC_Phone, OPC_Fax", "The firm's phone and fax");
    add(kOpc, "Begin_OPC ... End_OPC", "Repeats for each opposing attorney on the case");
    add(kOpc, "Begin_IfOPC ... End_IfOPC", "Shown only if there is opposing counsel (Begin_IfNoOPC for none, e.g. pro se)");

    const std::string kOther = "Other people and players";
    add(kOther, "Begin_Other ... End_Other",
        "Repeats for each other person (prior counsel, third party, expert...): Other, Other_Role, Other_Firm, "
        "Other_BarNumber, Other_Address, Other_Street, Other_CityStateZip, Other_Phone, Other_Fax, Other_Email");
    add(kOther, "Begin_Players ... End_Players",
        "Repeats for opposing counsel, then the other people: Player, Player_Role (\"Opposing Counsel\" or their "
        "role), Player_Firm, Player_BarNumber, Player_Address, Player_Street, Player_CityStateZip, Player_Phone, "
        "Player_Fax, Player_Email (an attorney's contact and service emails, one per line)");

    const std::string kHow = "How to use";
    add(kHow, "\\* Upper", "Add to a field for capitals: MERGEFIELD County \\* Upper (also \\* Lower, \\* Caps)");
    add(kHow, "\\b \"Fax: \"", "Text printed before the value, only when there is one (\\f for after)");
    add(kHow, "Blank lines", "A line holding only fields that are all blank is left out");
    return out;
}

}  // namespace finrenamer
