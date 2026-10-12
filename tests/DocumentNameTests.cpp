#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/DocumentName.h"
#include "finrenamer/RenamePlan.h"
#include "finrenamer/RenameSession.h"

using namespace finrenamer;
using testing::TempDir;

namespace {

DocumentInfo email(std::string from, std::string to, std::string description = {})
{
    DocumentInfo d;
    d.kind = DocKind::Email;
    d.sender = std::move(from);
    d.recipient = std::move(to);
    d.description = std::move(description);
    return d;
}

DocumentInfo text(TextStyle style, std::string label, std::string a, std::string b = {}, std::string description = {})
{
    DocumentInfo d;
    d.kind = DocKind::Text;
    d.textStyle = style;
    d.label = std::move(label);
    d.sender = std::move(a);
    d.recipient = std::move(b);
    d.description = std::move(description);
    return d;
}

const DateSpec kJan31 = SingleDate{makeDate(2026, 1, 31)};
const DateSpec kJanPeriod = Period{makeDate(2026, 1, 1), makeDate(2026, 1, 31)};

}  // namespace

TEST_CASE("Email names: date, Email from sender to recipient (description)")
{
    CHECK(buildDocumentFilename(email("John Smith", "Jane Doe", "Re settlement offer"), kJan31) ==
          "2026.01.31 Email from John Smith to Jane Doe (Re settlement offer).pdf");
    CHECK(buildDocumentFilename(email("John", "Jane"), kJan31) == "2026.01.31 Email from John to Jane.pdf");
    CHECK(buildDocumentFilename(email("John", "Jane"), kJan31, "") == "2026.01.31 Email from John to Jane");
}

TEST_CASE("Text names: the label is typed, three styles")
{
    CHECK(buildDocumentFilename(text(TextStyle::Single, "WhatsApp", "John", "Jane", "Re pickup time"), kJan31) ==
          "2026.01.31 WhatsApp from John to Jane (Re pickup time).pdf");
    CHECK(buildDocumentFilename(text(TextStyle::Conversation, "Our Family Wizard", "John", "Jane", "January"), kJanPeriod) ==
          "2026.01.01 - 2026.01.31 Our Family Wizard between John and Jane (January).pdf");
    CHECK(buildDocumentFilename(text(TextStyle::Group, "Texts", "the Smith family chat"), kJanPeriod) ==
          "2026.01.01 - 2026.01.31 Texts with the Smith family chat.pdf");
}

TEST_CASE("Letter and Other names")
{
    DocumentInfo letter;
    letter.kind = DocKind::Letter;
    letter.label = "Ltr";
    letter.sender = "Acme Bank";
    letter.description = "demand";
    CHECK(buildDocumentFilename(letter, kJan31) == "2026.01.31 Ltr from Acme Bank (demand).pdf");

    DocumentInfo other;
    other.kind = DocKind::Other;
    other.label = "Court Order";
    CHECK(buildDocumentFilename(other, kJan31) == "2026.01.31 Court Order.pdf");
    other.description = "custody";
    CHECK(buildDocumentFilename(other, kJanPeriod) == "2026.01.01 - 2026.01.31 Court Order (custody).pdf");
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
    CHECK(documentMissing(text(TextStyle::Single, "", "", "")) == "Needs a label, sender and recipient");
    CHECK(documentMissing(text(TextStyle::Conversation, "Texts", "John", "")) == "Needs the second person");
    CHECK(documentMissing(text(TextStyle::Group, "Texts", "")) == "Needs the people or group name");
    DocumentInfo other;
    other.kind = DocKind::Other;
    CHECK(documentMissing(other) == "Needs a label");
    other.label = "Photo";
    CHECK_FALSE(documentMissing(other));  // the description is optional
    DocumentInfo letter;
    letter.kind = DocKind::Letter;
    letter.label = "Ltr";
    CHECK(documentMissing(letter) == "Needs sender");
}

TEST_CASE("Which dates each kind accepts")
{
    CHECK_FALSE(validateDocumentDate(email("a", "b"), kJan31));
    CHECK(validateDocumentDate(email("a", "b"), kJanPeriod));
    CHECK(validateDocumentDate(text(TextStyle::Single, "T", "a", "b"), kJanPeriod));
    CHECK_FALSE(validateDocumentDate(text(TextStyle::Conversation, "T", "a", "b"), kJanPeriod));
    CHECK_FALSE(validateDocumentDate(text(TextStyle::Group, "T", "a"), kJanPeriod));
    DocumentInfo other;
    other.kind = DocKind::Other;
    CHECK_FALSE(validateDocumentDate(other, kJanPeriod));
    CHECK(validateDocumentDate(other, Quarter{2026, 1}));
    CHECK(allowedDateModes(email("a", "b")).period == false);
    CHECK(allowedDateModes(other).period);
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
    inputs[2].date = kJanPeriod;               // a period isn't allowed for email

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

TEST_CASE("Carry-forward after a document copies kind, label and people with the same date")
{
    TempDir dir;
    dir.touch("a.pdf");
    dir.touch("b.pdf");
    dir.touch("c.pdf");
    RenameSession session(dir.path(), {});
    REQUIRE(session.size() == 3);

    session.row(0).document = text(TextStyle::Conversation, "WhatsApp", "John", "Jane", "January");
    session.row(0).date = kJanPeriod;
    CHECK(session.carryForward(0, 1));
    CHECK(session.row(1).document.kind == DocKind::Text);
    CHECK(session.row(1).document.label == "WhatsApp");
    CHECK(session.row(1).document.sender == "John");
    CHECK(session.row(1).document.description.empty());
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
