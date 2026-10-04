#include "MainWindow.h"

#include <QAction>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "AccountDialog.h"
#include "CaseDialog.h"
#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

constexpr int kIdRole = Qt::UserRole;

std::optional<std::int64_t> idOf(const QListWidgetItem* item)
{
    if (!item) return std::nullopt;
    return item->data(kIdRole).toLongLong();
}

QHBoxLayout* buttonRow(std::initializer_list<QPushButton*> buttons)
{
    auto* row = new QHBoxLayout;
    for (auto* b : buttons) row->addWidget(b);
    row->addStretch();
    return row;
}

}  // namespace

MainWindow::MainWindow(Database& db, const QString& dataFolder, QWidget* parent)
    : QMainWindow(parent), db_(db), dataFolder_(dataFolder)
{
    setWindowTitle(tr("FinRenamer"));
    buildUi();
    buildMenus();
    statusBar()->showMessage(tr("Data folder: %1").arg(dataFolder_));
    resize(1100, 680);
    reloadCases();
}

// ---------------------------------------------------------------------------
// Layout

void MainWindow::buildUi()
{
    // ---- Left: case list ----
    caseFilter_ = new QLineEdit;
    caseFilter_->setPlaceholderText(tr("Search cases"));
    caseFilter_->setClearButtonEnabled(true);

    caseList_ = new QListWidget;

    auto* newCaseBtn = new QPushButton(tr("New Case"));
    editCaseBtn_ = new QPushButton(tr("Edit"));
    deleteCaseBtn_ = new QPushButton(tr("Delete"));

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(caseFilter_);
    leftLayout->addWidget(caseList_, 1);
    leftLayout->addLayout(buttonRow({newCaseBtn, editCaseBtn_, deleteCaseBtn_}));

    // ---- Right: empty-state page ----
    auto* emptyPage = new QLabel(tr("Select a case, or click New Case to create one."));
    emptyPage->setAlignment(Qt::AlignCenter);
    emptyPage->setStyleSheet("color: palette(placeholder-text);");

    // ---- Right: case details page ----
    caseTitle_ = new QLabel;
    QFont titleFont = caseTitle_->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    caseTitle_->setFont(titleFont);

    caseNotes_ = new QLabel;
    caseNotes_->setWordWrap(true);
    caseNotes_->setStyleSheet("color: palette(placeholder-text);");

    renameFilesBtn_ = new QPushButton(tr("Rename Files..."));
    renameFilesBtn_->setEnabled(false);
    renameFilesBtn_->setToolTip(tr("The rename screen is the next step to be built."));

    auto* header = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->addWidget(caseTitle_);
    titles->addWidget(caseNotes_);
    header->addLayout(titles, 1);
    header->addWidget(renameFilesBtn_, 0, Qt::AlignTop);

    // People
    peopleList_ = new QListWidget;
    auto* addPersonBtn = new QPushButton(tr("Add"));
    renamePersonBtn_ = new QPushButton(tr("Rename"));
    deletePersonBtn_ = new QPushButton(tr("Delete"));

    auto* peopleBox = new QGroupBox(tr("People"));
    auto* peopleLayout = new QVBoxLayout(peopleBox);
    peopleLayout->addWidget(peopleList_, 1);
    peopleLayout->addLayout(buttonRow({addPersonBtn, renamePersonBtn_, deletePersonBtn_}));

    // Accounts
    accountTable_ = new QTableWidget(0, 4);
    accountTable_->setHorizontalHeaderLabels({tr("Institution"), tr("Type"), tr("Last 4"), tr("Owners")});
    accountTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    accountTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    accountTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    accountTable_->verticalHeader()->hide();
    accountTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    accountTable_->horizontalHeader()->setStretchLastSection(true);

    auto* addAccountBtn = new QPushButton(tr("Add"));
    editAccountBtn_ = new QPushButton(tr("Edit"));
    deleteAccountBtn_ = new QPushButton(tr("Delete"));

    auto* accountsBox = new QGroupBox(tr("Accounts"));
    auto* accountsLayout = new QVBoxLayout(accountsBox);
    accountsLayout->addWidget(accountTable_, 1);
    accountsLayout->addLayout(buttonRow({addAccountBtn, editAccountBtn_, deleteAccountBtn_}));

    auto* lists = new QHBoxLayout;
    lists->addWidget(peopleBox, 1);
    lists->addWidget(accountsBox, 3);

    auto* detailPage = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addLayout(header);
    detailLayout->addLayout(lists, 1);

    detailStack_ = new QStackedWidget;
    detailStack_->addWidget(emptyPage);   // index 0
    detailStack_->addWidget(detailPage);  // index 1

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(detailStack_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({260, 840});

    auto* central = new QWidget;
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->addWidget(splitter);
    setCentralWidget(central);

    // ---- Wiring ----
    connect(caseFilter_, &QLineEdit::textChanged, this, &MainWindow::filterCases);
    connect(caseList_, &QListWidget::currentItemChanged, this, &MainWindow::showSelectedCase);
    connect(caseList_, &QListWidget::itemDoubleClicked, this, &MainWindow::editCase);
    connect(newCaseBtn, &QPushButton::clicked, this, &MainWindow::newCase);
    connect(editCaseBtn_, &QPushButton::clicked, this, &MainWindow::editCase);
    connect(deleteCaseBtn_, &QPushButton::clicked, this, &MainWindow::deleteCase);

    connect(peopleList_, &QListWidget::currentItemChanged, this, &MainWindow::updateButtons);
    connect(peopleList_, &QListWidget::itemDoubleClicked, this, &MainWindow::renamePerson);
    connect(addPersonBtn, &QPushButton::clicked, this, &MainWindow::addPerson);
    connect(renamePersonBtn_, &QPushButton::clicked, this, &MainWindow::renamePerson);
    connect(deletePersonBtn_, &QPushButton::clicked, this, &MainWindow::deletePerson);

    connect(accountTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(accountTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editAccount);
    connect(addAccountBtn, &QPushButton::clicked, this, &MainWindow::addAccount);
    connect(editAccountBtn_, &QPushButton::clicked, this, &MainWindow::editAccount);
    connect(deleteAccountBtn_, &QPushButton::clicked, this, &MainWindow::deleteAccount);
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));

    QAction* newCase = file->addAction(tr("&New Case..."), this, &MainWindow::newCase);
    newCase->setShortcut(QKeySequence::New);

    file->addAction(tr("Show &Data Folder"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dataFolder_));
    });
    file->addSeparator();

    QAction* quit = file->addAction(tr("E&xit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
}

// ---------------------------------------------------------------------------
// Loading

void MainWindow::reloadCases(std::optional<std::int64_t> select)
{
    if (!select) select = currentCaseId();

    std::vector<ClientCase> cases;
    if (!runGuarded(this, [&] { cases = db_.listCases(); })) return;

    {
        const QSignalBlocker block(caseList_);  // don't fire selection changes while rebuilding
        caseList_->clear();
        for (const auto& c : cases) {
            auto* item = new QListWidgetItem(qstr(c.clientName), caseList_);
            item->setData(kIdRole, QVariant::fromValue<qlonglong>(c.id));
            if (select && c.id == *select) caseList_->setCurrentItem(item);
        }
    }
    filterCases(caseFilter_->text());
    showSelectedCase();
}

void MainWindow::selectCase(std::int64_t caseId)
{
    reloadCases(caseId);
}

void MainWindow::filterCases(const QString& text)
{
    for (int i = 0; i < caseList_->count(); ++i) {
        QListWidgetItem* item = caseList_->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

void MainWindow::showSelectedCase()
{
    const auto id = currentCaseId();
    std::optional<ClientCase> c;
    if (id) runGuarded(this, [&] { c = db_.getCase(*id); });

    if (!c) {
        detailStack_->setCurrentIndex(0);
        updateButtons();
        return;
    }

    caseTitle_->setText(qstr(c->clientName));
    caseNotes_->setText(qstr(c->notes));
    caseNotes_->setVisible(!c->notes.empty());
    detailStack_->setCurrentIndex(1);
    reloadPeople();
    reloadAccounts();
    updateButtons();
}

void MainWindow::reloadPeople()
{
    const auto caseId = currentCaseId();
    const auto keep = currentPersonId();
    peopleList_->clear();
    if (!caseId) return;

    runGuarded(this, [&] {
        for (const auto& p : db_.listPeople(*caseId)) {
            auto* item = new QListWidgetItem(qstr(p.fullName), peopleList_);
            item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
            if (keep && p.id == *keep) peopleList_->setCurrentItem(item);
        }
    });
}

void MainWindow::reloadAccounts(std::optional<std::int64_t> select)
{
    if (!select) select = currentAccountId();
    const auto caseId = currentCaseId();
    accountTable_->setRowCount(0);
    if (!caseId) return;

    runGuarded(this, [&] {
        const auto accounts = db_.loadAccounts(*caseId);
        accountTable_->setRowCount(static_cast<int>(accounts.size()));
        for (int row = 0; row < static_cast<int>(accounts.size()); ++row) {
            const Account& a = accounts[static_cast<std::size_t>(row)];
            QStringList owners;
            for (const auto& o : a.owners) owners << qstr(o);

            const QStringList cells{qstr(a.institution), qstr(a.accountType), qstr(a.lastFour),
                                    owners.join(QStringLiteral("; "))};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(a.id));
                accountTable_->setItem(row, col, item);
            }
            if (select && a.id == *select) accountTable_->selectRow(row);
        }
    });
}

void MainWindow::updateButtons()
{
    const bool hasCase = currentCaseId().has_value();
    editCaseBtn_->setEnabled(hasCase);
    deleteCaseBtn_->setEnabled(hasCase);

    const bool hasPerson = currentPersonId().has_value();
    renamePersonBtn_->setEnabled(hasPerson);
    deletePersonBtn_->setEnabled(hasPerson);

    const bool hasAccount = currentAccountId().has_value();
    editAccountBtn_->setEnabled(hasAccount);
    deleteAccountBtn_->setEnabled(hasAccount);
}

std::optional<std::int64_t> MainWindow::currentCaseId() const
{
    return idOf(caseList_->currentItem());
}

std::optional<std::int64_t> MainWindow::currentPersonId() const
{
    return idOf(peopleList_->currentItem());
}

std::optional<std::int64_t> MainWindow::currentAccountId() const
{
    const auto rows = accountTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return std::nullopt;
    const QTableWidgetItem* item = accountTable_->item(rows.first().row(), 0);
    if (!item) return std::nullopt;
    return item->data(kIdRole).toLongLong();
}

// ---------------------------------------------------------------------------
// Cases

void MainWindow::newCase()
{
    CaseDialog dialog(ClientCase{}, this);
    if (dialog.exec() != QDialog::Accepted) return;

    std::int64_t id = 0;
    if (runGuarded(this, [&] { id = db_.createCase(dialog.result()); })) {
        caseFilter_->clear();
        reloadCases(id);
    }
}

void MainWindow::editCase()
{
    const auto id = currentCaseId();
    if (!id) return;
    std::optional<ClientCase> c;
    if (!runGuarded(this, [&] { c = db_.getCase(*id); }) || !c) return;

    CaseDialog dialog(*c, this);
    if (dialog.exec() != QDialog::Accepted) return;
    if (runGuarded(this, [&] { db_.updateCase(dialog.result()); })) reloadCases(*id);
}

void MainWindow::deleteCase()
{
    const auto id = currentCaseId();
    if (!id) return;

    std::size_t people = 0, accounts = 0;
    runGuarded(this, [&] {
        people = db_.listPeople(*id).size();
        accounts = db_.listAccountRecords(*id).size();
    });

    const QString name = caseList_->currentItem()->text();
    const auto answer = QMessageBox::warning(
        this, tr("Delete Case"),
        tr("Delete the case \"%1\"?\n\nThis also deletes its %2 people and %3 accounts. "
           "Files you have already renamed are not affected, and their rename history is kept.")
            .arg(name).arg(people).arg(accounts),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    if (runGuarded(this, [&] { db_.deleteCase(*id); })) {
        caseList_->setCurrentItem(nullptr);
        reloadCases();
    }
}

// ---------------------------------------------------------------------------
// People

void MainWindow::addPerson()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add Person"),
                                               tr("Full name as it should appear in filenames:"),
                                               QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    std::int64_t id = 0;
    if (runGuarded(this, [&] { id = db_.addPerson(*caseId, stdstr(name)); })) {
        reloadPeople();
        for (int i = 0; i < peopleList_->count(); ++i)
            if (idOf(peopleList_->item(i)) == id) peopleList_->setCurrentRow(i);
    }
}

void MainWindow::renamePerson()
{
    const auto id = currentPersonId();
    if (!id) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename Person"),
        tr("Full name (this updates every account they own):"),
        QLineEdit::Normal, peopleList_->currentItem()->text(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (runGuarded(this, [&] { db_.renamePerson(*id, stdstr(name)); })) {
        reloadPeople();
        reloadAccounts();
    }
}

void MainWindow::deletePerson()
{
    const auto id = currentPersonId();
    if (!id) return;

    const auto answer = QMessageBox::question(
        this, tr("Delete Person"),
        tr("Remove \"%1\" from this case?").arg(peopleList_->currentItem()->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // The database refuses (with a clear message) if they still own an account.
    if (runGuarded(this, [&] { db_.deletePerson(*id); })) reloadPeople();
    updateButtons();
}

// ---------------------------------------------------------------------------
// Accounts

void MainWindow::addAccount()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;

    AccountDialog dialog(db_, *caseId, std::nullopt, this);
    const bool saved = dialog.exec() == QDialog::Accepted;
    if (dialog.addedPeople()) reloadPeople();
    if (saved) reloadAccounts(dialog.savedAccountId());
    updateButtons();
}

void MainWindow::editAccount()
{
    const auto caseId = currentCaseId();
    const auto accountId = currentAccountId();
    if (!caseId || !accountId) return;

    std::optional<AccountRecord> record;
    if (!runGuarded(this, [&] { record = db_.getAccount(*accountId); }) || !record) return;

    AccountDialog dialog(db_, *caseId, record, this);
    const bool saved = dialog.exec() == QDialog::Accepted;
    if (dialog.addedPeople()) reloadPeople();
    if (saved) reloadAccounts(*accountId);
    updateButtons();
}

void MainWindow::deleteAccount()
{
    const auto id = currentAccountId();
    if (!id) return;

    const int row = accountTable_->currentRow();
    const QString label = QStringLiteral("%1 %2 %3").arg(accountTable_->item(row, 0)->text(),
                                                         accountTable_->item(row, 1)->text(),
                                                         accountTable_->item(row, 2)->text());
    const auto answer = QMessageBox::question(
        this, tr("Delete Account"),
        tr("Delete the account \"%1\"?\n\nFiles already renamed for it are not affected.").arg(label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    if (runGuarded(this, [&] { db_.deleteAccount(*id); })) reloadAccounts();
    updateButtons();
}
