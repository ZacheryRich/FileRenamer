#include "HomeWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
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

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(heading);
    layout->addWidget(intro);
    layout->addSpacing(6);
    layout->addWidget(cases);
    layout->addWidget(renamer);
    layout->addWidget(deficiency);
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
    footer->addWidget(closeBtn);
    layout->addLayout(footer);
    setCentralWidget(central);

    connect(cases, &QPushButton::clicked, this, &HomeWindow::openCaseList);
    connect(renamer, &QPushButton::clicked, this, &HomeWindow::openFileRenamer);
    connect(deficiency, &QPushButton::clicked, this, &HomeWindow::openDeficiencyList);
    resize(520, 400);
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
