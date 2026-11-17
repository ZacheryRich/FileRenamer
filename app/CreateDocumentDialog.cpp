#include "CreateDocumentDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <map>
#include <set>

#include "QtHelpers.h"
#include "SignaturesDialog.h"
#include "finrenamer/DateSpec.h"
#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/VacationSheet.h"  // normalizeHeader
#include "finrenamer/WordTemplate.h"

using namespace finrenamer;
using namespace ui;

namespace {

constexpr int kMaxRecentTemplates = 12;

std::chrono::year_month_day todayDate()
{
    const QDate d = QDate::currentDate();
    return makeDate(d.year(), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

}  // namespace

CaseFile loadCaseFile(Database& db, std::int64_t caseId)
{
    CaseFile f;
    if (const auto c = db.getCase(caseId)) f.info = *c;
    f.parties = db.listParties(caseId);
    f.children = db.listChildren(caseId);
    f.contacts = db.listContacts(caseId);
    std::map<std::int64_t, Firm> firms;
    for (const auto& firm : db.listFirms()) firms[firm.id] = firm;
    for (const auto& a : db.listCaseAttorneys(caseId)) f.attorneys.push_back({a, firms[a.firmId]});
    return f;
}

CreateDocumentDialog::CreateDocumentDialog(Database& db, std::int64_t caseId, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId)
{
    runGuarded(this, [&] { file_ = loadCaseFile(db_, caseId_); });
    setWindowTitle(tr("Create Document - %1").arg(qstr(caseDisplayName(file_.info))));

    auto* intro = new QLabel(tr("Fills a Word template with this case's information. The template stays as it is; "
                                "the filled-in document is saved as a new file."));
    intro->setWordWrap(true);

    template_ = new QComboBox;
    template_->setObjectName("template");
    template_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    template_->setMinimumContentsLength(40);
    auto* browse = new QPushButton(tr("Browse..."));
    browse->setObjectName("browseTemplate");
    browse->setAutoDefault(false);
    auto* names = new QPushButton(tr("Field Names..."));
    names->setObjectName("fieldNames");
    names->setAutoDefault(false);
    names->setToolTip(tr("The merge field names to use in a template"));
    auto* templateRow = new QHBoxLayout;
    templateRow->addWidget(template_, 1);
    templateRow->addWidget(browse);
    templateRow->addWidget(names);

    signer_ = new QComboBox;
    signer_->setObjectName("signer");
    for (const auto& a : file_.attorneys)
        if (a.firm.ours) signer_->addItem(qstr(a.attorney.name), QVariant::fromValue<qlonglong>(a.attorney.id));
    if (signer_->count() == 0) {
        signer_->addItem(tr("(none of our attorneys is on this case: Assign Attorneys on the Case List)"),
                         QVariant::fromValue<qlonglong>(0));
        signer_->setEnabled(false);
    }
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const qlonglong lastSigner = settings.value(QStringLiteral("caseDocuments/case%1/signer").arg(caseId_)).toLongLong();
    const int at = signer_->findData(QVariant::fromValue(lastSigner));
    if (at >= 0) signer_->setCurrentIndex(at);

    check_ = new QLabel;
    check_->setObjectName("check");
    check_->setWordWrap(true);
    check_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* form = new QFormLayout;
    form->addRow(tr("Template:"), templateRow);
    form->addRow(QString(), check_);
    form->addRow(tr("Signed by:"), signer_);

    openAfter_ = new QCheckBox(tr("Open the document when it is made"));
    openAfter_->setObjectName("openAfter");
    openAfter_->setChecked(settings.value("caseDocuments/openAfter", true).toBool());

    status_ = new QLabel;
    status_->setObjectName("status");
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* buttons = new QDialogButtonBox;
    createBtn_ = buttons->addButton(tr("Create..."), QDialogButtonBox::AcceptRole);
    createBtn_->setObjectName("create");
    auto* close = buttons->addButton(QDialogButtonBox::Close);
    close->setObjectName("close");
    connect(createBtn_, &QPushButton::clicked, this, &CreateDocumentDialog::create);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(openAfter_);
    layout->addWidget(status_, 1);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &CreateDocumentDialog::browseTemplate);
    connect(names, &QPushButton::clicked, this, &CreateDocumentDialog::showFieldNames);
    connect(template_, &QComboBox::currentIndexChanged, this, &CreateDocumentDialog::templateChanged);

    fillTemplateList(QString());
    templateChanged();
    resize(760, 330);
}

CreateDocumentDialog::~CreateDocumentDialog()
{
    disconnectChildren(this);
}

void CreateDocumentDialog::fillTemplateList(const QString& select)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QStringList recent = settings.value("caseDocuments/templates").toStringList();
    const QSignalBlocker block(template_);
    template_->clear();
    for (const QString& f : recent) {
        template_->addItem(QFileInfo(f).completeBaseName() + "   (" + QDir::toNativeSeparators(QFileInfo(f).path()) + ")", f);
        template_->setItemData(template_->count() - 1, QDir::toNativeSeparators(f), Qt::ToolTipRole);
    }
    if (template_->count() == 0) template_->addItem(tr("(click Browse... to choose a Word template)"), QString());
    const int at = select.isEmpty() ? 0 : template_->findData(select);
    template_->setCurrentIndex(at >= 0 ? at : 0);
}

void CreateDocumentDialog::rememberTemplate(const QString& file)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    QStringList recent = settings.value("caseDocuments/templates").toStringList();
    recent.removeAll(file);
    recent.prepend(file);
    while (recent.size() > kMaxRecentTemplates) recent.removeLast();
    settings.setValue("caseDocuments/templates", recent);
}

QString CreateDocumentDialog::templateFile() const
{
    return template_->currentData().toString();
}

void CreateDocumentDialog::setTemplate(const QString& file)
{
    rememberTemplate(file);
    fillTemplateList(file);
    templateChanged();
}

void CreateDocumentDialog::browseTemplate()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString start = templateFile().isEmpty() ? settings.value("caseDocuments/templateDir").toString()
                                                   : QFileInfo(templateFile()).path();
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Choose a Word Template"), start, tr("Word documents (*.docx *.dotx)"));
    if (file.isEmpty()) return;
    settings.setValue("caseDocuments/templateDir", QFileInfo(file).path());
    setTemplate(file);
}

void CreateDocumentDialog::templateChanged()
{
    const QString file = templateFile();
    createBtn_->setEnabled(!file.isEmpty());
    check_->clear();
    if (file.isEmpty()) return;
    std::vector<std::string> fields;
    try {
        fields = wordTemplateFields(toPath(file));
    } catch (const std::exception& e) {
        check_->setText(qstr(e.what()));
        check_->setStyleSheet("color: #b00020;");
        createBtn_->setEnabled(false);
        return;
    }
    // Which of the template's fields the case can't fill (a typo, or a field for something else).
    const MergeData sample = caseMergeData(file_, 0, todayDate());
    std::set<std::string> known;
    for (const auto& [k, v] : sample.fields) known.insert(k);
    for (const auto& [g, items] : sample.groups)
        for (const MergeRecord& r : items)
            for (const auto& [k, v] : r) known.insert(k);
    // Group items' keys exist only when the case has such items: take them from a full example too.
    CaseFile full;
    full.parties = {CaseParty{1, 0, "x", PartyRole::Petitioner, true}, CaseParty{2, 0, "y", PartyRole::Respondent}};
    full.children = {CaseChild{1, 0, "c"}};
    full.contacts = {CaseContact{1, 0, "r", "n"}};
    full.attorneys = {{Attorney{1, 1, "a"}, Firm{1, "f", "", "", "", true}}, {Attorney{2, 2, "b"}, Firm{2, "g"}}};
    for (const auto& [g, items] : caseMergeData(full, 0, todayDate()).groups)
        for (const MergeRecord& r : items)
            for (const auto& [k, v] : r) known.insert(k);
    const std::set<std::string> groups = [&] {
        std::set<std::string> g;
        for (const auto& [name, items] : sample.groups) g.insert(name);
        return g;
    }();
    QStringList unknown;
    int count = 0;
    for (const std::string& name : fields) {
        const std::string key = normalizeHeader(name);
        std::string lower = key;
        bool marker = false;
        for (const char* prefix : {"begin", "end", "tablestart", "tableend"}) {
            const std::string p = prefix;
            if (lower.size() > p.size() && lower.compare(0, p.size(), p) == 0 && groups.count(lower.substr(p.size()))) marker = true;
        }
        if (marker || key == "signature") continue;
        ++count;
        if (!known.count(key)) unknown << qstr(name);
    }
    if (fields.empty()) {
        check_->setText(tr("This template has no merge fields yet, so nothing will be filled in. Click Field Names... "
                           "to see what you can use."));
        check_->setStyleSheet("color: #9a5b00;");
    } else if (!unknown.isEmpty()) {
        check_->setText(tr("%n field(s) in this template aren't case information and will be left blank: %1", nullptr,
                           static_cast<int>(unknown.size()))
                            .arg(unknown.join(", ")));
        check_->setStyleSheet("color: #9a5b00;");
    } else {
        check_->setText(tr("%n field(s), all of them case information.", nullptr, count));
        check_->setStyleSheet("color: palette(placeholder-text);");
    }
}

void CreateDocumentDialog::showFieldNames()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Field Names"));
    auto* intro = new QLabel(tr("In Word: Insert > Quick Parts > Field > MergeField, and type the name. Names ignore "
                                "capitals, spaces and underscores. To repeat part of a template for each person, put a "
                                "Begin_ field before it and the matching End_ field after it (in the same table cell or "
                                "paragraph level; across table rows, the rows repeat)."));
    intro->setWordWrap(true);
    auto* table = new QTableWidget;
    table->setObjectName("fieldTable");
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Field"), tr("What it holds")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(true);
    QString section;
    QStringList all;
    for (const FieldHelp& h : caseFieldReference()) {
        if (qstr(h.section) != section) {
            section = qstr(h.section);
            const int r = table->rowCount();
            table->insertRow(r);
            auto* head = new QTableWidgetItem(section);
            QFont bold = head->font();
            bold.setBold(true);
            head->setFont(bold);
            table->setItem(r, 0, head);
            table->setSpan(r, 0, 1, 2);
            all << "" << section;
        }
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(qstr(h.name)));
        table->setItem(r, 1, new QTableWidgetItem(qstr(h.description)));
        all << qstr(h.name) + "\t" + qstr(h.description);
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->resizeRowsToContents();
    auto* copy = new QPushButton(tr("Copy List"));
    copy->setToolTip(tr("Copy all the names and descriptions, e.g. to paste into a note"));
    connect(copy, &QPushButton::clicked, &dlg, [all] { QApplication::clipboard()->setText(all.join('\n').trimmed()); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(copy, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(intro);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);
    dlg.resize(820, 620);
    dlg.exec();
}

bool CreateDocumentDialog::createTo(const QString& output)
{
    const QString tmpl = templateFile();
    if (tmpl.isEmpty()) return false;
    const std::int64_t signerId = signer_->currentData().toLongLong();
    MergeData data = caseMergeData(file_, signerId, todayDate());
    // The signer's signature picture, from Vacation Letters' Signatures list.
    if (signerId != 0) data.signature = signatureFor(loadSignatures(), stdstr(signer_->currentText()));

    TemplateFillResult result;
    const QString today = QLocale(QLocale::English).toString(QDate::currentDate(), "MMMM d, yyyy");
    if (!runGuarded(this, [&] { result = fillWordTemplate(toPath(tmpl), toPath(output), data, stdstr(today)); }))
        return false;

    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("caseDocuments/case%1/signer").arg(caseId_), QVariant::fromValue<qlonglong>(signerId));
    rememberTemplate(tmpl);

    QStringList notes;
    notes << tr("Saved %1.").arg(QDir::toNativeSeparators(output));
    if (!result.unknownFields.empty()) {
        QStringList u;
        for (const auto& n : result.unknownFields) u << qstr(n);
        notes << tr("Left blank (not case information): %1.").arg(u.join(", "));
    }
    if (!result.unknownGroups.empty()) {
        QStringList u;
        for (const auto& n : result.unknownGroups) u << qstr(n);
        notes << tr("Blocks removed because there is no such group: %1.").arg(u.join(", "));
    }
    for (const auto& p : result.problems) notes << qstr(p);
    if (result.signatureField && !result.signatureAdded)
        notes << tr("No signature picture for %1 (add one under Vacation Letters > Signatures...).").arg(signer_->currentText());
    status_->setText(notes.join("\n"));
    return true;
}

void CreateDocumentDialog::create()
{
    const QString tmpl = templateFile();
    if (tmpl.isEmpty()) return;
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString folder = settings.value("caseDocuments/saveFolder", QDir::homePath()).toString();
    // e.g. "Case Information Sheet - Smith.docx"
    QString base = QFileInfo(tmpl).completeBaseName() + " - " + qstr(caseDisplayName(file_.info));
    base = qstr(sanitizeComponent(stdstr(base)));
    const QString output = QFileDialog::getSaveFileName(this, tr("Save Document"), QDir(folder).filePath(base + ".docx"),
                                                        tr("Word documents (*.docx)"));
    if (output.isEmpty()) return;
    if (QFileInfo(output).absoluteFilePath() == QFileInfo(tmpl).absoluteFilePath()) {
        QMessageBox::warning(this, tr("Create Document"), tr("Choose a different name: that is the template itself."));
        return;
    }
    settings.setValue("caseDocuments/saveFolder", QFileInfo(output).path());
    settings.setValue("caseDocuments/openAfter", openAfter_->isChecked());
    if (createTo(output) && openAfter_->isChecked()) QDesktopServices::openUrl(QUrl::fromLocalFile(output));
}
