#include "finrenamer/DocumentName.h"

#include <pugixml.hpp>
#include <sstream>

#include <algorithm>
#include <cctype>
#include <set>

#include "finrenamer/FilenameBuilder.h"

namespace finrenamer {

namespace {

bool blank(const std::string& s)
{
    return sanitizeComponent(s).empty();
}

std::string joinNeeds(const std::vector<std::string>& needs)
{
    std::string out = "Needs ";
    for (std::size_t i = 0; i < needs.size(); ++i) {
        if (i > 0) out += i + 1 == needs.size() ? " and " : ", ";
        out += needs[i];
    }
    return out;
}

std::string lower(std::string s)
{
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimSpaces(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

constexpr std::size_t kMaxFieldName = 40;

// ---- Pattern parsing ----

struct Item {
    bool isField = false;
    std::string text;  // fixed text, or the field name as written
};

struct Group {
    bool optional = false;
    std::vector<Item> items;
};

struct Parsed {
    std::vector<Group> groups;
    std::string error;
};

Parsed parsePattern(std::string_view p)
{
    Parsed out;
    Group current;
    std::string text;
    auto flushText = [&] {
        if (!text.empty()) current.items.push_back({false, text});
        text.clear();
    };
    auto closeGroup = [&] {
        flushText();
        if (!current.items.empty()) out.groups.push_back(std::move(current));
        current = Group{};
    };
    bool inGroup = false;

    for (std::size_t i = 0; i < p.size(); ++i) {
        const char c = p[i];
        if (c == '\\') {
            if (i + 1 >= p.size()) { out.error = "A \\ at the end must be followed by a character."; return out; }
            text += p[++i];
        } else if (c == '{') {
            const auto close = p.find('}', i + 1);
            const auto another = p.find('{', i + 1);
            if (close == std::string_view::npos || (another != std::string_view::npos && another < close)) {
                out.error = "A { has no matching }.";
                return out;
            }
            const std::string name = trimSpaces(p.substr(i + 1, close - i - 1));
            if (name.empty()) { out.error = "A { } has no field name in it."; return out; }
            if (name.size() > kMaxFieldName) { out.error = "A field name is too long: " + name; return out; }
            if (name.find_first_of("[]\\") != std::string::npos) {
                out.error = "A field name can't contain [ ] or \\: " + name;
                return out;
            }
            flushText();
            current.items.push_back({true, name});
            i = close;
        } else if (c == '}') {
            out.error = "A } has no matching {.";
            return out;
        } else if (c == '[') {
            if (inGroup) { out.error = "Optional parts [ ] can't be nested."; return out; }
            closeGroup();
            current.optional = true;
            inGroup = true;
        } else if (c == ']') {
            if (!inGroup) { out.error = "A ] has no matching [."; return out; }
            closeGroup();
            inGroup = false;
        } else {
            text += c;
        }
    }
    if (inGroup) { out.error = "A [ has no matching ]."; return out; }
    closeGroup();
    if (out.groups.empty()) out.error = "The pattern is empty.";
    return out;
}

std::string fieldValue(const DocumentInfo& info, const std::string& name)
{
    return sanitizeComponent(info.field(name));
}

// Groups to print: optional ones only when something in them is filled.
bool groupUsed(const DocumentInfo& info, const Group& g)
{
    if (!g.optional) return true;
    for (const Item& it : g.items)
        if (it.isField && !fieldValue(info, it.text).empty()) return true;
    return false;
}

std::string renderTemplate(const DocumentInfo& info)
{
    const Parsed parsed = parsePattern(info.tmpl.pattern);
    if (!parsed.error.empty()) return {};
    std::string out;
    for (const Group& g : parsed.groups) {
        if (!groupUsed(info, g)) continue;
        for (const Item& it : g.items) out += it.isField ? fieldValue(info, it.text) : it.text;
    }
    return sanitizeComponent(out);
}

}  // namespace

std::string fieldKey(std::string_view name)
{
    std::string out;
    bool space = false;
    for (char c : lower(trimSpaces(name))) {
        if (c == ' ' || c == '\t') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

std::optional<std::string> validatePattern(std::string_view pattern)
{
    const Parsed p = parsePattern(pattern);
    if (!p.error.empty()) return p.error;
    return std::nullopt;
}

std::optional<std::string> validateTemplate(const DocumentTemplate& t)
{
    if (sanitizeComponent(t.name).empty() || trimSpaces(t.name).empty()) return std::string("The template needs a name.");
    const std::string key = fieldKey(t.name);
    if (key == "statement" || key == "other") return std::string("\"" + trimSpaces(t.name) + "\" is already a built-in type; choose another name.");
    return validatePattern(t.pattern);
}

std::vector<TemplateField> templateFields(std::string_view pattern)
{
    const Parsed parsed = parsePattern(pattern);
    std::vector<TemplateField> out;
    if (!parsed.error.empty()) return out;
    for (const Group& g : parsed.groups)
        for (const Item& it : g.items) {
            if (!it.isField) continue;
            const std::string key = fieldKey(it.text);
            auto found = std::find_if(out.begin(), out.end(), [&](const TemplateField& f) { return fieldKey(f.name) == key; });
            if (found == out.end()) out.push_back({it.text, g.optional});
            else if (!g.optional) found->optional = false;
        }
    return out;
}

std::string sampleLabel(std::string_view pattern)
{
    DocumentInfo info;
    info.kind = DocKind::Template;
    info.tmpl.pattern = std::string(pattern);
    for (const TemplateField& f : templateFields(pattern)) info.setField(f.name, f.name);
    return renderTemplate(info);
}

std::vector<DocumentTemplate> exampleTemplates()
{
    return {
        {0, "Email", "Email from {Sender} to {Recipient}[ ({Description})]", false, true},
        {0, "Text", "Texts from {Sender} to {Recipient}[ ({Description})]", false, true},
        {0, "Text Conversation", "Texts between {Person 1} and {Person 2}[ ({Description})]", true, true},
        {0, "Group Text", "Texts with {People or group name}[ ({Description})]", true, true},
        {0, "Letter", "Ltr from {Sender}[ ({Description})]", false, true},
    };
}

std::string DocumentInfo::field(std::string_view name) const
{
    const auto it = fields.find(fieldKey(name));
    return it == fields.end() ? std::string() : it->second;
}

void DocumentInfo::setField(std::string_view name, std::string value)
{
    fields[fieldKey(name)] = std::move(value);
}

std::string documentLabel(const DocumentInfo& info)
{
    switch (info.kind) {
    case DocKind::Statement: return {};
    case DocKind::Template: return renderTemplate(info);
    case DocKind::Other: {
        std::string out = sanitizeComponent(info.label);
        const std::string description = sanitizeComponent(info.description);
        if (!description.empty()) out += " (" + description + ")";
        return sanitizeComponent(out);
    }
    }
    return {};
}

bool documentUsesDate(const DocumentInfo& info)
{
    switch (info.kind) {
    case DocKind::Statement: return true;
    case DocKind::Template: return info.tmpl.useDate;
    case DocKind::Other: return !info.noDate;
    }
    return true;
}

std::string buildDocumentFilename(const DocumentInfo& info, const std::optional<DateSpec>& date, std::string_view extension)
{
    const std::string label = documentLabel(info);
    if (!documentUsesDate(info) || !date) return label + std::string(extension);
    return formatDateSpec(*date) + " " + label + std::string(extension);
}

std::optional<std::string> documentMissing(const DocumentInfo& info, bool dateMissing)
{
    std::vector<std::string> needs;
    std::set<std::string> seen;
    switch (info.kind) {
    case DocKind::Statement: return std::string("Needs an account");
    case DocKind::Template: {
        const Parsed parsed = parsePattern(info.tmpl.pattern);
        if (!parsed.error.empty()) return "This template is not valid: " + parsed.error;
        for (const Group& g : parsed.groups) {
            // A required part needs all its boxes; an optional part only if it was started.
            if (!groupUsed(info, g)) continue;
            for (const Item& it : g.items)
                if (it.isField && blank(info.field(it.text)) && seen.insert(fieldKey(it.text)).second)
                    needs.push_back(lower(it.text));
        }
        break;
    }
    case DocKind::Other:
        if (blank(info.label)) needs.push_back("a label");
        break;
    }
    if (!documentUsesDate(info)) {
        // Nothing but the label names the file, so it can't be empty.
        if (needs.empty() && documentLabel(info).empty()) needs.push_back("a name");
    } else if (dateMissing) {
        needs.push_back("a date");
    }
    if (needs.empty()) return std::nullopt;
    return joinNeeds(needs);
}

DateModes allowedDateModes(const DocumentInfo& info)
{
    switch (info.kind) {
    case DocKind::Statement: return {true, true, true};
    case DocKind::Template: return {true, info.tmpl.allowPeriod, false};
    case DocKind::Other: return {true, true, false};
    }
    return {};
}

std::optional<std::string> validateDocumentDate(const DocumentInfo& info, const DateSpec& date)
{
    if (!documentUsesDate(info)) return std::nullopt;
    const DateModes modes = allowedDateModes(info);
    if (std::holds_alternative<Period>(date) && !modes.period) return std::string("This kind of file takes a single date, not a period.");
    if (std::holds_alternative<Quarter>(date) && !modes.quarter) return std::string("Only statements can use a quarter.");
    return std::nullopt;
}

void clearOptionalFields(DocumentInfo& info)
{
    if (info.kind == DocKind::Other) {
        info.description.clear();
        return;
    }
    if (info.kind != DocKind::Template) return;
    for (const TemplateField& f : templateFields(info.tmpl.pattern))
        if (f.optional) info.fields[fieldKey(f.name)].clear();
}

}  // namespace finrenamer

namespace finrenamer {

std::string templatesToXml(const std::vector<DocumentTemplate>& templates)
{
    pugi::xml_document doc;
    auto decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    auto root = doc.append_child("fileNameTemplates");
    root.append_attribute("version") = 1;
    for (const DocumentTemplate& t : templates) {
        auto node = root.append_child("template");
        node.append_attribute("name") = t.name.c_str();
        node.append_attribute("pattern") = t.pattern.c_str();
        node.append_attribute("allowPeriod") = t.allowPeriod ? 1 : 0;
        node.append_attribute("useDate") = t.useDate ? 1 : 0;
    }
    std::ostringstream out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

TemplateImport templatesFromXml(const std::string& xml)
{
    TemplateImport result;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8)) {
        result.problems.push_back("This isn't a template file (it can't be read).");
        return result;
    }
    const auto root = doc.child("fileNameTemplates");
    if (!root) {
        result.problems.push_back("This isn't a template file (it isn't one of this program's exports).");
        return result;
    }
    for (const auto node : root.children("template")) {
        DocumentTemplate t;
        t.name = node.attribute("name").as_string();
        t.pattern = node.attribute("pattern").as_string();
        t.allowPeriod = node.attribute("allowPeriod").as_bool(false);
        t.useDate = node.attribute("useDate").as_bool(true);
        const std::string label = t.name.empty() ? std::string("(no name)") : "\"" + t.name + "\"";
        if (const auto error = validateTemplate(t)) {
            result.problems.push_back(label + " was skipped: " + *error);
            continue;
        }
        const bool repeated = std::any_of(result.templates.begin(), result.templates.end(), [&](const DocumentTemplate& o) {
            return fieldKey(o.name) == fieldKey(t.name);
        });
        if (repeated) {
            result.problems.push_back(label + " was skipped: the file lists that name twice.");
            continue;
        }
        result.templates.push_back(std::move(t));
    }
    if (result.templates.empty() && result.problems.empty())
        result.problems.push_back("The file has no templates in it.");
    return result;
}

}  // namespace finrenamer
