#include "MainWindow.h"

#include <QAction>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <map>

#include "AccountDialog.h"
#include "CaseDialog.h"
#include "HistoryDialog.h"
#include "PersonDialog.h"
#include "RenameWindow.h"
#include "DeficiencyDialog.h"
#include "UpdateNamesDialog.h"
#include "QtHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;
using namespace ui;

namespace {

constexpr int kIdRole = Qt::UserRole;

std::optional<std::int64_t> idOf(const QListWidgetItem* item)
{
    if (!item) return std::nullopt;
    return item->data(kIdRole).toLongLong();
}

std::optional<std::int64_t> selectedRowId(const QTableWidget* table)
{
    const auto rows = table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return std::nullopt;
    const QTableWidgetItem* item = table->item(rows.first().row(), 0);
    if (!item) return std::nullopt;
    return item->data(kIdRole).toLongLong();
}

QTableWidget* makeTable(const QStringList& headers)
{
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

void selectRowWithId(QTableWidget* table, std::optional<std::int64_t> id)
{
    if (!id) return;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(kIdRole).toLongLong() == *id) table->selectRow(row);
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
    setWindowTitle(tr("Case List - %1").arg(appTitle()));
    buildUi();
    buildMenus();
    resize(1100, 680);
    reloadCases();
}

MainWindow::~MainWindow()
{
    disconnectChildren(this);
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

    // Same boxed look as People / Accounts and the File Renamer's panels.
    auto* left = new QGroupBox(tr("Cases"));
    auto* leftLayout = new QVBoxLayout(left);
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

    renameFilesBtn_ = new QPushButton(tr("File Renamer..."));
    renameFilesBtn_->setToolTip(tr("Choose a folder of statements and rename them using this case's accounts"));
    QFont big = renameFilesBtn_->font();
    big.setBold(true);
    renameFilesBtn_->setFont(big);

    auto* header = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->addWidget(caseTitle_);
    titles->addWidget(caseNotes_);
    header->addLayout(titles, 1);
    updateNamesBtn_ = new QPushButton(tr("Update File Names..."));
    updateNamesBtn_->setToolTip(tr("Find files and folders still using an account's old name "
                                   "(after an edit) and update them"));
    deficiencyBtn_ = new QPushButton(tr("Deficiency List..."));
    deficiencyBtn_->setToolTip(tr("Check the case's folders for missing monthly statements "
                                  "and save a Word report"));
    auto* actions = new QVBoxLayout;
    actions->addWidget(renameFilesBtn_);
    actions->addWidget(updateNamesBtn_);
    actions->addWidget(deficiencyBtn_);
    header->addLayout(actions);

    // People
    peopleTable_ = makeTable({tr("Name"), tr("In filenames")});
    auto* addPersonBtn = new QPushButton(tr("Add"));
    editPersonBtn_ = new QPushButton(tr("Edit"));
    deletePersonBtn_ = new QPushButton(tr("Delete"));

    auto* peopleBox = new QGroupBox(tr("People"));
    auto* peopleLayout = new QVBoxLayout(peopleBox);
    peopleLayout->addWidget(peopleTable_, 1);
    peopleLayout->addLayout(buttonRow({addPersonBtn, editPersonBtn_, deletePersonBtn_}));

    // Accounts
    accountTable_ = makeTable({tr("Institution"), tr("Type"), tr("Last 4"), tr("Owners")});

    auto* addAccountBtn = new QPushButton(tr("Add"));
    editAccountBtn_ = new QPushButton(tr("Edit"));
    deleteAccountBtn_ = new QPushButton(tr("Delete"));

    auto* accountsBox = new QGroupBox(tr("Accounts"));
    auto* accountsLayout = new QVBoxLayout(accountsBox);
    accountsLayout->addWidget(accountTable_, 1);
    accountsLayout->addLayout(buttonRow({addAccountBtn, editAccountBtn_, deleteAccountBtn_}));

    auto* lists = new QHBoxLayout;
    lists->addWidget(peopleBox, 2);
    lists->addWidget(accountsBox, 5);

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
    centralLayout->addWidget(splitter, 1);

    // Footer like the File Renamer's: information on the left, Close on the right.
    auto* dataLabel = new QLabel(tr("Data folder: %1").arg(dataFolder_));
    dataLabel->setObjectName("dataFolder");
    dataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setToolTip(tr("Back to the start screen"));
    closeBtn->setAutoDefault(false);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    auto* footer = new QHBoxLayout;
    footer->addWidget(dataLabel, 1);
    footer->addWidget(closeBtn);
    centralLayout->addLayout(footer);
    setCentralWidget(central);

    // ---- Wiring ----
    connect(caseFilter_, &QLineEdit::textChanged, this, &MainWindow::filterCases);
    connect(caseList_, &QListWidget::currentItemChanged, this, &MainWindow::showSelectedCase);
    connect(caseList_, &QListWidget::itemDoubleClicked, this, &MainWindow::editCase);
    connect(newCaseBtn, &QPushButton::clicked, this, &MainWindow::newCase);
    connect(renameFilesBtn_, &QPushButton::clicked, this, &MainWindow::renameFiles);
    connect(updateNamesBtn_, &QPushButton::clicked, this, &MainWindow::updateFileNames);
    connect(deficiencyBtn_, &QPushButton::clicked, this, &MainWindow::deficiencyList);
    connect(editCaseBtn_, &QPushButton::clicked, this, &MainWindow::editCase);
    connect(deleteCaseBtn_, &QPushButton::clicked, this, &MainWindow::deleteCase);

    connect(peopleTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(peopleTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editPerson);
    connect(addPersonBtn, &QPushButton::clicked, this, &MainWindow::addPerson);
    connect(editPersonBtn_, &QPushButton::clicked, this, &MainWindow::editPerson);
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

    QAction* history = file->addAction(tr("Rename &History..."), this, [this] {
        HistoryDialog dialog(db_, this);
        dialog.exec();
    });
    history->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));

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
    peopleTable_->setRowCount(0);
    if (!caseId) return;

    runGuarded(this, [&] {
        const auto people = db_.listPeople(*caseId);
        peopleTable_->setRowCount(static_cast<int>(people.size()));
        for (int row = 0; row < static_cast<int>(people.size()); ++row) {
            const Person& p = people[static_cast<std::size_t>(row)];
            const QStringList cells{qstr(p.fullName), qstr(p.displayName)};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
                peopleTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(peopleTable_, keep);
}

void MainWindow::reloadAccounts(std::optional<std::int64_t> select)
{
    if (!select) select = currentAccountId();
    const auto caseId = currentCaseId();
    accountTable_->setRowCount(0);
    if (!caseId) return;

    runGuarded(this, [&] {
        std::map<std::int64_t, Person> people;
        for (const auto& p : db_.listPeople(*caseId)) people[p.id] = p;

        const auto accounts = db_.listAccountRecords(*caseId);
        accountTable_->setRowCount(static_cast<int>(accounts.size()));
        for (int row = 0; row < static_cast<int>(accounts.size()); ++row) {
            const AccountRecord& a = accounts[static_cast<std::size_t>(row)];
            QStringList owners;
            for (const auto id : a.ownerIds) owners << personLabel(people[id]);

            const QString institution =
                a.institutionDisplay == a.institution
                    ? qstr(a.institution)
                    : QStringLiteral("%1 (%2)").arg(qstr(a.institution), qstr(a.institutionDisplay));
            QString type = qstr(a.accountType);
            QString number = qstr(a.lastFour);
            if (a.isCombined()) {
                // "Combined: Chk, Sav" / "1111, 2222", in the statement's order.
                QStringList types, numbers;
                for (const auto memberId : a.memberIds)
                    for (const AccountRecord& m : accounts)
                        if (m.id == memberId) {
                            if (!m.accountType.empty()) types << qstr(m.accountType);
                            numbers << qstr(m.lastFour);
                        }
                type = types.isEmpty() ? tr("Combined") : tr("Combined: %1").arg(types.join(QStringLiteral(", ")));
                number = numbers.join(QStringLiteral(", "));
            } else if (!a.previousLastFour.empty()) {
                QStringList was;
                for (const auto& n : a.previousLastFour) was << qstr(n);
                number += tr(" (was %1)").arg(was.join(QStringLiteral(", ")));
            }
            const QStringList cells{institution, type, number, owners.join(QStringLiteral("; "))};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(a.id));
                accountTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(accountTable_, select);
}

void MainWindow::updateButtons()
{
    const bool hasCase = currentCaseId().has_value();
    renameFilesBtn_->setEnabled(hasCase);
    updateNamesBtn_->setEnabled(hasCase);
    deficiencyBtn_->setEnabled(hasCase);
    editCaseBtn_->setEnabled(hasCase);
    deleteCaseBtn_->setEnabled(hasCase);

    const bool hasPerson = currentPersonId().has_value();
    editPersonBtn_->setEnabled(hasPerson);
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
    return selectedRowId(peopleTable_);
}

std::optional<std::int64_t> MainWindow::currentAccountId() const
{
    return selectedRowId(accountTable_);
}

// ---------------------------------------------------------------------------
// Cases

void MainWindow::newCase()
{
    CaseDialog dialog(ClientCase{}, this);
    if (dialog.exec() != QDialog::Accepted) return;

    std::int64_t id = 0;
    if (runGuarded(this, [&] { id = db_.createCase(dialog.result(), defaultCasePeople()); })) {
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

    PersonDialog dialog(tr("Add Person"), {}, {}, this);
    if (dialog.exec() != QDialog::Accepted) return;

    std::int64_t id = 0;
    if (runGuarded(this, [&] {
            id = db_.addPerson(*caseId, stdstr(dialog.fullName()), stdstr(dialog.displayName()));
        })) {
        reloadPeople();
        selectRowWithId(peopleTable_, id);
    }
}

void MainWindow::editPerson()
{
    const auto id = currentPersonId();
    if (!id) return;
    const int row = peopleTable_->currentRow();

    PersonDialog dialog(tr("Edit Person"), peopleTable_->item(row, 0)->text(),
                        peopleTable_->item(row, 1)->text(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    // Changing the display name updates every account this person owns.
    const auto before = currentNames();
    if (runGuarded(this, [&] {
            db_.updatePerson(*id, stdstr(dialog.fullName()), stdstr(dialog.displayName()));
        })) {
        reloadPeople();
        reloadAccounts();
        offerNameUpdate(before);
    }
}

void MainWindow::deletePerson()
{
    const auto id = currentPersonId();
    if (!id) return;

    const auto answer = QMessageBox::question(
        this, tr("Delete Person"),
        tr("Remove \"%1\" from this case?").arg(peopleTable_->item(peopleTable_->currentRow(), 0)->text()),
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

    const auto before = currentNames();
    AccountDialog dialog(db_, *caseId, record, this);
    const bool saved = dialog.exec() == QDialog::Accepted;
    if (dialog.addedPeople()) reloadPeople();
    if (saved) reloadAccounts(*accountId);
    updateButtons();
    if (saved) offerNameUpdate(before);
}

void MainWindow::deleteAccount()
{
    const auto id = currentAccountId();
    if (!id) return;

    QString label;
    bool combined = false;
    if (const auto caseId = currentCaseId())
        runGuarded(this, [&] {
            for (const Account& a : db_.loadAccounts(*caseId))
                if (a.id == *id) {
                    label = qstr(accountLabel(a));
                    combined = a.isCombined();
                }
        });
    const auto answer = QMessageBox::question(
        this, combined ? tr("Delete Combined Statement") : tr("Delete Account"),
        (combined ? tr("Delete the combined statement \"%1\"?\n\nIts accounts are kept. Files already "
                       "renamed for it are not affected.")
                  : tr("Delete the account \"%1\"?\n\nFiles already renamed for it are not affected."))
            .arg(label),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    if (runGuarded(this, [&] { db_.deleteAccount(*id); })) reloadAccounts();
    updateButtons();
}

// ---------------------------------------------------------------------------
// Renaming

void MainWindow::renameFiles()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;

    RenameWindow window(db_, *caseId, this);
    if (!window.promptForFolder()) return;
    window.exec();

    // Accounts or people may have been added from the rename screen.
    reloadPeople();
    reloadAccounts();
    updateButtons();
}

// ---------------------------------------------------------------------------
// Updating names after edits

// Every file label and folder name this case's accounts produce right now.
std::set<std::string> MainWindow::currentNames()
{
    std::set<std::string> names;
    const auto caseId = currentCaseId();
    if (!caseId) return names;
    runGuarded(this, [&] {
        for (const Account& a : db_.loadAccounts(*caseId)) {
            names.insert("file:" + accountLabel(a));
            for (const auto& n : a.previousLastFour) names.insert("file:" + accountLabel(a, n));
            names.insert("folder:" + accountFolderLabel(a));
        }
    });
    return names;
}

void MainWindow::offerNameUpdate(const std::set<std::string>& namesBefore)
{
    if (currentNames() == namesBefore) return;  // nothing that appears in names changed
    const auto answer = QMessageBox::question(
        this, tr("Update File Names"),
        tr("This changes how the account's files and folders are named. Files and folders "
           "you've already renamed still use the old version.\n\n"
           "Update them now? You'll see every change before anything is renamed."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) updateFileNames();
}

void MainWindow::deficiencyList()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    DeficiencyDialog dialog(db_, *caseId, this);
    if (!dialog.hasFolders() && !dialog.promptForFolder()) return;
    dialog.exec();
}

void MainWindow::updateFileNames()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    UpdateNamesDialog dialog(db_, *caseId, this);
    // Folders used for this case before are remembered; ask for one the first time.
    if (!dialog.hasFolders() && !dialog.promptForFolder()) return;
    dialog.exec();
}
