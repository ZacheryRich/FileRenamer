#include "MainWindow.h"

#include "ImportFormDialog.h"

#include <QAction>
#include <QDate>
#include <QEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QGridLayout>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <map>

#include "AccountDialog.h"
#include "CaseAttorneysDialog.h"
#include "CasePeopleDialogs.h"
#include "CreateDocumentDialog.h"
#include "DirectoryEditors.h"
#include "CaseDialog.h"
#include "HistoryDialog.h"
#include "PersonDialog.h"
#include "RenameWindow.h"
#include "DeficiencyDialog.h"
#include "UpdateNamesDialog.h"
#include "QtHelpers.h"
#include "finrenamer/CaseDocument.h"
#include "finrenamer/DateSpec.h"
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
    resize(1180, 740);
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
    newCaseBtn->setObjectName("newCase");
    editCaseBtn_->setObjectName("editCase");
    deleteCaseBtn_->setObjectName("deleteCase");

    // Same boxed look as People / Accounts and the File Renamer's panels.
    auto* left = new QGroupBox(tr("Cases"));
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(caseFilter_);
    leftLayout->addWidget(caseList_, 1);
    leftLayout->addLayout(buttonRow({newCaseBtn, editCaseBtn_, deleteCaseBtn_}));
    auto* newFromFormBtn = new QPushButton(tr("New from Form..."));
    newFromFormBtn->setObjectName("newCaseFromForm");
    newFromFormBtn->setToolTip(tr("Create a case from a client's filled-in Information Form (fillable PDF)"));
    leftLayout->addWidget(newFromFormBtn);

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

    caseCourt_ = new QLabel;
    caseCourt_->setObjectName("caseCourt");
    caseCourt_->setWordWrap(true);
    caseCourt_->setTextInteractionFlags(Qt::TextSelectableByMouse);

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
    titles->addWidget(caseCourt_);
    titles->addWidget(caseNotes_);
    header->addLayout(titles, 1);
    updateNamesBtn_ = new QPushButton(tr("Update File Names..."));
    updateNamesBtn_->setToolTip(tr("Find files and folders still using an account's old name "
                                   "(after an edit) and update them"));
    deficiencyBtn_ = new QPushButton(tr("Deficiency List..."));
    deficiencyBtn_->setToolTip(tr("Check the case's folders for missing monthly statements "
                                  "and save a Word report"));
    createDocBtn_ = new QPushButton(tr("Create Document..."));
    createDocBtn_->setObjectName("createDocument");
    createDocBtn_->setToolTip(tr("Fill a Word template (information sheet, letterhead...) with this case's information"));
    auto* actions = new QGridLayout;
    actions->addWidget(renameFilesBtn_, 0, 0);
    actions->addWidget(createDocBtn_, 0, 1);
    actions->addWidget(updateNamesBtn_, 1, 0);
    actions->addWidget(deficiencyBtn_, 1, 1);
    fillFromFormBtn_ = new QPushButton(tr("Fill from Form..."));
    fillFromFormBtn_->setObjectName("fillFromForm");
    fillFromFormBtn_->setToolTip(tr("Fill in this case from the client's filled-in Information Form (fillable PDF); "
                                    "you choose what goes in before anything is saved"));
    actions->addWidget(fillFromFormBtn_, 2, 0);
    header->addLayout(actions);

    // Parties
    partyTable_ = makeTable({tr("Name"), tr("Role"), tr("Our client")});
    partyTable_->setObjectName("parties");
    auto* addPartyBtn = new QPushButton(tr("Add"));
    addPartyBtn->setObjectName("addParty");
    editPartyBtn_ = new QPushButton(tr("Edit"));
    editPartyBtn_->setObjectName("editParty");
    deletePartyBtn_ = new QPushButton(tr("Delete"));
    deletePartyBtn_->setObjectName("deleteParty");
    auto* partiesBox = new QGroupBox(tr("Parties"));
    auto* partiesLayout = new QVBoxLayout(partiesBox);
    partiesLayout->addWidget(partyTable_, 1);
    partiesLayout->addLayout(buttonRow({addPartyBtn, editPartyBtn_, deletePartyBtn_}));

    // Attorneys on the case
    caseAttorneyTable_ = makeTable({tr("Attorney"), tr("Firm"), tr("Side"), tr("Email")});
    caseAttorneyTable_->setObjectName("caseAttorneys");
    auto* assignBtn = new QPushButton(tr("Assign Attorneys..."));
    assignBtn->setObjectName("assignAttorneys");
    assignBtn->setToolTip(tr("Choose this case's attorneys, ours and opposing counsel, from the Attorney Directory"));
    editCaseAttorneyBtn_ = new QPushButton(tr("Edit"));
    editCaseAttorneyBtn_->setObjectName("editCaseAttorney");
    editCaseAttorneyBtn_->setToolTip(tr("Edit this attorney in the Attorney Directory"));
    removeCaseAttorneyBtn_ = new QPushButton(tr("Remove"));
    removeCaseAttorneyBtn_->setObjectName("removeCaseAttorney");
    removeCaseAttorneyBtn_->setToolTip(tr("Take this attorney off the case (they stay in the directory)"));
    auto* attorneysBox = new QGroupBox(tr("Attorneys"));
    auto* attorneysLayout = new QVBoxLayout(attorneysBox);
    attorneysLayout->addWidget(caseAttorneyTable_, 1);
    attorneysLayout->addLayout(buttonRow({assignBtn, editCaseAttorneyBtn_, removeCaseAttorneyBtn_}));

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

    // Other people (prior counsel, third parties, experts...)
    contactTable_ = makeTable({tr("Role"), tr("Name"), tr("Firm / organization"), tr("Email")});
    contactTable_->setObjectName("contacts");
    auto* addContactBtn = new QPushButton(tr("Add"));
    addContactBtn->setObjectName("addContact");
    editContactBtn_ = new QPushButton(tr("Edit"));
    editContactBtn_->setObjectName("editContact");
    deleteContactBtn_ = new QPushButton(tr("Delete"));
    deleteContactBtn_->setObjectName("deleteContact");
    auto* contactsBox = new QGroupBox(tr("Other People"));
    contactsBox->setToolTip(tr("Prior counsel, third parties, experts and anyone else involved"));
    auto* contactsLayout = new QVBoxLayout(contactsBox);
    contactsLayout->addWidget(contactTable_, 1);
    contactsLayout->addLayout(buttonRow({addContactBtn, editContactBtn_, deleteContactBtn_}));

    // Children
    childTable_ = makeTable({tr("Name"), tr("Date of birth"), tr("Age"), tr("School")});
    childTable_->setObjectName("children");
    auto* addChildBtn = new QPushButton(tr("Add"));
    addChildBtn->setObjectName("addChild");
    editChildBtn_ = new QPushButton(tr("Edit"));
    editChildBtn_->setObjectName("editChild");
    deleteChildBtn_ = new QPushButton(tr("Delete"));
    deleteChildBtn_->setObjectName("deleteChild");
    auto* childrenBox = new QGroupBox(tr("Children"));
    auto* childrenLayout = new QVBoxLayout(childrenBox);
    childrenLayout->addWidget(childTable_, 1);
    childrenLayout->addLayout(buttonRow({addChildBtn, editChildBtn_, deleteChildBtn_}));

    // Tab 1: the case's information; tab 2: account owners and accounts (the File Renamer's data).
    auto* infoPage = new QWidget;
    auto* infoGrid = new QGridLayout(infoPage);
    infoGrid->addWidget(partiesBox, 0, 0);
    infoGrid->addWidget(attorneysBox, 0, 1);
    infoGrid->addWidget(contactsBox, 1, 0);
    infoGrid->addWidget(childrenBox, 1, 1);
    infoGrid->setColumnStretch(0, 2);
    infoGrid->setColumnStretch(1, 3);
    auto* accountsPage = new QWidget;
    auto* lists = new QHBoxLayout(accountsPage);
    lists->addWidget(peopleBox, 2);
    lists->addWidget(accountsBox, 5);
    caseTabs_ = new QTabWidget;
    caseTabs_->setObjectName("caseTabs");
    caseTabs_->addTab(infoPage, tr("Case Information"));
    caseTabs_->addTab(accountsPage, tr("Accounts"));
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        caseTabs_->setCurrentIndex(settings.value("caseList/tab", 0).toInt() == 1 ? 1 : 0);
    }
    connect(caseTabs_, &QTabWidget::currentChanged, this, [](int index) {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        settings.setValue("caseList/tab", index);
    });

    auto* detailPage = new QWidget;
    auto* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addLayout(header);
    detailLayout->addWidget(caseTabs_, 1);

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
    connect(newFromFormBtn, &QPushButton::clicked, this, [this] { importForm(false); });
    connect(fillFromFormBtn_, &QPushButton::clicked, this, [this] { importForm(true); });
    connect(renameFilesBtn_, &QPushButton::clicked, this, &MainWindow::renameFiles);
    connect(updateNamesBtn_, &QPushButton::clicked, this, &MainWindow::updateFileNames);
    connect(deficiencyBtn_, &QPushButton::clicked, this, &MainWindow::deficiencyList);
    connect(editCaseBtn_, &QPushButton::clicked, this, &MainWindow::editCase);
    connect(deleteCaseBtn_, &QPushButton::clicked, this, &MainWindow::deleteCase);

    connect(createDocBtn_, &QPushButton::clicked, this, &MainWindow::createDocument);
    connect(contactTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(contactTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editContact);
    connect(addContactBtn, &QPushButton::clicked, this, &MainWindow::addContact);
    connect(editContactBtn_, &QPushButton::clicked, this, &MainWindow::editContact);
    connect(deleteContactBtn_, &QPushButton::clicked, this, &MainWindow::deleteContact);
    connect(childTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(childTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editChild);
    connect(addChildBtn, &QPushButton::clicked, this, &MainWindow::addChild);
    connect(editChildBtn_, &QPushButton::clicked, this, &MainWindow::editChild);
    connect(deleteChildBtn_, &QPushButton::clicked, this, &MainWindow::deleteChild);
    connect(partyTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(partyTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editParty);
    connect(addPartyBtn, &QPushButton::clicked, this, &MainWindow::addParty);
    connect(editPartyBtn_, &QPushButton::clicked, this, &MainWindow::editParty);
    connect(deletePartyBtn_, &QPushButton::clicked, this, &MainWindow::deleteParty);

    connect(caseAttorneyTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(caseAttorneyTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::editCaseAttorney);
    connect(assignBtn, &QPushButton::clicked, this, &MainWindow::assignAttorneys);
    connect(editCaseAttorneyBtn_, &QPushButton::clicked, this, &MainWindow::editCaseAttorney);
    connect(removeCaseAttorneyBtn_, &QPushButton::clicked, this, &MainWindow::removeCaseAttorney);

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
    file->addAction(tr("New Case from &Information Form..."), this, [this] { importForm(false); });

    QAction* history = file->addAction(tr("Rename &History..."), this, [this] {
        HistoryDialog dialog(db_, this);
        dialog.exec();
    });
    history->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));

    file->addAction(tr("&Back Up Database..."), this, [this] {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        QString dir = settings.value("backup/lastDir").toString();
        if (dir.isEmpty() || !QFileInfo(dir).isDir())
            dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString suggested = QDir(dir).filePath(
            QStringLiteral("FinRenamer backup %1.db").arg(QDate::currentDate().toString("yyyy.MM.dd")));
        const QString target = QFileDialog::getSaveFileName(this, tr("Back Up Database"), suggested,
                                                           tr("Database copy (*.db)"));
        if (target.isEmpty()) return;
        bool ok = false;
        runGuarded(this, [&] {
            db_.backupTo(toPath(target));
            ok = true;
        });
        if (!ok) return;
        settings.setValue("backup/lastDir", QFileInfo(target).absolutePath());
        QMessageBox::information(this, tr("Back Up Database"),
                                 tr("Saved a copy of all cases, people, accounts, templates and rename history to:\n%1\n\n"
                                    "To use it, close the program and put the file back as finrenamer.db in the data folder.")
                                     .arg(QDir::toNativeSeparators(target)));
    });
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
            auto* item = new QListWidgetItem(qstr(caseDisplayName(c)), caseList_);
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

    caseTitle_->setText(qstr(caseDisplayName(*c)));
    caseNotes_->setText(qstr(c->notes));
    caseNotes_->setVisible(!c->notes.empty());
    QStringList court;
    if (!c->causeNumber.empty()) court << tr("Cause No. %1").arg(qstr(c->causeNumber));
    if (!c->county.empty()) court << tr("%1 County").arg(qstr(c->county));
    if (!c->court.empty()) court << qstr(c->court);
    caseCourt_->setText(court.isEmpty() ? tr("No cause number, county or court yet (Edit the case to add them).")
                                        : court.join("   |   "));
    caseCourt_->setStyleSheet(court.isEmpty() ? "color: palette(placeholder-text);" : QString());
    detailStack_->setCurrentIndex(1);
    reloadParties();
    reloadCaseAttorneys();
    reloadContacts();
    reloadChildren();
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
                for (const CombinedPart& part : a.parts) {
                    if (!part.accountType.empty()) types << qstr(part.accountType);
                    numbers << qstr(part.lastFour);
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

    createDocBtn_->setEnabled(hasCase);
    fillFromFormBtn_->setEnabled(hasCase);
    editContactBtn_->setEnabled(selectedRowId(contactTable_).has_value());
    deleteContactBtn_->setEnabled(selectedRowId(contactTable_).has_value());
    editChildBtn_->setEnabled(selectedRowId(childTable_).has_value());
    deleteChildBtn_->setEnabled(selectedRowId(childTable_).has_value());
    const bool hasParty = currentPartyId().has_value();
    editPartyBtn_->setEnabled(hasParty);
    deletePartyBtn_->setEnabled(hasParty);
    const bool hasCaseAttorney = currentCaseAttorneyId().has_value();
    editCaseAttorneyBtn_->setEnabled(hasCaseAttorney);
    removeCaseAttorneyBtn_->setEnabled(hasCaseAttorney);

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

std::optional<std::int64_t> MainWindow::currentPartyId() const
{
    return selectedRowId(partyTable_);
}

std::optional<std::int64_t> MainWindow::currentCaseAttorneyId() const
{
    return selectedRowId(caseAttorneyTable_);
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

void MainWindow::importForm(bool intoCurrentCase)
{
    std::int64_t caseId = 0;
    if (intoCurrentCase) {
        const auto id = currentCaseId();
        if (!id) return;
        caseId = *id;
    }
    QSettings settings(settingsFile(), QSettings::IniFormat);
    QString dir = settings.value("infoForm/lastDir").toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString pdf = QFileDialog::getOpenFileName(this, tr("Choose the Filled-In Information Form"), dir,
                                                     tr("PDF files (*.pdf);;All files (*)"));
    if (pdf.isEmpty()) return;
    settings.setValue("infoForm/lastDir", QFileInfo(pdf).absolutePath());

    ImportFormDialog dialog(db_, caseId, pdf, this);
    if (!dialog.loaded() || dialog.exec() != QDialog::Accepted) return;
    if (!intoCurrentCase) caseFilter_->clear();
    reloadCases(dialog.savedCaseId());
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
        tr("Delete the case \"%1\"?\n\nThis also deletes its %2 people and %3 accounts, its parties, children, "
           "other people and list of attorneys (the attorneys stay in the Attorney Directory). "
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
// Parties

void MainWindow::reloadParties()
{
    const auto caseId = currentCaseId();
    const auto keep = currentPartyId();
    partyTable_->setRowCount(0);
    if (!caseId) return;
    runGuarded(this, [&] {
        const auto parties = db_.listParties(*caseId);
        partyTable_->setRowCount(static_cast<int>(parties.size()));
        for (int row = 0; row < static_cast<int>(parties.size()); ++row) {
            const CaseParty& p = parties[static_cast<std::size_t>(row)];
            const QStringList cells{p.name.empty() ? tr("(no name)") : qstr(p.name),
                                    p.role == PartyRole::Respondent ? tr("Respondent") : tr("Petitioner"),
                                    p.ourClient ? tr("Yes") : QString()};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
                if (col == 1) item->setData(kIdRole + 1, static_cast<int>(p.role));
                partyTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(partyTable_, keep);
}

void MainWindow::addParty()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    CaseParty blank;
    blank.caseId = *caseId;
    // The second party is usually the other side.
    if (partyTable_->rowCount() == 1 && partyTable_->item(0, 1)->data(kIdRole + 1).toInt() == static_cast<int>(PartyRole::Petitioner))
        blank.role = PartyRole::Respondent;
    PartyDialog dialog(blank, this);
    while (dialog.exec() == QDialog::Accepted) {
        std::int64_t id = 0;
        if (runGuarded(this, [&] { id = db_.addParty(dialog.result()); })) {
            reloadParties();
            selectRowWithId(partyTable_, id);
            break;
        }
    }
    updateButtons();
}

void MainWindow::editParty()
{
    const auto id = currentPartyId();
    const auto caseId = currentCaseId();
    if (!id || !caseId) return;
    std::optional<CaseParty> p;
    runGuarded(this, [&] {
        for (const auto& x : db_.listParties(*caseId))
            if (x.id == *id) p = x;
    });
    if (!p) return;
    PartyDialog dialog(*p, this);
    while (dialog.exec() == QDialog::Accepted) {
        if (runGuarded(this, [&] { db_.updateParty(dialog.result()); })) {
            reloadParties();
            break;
        }
    }
}

void MainWindow::deleteParty()
{
    const auto id = currentPartyId();
    if (!id) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Party"),
        tr("Remove \"%1\" from this case's parties?").arg(partyTable_->item(partyTable_->currentRow(), 0)->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (runGuarded(this, [&] { db_.deleteParty(*id); })) reloadParties();
    updateButtons();
}

// ---------------------------------------------------------------------------
// Other people and children

void MainWindow::reloadContacts()
{
    const auto caseId = currentCaseId();
    const auto keep = selectedRowId(contactTable_);
    contactTable_->setRowCount(0);
    if (!caseId) return;
    runGuarded(this, [&] {
        const auto list = db_.listContacts(*caseId);
        contactTable_->setRowCount(static_cast<int>(list.size()));
        for (int row = 0; row < static_cast<int>(list.size()); ++row) {
            const CaseContact& c = list[static_cast<std::size_t>(row)];
            const QStringList cells{qstr(c.role), c.name.empty() ? tr("(no name)") : qstr(c.name), qstr(c.firm), qstr(c.email)};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(c.id));
                contactTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(contactTable_, keep);
}

void MainWindow::addContact()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    CaseContact blank;
    blank.caseId = *caseId;
    ContactDialog dialog(blank, this);
    while (dialog.exec() == QDialog::Accepted) {
        std::int64_t id = 0;
        if (runGuarded(this, [&] { id = db_.addContact(dialog.result()); })) {
            reloadContacts();
            selectRowWithId(contactTable_, id);
            break;
        }
    }
    updateButtons();
}

void MainWindow::editContact()
{
    const auto id = selectedRowId(contactTable_);
    const auto caseId = currentCaseId();
    if (!id || !caseId) return;
    std::optional<CaseContact> c;
    runGuarded(this, [&] {
        for (const auto& x : db_.listContacts(*caseId))
            if (x.id == *id) c = x;
    });
    if (!c) return;
    ContactDialog dialog(*c, this);
    while (dialog.exec() == QDialog::Accepted) {
        if (runGuarded(this, [&] { db_.updateContact(dialog.result()); })) {
            reloadContacts();
            break;
        }
    }
}

void MainWindow::deleteContact()
{
    const auto id = selectedRowId(contactTable_);
    if (!id) return;
    const int row = contactTable_->currentRow();
    const auto answer = QMessageBox::question(
        this, tr("Delete Person"),
        tr("Remove \"%1\" from this case?").arg(contactTable_->item(row, 1)->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (runGuarded(this, [&] { db_.deleteContact(*id); })) reloadContacts();
    updateButtons();
}

void MainWindow::reloadChildren()
{
    const auto caseId = currentCaseId();
    const auto keep = selectedRowId(childTable_);
    childTable_->setRowCount(0);
    if (!caseId) return;
    const QDate d = QDate::currentDate();
    const auto today = makeDate(d.year(), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
    runGuarded(this, [&] {
        const auto list = db_.listChildren(*caseId);
        childTable_->setRowCount(static_cast<int>(list.size()));
        for (int row = 0; row < static_cast<int>(list.size()); ++row) {
            const CaseChild& c = list[static_cast<std::size_t>(row)];
            const std::string age = c.age.empty() ? ageOn(c.dob, today) : c.age;
            const QStringList cells{c.name.empty() ? tr("(no name)") : qstr(c.name), qstr(c.dob), qstr(age), qstr(c.school)};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(c.id));
                childTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(childTable_, keep);
}

void MainWindow::addChild()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    CaseChild blank;
    blank.caseId = *caseId;
    ChildDialog dialog(blank, this);
    while (dialog.exec() == QDialog::Accepted) {
        std::int64_t id = 0;
        if (runGuarded(this, [&] { id = db_.addChild(dialog.result()); })) {
            reloadChildren();
            selectRowWithId(childTable_, id);
            break;
        }
    }
    updateButtons();
}

void MainWindow::editChild()
{
    const auto id = selectedRowId(childTable_);
    const auto caseId = currentCaseId();
    if (!id || !caseId) return;
    std::optional<CaseChild> c;
    runGuarded(this, [&] {
        for (const auto& x : db_.listChildren(*caseId))
            if (x.id == *id) c = x;
    });
    if (!c) return;
    ChildDialog dialog(*c, this);
    while (dialog.exec() == QDialog::Accepted) {
        if (runGuarded(this, [&] { db_.updateChild(dialog.result()); })) {
            reloadChildren();
            break;
        }
    }
}

void MainWindow::deleteChild()
{
    const auto id = selectedRowId(childTable_);
    if (!id) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Child"),
        tr("Remove \"%1\" from this case?").arg(childTable_->item(childTable_->currentRow(), 0)->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (runGuarded(this, [&] { db_.deleteChild(*id); })) reloadChildren();
    updateButtons();
}

void MainWindow::createDocument()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    CreateDocumentDialog dialog(db_, *caseId, this);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Attorneys on the case

void MainWindow::reloadCaseAttorneys()
{
    const auto caseId = currentCaseId();
    const auto keep = currentCaseAttorneyId();
    caseAttorneyTable_->setRowCount(0);
    if (!caseId) return;
    runGuarded(this, [&] {
        const auto attorneys = db_.listCaseAttorneys(*caseId);
        std::map<std::int64_t, Firm> firms;
        for (const auto& f : db_.listFirms()) firms[f.id] = f;
        caseAttorneyTable_->setRowCount(static_cast<int>(attorneys.size()));
        for (int row = 0; row < static_cast<int>(attorneys.size()); ++row) {
            const Attorney& a = attorneys[static_cast<std::size_t>(row)];
            const Firm& f = firms[a.firmId];
            const QStringList cells{qstr(a.name), qstr(f.name), f.ours ? tr("Our firm") : tr("Opposing counsel"),
                                    qstr(a.email)};
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col]);
                item->setData(kIdRole, QVariant::fromValue<qlonglong>(a.id));
                caseAttorneyTable_->setItem(row, col, item);
            }
        }
    });
    selectRowWithId(caseAttorneyTable_, keep);
}

void MainWindow::assignAttorneys()
{
    const auto caseId = currentCaseId();
    if (!caseId) return;
    std::set<std::int64_t> assigned;
    runGuarded(this, [&] {
        for (const auto& a : db_.listCaseAttorneys(*caseId)) assigned.insert(a.id);
    });
    CaseAttorneysDialog dialog(db_, caseTitle_->text(), assigned, this);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (ok) runGuarded(this, [&] { db_.setCaseAttorneys(*caseId, dialog.chosen()); });
    reloadCaseAttorneys();  // also after Cancel: names may have been edited in the directory
    updateButtons();
}

void MainWindow::editCaseAttorney()
{
    const auto id = currentCaseAttorneyId();
    const auto caseId = currentCaseId();
    if (!id || !caseId) return;
    std::optional<Attorney> a;
    runGuarded(this, [&] {
        for (const auto& x : db_.listCaseAttorneys(*caseId))
            if (x.id == *id) a = x;
    });
    if (a && editAttorneyInteractive(db_, this, *a)) reloadCaseAttorneys();
}

void MainWindow::removeCaseAttorney()
{
    const auto id = currentCaseAttorneyId();
    const auto caseId = currentCaseId();
    if (!id || !caseId) return;
    const auto answer = QMessageBox::question(
        this, tr("Remove Attorney"),
        tr("Take %1 off this case?\n\nThey stay in the Attorney Directory.")
            .arg(caseAttorneyTable_->item(caseAttorneyTable_->currentRow(), 0)->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    if (runGuarded(this, [&] { db_.removeCaseAttorney(*caseId, *id); })) reloadCaseAttorneys();
    updateButtons();
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
        (combined ? tr("Delete the combined statement \"%1\"?\n\nFiles already renamed for it are not affected.")
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
    if (const auto caseId = currentCaseId()) emit fileRenamerRequested(*caseId);
}

bool MainWindow::event(QEvent* e)
{
    if (e->type() == QEvent::WindowActivate && caseAttorneyTable_ && currentCaseId()) {
        reloadCaseAttorneys();
        updateButtons();
    }
    return QMainWindow::event(e);
}

void MainWindow::refreshAfterTool()
{
    reloadCaseAttorneys();
    reloadPeople();
    reloadAccounts();
    updateButtons();
}

// ---------------------------------------------------------------------------
// Updating names after edits

// Every file label and folder name this case's accounts produce right now.
std::set<std::string> MainWindow::currentNames()
{
    const auto caseId = currentCaseId();
    if (!caseId) return {};
    return accountNameSet(db_, *caseId, this);
}

void MainWindow::offerNameUpdate(const std::set<std::string>& namesBefore)
{
    if (const auto caseId = currentCaseId()) ::offerNameUpdate(this, db_, *caseId, namesBefore);
}

void MainWindow::deficiencyList()
{
    if (const auto caseId = currentCaseId()) emit deficiencyListRequested(*caseId);
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
