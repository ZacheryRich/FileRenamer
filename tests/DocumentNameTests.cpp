#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/DocumentName.h"
#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/RenamePlan.h"
#include "finrenamer/RenameSession.h"

using namespace finrenamer;
using testing::TempDir;

namespace {

DocumentTemplate emailTemplate()
{
    return {1, "Email", "Email from {Sender} to {Recipient}[ ({Description})]", false};
}

DocumentTemplate conversationTemplate()
{
    return {2, "Text Conversation", "{Label} between {Person 1} and {Person 2}[ ({Description})]", true};
}

DocumentInfo filled(const DocumentTemplate& t, std::initializer_list<std::pair<const char*, const char*>> values)
{
    DocumentInfo d;
    d.kind = DocKind::Template;
    d.tmpl = t;
    for (const auto& [name, value] : values) d.setField(name, value);
    return d;
}

DocumentInfo email(std::string from, std::string to, std::string description = {})
{
    return filled(emailTemplate(), {{"Sender", from.c_str()}, {"Recipient", to.c_str()}, {"Description", description.c_str()}});
}

DocumentInfo other(std::string label, std::string description = {})
{
    DocumentInfo d;
    d.kind = DocKind::Other;
    d.label = std::move(label);
    d.description = std::move(description);
    return d;
}

const DateSpec kJan31 = SingleDate{makeDate(2026, 1, 31)};
const DateSpec kJanPeriod = Period{makeDate(2026, 1, 1), makeDate(2026, 1, 31)};

}  // namespace

TEST_CASE("Patterns: fields, optional parts and escapes")
{
    CHECK_FALSE(validatePattern("Email from {Sender} to {Recipient}[ ({Description})]"));
    CHECK_FALSE(validatePattern("Just words"));
    CHECK_FALSE(validatePattern("\\[{A}\\]"));

    CHECK(validatePattern(""));
    CHECK(validatePattern("{A"));
    CHECK(validatePattern("A}"));
    CHECK(validatePattern("{}"));
    CHECK(validatePattern("{  }"));
    CHECK(validatePattern("[ {A}"));
    CHECK(validatePattern("{A} ]"));
    CHECK(validatePattern("[[{A}]]"));
    CHECK(validatePattern("{A{B}}"));
    CHECK(validatePattern("{A}\\"));
    CHECK(validatePattern("{" + std::string(60, 'x') + "}"));

    const auto fields = templateFields("{Sender} to {Recipient}[ ({description}) {Sender}] {SENDER}");
    REQUIRE(fields.size() == 3);
    CHECK(fields[0].name == "Sender");
    CHECK_FALSE(fields[0].optional);  // also used outside an optional part
    CHECK(fields[1].name == "Recipient");
    CHECK(fields[2].name == "description");
    CHECK(fields[2].optional);
    CHECK(templateFields("{A").empty());

    CHECK(fieldKey("  Person   1 ") == "person 1");
    CHECK(sampleLabel("Email from {Sender} to {Recipient}[ ({Description})]") == "Email from Sender to Recipient (Description)");
}

TEST_CASE("Template names: date, then the pattern filled in")
{
    CHECK(buildDocumentFilename(email("John Smith", "Jane Doe", "Re settlement offer"), kJan31) ==
          "2026.01.31 Email from John Smith to Jane Doe (Re settlement offer).pdf");
    CHECK(buildDocumentFilename(email("John", "Jane"), kJan31) == "2026.01.31 Email from John to Jane.pdf");
    CHECK(buildDocumentFilename(email("John", "Jane"), kJan31, "") == "2026.01.31 Email from John to Jane");

    const DocumentInfo convo = filled(conversationTemplate(), {{"Label", "Our Family Wizard"}, {"person 1", "John"},
                                                              {"PERSON 2", "Jane"}, {"description", "January"}});
    CHECK(buildDocumentFilename(convo, kJanPeriod) ==
          "2026.01.01 - 2026.01.31 Our Family Wizard between John and Jane (January).pdf");
}

TEST_CASE("A field used twice is typed once, and brackets can be literal")
{
    const DocumentTemplate t{3, "Memo", "\\[{Case}\\] {Case} memo from {Author}", false};
    const DocumentInfo d = filled(t, {{"case", "Smith"}, {"Author", "Pat"}});
    CHECK(documentLabel(d) == "[Smith] Smith memo from Pat");
}

TEST_CASE("Other names: label and optional description")
{
    CHECK(buildDocumentFilename(other("Court Order"), kJan31) == "2026.01.31 Court Order.pdf");
    CHECK(buildDocumentFilename(other("Court Order", "custody"), kJanPeriod) ==
          "2026.01.01 - 2026.01.31 Court Order (custody).pdf");
}

TEST_CASE("Typed text is made safe for Windows file names")
{
    CHECK(buildDocumentFilename(email("A/B", "C: D", "what? <now>"), kJan31) == "2026.01.31 Email from A-B to C- D (what now).pdf");
    CHECK(buildDocumentFilename(email("  John   Smith ", "Jane", "  "), kJan31) == "2026.01.31 Email from John Smith to Jane.pdf");
}

TEST_CASE("What is still missing")
{
    CHECK(documentMissing(email("", "")) == "Needs sender and recipient");
    CHECK(documentMissing(email("John", ""), true) == "Needs recipient and a date");
    CHECK_FALSE(documentMissing(email("John", "Jane")));

    const DocumentTemplate t{4, "Pair", "{A} and {B}[ re {C} / {D}]", false};
    CHECK(documentMissing(filled(t, {{"A", "x"}, {"B", "y"}})) == std::nullopt);
    CHECK(documentMissing(filled(t, {{"A", "x"}, {"B", "y"}, {"C", "z"}})) == "Needs d");   // an optional part once started
    CHECK(documentMissing(filled(t, {})) == "Needs a and b");

    CHECK(documentMissing(other("")) == "Needs a label");
    CHECK_FALSE(documentMissing(other("Photo")));  // the description is optional

    DocumentInfo broken = filled({5, "Bad", "{A", false}, {{"A", "x"}});
    CHECK(documentMissing(broken)->find("not valid") != std::string::npos);
}

TEST_CASE("Which dates each kind accepts")
{
    CHECK_FALSE(validateDocumentDate(email("a", "b"), kJan31));
    CHECK(validateDocumentDate(email("a", "b"), kJanPeriod));
    CHECK_FALSE(validateDocumentDate(filled(conversationTemplate(), {}), kJanPeriod));
    CHECK_FALSE(validateDocumentDate(other("x"), kJanPeriod));
    CHECK(validateDocumentDate(other("x"), Quarter{2026, 1}));
    CHECK(validateDocumentDate(email("a", "b"), Quarter{2026, 1}));
    CHECK(allowedDateModes(email("a", "b")).period == false);
    CHECK(allowedDateModes(other("x")).period);
}

TEST_CASE("Documents are renamed through the plan, and never sorted into folders")
{
    TempDir dir;
    const auto a = dir.touch("scan1.pdf");
    const auto b = dir.touch("scan2.pdf");
    const auto c = dir.touch("scan3.pdf");

    std::vector<PlanInput> inputs(3);
    inputs[0].source = a;
    inputs[0].document = email("John", "Jane", "Re pickup");
    inputs[0].date = kJan31;
    inputs[1].source = b;
    inputs[1].document = email("John", "");   // recipient missing
    inputs[1].date = kJan31;
    inputs[2].source = c;
    inputs[2].document = email("John", "Jane");
    inputs[2].date = kJanPeriod;               // a period isn't allowed for this template

    PlanOptions options;
    options.sort.byAccount = true;  // both ignored for documents
    options.sort.byYear = true;
    const RenamePlan plan = buildPlan(dir.path(), inputs, options);
    REQUIRE(plan.moves.size() == 3);
    CHECK(plan.moves[0].status == MoveStatus::Ready);
    CHECK(plan.moves[0].destination == plan.root / "2026.01.31 Email from John to Jane (Re pickup).pdf");
    CHECK(plan.moves[1].status == MoveStatus::Incomplete);
    CHECK(plan.moves[1].message == "Needs recipient");
    CHECK(plan.moves[2].status == MoveStatus::Error);
}

TEST_CASE("A document without a date is incomplete, and two with the same name get (2)")
{
    TempDir dir;
    std::vector<PlanInput> inputs(3);
    for (int i = 0; i < 3; ++i) {
        inputs[static_cast<std::size_t>(i)].source = dir.touch("f" + std::to_string(i) + ".pdf");
        inputs[static_cast<std::size_t>(i)].document = email("John", "Jane");
    }
    inputs[0].date = kJan31;
    inputs[1].date = kJan31;
    const RenamePlan plan = buildPlan(dir.path(), inputs);
    CHECK(plan.moves[0].destination.filename() == "2026.01.31 Email from John to Jane.pdf");
    CHECK(plan.moves[1].destination.filename() == "2026.01.31 Email from John to Jane (2).pdf");
    CHECK(plan.moves[2].status == MoveStatus::Incomplete);
    CHECK(plan.moves[2].message == "Needs a date");
}

TEST_CASE("Carry-forward after a template file copies the text and the same date, minus optional boxes")
{
    TempDir dir;
    dir.touch("a.pdf");
    dir.touch("b.pdf");
    dir.touch("c.pdf");
    RenameSession session(dir.path(), {});
    REQUIRE(session.size() == 3);

    session.row(0).document = filled(conversationTemplate(), {{"Label", "WhatsApp"}, {"Person 1", "John"},
                                                              {"Person 2", "Jane"}, {"Description", "January"}});
    session.row(0).date = kJanPeriod;
    CHECK(session.carryForward(0, 1));
    CHECK(session.row(1).document.kind == DocKind::Template);
    CHECK(session.row(1).document.tmpl.id == 2);
    CHECK(session.row(1).document.field("Label") == "WhatsApp");
    CHECK(session.row(1).document.field("person 1") == "John");
    CHECK(session.row(1).document.field("Description").empty());
    REQUIRE(session.row(1).date.has_value());
    CHECK(formatDateSpec(*session.row(1).date) == formatDateSpec(kJanPeriod));
    CHECK_FALSE(session.carryForward(0, 1));  // no longer blank

    CHECK(session.hasUnappliedEntries());
    const auto preview = session.preview({});
    CHECK(preview[0].status == MoveStatus::Ready);
    CHECK(preview[0].destination.filename() == "2026.01.01 - 2026.01.31 WhatsApp between John and Jane (January).pdf");
    CHECK(preview[1].destination.filename() == "2026.01.01 - 2026.01.31 WhatsApp between John and Jane.pdf");

    const auto result = session.apply({});
    CHECK(result.record.moves.size() == 2);
    CHECK(session.row(2).isBlank());
}

TEST_CASE("Carry-forward after Other keeps the label and blanks the description")
{
    TempDir dir;
    dir.touch("a.pdf");
    dir.touch("b.pdf");
    RenameSession session(dir.path(), {});
    session.row(0).document = other("Photo", "front");
    session.row(0).date = kJan31;
    CHECK(session.carryForward(0, 1));
    CHECK(session.row(1).document.kind == DocKind::Other);
    CHECK(session.row(1).document.label == "Photo");
    CHECK(session.row(1).document.description.empty());
}

TEST_CASE("Editing or deleting a template updates the rows that use it")
{
    TempDir dir;
    dir.touch("a.pdf");
    dir.touch("b.pdf");
    dir.touch("c.pdf");
    RenameSession session(dir.path(), {});
    session.row(0).document = email("John", "Jane");
    session.row(0).date = kJan31;
    session.row(1).document = filled(emailTemplate(), {});  // chosen but nothing typed yet
    session.row(2).document = filled(conversationTemplate(), {{"Label", "T"}, {"Person 1", "a"}, {"Person 2", "b"}});

    DocumentTemplate changed = emailTemplate();
    changed.pattern = "Mail from {Sender} to {Recipient}";
    session.refreshTemplates({changed, conversationTemplate()});
    CHECK(documentLabel(session.row(0).document) == "Mail from John to Jane");
    CHECK(session.row(0).document.tmpl.pattern == changed.pattern);

    session.refreshTemplates({});  // both deleted
    CHECK(session.row(0).document.kind == DocKind::Other);
    CHECK(session.row(0).document.label == "Mail from John to Jane");
    CHECK(session.row(1).document.kind == DocKind::Statement);  // nothing typed: back to a blank row
    CHECK(session.row(2).document.kind == DocKind::Other);
    CHECK(session.row(2).document.label == "T between a and b");
}

TEST_CASE("Every kind of file can be dated with just a year")
{
    const DateSpec year = WholeYear{2025};
    CHECK_FALSE(validateDocumentDate(email("a", "b"), year));
    CHECK_FALSE(validateDocumentDate(other("Tax Return"), year));
    CHECK(buildDocumentFilename(other("Tax Return"), year) == "2025 Tax Return.pdf");
    CHECK(buildDocumentFilename(email("John", "Jane"), year) == "2025 Email from John to Jane.pdf");

    const DateSpec month = YearMonth{2025, 3};
    CHECK_FALSE(validateDocumentDate(email("a", "b"), month));
    CHECK(buildDocumentFilename(other("Tax Return"), month) == "2025.03 Tax Return.pdf");
}

TEST_CASE("Template validation and the example templates")
{
    CHECK(validateTemplate({0, "", "{A}", false}));
    CHECK(validateTemplate({0, "Statement", "{A}", false}));
    CHECK(validateTemplate({0, "other", "{A}", false}));
    CHECK(validateTemplate({0, "Email", "{A", false}));
    CHECK_FALSE(validateTemplate({0, "Email", "{A}", false}));

    const auto examples = exampleTemplates();
    CHECK(examples.size() == 5);
    for (const auto& t : examples) CHECK_FALSE(validateTemplate(t));
    CHECK(sampleLabel(examples[0].pattern) == "Email from Sender to Recipient (Description)");
}

TEST_CASE("A template or an Other file can leave the date out")
{
    DocumentTemplate t{7, "Photo", "Photo of {Thing}", false, false};
    DocumentInfo d = filled(t, {{"Thing", "the car"}});
    CHECK_FALSE(documentUsesDate(d));
    CHECK(buildDocumentFilename(d, kJan31) == "Photo of the car.pdf");   // a date, if one is typed, is ignored
    CHECK(buildDocumentFilename(d, std::nullopt) == "Photo of the car.pdf");
    CHECK_FALSE(documentMissing(d, true));                               // no date needed
    CHECK(documentMissing(filled(t, {}), true) == "Needs thing");
    CHECK_FALSE(validateDocumentDate(d, kJanPeriod));                    // nothing to validate

    const DocumentTemplate optionalOnly{8, "Note", "[{A}]", false, false};
    CHECK(documentMissing(filled(optionalOnly, {})) == "Needs a name");

    DocumentInfo o = other("Court Order");
    CHECK(documentUsesDate(o));
    CHECK(documentMissing(o, true) == "Needs a date");
    o.noDate = true;
    CHECK_FALSE(documentUsesDate(o));
    CHECK_FALSE(documentMissing(o, true));
    CHECK(buildDocumentFilename(o, std::nullopt) == "Court Order.pdf");
    CHECK(buildDocumentFilename(o, kJan31) == "Court Order.pdf");
}

TEST_CASE("Files without a date are renamed through the plan with no date")
{
    TempDir dir;
    std::vector<PlanInput> inputs(2);
    inputs[0].source = dir.touch("a.pdf");
    inputs[0].document = other("Court Order", "custody");
    inputs[0].document->noDate = true;                  // no date typed
    inputs[1].source = dir.touch("b.pdf");
    inputs[1].document = filled({9, "Photo", "Photo of {Thing}", false, false}, {{"Thing", "car"}});
    const RenamePlan plan = buildPlan(dir.path(), inputs);
    CHECK(plan.moves[0].status == MoveStatus::Ready);
    CHECK(plan.moves[0].destination.filename() == "Court Order (custody).pdf");
    CHECK(plan.moves[1].status == MoveStatus::Ready);
    CHECK(plan.moves[1].destination.filename() == "Photo of car.pdf");
}

TEST_CASE("Names Windows reserves are refused in the preview, not at rename time")
{
    CHECK(isReservedWindowsName("CON.pdf"));
    CHECK(isReservedWindowsName("nul.pdf"));
    CHECK(isReservedWindowsName("Com3.pdf"));
    CHECK(isReservedWindowsName("lpt9.report.pdf"));
    CHECK_FALSE(isReservedWindowsName("COM0.pdf"));
    CHECK_FALSE(isReservedWindowsName("Console.pdf"));
    CHECK_FALSE(isReservedWindowsName("Con Edison.pdf"));
    CHECK_FALSE(isReservedWindowsName("2026.01.31 Con.pdf"));

    TempDir dir;
    std::vector<PlanInput> inputs(2);
    for (std::size_t i = 0; i < 2; ++i) {
        inputs[i].source = dir.touch("f" + std::to_string(i) + ".pdf");
        inputs[i].document = other(i == 0 ? "Con" : "Con Edison");
        inputs[i].document->noDate = true;
    }
    const RenamePlan plan = buildPlan(dir.path(), inputs);
    REQUIRE(plan.moves.size() == 2);
    CHECK(plan.moves[0].status == MoveStatus::Error);
    CHECK(plan.moves[0].message.find("doesn't allow") != std::string::npos);
    CHECK(plan.moves[1].status == MoveStatus::Ready);
}

TEST_CASE("Templates survive an export and import, and bad ones are reported")
{
    std::vector<DocumentTemplate> mine = exampleTemplates();
    mine.push_back({0, "Photo & <Scan> \"x\"", "Photo of {Thing}[ ({Where})]", true, false});
    const std::string xml = templatesToXml(mine);
    const TemplateImport back = templatesFromXml(xml);
    REQUIRE(back.problems.empty());
    REQUIRE(back.templates.size() == mine.size());
    for (std::size_t i = 0; i < mine.size(); ++i) {
        CHECK(back.templates[i].name == mine[i].name);
        CHECK(back.templates[i].pattern == mine[i].pattern);
        CHECK(back.templates[i].allowPeriod == mine[i].allowPeriod);
        CHECK(back.templates[i].useDate == mine[i].useDate);
        CHECK(back.templates[i].id == 0);
    }

    const TemplateImport bad = templatesFromXml(
        "<fileNameTemplates><template name='Good' pattern='Good {A}'/>"
        "<template name='Statement' pattern='x {A}'/>"
        "<template name='Broken' pattern='Broken {A'/>"
        "<template name='good' pattern='Again {B}'/></fileNameTemplates>");
    CHECK(bad.templates.size() == 1);
    CHECK(bad.templates[0].useDate);   // defaults when missing
    CHECK(bad.problems.size() == 3);

    CHECK(templatesFromXml("not xml at all").templates.empty());
    CHECK(templatesFromXml("<other/>").problems.size() == 1);
    CHECK(templatesFromXml("<fileNameTemplates/>").problems.size() == 1);
}
