#include "HomeWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include "CasePickerDialog.h"
#include "DeficiencyDialog.h"
#include "MainWindow.h"
#include "QtHelpers.h"
#include "RenameWindow.h"

using namespace finrenamer;
using namespace ui;

namespace {

QPushButton* makeTile(const QString& objectName, const QString& title, const QString& caption)
{
    auto* button = new QPushButton(QStringLiteral("%1\n%2").arg(title, caption));
    button->setObjectName(objectName);
    button->setMinimumSize(300, 96);
    button->setAutoDefault(false);
    QFont font = button->font();
    font.setPointSizeF(font.pointSizeF() + 3);
    button->setFont(font);
    button->setStyleSheet("QPushButton { text-align: left; padding: 12px 18px; }");
    return button;
}

}  // namespace

HomeWindow::HomeWindow(Database& db, const QString& dataFolder, QWidget* parent)
    : QMainWindow(parent), db_(db), dataFolder_(dataFolder)
{
    setWindowTitle(tr("FinRenamer"));

    auto* heading = new QLabel(tr("FinRenamer"));
    QFont big = heading->font();
    big.setPointSizeF(big.pointSizeF() + 12);
    big.setBold(true);
    heading->setFont(big);

    auto* cases = makeTile("caseList", tr("Case List"), tr("Add and edit cases, people and accounts"));
    auto* renamer = makeTile("fileRenamer", tr("File Renamer"), tr("Rename a folder of statements for a case"));
    auto* deficiency = makeTile("deficiencyList", tr("Deficiency List"), tr("Find missing statements; save a Word report"));

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(40, 30, 40, 30);
    layout->setSpacing(14);
    layout->addWidget(heading);
    layout->addSpacing(8);
    layout->addWidget(cases);
    layout->addWidget(renamer);
    layout->addWidget(deficiency);
    layout->addStretch();
    setCentralWidget(central);

    statusBar()->showMessage(tr("Data folder: %1").arg(dataFolder_));

    connect(cases, &QPushButton::clicked, this, &HomeWindow::openCaseList);
    connect(renamer, &QPushButton::clicked, this, &HomeWindow::openFileRenamer);
    connect(deficiency, &QPushButton::clicked, this, &HomeWindow::openDeficiencyList);
    resize(500, 440);
}

HomeWindow::~HomeWindow()
{
    disconnectChildren(this);
}

void HomeWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) QApplication::quit();  // the Case List may be hiding this window, not closing it
}

std::optional<std::int64_t> HomeWindow::chooseCase(const QString& purpose)
{
    bool any = false;
    runGuarded(this, [&] { any = !db_.listCases().empty(); });
    if (!any) {
        QMessageBox::information(this, tr("No cases yet"),
                                 tr("There are no cases yet. Open Case List and add one first."));
        return std::nullopt;
    }
    CasePickerDialog picker(db_, purpose, this);
    if (picker.exec() != QDialog::Accepted) return std::nullopt;
    return picker.selectedCaseId();
}

void HomeWindow::openCaseList()
{
    auto* window = new MainWindow(db_, dataFolder_);
    window->setAttribute(Qt::WA_DeleteOnClose);
    // Back to the start screen when the Case List is closed.
    connect(window, &QObject::destroyed, this, [this] {
        show();
        activateWindow();
    });
    hide();
    window->show();
}

void HomeWindow::openFileRenamer()
{
    const auto caseId = chooseCase(tr("File Renamer"));
    if (!caseId) return;
    RenameWindow window(db_, *caseId, this);
    if (!window.promptForFolder()) return;
    window.exec();
}

void HomeWindow::openDeficiencyList()
{
    const auto caseId = chooseCase(tr("Deficiency List"));
    if (!caseId) return;
    DeficiencyDialog dialog(db_, *caseId, this);
    if (!dialog.hasFolders() && !dialog.promptForFolder()) return;
    dialog.exec();
}
