#include "finrenamer/WordTemplate.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>

#include "DocxPackage.h"
#include "finrenamer/Utf8Path.h"
#include "finrenamer/VacationSheet.h"  // normalizeHeader

namespace fs = std::filesystem;

namespace finrenamer {

// ---- text helpers --------------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Latin-1 letters in UTF-8 are C3 80..C3 BE; upper case 80..9E (but not D7 x), lower A0..BE (but not F7 /).
std::string changeCase(const std::string& s, bool upper)
{
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto c = static_cast<unsigned char>(out[i]);
        if (c < 0x80) {
            out[i] = static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
        } else if (c == 0xC3 && i + 1 < out.size()) {
            auto& n = reinterpret_cast<unsigned char&>(out[i + 1]);
            if (upper && n >= 0xA0 && n <= 0xBE && n != 0xB7) n = static_cast<unsigned char>(n - 0x20);
            else if (!upper && n >= 0x80 && n <= 0x9E && n != 0x97) n = static_cast<unsigned char>(n + 0x20);
            ++i;
        }
    }
    return out;
}

bool isLetterStart(const std::string& s, std::size_t i)
{
    const auto c = static_cast<unsigned char>(s[i]);
    return std::isalpha(c) || c == 0xC3;
}

std::string capitalizeWords(const std::string& s, bool everyWord)
{
    std::string out = changeCase(s, false);
    bool atStart = true;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto c = static_cast<unsigned char>(out[i]);
        if (isLetterStart(out, i)) {
            if (atStart) {
                const std::string one = out.substr(i, c == 0xC3 ? 2 : 1);
                out.replace(i, one.size(), changeCase(one, true));
                atStart = false;
                if (!everyWord) return out;
            }
            if (c == 0xC3) ++i;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '-' || c == '(' || c == '"') {
            atStart = true;
        }
    }
    return out;
}

// ---- field codes -----------------------------------------------------------------------------------

struct FieldCode {
    std::string name;    // merge field name as written ("" if this isn't a merge field)
    std::string format;  // \* switch, lower case ("upper", "lower", "caps", "firstcap") or ""
    std::string before;  // \b
    std::string after;   // \f
    bool date = false;   // a DATE or TIME field
};

// Splits a field code into words; "quoted text" is one word.
std::vector<std::string> codeWords(const std::string& code)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < code.size()) {
        while (i < code.size() && code[i] == ' ') ++i;
        if (i >= code.size()) break;
        std::string w;
        if (code[i] == '"') {
            ++i;
            while (i < code.size() && code[i] != '"') w += code[i++];
            ++i;
            words.push_back(w);
            continue;
        }
        while (i < code.size() && code[i] != ' ') w += code[i++];
        words.push_back(w);
    }
    return words;
}

FieldCode parseCode(const std::string& code)
{
    FieldCode f;
    const auto words = codeWords(code);
    if (words.empty()) return f;
    std::string kind = changeCase(words[0], true);
    if (kind == "DATE" || kind == "TIME") {
        f.date = true;
        return f;
    }
    if (kind != "MERGEFIELD" || words.size() < 2) return f;
    f.name = trim(words[1]);
    for (std::size_t i = 2; i < words.size(); ++i) {
        const std::string sw = changeCase(words[i], false);
        if (sw == "\\*" && i + 1 < words.size()) {
            const std::string fmt = changeCase(words[++i], false);
            if (fmt != "mergeformat" && fmt != "charformat") f.format = fmt;
        } else if (sw == "\\b" && i + 1 < words.size()) {
            f.before = words[++i];
        } else if (sw == "\\f" && i + 1 < words.size()) {
            f.after = words[++i];
        }
    }
    return f;
}

std::string applyFormat(const FieldCode& f, std::string value)
{
    if (trim(value).empty()) return {};
    if (f.format == "upper") value = changeCase(value, true);
    else if (f.format == "lower") value = changeCase(value, false);
    else if (f.format == "caps") value = capitalizeWords(value, true);
    else if (f.format == "firstcap") value = capitalizeWords(value, false);
    return f.before + value + f.after;
}

// "Begin_OPC", "End OPC", "TableStart:OPC" -> {true/false, "opc"}; empty group if not a marker.
struct Marker {
    bool begin = false;
    std::string group;  // normalized; "" = not a marker
    std::string written;
};

Marker markerOf(const std::string& name)
{
    Marker m;
    const std::string lower = changeCase(name, false);
    for (const auto& [prefix, begin] : std::vector<std::pair<std::string, bool>>{
             {"begin", true}, {"end", false}, {"tablestart", true}, {"tableend", false}}) {
        if (lower.size() > prefix.size() + 1 && lower.compare(0, prefix.size(), prefix) == 0) {
            const char sep = lower[prefix.size()];
            if (sep == '_' || sep == ':' || sep == ' ' || sep == '-') {
                m.begin = begin;
                m.group = normalizeHeader(name.substr(prefix.size() + 1));
                m.written = name.substr(prefix.size() + 1);
                return m;
            }
        }
    }
    return m;
}

// ---- fields in the XML tree ---------------------------------------------------------------------------

bool isElement(pugi::xml_node n, const char* name) { return n.type() == pugi::node_element && std::string(n.name()) == name; }

std::string fldCharType(pugi::xml_node run)
{
    for (pugi::xml_node c : run.children())
        if (isElement(c, "w:fldChar")) return c.attribute("w:fldCharType").value();
    return {};
}

struct FieldSpan {
    pugi::xml_node first, last;  // the run with "begin" and the one with "end"; or the w:fldSimple (both)
    pugi::xml_node propsFrom;    // a run whose w:rPr the value should get
    std::string code;
    bool simple = false;
    bool nested = false;
    std::vector<pugi::xml_node> resultTexts;  // w:t of the result (for DATE fields)
};

// From a "begin" run, the span up to the matching "end" run among its following siblings.
bool complexField(pugi::xml_node begin, FieldSpan& f)
{
    f = FieldSpan{};
    f.first = begin;
    int depth = 0;
    bool separated = false;
    for (pugi::xml_node n = begin; n; n = n.next_sibling()) {
        if (isElement(n, "w:r")) {
            const std::string t = fldCharType(n);
            if (t == "begin") {
                ++depth;
                if (depth > 1) f.nested = true;
            } else if (t == "separate" && depth == 1) {
                separated = true;
            } else if (t == "end") {
                if (--depth == 0) {
                    f.last = n;
                    if (!f.propsFrom) f.propsFrom = begin;
                    return true;
                }
            }
            if (depth == 1 && !separated) {
                for (pugi::xml_node c : n.children())
                    if (isElement(c, "w:instrText")) f.code += c.text().get();
            } else if (depth == 1 && separated && t.empty()) {
                if (!f.propsFrom && n.child("w:rPr")) f.propsFrom = n;
                for (pugi::xml_node c : n.children())
                    if (isElement(c, "w:t")) f.resultTexts.push_back(c);
            }
        }
    }
    return false;  // the field goes on past this paragraph: left alone
}

void collectFields(pugi::xml_node node, std::vector<FieldSpan>& out)
{
    for (pugi::xml_node c = node.first_child(); c;) {
        if (isElement(c, "w:fldSimple")) {
            FieldSpan f;
            f.first = f.last = c;
            f.simple = true;
            f.code = c.attribute("w:instr").value();
            for (pugi::xml_node r : c.children("w:r")) {
                if (!f.propsFrom) f.propsFrom = r;
                for (pugi::xml_node t : r.children("w:t")) f.resultTexts.push_back(t);
            }
            out.push_back(f);
            c = c.next_sibling();
            continue;
        }
        if (isElement(c, "w:r") && fldCharType(c) == "begin") {
            FieldSpan f;
            if (complexField(c, f)) {
                out.push_back(f);
                c = f.last.next_sibling();
                continue;
            }
        }
        if (c.type() == pugi::node_element) collectFields(c, out);
        c = c.next_sibling();
    }
}

std::vector<FieldSpan> fieldsIn(pugi::xml_node first, pugi::xml_node last)
{
    std::vector<FieldSpan> out;
    for (pugi::xml_node n = first; n; n = n.next_sibling()) {
        if (isElement(n, "w:fldSimple")) {
            FieldSpan f;
            f.first = f.last = n;
            f.simple = true;
            f.code = n.attribute("w:instr").value();
            for (pugi::xml_node r : n.children("w:r")) {
                if (!f.propsFrom) f.propsFrom = r;
                for (pugi::xml_node t : r.children("w:t")) f.resultTexts.push_back(t);
            }
            out.push_back(f);
        } else if (isElement(n, "w:r") && fldCharType(n) == "begin") {
            FieldSpan f;
            if (complexField(n, f)) {
                out.push_back(f);
                if (f.last == last) break;
                bool passedLast = false;
                for (pugi::xml_node m = n; m != f.last; m = m.next_sibling())
                    if (m == last) passedLast = true;
                n = f.last;
                if (passedLast) break;
                continue;
            }
        } else if (n.type() == pugi::node_element) {
            collectFields(n, out);
        }
        if (n == last) break;
    }
    return out;
}

// Removes a field's nodes; returns the node that followed it.
pugi::xml_node removeField(const FieldSpan& f)
{
    pugi::xml_node parent = f.first.parent();
    pugi::xml_node after = f.last.next_sibling();
    for (pugi::xml_node n = f.first; n;) {
        pugi::xml_node next = n.next_sibling();
        const bool stop = n == f.last;
        parent.remove_child(n);
        if (stop) break;
        n = next;
    }
    return after;
}

pugi::xml_node ancestorParagraph(pugi::xml_node n)
{
    for (; n; n = n.parent())
        if (isElement(n, "w:p")) return n;
    return {};
}

bool hasVisibleContent(pugi::xml_node p)
{
    bool found = false;
    struct Walker : pugi::xml_tree_walker {
        bool* found;
        bool for_each(pugi::xml_node& n) override
        {
            const std::string name = n.name();
            if (name == "w:t" && !trim(n.text().get()).empty()) *found = true;
            if (name == "w:drawing" || name == "w:pict" || name == "w:object" || name == "w:fldChar" ||
                name == "w:fldSimple" || name == "w:sym")
                *found = true;
            return !*found;
        }
    } w;
    w.found = &found;
    p.traverse(w);
    return found;
}

// Removes a paragraph that shows nothing, if that leaves its container valid.
void dropIfEmpty(pugi::xml_node p)
{
    if (!p || !isElement(p, "w:p") || hasVisibleContent(p)) return;
    if (p.child("w:pPr").child("w:sectPr")) return;  // carries a section break
    pugi::xml_node parent = p.parent();
    if (!parent) return;
    const std::string pname = parent.name();
    if (pname == "w:tc" || pname == "w:txbxContent" || pname == "w:hdr" || pname == "w:ftr" ||
        pname == "w:footnote" || pname == "w:endnote" || pname == "w:sdtContent") {
        int paragraphs = 0;
        for (pugi::xml_node c : parent.children())
            if (isElement(c, "w:p") || isElement(c, "w:tbl")) ++paragraphs;
        if (paragraphs <= 1) return;
        // A table cell must end with a paragraph.
        if (pname == "w:tc" && !p.next_sibling("w:p") && !p.next_sibling("w:tbl")) {
            pugi::xml_node prev = p.previous_sibling();
            while (prev && !isElement(prev, "w:p") && !isElement(prev, "w:tbl")) prev = prev.previous_sibling();
            if (!prev || isElement(prev, "w:tbl")) return;
        }
    }
    parent.remove_child(p);
}

// Adds the run(s) showing `value` before `where`, with `props` (a w:rPr or empty).
void insertValue(pugi::xml_node parent, pugi::xml_node where, pugi::xml_node props, const std::string& value)
{
    pugi::xml_node run = where ? parent.insert_child_before("w:r", where) : parent.append_child("w:r");
    if (props) run.append_copy(props);
    std::string line;
    auto flush = [&] {
        if (line.empty()) return;
        pugi::xml_node t = run.append_child("w:t");
        t.append_attribute("xml:space") = "preserve";
        t.text().set(line.c_str());
        line.clear();
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') continue;
            flush();
            run.append_child("w:br");
        } else if (c == '\n') {
            flush();
            run.append_child("w:br");
        } else if (c == '\t') {
            flush();
            run.append_child("w:tab");
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            line += c;
        }
    }
    flush();
}

// ---- filling one part ----------------------------------------------------------------------------------

constexpr const char* kMarkerAttr = "fr-marker";

struct Filler {
    const MergeData& data;
    std::string today;
    docx::ImageStore* images = nullptr;  // null in headers/footers
    TemplateFillResult& result;
    std::set<std::string> unknownFields, unknownGroups;

    using Context = std::vector<const MergeRecord*>;

    std::string lookup(const Context& ctx, const std::string& name, bool& found) const
    {
        const std::string key = normalizeHeader(name);
        for (auto it = ctx.rbegin(); it != ctx.rend(); ++it) {
            const auto v = (*it)->find(key);
            if (v != (*it)->end()) {
                found = true;
                return v->second;
            }
        }
        found = false;
        return {};
    }

    // Lowest node holding both a and b; and the children of it holding each.
    static pugi::xml_node commonAncestor(pugi::xml_node a, pugi::xml_node b)
    {
        std::vector<pugi::xml_node> chain;
        for (pugi::xml_node n = a; n; n = n.parent()) chain.push_back(n);
        for (pugi::xml_node n = b; n; n = n.parent())
            if (std::find(chain.begin(), chain.end(), n) != chain.end()) return n;
        return {};
    }
    static pugi::xml_node childOf(pugi::xml_node ancestor, pugi::xml_node n)
    {
        for (; n && n.parent() != ancestor; n = n.parent()) {}
        return n;
    }

    // Expands the blocks among the siblings first..last (inclusive) of `parent`, then fills the
    // remaining fields with `ctx`. first/last may change; returns the new bounds.
    void process(pugi::xml_node parent, pugi::xml_node& first, pugi::xml_node& last, Context& ctx)
    {
        if (!first) return;
        while (true) {
            const auto fields = fieldsIn(first, last);
            std::size_t bi = fields.size();
            Marker bm;
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].nested) continue;
                const Marker m = markerOf(parseCode(fields[i].code).name);
                if (!m.group.empty()) {
                    bi = i;
                    bm = m;
                    break;
                }
            }
            if (bi == fields.size()) break;
            if (!bm.begin) {  // an End_ with no Begin_
                result.problems.push_back("\"End_" + bm.written + "\" has no \"Begin_" + bm.written + "\" before it (ignored).");
                removeMarker(fields[bi], parent, first, last);
                continue;
            }
            std::size_t ei = fields.size();
            int depth = 0;
            for (std::size_t i = bi + 1; i < fields.size(); ++i) {
                if (fields[i].nested) continue;
                const Marker m = markerOf(parseCode(fields[i].code).name);
                if (m.group != bm.group) continue;
                if (m.begin) ++depth;
                else if (depth-- == 0) {
                    ei = i;
                    break;
                }
            }
            if (ei == fields.size()) {
                result.problems.push_back("\"Begin_" + bm.written + "\" has no \"End_" + bm.written + "\" after it (ignored).");
                removeMarker(fields[bi], parent, first, last);
                continue;
            }
            expandBlock(fields[bi], fields[ei], bm, parent, first, last, ctx);
        }
        fillFields(parent, first, last, ctx);
    }

    // Removes a marker field, keeping first/last valid.
    void removeMarker(const FieldSpan& f, pugi::xml_node parent, pugi::xml_node& first, pugi::xml_node& last)
    {
        pugi::xml_node p = ancestorParagraph(f.first);
        const bool isFirst = f.first == first, isLast = f.last == last;
        pugi::xml_node before = f.first.previous_sibling();
        pugi::xml_node after = removeField(f);
        if (isFirst && isLast) first = last = pugi::xml_node();
        else if (isFirst) first = after;
        else if (isLast) last = before;
        if (!p) return;
        if (p.parent() == parent) dropIfEmptyBounded(p, first, last);
        else dropIfEmpty(p);
    }

    void dropIfEmptyBounded(pugi::xml_node p, pugi::xml_node& first, pugi::xml_node& last)
    {
        pugi::xml_node prev = p.previous_sibling(), next = p.next_sibling();
        pugi::xml_node parent = p.parent();
        dropIfEmpty(p);
        if (p.parent() == parent) return;  // kept
        if (p == first && p == last) first = last = pugi::xml_node();
        else if (p == first) first = next;
        else if (p == last) last = prev;
    }

    void expandBlock(const FieldSpan& b, const FieldSpan& e, const Marker& m, pugi::xml_node parent,
                     pugi::xml_node& first, pugi::xml_node& last, Context& ctx)
    {
        pugi::xml_node pBegin = ancestorParagraph(b.first), pEnd = ancestorParagraph(e.first);
        const bool inline_ = pBegin && pBegin == pEnd;
        pugi::xml_node lca, a, z;
        if (inline_) {
            // Both in one paragraph: the runs between them repeat (markers' own runs dropped).
            lca = commonAncestor(b.first, e.first);
            a = childOf(lca, b.first);
            z = childOf(lca, e.last);
        } else {
            lca = commonAncestor(pBegin ? pBegin : b.first, pEnd ? pEnd : e.first);
            a = childOf(lca, pBegin ? pBegin : b.first);
            z = childOf(lca, pEnd ? pEnd : e.first);
        }
        if (!lca || !a || !z) {
            result.problems.push_back("Couldn't repeat \"" + m.written + "\" (its Begin and End are in places that can't be repeated).");
            removeField(b);
            removeField(e);
            return;
        }
        // Mark the marker paragraphs, then remove the markers themselves.
        if (!inline_) {
            if (pBegin && !pBegin.attribute(kMarkerAttr)) pBegin.append_attribute(kMarkerAttr) = "1";
            if (pEnd && !pEnd.attribute(kMarkerAttr)) pEnd.append_attribute(kMarkerAttr) = "1";
        }
        const bool aIsBegin = a == b.first, zIsEnd = z == e.last;
        auto dropEmptyLine = [&] {  // an inline block can leave its paragraph with nothing in it
            if (!inline_ || !pBegin || hasVisibleContent(pBegin)) return;
            for (pugi::xml_node n = parent; n; n = n.parent())
                if (n == pBegin) return;  // the paragraph holds the range being worked on: keep it
            if (pBegin.parent() == parent) dropIfEmptyBounded(pBegin, first, last);
            else dropIfEmpty(pBegin);
        };
        if (inline_ && aIsBegin && zIsEnd && b.last.next_sibling() == e.first) {  // nothing between them
            removeField(b);
            removeField(e);
            dropEmptyLine();
            return;
        }
        const pugi::xml_node afterBegin = b.last.next_sibling(), beforeEnd = e.first.previous_sibling();
        removeField(b);
        removeField(e);
        if (aIsBegin) a = afterBegin;  // a/z were the marker runs themselves: the block is what lay between
        if (zIsEnd) z = beforeEnd;
        if (!a || !z) return;

        // The block's sibling range a..z under lca; note where it sits in the caller's range.
        const bool startsRange = lca == parent && a == first;
        const bool endsRange = lca == parent && z == last;
        std::vector<pugi::xml_node> range;
        for (pugi::xml_node n = a; n; n = n.next_sibling()) {
            range.push_back(n);
            if (n == z) break;
        }

        const auto group = data.groups.find(m.group);
        if (group == data.groups.end()) unknownGroups.insert(m.written);
        static const std::vector<MergeRecord> none;
        const auto& items = group == data.groups.end() ? none : group->second;

        pugi::xml_node newFirst, newLast;
        for (const MergeRecord& item : items) {
            pugi::xml_node cFirst, cLast;
            for (pugi::xml_node n : range) {
                pugi::xml_node copy = lca.insert_copy_before(n, a);
                if (!cFirst) cFirst = copy;
                cLast = copy;
            }
            ctx.push_back(&item);
            process(lca, cFirst, cLast, ctx);
            ctx.pop_back();
            cleanMarkers(lca, cFirst, cLast);
            if (cFirst && !newFirst) newFirst = cFirst;
            if (cLast) newLast = cLast;
        }
        pugi::xml_node after = z.next_sibling(), before = a.previous_sibling();
        for (pugi::xml_node n : range) lca.remove_child(n);
        if (startsRange) first = newFirst ? newFirst : after;
        if (endsRange) last = newLast ? newLast : before;
        if (startsRange && endsRange && !newFirst) first = last = pugi::xml_node();
        dropEmptyLine();
    }

    // In the siblings first..last: marker paragraphs left empty are removed; the mark is dropped.
    void cleanMarkers(pugi::xml_node parent, pugi::xml_node& first, pugi::xml_node& last)
    {
        std::vector<pugi::xml_node> marked;
        for (pugi::xml_node n = first; n; n = n.next_sibling()) {
            if (n.attribute(kMarkerAttr)) marked.push_back(n);
            for (pugi::xpath_node x : n.select_nodes(".//*[@fr-marker]")) marked.push_back(x.node());
            if (n == last) break;
        }
        for (pugi::xml_node p : marked) {
            p.remove_attribute(kMarkerAttr);
            if (p.parent() == parent) dropIfEmptyBounded(p, first, last);
            else dropIfEmpty(p);
        }
    }

    void fillFields(pugi::xml_node parent0, pugi::xml_node& first, pugi::xml_node& last, const Context& ctx)
    {
        if (!first) return;
        const auto fields = fieldsIn(first, last);
        std::vector<pugi::xml_node> blanked;
        for (const FieldSpan& f : fields) {
            if (f.nested) continue;
            const FieldCode code = parseCode(f.code);
            if (code.date) {
                if (!f.resultTexts.empty()) {
                    f.resultTexts.front().text().set(today.c_str());
                    for (std::size_t i = 1; i < f.resultTexts.size(); ++i) f.resultTexts[i].text().set("");
                }
                continue;
            }
            if (code.name.empty()) continue;

            pugi::xml_node parent = f.first.parent();
            pugi::xml_node p = ancestorParagraph(f.first);
            // An inline block's bounds can be the field's own runs: what replaces them takes their place.
            const bool wasFirst = f.first == first, wasLast = f.last == last;
            pugi::xml_node prevNode = f.first.previous_sibling();
            pugi::xml_document propsCopy;
            if (f.propsFrom && f.propsFrom.child("w:rPr")) propsCopy.append_copy(f.propsFrom.child("w:rPr"));

            pugi::xml_node after = removeField(f);
            bool placedSomething = false;
            if (!markerOf(code.name).group.empty()) {
                // a stray marker: dropped
            } else if (normalizeHeader(code.name) == "signature") {
                result.signatureField = true;
                if (images && !data.signature.empty()) {
                    pugi::xml_document frag;
                    const std::string xml = images->run(data.signature);
                    frag.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
                    if (after) parent.insert_copy_before(frag.first_child(), after);
                    else parent.append_copy(frag.first_child());
                    result.signatureAdded = true;
                    placedSomething = true;
                }
            } else {
                bool found = false;
                const std::string raw = lookup(ctx, code.name, found);
                if (!found) unknownFields.insert(code.name);
                const std::string value = applyFormat(code, raw);
                if (!value.empty()) {
                    insertValue(parent, after, propsCopy.first_child(), value);
                    placedSomething = true;
                }
            }
            if (!placedSomething && p) blanked.push_back(p);
            if (wasFirst || wasLast) {
                pugi::xml_node placed = placedSomething ? (prevNode ? prevNode.next_sibling() : parent.first_child())
                                                        : pugi::xml_node();
                if (wasFirst && wasLast && !placed) first = last = pugi::xml_node();
                else {
                    if (wasFirst) first = placed ? placed : after;
                    if (wasLast) last = placed ? placed : prevNode;
                }
            }
        }
        for (pugi::xml_node p : blanked) {
            if (p.parent() == parent0) dropIfEmptyBounded(p, first, last);
            else dropIfEmpty(p);
        }
    }
};

std::string serialize(const pugi::xml_document& doc)
{
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw, pugi::encoding_utf8);
    return out.str();
}

void openTemplate(OpenXLSX::XLZipArchive& zip, const fs::path& file)
{
    std::error_code ec;
    if (!fs::exists(file, ec)) throw std::runtime_error("The template was not found:\n" + utf8FromPath(file));
    try {
        zip.open(utf8FromPath(file));
    } catch (const std::exception& e) {
        throw std::runtime_error("Couldn't open the template. Is it a Word (.docx) file?\n(" + std::string(e.what()) + ")");
    }
    if (!zip.hasEntry("word/document.xml")) {
        zip.close();
        throw std::runtime_error("The template is not a Word (.docx) document.");
    }
}

std::vector<std::string> partNames(const OpenXLSX::XLZipArchive& zip)
{
    std::vector<std::string> parts{"word/document.xml"};
    for (const char* kind : {"header", "footer"})
        for (int i = 1; i <= 30; ++i) {
            const std::string name = std::string("word/") + kind + std::to_string(i) + ".xml";
            if (zip.hasEntry(name)) parts.push_back(name);
        }
    return parts;
}

void parsePart(const std::string& xml, pugi::xml_document& doc, const std::string& part)
{
    const auto r = doc.load_buffer(xml.data(), xml.size(),
                                   pugi::parse_default | pugi::parse_declaration | pugi::parse_ws_pcdata,
                                   pugi::encoding_utf8);
    if (!r) throw std::runtime_error("The template is damaged (" + part + ": " + r.description() + ").");
}

}  // namespace

std::string toUpperText(const std::string& s) { return changeCase(s, true); }
std::string toLowerText(const std::string& s) { return changeCase(s, false); }

std::vector<std::string> wordTemplateFields(const fs::path& templateDocx)
{
    OpenXLSX::XLZipArchive zip;
    openTemplate(zip, templateDocx);
    std::vector<std::string> names;
    try {
        for (const std::string& part : partNames(zip)) {
            pugi::xml_document doc;
            parsePart(zip.getEntry(part), doc, part);
            std::vector<FieldSpan> fields;
            collectFields(doc.document_element(), fields);
            for (const FieldSpan& f : fields) {
                const std::string name = f.nested ? std::string() : parseCode(f.code).name;
                if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
            }
        }
    } catch (...) {
        zip.close();
        throw;
    }
    zip.close();
    return names;
}

TemplateFillResult fillWordTemplate(const fs::path& templateDocx, const fs::path& output, const MergeData& data,
                                    const std::string& todayText)
{
    OpenXLSX::XLZipArchive zip;
    openTemplate(zip, templateDocx);
    const fs::path temp = fs::path(output).concat(".saving");
    TemplateFillResult result;
    try {
        docx::ImageStore images;
        Filler filler{data, todayText, nullptr, result, {}, {}};
        for (const std::string& part : partNames(zip)) {
            pugi::xml_document doc;
            parsePart(zip.getEntry(part), doc, part);
            pugi::xml_node root = doc.document_element();
            pugi::xml_node container = part == "word/document.xml" ? root.child("w:body") : root;
            if (!container) continue;
            filler.images = part == "word/document.xml" ? &images : nullptr;
            pugi::xml_node first = container.first_child(), last = container.last_child();
            if (isElement(last, "w:sectPr")) last = last.previous_sibling();  // the section settings stay put
            Filler::Context ctx{&data.fields};
            if (first && last) filler.process(container, first, last, ctx);
            // Any mark left behind (a marker whose block couldn't be expanded).
            for (pugi::xpath_node x : container.select_nodes(".//*[@fr-marker]")) x.node().remove_attribute(kMarkerAttr);
            zip.addEntry(part, serialize(doc));
        }
        result.unknownFields.assign(filler.unknownFields.begin(), filler.unknownFields.end());
        result.unknownGroups.assign(filler.unknownGroups.begin(), filler.unknownGroups.end());
        docx::addImagesToPackage(zip, images);
        docx::dropMailMergeLink(zip);
        zip.save(utf8FromPath(temp));
        zip.close();
    } catch (...) {
        if (zip.isOpen()) zip.close();
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw;
    }
    std::error_code ec;
    fs::rename(temp, output, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw std::runtime_error("Couldn't save the document. If a file with that name is open in Word, close it or "
                                 "choose another name.\n(" + ec.message() + ")");
    }
    return result;
}

}  // namespace finrenamer
