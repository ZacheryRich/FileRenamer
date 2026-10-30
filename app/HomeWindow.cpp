#include "HomeWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "BatesDialog.h"
#include "CasePickerDialog.h"
#include "DeficiencyDialog.h"
#include "MainWindow.h"
#include "NoticesDialog.h"
#include "QtHelpers.h"
#include "RenameWindow.h"
#include "SplitDialog.h"
#include "UnlockDialog.h"
#include "VacationDialog.h"

using namespace finrenamer;
using namespace ui;

namespace {

QPushButton* makeTile(const QString& objectName, const QString& title, const QString& caption)
{
    auto* button = new QPushButton(QStringLiteral("%1\n%2").arg(title, caption));
    button->setObjectName(objectName);
    button->setMinimumHeight(64);
    button->setAutoDefault(false);
    button->setStyleSheet("QPushButton { text-align: left; padding: 8px 16px; }");
    return button;
}

}  // namespace

HomeWindow::HomeWindow(Database& db, const QString& dataFolder, QWidget* parent)
    : QMainWindow(parent), db_(db), dataFolder_(dataFolder)
{
    setWindowTitle(appTitle());

    auto* heading = new QLabel(appTitle());
    QFont big = heading->font();  // same title style as the Case List's case name
    big.setPointSizeF(big.pointSizeF() * 1.5);
    big.setBold(true);
    heading->setFont(big);
    auto* intro = new QLabel(tr("What would you like to work on?"));
    intro->setStyleSheet("color: palette(placeholder-text);");

    auto* cases = makeTile("caseList", tr("Case List"), tr("Add and edit cases, people and accounts"));
    auto* renamer = makeTile("fileRenamer", tr("File Renamer"), tr("Rename a folder of statements for a case"));
    auto* deficiency = makeTile("deficiencyList", tr("Deficiency List"), tr("Find missing statements; save a Word report"));

    auto* bates = makeTile("batesLog", tr("Bates Log"), tr("List a folder's files in Excel; add Bates numbers from Acrobat's log"));

    auto* vacation = makeTile("vacationLetters", tr("Vacation Letters"), tr("Edit the shared case list; draft vacation letters from the template"));

    auto* unlocker = makeTile("pdfUnlocker", tr("PDF Unlocker"), tr("Remove print, copy and edit restrictions; open password-protected PDFs"));

    auto* splitter = makeTile("pdfSplitter", tr("PDF Splitter"), tr("Split one big PDF into separate files at the pages you mark"));

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(heading);
    layout->addWidget(intro);
    layout->addSpacing(6);
    layout->addWidget(cases);
    layout->addWidget(renamer);
    layout->addWidget(deficiency);
    layout->addWidget(bates);
    layout->addWidget(vacation);
    layout->addWidget(unlocker);
    layout->addWidget(splitter);
    layout->addStretch();

    // Footer like the File Renamer's: information on the left, Close on the right.
    auto* dataLabel = new QLabel(tr("Data folder: %1").arg(dataFolder_));
    dataLabel->setObjectName("dataFolder");
    dataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setAutoDefault(false);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    auto* footer = new QHBoxLayout;
    footer->addWidget(dataLabel, 1);
    auto* licencesBtn = new QPushButton(tr("Licences..."));
    licencesBtn->setObjectName("licences");
    licencesBtn->setAutoDefault(false);
    connect(licencesBtn, &QPushButton::clicked, this, [this] { NoticesDialog(this).exec(); });
    footer->addWidget(licencesBtn);
    footer->addWidget(closeBtn);
    layout->addLayout(footer);
    setCentralWidget(central);

    connect(cases, &QPushButton::clicked, this, &HomeWindow::openCaseList);
    connect(renamer, &QPushButton::clicked, this, &HomeWindow::openFileRenamer);
    connect(deficiency, &QPushButton::clicked, this, &HomeWindow::openDeficiencyList);
    connect(bates, &QPushButton::clicked, this, &HomeWindow::openBatesLog);
    connect(vacation, &QPushButton::clicked, this, &HomeWindow::openVacationLetters);
    connect(unlocker, &QPushButton::clicked, this, &HomeWindow::openPdfUnlocker);
    connect(splitter, &QPushButton::clicked, this, &HomeWindow::openPdfSplitter);
    resize(520, 700);
}

HomeWindow::~HomeWindow()
{
    disconnectChildren(this);
}

void HomeWindow::closeEvent(QCloseEvent* event)
{
    // The tools are separate windows; closing the start screen ends the program, so ask first.
    const QList<QPointer<QWidget>> open = openTools();
    if (!open.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, appTitle(),
            tr("%n tool(s) are still open. Close them and exit?", nullptr, static_cast<int>(open.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        for (const QPointer<QWidget>& tool : open)
            if (tool) tool->close();  // each tool's own "unsaved changes?" question still applies
        for (const QPointer<QWidget>& tool : open) {
            if (tool && tool->isVisible()) {  // the user kept one open
                event->ignore();
                return;
            }
        }
    }
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) QApplication::quit();
}

std::optional<std::int64_t> HomeWindow::chooseCase(const QString& purpose)
{
    CasePickerDialog picker(db_, purpose, this);
    if (picker.exec() != QDialog::Accepted) return std::nullopt;
    return picker.selectedCaseId();
}

QList<QPointer<QWidget>> HomeWindow::openTools() const
{
    QList<QPointer<QWidget>> open;
    for (const QPointer<QWidget>& tool : {caseList_, renamer_, deficiency_, bates_, vacation_, unlocker_, splitter_})
        if (tool && tool->isVisible()) open.append(tool);
    return open;
}

void HomeWindow::showTool(QPointer<QWidget>& slot, const std::function<QWidget*()>& make)
{
    if (slot && slot->isVisible()) {  // already open: bring it forward
        if (slot->isMinimized()) slot->setWindowState(slot->windowState() & ~Qt::WindowMinimized);
        slot->raise();
        slot->activateWindow();
        return;
    }
    delete slot.data();  // closed but not yet cleaned up
    QWidget* tool = make();
    if (!tool) return;
    slot = tool;
    tool->setAttribute(Qt::WA_DeleteOnClose);
    tool->show();
    tool->raise();
    tool->activateWindow();
}

void HomeWindow::openCaseList()
{
    showTool(caseList_, [this]() -> QWidget* {
        auto* window = new MainWindow(db_, dataFolder_);
        connect(window, &MainWindow::fileRenamerRequested, this, &HomeWindow::openFileRenamerFor);
        connect(window, &MainWindow::deficiencyListRequested, this, &HomeWindow::openDeficiencyListFor);
        return window;
    });
}

void HomeWindow::openFileRenamer() { openFileRenamerImpl(std::nullopt); }
void HomeWindow::openFileRenamerFor(std::int64_t caseId) { openFileRenamerImpl(caseId); }

void HomeWindow::openFileRenamerImpl(std::optional<std::int64_t> caseId)
{
    showTool(renamer_, [this, caseId]() -> QWidget* {
        const auto id = caseId ? caseId : chooseCase(tr("File Renamer"));
        if (!id) return nullptr;
        auto* window = new RenameWindow(db_, *id);
        if (!window->promptForFolder()) {
            delete window;
            return nullptr;
        }
        // Accounts or people may have been added from the rename screen: refresh the Case List.
        connect(window, &QObject::destroyed, this, [this] {
            if (auto* list = qobject_cast<MainWindow*>(caseList_.data())) list->refreshAfterTool();
        });
        return window;
    });
}

void HomeWindow::openDeficiencyList() { openDeficiencyListImpl(std::nullopt); }
void HomeWindow::openDeficiencyListFor(std::int64_t caseId) { openDeficiencyListImpl(caseId); }

void HomeWindow::openDeficiencyListImpl(std::optional<std::int64_t> caseId)
{
    showTool(deficiency_, [this, caseId]() -> QWidget* {
        const auto id = caseId ? caseId : chooseCase(tr("Deficiency List"));
        if (!id) return nullptr;
        auto* dialog = new DeficiencyDialog(db_, *id);
        if (!dialog->hasFolders() && !dialog->promptForFolder()) {
            delete dialog;
            return nullptr;
        }
        return dialog;
    });
}

void HomeWindow::openBatesLog()
{
    showTool(bates_, [] { return new BatesDialog; });
}

void HomeWindow::openVacationLetters()
{
    showTool(vacation_, [] { return new VacationDialog; });
}

void HomeWindow::openPdfUnlocker()
{
    showTool(unlocker_, [] { return new UnlockDialog; });
}

void HomeWindow::openPdfSplitter()
{
    showTool(splitter_, [] { return new SplitDialog; });
}
