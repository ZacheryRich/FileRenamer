#include "DeficiencyDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "CaseFolderList.h"
#include "QtHelpers.h"
#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

using namespace finrenamer;
using namespace ui;
namespace chr = std::chrono;
namespace fs = std::filesystem;

namespace {

constexpr int kAccountIdRole = Qt::UserRole;

Month currentLastCompletedMonth()
{
    const chr::year_month_day today{chr::floor<chr::days>(chr::system_clock::now())};
    return lastCompletedMonth(today);
}

QString settingsPrefix(std::int64_t caseId)
{
    return QStringLiteral("deficiency/case%1/").arg(caseId);
}

QString html(const std::string& utf8)
{
    return qstr(utf8).toHtmlEscaped();
}

QString reportHtml(const DeficiencyReport& r)
{
    QString out;
    out += QStringLiteral("<h2 style='margin-bottom:0'>%1</h2><p style='margin-top:2px'>%2</p>")
               .arg(html(r.title), html(r.rangeLine));
    for (const ReportTable& t : r.tables) {
        out += QStringLiteral("<h3 style='margin-bottom:0'>%1</h3>").arg(html(t.heading));
        if (!t.note.empty()) out += QStringLiteral("<p style='margin:0'><i>%1</i></p>").arg(html(t.note));
        out += QStringLiteral("<table border='1' cellspacing='0' cellpadding='4' width='100%'>");
        out += QStringLiteral("<tr bgcolor='#e5e7eb'><th align='left' width='14%'>Year</th>");
        if (r.showFound) out += QStringLiteral("<th align='left'>Months Found</th>");
        if (r.showMissing) out += QStringLiteral("<th align='left'>Months Missing</th>");
        out += QStringLiteral("</tr>");
        for (const ReportRow& row : t.rows) {
            out += QStringLiteral("<tr><td>%1</td>").arg(html(row.year));
            if (r.showFound) out += QStringLiteral("<td>%1</td>").arg(html(row.found));
            if (r.showMissing) out += QStringLiteral("<td>%1</td>").arg(html(row.missing));
            out += QStringLiteral("</tr>");
        }
        out += QStringLiteral("</table>");
    }
    return out;
}

}  // namespace

DeficiencyDialog::DeficiencyDialog(Database& db, std::int64_t caseId, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId)
{
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    std::optional<ClientCase> c;
    runGuarded(this, [&] { c = db_.getCase(caseId_); });
    caseName_ = c ? qstr(c->clientName) : QString();
    setWindowTitle(tr("Deficiency List - %1").arg(caseName_));

    auto* intro = new QLabel(tr(
        "Searches the folders below for statements already renamed with this program and lists the "
        "months each account is missing. Nothing in the folders is changed."));
    intro->setWordWrap(true);

    // ---- Folders (shared with Update File Names) ----
    folderList_ = new CaseFolderList(caseId_);
    subfolders_ = new QCheckBox(tr("Include subfolders"));
    subfolders_->setObjectName("subfolders");
    folderList_->addSideWidget(subfolders_);

    // ---- Date range ----
    auto makeMonthBox = [](const char* name) {
        auto* box = new QComboBox;
        box->setObjectName(name);
        for (int m = 1; m <= 12; ++m) box->addItem(QLocale(QLocale::English).monthName(m), m);
        return box;
    };
    auto makeYearBox = [](const char* name) {
        auto* box = new QSpinBox;
        box->setObjectName(name);
        box->setRange(1900, 2200);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setMinimumWidth(60);
        return box;
    };
    fromMonthBox_ = makeMonthBox("fromMonth");
    fromYear_ = makeYearBox("fromYear");
    toMonthBox_ = makeMonthBox("toMonth");
    toYear_ = makeYearBox("toYear");
    present_ = new QCheckBox(tr("Through present"));
    present_->setObjectName("present");
    present_->setToolTip(tr("The range ends with \"Present\": up to last month, the last month "
                            "that can have a statement."));

    auto* rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("From")));
    rangeRow->addWidget(fromMonthBox_);
    rangeRow->addWidget(fromYear_);
    rangeRow->addSpacing(12);
    rangeRow->addWidget(new QLabel(tr("To")));
    rangeRow->addWidget(toMonthBox_);
    rangeRow->addWidget(toYear_);
    rangeRow->addWidget(present_);
    rangeRow->addStretch();

    // ---- Columns ----
    found_ = new QCheckBox(tr("Months found"));
    found_->setObjectName("found");
    missing_ = new QCheckBox(tr("Months missing"));
    missing_->setObjectName("missing");
    auto* columnsRow = new QHBoxLayout;
    columnsRow->addWidget(new QLabel(tr("Show:")));
    columnsRow->addWidget(found_);
    columnsRow->addWidget(missing_);
    columnsRow->addStretch();

    // ---- Accounts ----
    accountList_ = new QListWidget;
    accountList_->setObjectName("accounts");
    auto* allBtn = new QPushButton(tr("All"));
    auto* noneBtn = new QPushButton(tr("None"));
    auto* accountButtons = new QVBoxLayout;
    accountButtons->addWidget(allBtn);
    accountButtons->addWidget(noneBtn);
    accountButtons->addStretch();
    auto* accountsRow = new QHBoxLayout;
    accountsRow->addWidget(accountList_, 1);
    accountsRow->addLayout(accountButtons);
    auto* accountsBox = new QGroupBox(tr("Accounts to include"));
    accountsBox->setLayout(accountsRow);

    // ---- Options column (left) and preview (right) ----
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addLayout(rangeRow);
    leftLayout->addLayout(columnsRow);
    leftLayout->addWidget(accountsBox, 1);

    preview_ = new QTextBrowser;
    preview_->setObjectName("preview");

    auto* top = new QHBoxLayout;
    top->addWidget(left, 2);
    top->addWidget(preview_, 3);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(folderList_);
    auto* lower = new QWidget;
    lower->setLayout(top);
    top->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(lower);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({130, 470});

    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setWordWrap(true);
    unmatchedBtn_ = new QPushButton(tr("Files Not Matched..."));
    unmatchedBtn_->setObjectName("unmatched");
    looseBtn_ = new QPushButton(tr("Other Name Styles..."));
    looseBtn_->setObjectName("loose");
    looseBtn_->setToolTip(tr("Files counted although their names aren't in this program's format"));
    auto* rescanBtn = new QPushButton(tr("Scan Again"));
    saveBtn_ = new QPushButton(tr("Save as Word..."));
    saveBtn_->setObjectName("save");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(unmatchedBtn_, QDialogButtonBox::ActionRole);
    buttons->addButton(looseBtn_, QDialogButtonBox::ActionRole);
    buttons->addButton(rescanBtn, QDialogButtonBox::ActionRole);
    buttons->addButton(saveBtn_, QDialogButtonBox::AcceptRole);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(summary_, 1);
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(splitter, 1);
    layout->addLayout(bottom);

    for (auto* b : findChildren<QPushButton*>()) b->setAutoDefault(false);

    // ---- Accounts and saved settings ----
    runGuarded(this, [&] {
        for (Account& a : db_.loadAccounts(caseId_))
            if (!a.isCombined()) accounts_.push_back(std::move(a));
    });
    for (const Account& a : accounts_) {
        auto* item = new QListWidgetItem(qstr(accountLabel(a)), accountList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(kAccountIdRole, QVariant::fromValue<qlonglong>(a.id));
    }
    loadSettings();

    // ---- Wiring ----
    connect(folderList_, &CaseFolderList::changed, this, &DeficiencyDialog::rescan);
    connect(subfolders_, &QCheckBox::toggled, this, [this] {
        saveSettings();
        rescan();
    });
    auto userEdited = [this] {
        if (loading_) return;
        rangeChosen_ = true;
        refresh();
    };
    connect(fromMonthBox_, &QComboBox::currentIndexChanged, this, userEdited);
    connect(toMonthBox_, &QComboBox::currentIndexChanged, this, userEdited);
    connect(fromYear_, &QSpinBox::valueChanged, this, userEdited);
    connect(toYear_, &QSpinBox::valueChanged, this, userEdited);
    connect(present_, &QCheckBox::toggled, this, userEdited);
    connect(found_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_) return;
        if (!on && !missing_->isChecked()) missing_->setChecked(true);  // at least one stays on
        refresh();
    });
    connect(missing_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_) return;
        if (!on && !found_->isChecked()) found_->setChecked(true);
        refresh();
    });
    connect(accountList_, &QListWidget::itemChanged, this, [this] { if (!loading_) refresh(); });
    connect(allBtn, &QPushButton::clicked, this, [this] {
        loading_ = true;
        for (int i = 0; i < accountList_->count(); ++i) accountList_->item(i)->setCheckState(Qt::Checked);
        loading_ = false;
        refresh();
    });
    connect(noneBtn, &QPushButton::clicked, this, [this] {
        loading_ = true;
        for (int i = 0; i < accountList_->count(); ++i) accountList_->item(i)->setCheckState(Qt::Unchecked);
        loading_ = false;
        refresh();
    });
    connect(rescanBtn, &QPushButton::clicked, this, &DeficiencyDialog::rescan);
    connect(unmatchedBtn_, &QPushButton::clicked, this, &DeficiencyDialog::showUnmatched);
    connect(looseBtn_, &QPushButton::clicked, this, &DeficiencyDialog::showLoose);
    connect(saveBtn_, &QPushButton::clicked, this, &DeficiencyDialog::saveAs);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(1150, 700);
    rescan();
}

DeficiencyDialog::~DeficiencyDialog()
{
    disconnectChildren(this);
}

// ---------------------------------------------------------------------------
// Folder list

bool DeficiencyDialog::promptForFolder()
{
    return folderList_->promptForFolder();
}

void DeficiencyDialog::addFolder(const QString& folder)
{
    folderList_->addFolder(folder);
}

bool DeficiencyDialog::hasFolders() const
{
    return folderList_->hasFolders();
}

// ---------------------------------------------------------------------------
// Range helpers

Month DeficiencyDialog::fromMonth() const
{
    return Month{chr::year{fromYear_->value()}, chr::month{static_cast<unsigned>(fromMonthBox_->currentData().toInt())}};
}

Month DeficiencyDialog::toMonth() const
{
    if (present_->isChecked()) return currentLastCompletedMonth();
    return Month{chr::year{toYear_->value()}, chr::month{static_cast<unsigned>(toMonthBox_->currentData().toInt())}};
}

void DeficiencyDialog::setFromMonth(Month m)
{
    const bool was = loading_;
    loading_ = true;
    fromYear_->setValue(static_cast<int>(m.year()));
    fromMonthBox_->setCurrentIndex(static_cast<int>(static_cast<unsigned>(m.month())) - 1);
    loading_ = was;
}

void DeficiencyDialog::setToMonth(Month m)
{
    const bool was = loading_;
    loading_ = true;
    toYear_->setValue(static_cast<int>(m.year()));
    toMonthBox_->setCurrentIndex(static_cast<int>(static_cast<unsigned>(m.month())) - 1);
    loading_ = was;
}

// ---------------------------------------------------------------------------
// Settings

void DeficiencyDialog::loadSettings()
{
    QSettings s(settingsFile(), QSettings::IniFormat);
    const QString p = settingsPrefix(caseId_);
    loading_ = true;
    subfolders_->setChecked(s.value("deficiency/includeSubfolders", true).toBool());
    found_->setChecked(s.value(p + "showFound", true).toBool());
    missing_->setChecked(s.value(p + "showMissing", true).toBool());
    if (!found_->isChecked() && !missing_->isChecked()) found_->setChecked(true);
    present_->setChecked(s.value(p + "present", true).toBool());

    const Month last = currentLastCompletedMonth();
    setToMonth(s.contains(p + "toIndex") ? monthFromIndex(s.value(p + "toIndex").toInt()) : last);
    if (s.contains(p + "fromIndex")) {
        setFromMonth(monthFromIndex(s.value(p + "fromIndex").toInt()));
        rangeChosen_ = true;
    } else {
        setFromMonth(last);  // replaced by the earliest statement found, once scanned
    }
    toMonthBox_->setEnabled(!present_->isChecked());
    toYear_->setEnabled(!present_->isChecked());
    loading_ = false;
}

void DeficiencyDialog::saveSettings() const
{
    if (loading_) return;
    QSettings s(settingsFile(), QSettings::IniFormat);
    const QString p = settingsPrefix(caseId_);
    s.setValue("deficiency/includeSubfolders", subfolders_->isChecked());
    s.setValue(p + "showFound", found_->isChecked());
    s.setValue(p + "showMissing", missing_->isChecked());
    s.setValue(p + "present", present_->isChecked());
    if (rangeChosen_) {
        s.setValue(p + "fromIndex", monthIndex(fromMonth()));
        s.setValue(p + "toIndex", monthIndex(Month{chr::year{toYear_->value()},
                                                   chr::month{static_cast<unsigned>(toMonthBox_->currentData().toInt())}}));
    }
}

// ---------------------------------------------------------------------------
// Scan / report

void DeficiencyDialog::rescan()
{
    scan_ = {};
    if (folderList_->hasFolders()) {
        const std::vector<fs::path> roots = folderList_->paths();
        const bool sub = subfolders_->isChecked();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        runGuarded(this, [&] {
            scan_ = scanStatements(roots, sub, db_.loadAccounts(caseId_), db_.oldAccountNames(caseId_));
        });
        QApplication::restoreOverrideCursor();
    }

    // Until the user picks a start, begin at the earliest statement found.
    if (!rangeChosen_) {
        std::optional<Month> earliest;
        for (const auto& [id, months] : scan_.covered)
            if (!months.empty() && (!earliest || *months.begin() < *earliest)) earliest = *months.begin();
        setFromMonth(earliest ? *earliest : currentLastCompletedMonth());
    }
    refresh();
}

void DeficiencyDialog::refresh()
{
    if (loading_) return;
    toMonthBox_->setEnabled(!present_->isChecked());
    toYear_->setEnabled(!present_->isChecked());
    if (present_->isChecked()) setToMonth(currentLastCompletedMonth());
    saveSettings();

    std::vector<Account> chosen;
    for (int i = 0; i < accountList_->count(); ++i) {
        if (accountList_->item(i)->checkState() != Qt::Checked) continue;
        const std::int64_t id = accountList_->item(i)->data(kAccountIdRole).toLongLong();
        for (const Account& a : accounts_)
            if (a.id == id) chosen.push_back(a);
    }

    const Month from = fromMonth();
    const Month to = toMonth();
    ReportSettings settings;
    settings.from = from;
    settings.to = to;
    settings.throughPresent = present_->isChecked();
    settings.showFound = found_->isChecked();
    settings.showMissing = missing_->isChecked();

    report_ = {};
    QString problem;
    if (monthIndex(to) < monthIndex(from))
        problem = tr("The end of the range is before its start.");
    else if (chosen.empty())
        problem = tr("Tick at least one account.");
    else if (!folderList_->hasFolders())
        problem = tr("Add the folders to search.");

    if (problem.isEmpty()) {
        report_ = buildDeficiencyReport(stdstr(caseName_), analyzeCoverage(chosen, scan_, from, to), settings);
        preview_->setHtml(reportHtml(report_));
    } else {
        preview_->setHtml(QStringLiteral("<p><i>%1</i></p>").arg(problem.toHtmlEscaped()));
    }
    saveBtn_->setEnabled(problem.isEmpty());

    if (!folderList_->hasFolders()) {
        summary_->setText(tr("Add the folders to search."));
    } else {
        const std::size_t unmatched = scan_.unmatched.size();
        QString text = tr("%1 PDF(s) found: %2 matched to accounts, %3 not matched.")
                           .arg(scan_.pdfCount)
                           .arg(scan_.matchedCount)
                           .arg(unmatched);
        if (!scan_.loose.empty())
            text += tr("  %1 matched were named in another style (see Other Name Styles).").arg(scan_.loose.size());
        summary_->setText(text);
    }
    unmatchedBtn_->setEnabled(!scan_.unmatched.empty());
    looseBtn_->setEnabled(!scan_.loose.empty());
}

QString DeficiencyDialog::previewText() const
{
    return preview_->toPlainText();
}

void DeficiencyDialog::showFileList(const QString& title, const QString& text,
                                    const std::vector<fs::path>& files)
{
    QStringList lines;
    for (std::size_t i = 0; i < files.size() && i < 500; ++i) lines << QDir::toNativeSeparators(qpath(files[i]));
    if (files.size() > 500) lines << tr("...and %1 more.").arg(files.size() - 500);

    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Close, this);
    box.setDetailedText(lines.join('\n'));
    box.exec();
}

void DeficiencyDialog::showUnmatched()
{
    showFileList(tr("Files not matched"),
                 tr("These %1 PDF(s) weren't counted: no readable date, or no account of this case "
                    "(its last four digits plus the institution or type) in the name. Rename them with "
                    "File Renamer, then scan again.")
                     .arg(scan_.unmatched.size()),
                 scan_.unmatched);
}

void DeficiencyDialog::showLoose()
{
    showFileList(tr("Other name styles"),
                 tr("These %1 PDF(s) aren't named the way this program names files, but each shows a "
                    "date and an account's last four digits with its institution or type, so they were "
                    "counted. Check the list; File Renamer can rename them properly.")
                     .arg(scan_.loose.size()),
                 scan_.loose);
}

// ---------------------------------------------------------------------------
// Saving

bool DeficiencyDialog::saveTo(const fs::path& file, QString* error)
{
    try {
        writeDocx(file, report_);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = qstr(e.what());
        return false;
    }
}

void DeficiencyDialog::saveAs()
{
    QSettings s(settingsFile(), QSettings::IniFormat);
    QString dir = s.value("deficiency/lastDir").toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        const QStringList f = folderList_->folders();
        dir = f.isEmpty() ? QDir::homePath() : f.first();
    }
    const QString base = qstr(sanitizeComponent(stdstr(caseName_) + " Deficiency List"));
    const QString file = QFileDialog::getSaveFileName(this, tr("Save Deficiency List"),
                                                      QDir(dir).filePath(base + ".docx"),
                                                      tr("Word documents (*.docx)"));
    if (file.isEmpty()) return;
    s.setValue("deficiency/lastDir", QFileInfo(file).absolutePath());

    QString error;
    if (!saveTo(toPath(file), &error)) {
        QMessageBox::warning(this, tr("Couldn't save"),
                             tr("The Word document couldn't be saved. If a file with that name is open "
                                "in Word, close it and try again.\n\n%1").arg(error));
        return;
    }
    if (QMessageBox::question(this, tr("Saved"), tr("Saved %1.\n\nOpen it now?").arg(QDir::toNativeSeparators(file)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}
