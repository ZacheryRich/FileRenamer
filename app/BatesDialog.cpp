#include "BatesDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include "QtHelpers.h"
#include "finrenamer/BatesLog.h"
#include "finrenamer/BatesSheet.h"
#include "finrenamer/DirectoryListing.h"

using namespace finrenamer;
using namespace ui;

namespace {

QString bulletList(const std::vector<std::string>& items, std::size_t limit = 15)
{
    QStringList lines;
    for (std::size_t i = 0; i < items.size() && i < limit; ++i) lines << QStringLiteral("    ") + qstr(items[i]);
    if (items.size() > limit) lines << QStringLiteral("    ...and %1 more").arg(items.size() - limit);
    return lines.join('\n');
}

// A line edit with a Browse button; returns the layout.
QHBoxLayout* pathRow(QLineEdit*& edit, const QString& objectName, QPushButton*& browse, const QString& browseName)
{
    edit = new QLineEdit;
    edit->setObjectName(objectName);
    browse = new QPushButton(QObject::tr("Browse..."));
    browse->setObjectName(browseName);
    browse->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

}  // namespace

BatesDialog::BatesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Bates Log - %1").arg(appTitle()));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);

    // ---- The spreadsheet ----
    QPushButton *browseSheet, *browseFolder, *browseLog;
    auto* sheetBox = new QGroupBox(tr("Spreadsheet"));
    auto* sheetLayout = new QVBoxLayout(sheetBox);
    auto* sheetHint = new QLabel(tr("Choose an existing spreadsheet to add to, or type a new name to create one."));
    sheetHint->setStyleSheet("color: palette(placeholder-text);");
    sheetLayout->addLayout(pathRow(workbook_, "workbook", browseSheet, "browseWorkbook"));
    sheetLayout->addWidget(sheetHint);

    // ---- 1. List files ----
    auto* listBox = new QGroupBox(tr("List the files in a folder"));
    auto* listLayout = new QVBoxLayout(listBox);
    auto* listNote = new QLabel(tr("Writes the name of every file in this folder and its subfolders into the spreadsheet. "
                                   "This works on its own - no Bates log is needed."));
    listNote->setWordWrap(true);
    listLayout->addWidget(listNote);
    listLayout->addLayout(pathRow(folder_, "folder", browseFolder, "browseFolder"));
    listBtn_ = new QPushButton(tr("List Files"));
    listBtn_->setObjectName("listFiles");
    listBtn_->setAutoDefault(false);
    auto* listButtons = new QHBoxLayout;
    listButtons->addStretch();
    listButtons->addWidget(listBtn_);
    listLayout->addLayout(listButtons);

    // ---- 2. Bates numbers ----
    auto* batesBox = new QGroupBox(tr("Add Bates numbers (optional)"));
    auto* batesLayout = new QVBoxLayout(batesBox);
    auto* batesNote = new QLabel(tr("Only if you have stamped the files in Adobe Acrobat: reads its Bates log (.log) and fills in "
                                    "Begin Bates and End Bates (columns B and C) for each file it finds in the spreadsheet. "
                                    "Leave this empty to skip it."));
    batesNote->setWordWrap(true);
    batesLayout->addWidget(batesNote);
    batesLayout->addLayout(pathRow(log_, "logFile", browseLog, "browseLog"));
    batesBtn_ = new QPushButton(tr("Add Bates Numbers"));
    batesBtn_->setObjectName("addBates");
    batesBtn_->setAutoDefault(false);
    auto* batesButtons = new QHBoxLayout;
    batesButtons->addStretch();
    batesButtons->addWidget(batesBtn_);
    batesLayout->addLayout(batesButtons);

    // ---- Results + footer ----
    results_ = new QPlainTextEdit;
    results_->setObjectName("results");
    results_->setReadOnly(true);
    results_->setPlaceholderText(tr("What was done shows up here."));
    results_->setLineWrapMode(QPlainTextEdit::NoWrap);

    openBtn_ = new QPushButton(tr("Open Spreadsheet"));
    openBtn_->setObjectName("openSheet");
    openBtn_->setAutoDefault(false);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setAutoDefault(false);
    auto* footer = new QHBoxLayout;
    footer->addWidget(openBtn_);
    footer->addStretch();
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(sheetBox);
    layout->addWidget(listBox);
    layout->addWidget(batesBox);
    layout->addWidget(results_, 1);
    layout->addLayout(footer);

    QSettings settings(settingsFile(), QSettings::IniFormat);
    workbook_->setText(QDir::toNativeSeparators(settings.value("bates/workbook").toString()));
    folder_->setText(QDir::toNativeSeparators(settings.value("bates/folder").toString()));
    log_->setText(QDir::toNativeSeparators(settings.value("bates/log").toString()));

    connect(browseSheet, &QPushButton::clicked, this, &BatesDialog::browseWorkbook);
    connect(browseFolder, &QPushButton::clicked, this, &BatesDialog::browseFolder);
    connect(browseLog, &QPushButton::clicked, this, &BatesDialog::browseLog);
    connect(listBtn_, &QPushButton::clicked, this, [this] { listFiles(); });
    connect(batesBtn_, &QPushButton::clicked, this, [this] { addBatesNumbers(); });
    connect(openBtn_, &QPushButton::clicked, this, &BatesDialog::openWorkbook);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    for (auto* edit : {workbook_, folder_, log_}) connect(edit, &QLineEdit::textChanged, this, &BatesDialog::updateButtons);

    updateButtons();
    resize(720, 640);
}

BatesDialog::~BatesDialog()
{
    disconnectChildren(this);
}

QString BatesDialog::workbookPath() const
{
    QString p = workbook_->text().trimmed();
    if (!p.isEmpty() && !p.endsWith(".xlsx", Qt::CaseInsensitive)) p += ".xlsx";
    return p;
}

void BatesDialog::setWorkbook(const QString& path) { workbook_->setText(path); }
void BatesDialog::setFolder(const QString& path) { folder_->setText(path); }
void BatesDialog::setLogFile(const QString& path) { log_->setText(path); }
QString BatesDialog::resultText() const { return results_->toPlainText(); }

void BatesDialog::updateButtons()
{
    const bool haveSheet = !workbook_->text().trimmed().isEmpty();
    listBtn_->setEnabled(haveSheet && !folder_->text().trimmed().isEmpty());
    batesBtn_->setEnabled(haveSheet && !log_->text().trimmed().isEmpty());
    openBtn_->setEnabled(haveSheet && QFileInfo::exists(workbookPath()));
}

void BatesDialog::report(const QString& text, bool error)
{
    results_->setPlainText(text);
    results_->setStyleSheet(error ? "QPlainTextEdit { color: #c42b1c; }" : QString());
    updateButtons();
}

void BatesDialog::browseWorkbook()
{
    const QString start = workbook_->text().trimmed().isEmpty() ? QDir::homePath() : workbook_->text();
    // Not asking "replace it?" -- an existing spreadsheet is added to, never replaced.
    const QString file = QFileDialog::getSaveFileName(this, tr("Spreadsheet to add to or create"), start,
                                                      tr("Excel files (*.xlsx)"), nullptr,
                                                      QFileDialog::DontConfirmOverwrite);
    if (!file.isEmpty()) workbook_->setText(QDir::toNativeSeparators(file));
}

void BatesDialog::browseFolder()
{
    const QString start = folder_->text().trimmed().isEmpty() ? QDir::homePath() : folder_->text();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder to list"), start);
    if (!dir.isEmpty()) folder_->setText(QDir::toNativeSeparators(dir));
}

void BatesDialog::browseLog()
{
    const QString start = log_->text().trimmed().isEmpty() ? QDir::homePath() : log_->text();
    const QString file = QFileDialog::getOpenFileName(this, tr("Bates log"), start, tr("Bates logs (*.log);;All files (*)"));
    if (!file.isEmpty()) log_->setText(QDir::toNativeSeparators(file));
}

void BatesDialog::openWorkbook()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(workbookPath()));
}

bool BatesDialog::listFiles()
{
    const QString sheet = workbookPath();
    const QString folder = folder_->text().trimmed();
    if (sheet.isEmpty() || folder.isEmpty()) return false;
    if (!QFileInfo(folder).isDir()) {
        report(tr("The folder \"%1\" doesn't exist.").arg(QDir::toNativeSeparators(folder)), true);
        return false;
    }
    if (!QFileInfo(sheet).absoluteDir().exists()) {
        report(tr("The folder for the spreadsheet doesn't exist:\n%1").arg(QDir::toNativeSeparators(QFileInfo(sheet).absolutePath())), true);
        return false;
    }

    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("bates/workbook", sheet);
    settings.setValue("bates/folder", folder);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        const auto listing = listDirectoryTree(toPath(folder), toPath(sheet));
        if (listing.empty()) {
            QApplication::restoreOverrideCursor();
            report(tr("There are no files in that folder."), true);
            return false;
        }
        // No Bates log chosen: a plain file list, without the Begin/End Bates headers.
        const ListingResult r = writeListing(toPath(sheet), listing, !log_->text().trimmed().isEmpty());
        QApplication::restoreOverrideCursor();
        report(tr("%1 %2 and %3 in \"%4\" (rows %5 to %6).\n\n"
                "If you later Bates-stamp these files in Acrobat, choose its log below and click Add Bates Numbers.")
                 .arg(r.created ? tr("Created the spreadsheet and listed") : tr("Added"))
                 .arg(tr("%n folder(s)", nullptr, static_cast<int>(r.folders)))
                 .arg(tr("%n file(s)", nullptr, static_cast<int>(r.files)))
                 .arg(QFileInfo(sheet).fileName())
                 .arg(r.firstRow)
                 .arg(r.lastRow));
        return true;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        report(qstr(e.what()), true);
        return false;
    }
}

bool BatesDialog::addBatesNumbers()
{
    const QString sheet = workbookPath();
    const QString logFile = log_->text().trimmed();
    if (sheet.isEmpty() || logFile.isEmpty()) return false;
    if (!QFileInfo::exists(sheet)) {
        report(tr("The spreadsheet doesn't exist yet. Use List Files first."), true);
        return false;
    }
    if (!QFileInfo::exists(logFile)) {
        report(tr("The Bates log \"%1\" doesn't exist.").arg(QDir::toNativeSeparators(logFile)), true);
        return false;
    }

    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("bates/workbook", sheet);
    settings.setValue("bates/log", logFile);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        const BatesLog log = parseBatesLog(toPath(logFile));
        if (log.lines == 0) {
            QApplication::restoreOverrideCursor();
            report(tr("That log has no file names with Bates numbers in it. Is it the .log file Acrobat wrote?"), true);
            return false;
        }
        const BatesApplyResult r = applyBatesLog(toPath(sheet), log);
        QApplication::restoreOverrideCursor();

        QString text = tr("Added Bates numbers to %1 of %2 files in \"%3\".")
                           .arg(r.matched).arg(r.fileRows).arg(QFileInfo(sheet).fileName());
        if (log.duplicates > 0)
            text += tr("\n\nThe log listed %n name(s) more than once; the last numbers for each were used.", nullptr,
                       static_cast<int>(log.duplicates));
        if (!r.unmatchedNames.empty())
            text += tr("\n\nNot in the log, so left blank (%1):\n%2").arg(r.unmatchedNames.size()).arg(bulletList(r.unmatchedNames));
        if (!r.unusedLogNames.empty())
            text += tr("\n\nIn the log but not in the spreadsheet (%1):\n%2").arg(r.unusedLogNames.size()).arg(bulletList(r.unusedLogNames));
        if (!r.sharedNames.empty())
            text += tr("\n\nOn more than one row, so those rows got the same numbers - check them (%1):\n%2")
                        .arg(r.sharedNames.size()).arg(bulletList(r.sharedNames));
        report(text);
        return true;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        report(qstr(e.what()), true);
        return false;
    }
}
