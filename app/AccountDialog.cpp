#include "AccountDialog.h"

#include <QMessageBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QVariantList>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

#include "DateWidgets.h"
#include "PersonDialog.h"
#include "QtHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;
using namespace ui;

namespace {
constexpr int kIdRole = Qt::UserRole;            // person id
constexpr int kDisplayRole = Qt::UserRole + 1;   // person's display name (for filenames)
constexpr int kSavedNumberRole = Qt::UserRole + 2;  // account number as last saved ("" = new)

QListWidgetItem* addNumberItem(QListWidget* list, const QString& number, int row = -1)
{
    auto* item = new QListWidgetItem(number);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setData(kSavedNumberRole, number);
    if (row < 0) list->addItem(item);
    else list->insertItem(row, item);
    return item;
}

void addOwnerItem(QListWidget* list, const Person& p)
{
    auto* item = new QListWidgetItem(personLabel(p), list);
    item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
    item->setData(kDisplayRole, qstr(p.displayName));
}

// "Who owns this account?" for one account on a combined statement: tick people; their names
// appear in the file name in the order they were ticked.
class PartOwnersDialog : public QDialog {
public:
    PartOwnersDialog(const std::vector<Person>& people, const std::vector<std::int64_t>& current, const QString& what,
                     QWidget* parent)
        : QDialog(parent), order_(current)
    {
        setWindowTitle(QObject::tr("Owners of %1").arg(what));
        list_ = new QListWidget;
        list_->setObjectName("partOwnerList");
        for (const Person& p : people) {
            auto* item = new QListWidgetItem(personLabel(p), list_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
            item->setCheckState(std::find(current.begin(), current.end(), p.id) != current.end() ? Qt::Checked : Qt::Unchecked);
        }
        auto* hint = new QLabel(QObject::tr("Tick who owns this account. Names appear in the file name in the order you tick them. "
                                            "Leave all unticked for no owner."));
        hint->setWordWrap(true);
        hint->setStyleSheet("color: palette(placeholder-text);");
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(hint);
        layout->addWidget(list_);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            const std::int64_t id = item->data(kIdRole).toLongLong();
            const auto it = std::find(order_.begin(), order_.end(), id);
            if (item->checkState() == Qt::Checked && it == order_.end()) order_.push_back(id);
            if (item->checkState() != Qt::Checked && it != order_.end()) order_.erase(it);
        });
        resize(360, 300);
    }
    ~PartOwnersDialog() override { disconnectChildren(this); }
    std::vector<std::int64_t> owners() const { return order_; }

private:
    QListWidget* list_ = nullptr;
    std::vector<std::int64_t> order_;
};
}

AccountDialog::AccountDialog(Database& db, std::int64_t caseId,
                             std::optional<AccountRecord> existing, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId), existing_(std::move(existing))
{
    const bool editingCombined = existing_ && existing_->isCombined();
    setWindowTitle(editingCombined ? tr("Edit Combined Statement")
                   : existing_     ? tr("Edit Account")
                                   : tr("New Account"));

    // ---- Single account or combined statement ----
    kind_ = new QComboBox;
    kind_->setObjectName("statementType");
    kind_->addItem(tr("Single account"));
    kind_->addItem(tr("Combined statement (several accounts on one PDF)"));
    if (existing_) {
        kind_->setCurrentIndex(editingCombined ? 1 : 0);
        kind_->setEnabled(false);
        kind_->setToolTip(tr("Fixed once saved. To switch, create a new one and delete this one."));
    }
    auto* kindForm = new QFormLayout;
    kindForm->addRow(tr("Statement:"), kind_);

    // ---- Account fields ----
    institution_ = new QLineEdit;
    institution_->setObjectName("institution");
    institution_->setPlaceholderText(tr("e.g. Chase"));

    institutionDisplay_ = new QLineEdit;
    institutionDisplay_->setPlaceholderText(tr("Same as institution"));
    institutionDisplay_->setToolTip(tr("What appears in filenames, e.g. \"BofA\" for Bank of America."));

    type_ = new QLineEdit;
    type_->setPlaceholderText(tr("Optional, e.g. Checking, Roth IRA, Credit Card"));
    // ---- Account numbers (last four), newest first ----
    numbers_ = new QListWidget;
    numbers_->setObjectName("numbers");
    numbers_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                              QAbstractItemView::AnyKeyPressed | QAbstractItemView::SelectedClicked);
    numbers_->setMaximumHeight(numbers_->fontMetrics().height() * 5 + 12);

    auto* addNumberBtn = new QPushButton(tr("Add Newer Number"));
    addNumberBtn->setToolTip(tr("This account got a new number (e.g. a replacement card). "
                                "It's added at the top as the current number."));
    removeNumberBtn_ = new QPushButton(tr("Remove"));
    numberUpBtn_ = new QPushButton(tr("Newer \u25B2"));
    numberDownBtn_ = new QPushButton(tr("Older \u25BC"));
    auto* numberButtons = new QGridLayout;
    numberButtons->addWidget(addNumberBtn, 0, 0, 1, 2);
    numberButtons->addWidget(numberUpBtn_, 1, 0);
    numberButtons->addWidget(numberDownBtn_, 1, 1);
    numberButtons->addWidget(removeNumberBtn_, 2, 0, 1, 2);
    numberButtons->setRowStretch(3, 1);

    auto* numbersRow = new QHBoxLayout;
    numbersRow->addWidget(numbers_, 1);
    numbersRow->addLayout(numberButtons);

    auto* numbersHint = new QLabel(tr("Last four digits, newest first. The top number is the current "
                                      "one; older ones show in the folder name as (was x...). "
                                      "Double-click a number to correct a typo."));
    numbersHint->setWordWrap(true);
    numbersHint->setStyleSheet("color: palette(placeholder-text);");
    auto* numbersBox = new QVBoxLayout;
    numbersBox->addLayout(numbersRow);
    numbersBox->addWidget(numbersHint);

    // ---- Optional opening and closing dates (for the Deficiency List) ----
    openedOn_ = new DateField;
    openedOn_->setObjectName("openedOn");
    closedOn_ = new DateField;
    closedOn_->setObjectName("closedOn");
    auto* datesHint = new QLabel(tr("Optional. The Deficiency List only expects statements from the month "
                                    "an account was opened through the month it was closed."));
    datesHint->setWordWrap(true);
    datesHint->setStyleSheet("color: palette(placeholder-text);");
    auto* datesBox = new QVBoxLayout;
    datesBox->addWidget(datesHint);

    kindForm->addRow(tr("Institution:"), institution_);
    kindForm->addRow(tr("Display name:"), institutionDisplay_);

    singlePage_ = new QWidget;
    auto* form = new QFormLayout(singlePage_);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Account type:"), type_);
    form->addRow(tr("Account numbers:"), numbersBox);
    form->addRow(tr("Opened on:"), openedOn_);
    form->addRow(tr("Closed on:"), closedOn_);
    form->addRow(QString(), datesBox);

    // ---- Combined statement: the accounts on it, typed, in name order ----
    parts_ = new QTableWidget(0, 3);
    parts_->setObjectName("parts");
    parts_->setHorizontalHeaderLabels({tr("Account type (optional)"), tr("Number (last four)"), tr("Owners (double-click)")});
    parts_->horizontalHeader()->setStretchLastSection(true);
    parts_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    parts_->verticalHeader()->hide();
    parts_->setSelectionBehavior(QAbstractItemView::SelectRows);
    parts_->setSelectionMode(QAbstractItemView::SingleSelection);
    parts_->setMinimumHeight(130);
    auto* addPartBtn = new QPushButton(tr("Add Account"));
    addPartBtn->setObjectName("addPart");
    removePartBtn_ = new QPushButton(tr("Remove"));
    removePartBtn_->setObjectName("removePart");
    partUpBtn_ = new QPushButton(tr("Move Up"));
    partDownBtn_ = new QPushButton(tr("Move Down"));
    partOwnersBtn_ = new QPushButton(tr("Owners..."));
    partOwnersBtn_->setObjectName("partOwners");
    partOwnersBtn_->setToolTip(tr("Choose who owns the selected account"));
    auto* partButtons = new QVBoxLayout;
    partButtons->addWidget(addPartBtn);
    partButtons->addWidget(partOwnersBtn_);
    partButtons->addWidget(partUpBtn_);
    partButtons->addWidget(partDownBtn_);
    partButtons->addWidget(removePartBtn_);
    partButtons->addStretch();
    auto* partRow = new QHBoxLayout;
    partRow->addWidget(parts_, 1);
    partRow->addLayout(partButtons);
    connect(addPartBtn, &QPushButton::clicked, this, [this] { addPart(); });

    auto* partsHint = new QLabel(tr("The accounts on this statement, in the order they appear in the "
                                    "name, e.g. Checking 1111, Savings 2222. Just type them; they don't "
                                    "need to be saved as accounts. At least two. Each account has its own "
                                    "owners (double-click the Owners cell); accounts with the same owners "
                                    "are grouped in the name, e.g. Chk x1111, Sav x2222 (W), Chk x3333 (H)."));
    partsHint->setWordWrap(true);
    partsHint->setStyleSheet("color: palette(placeholder-text);");

    combinedPage_ = new QGroupBox(tr("Accounts on the statement"));
    auto* combinedLayout = new QVBoxLayout(combinedPage_);
    combinedLayout->addWidget(partsHint);
    combinedLayout->addLayout(partRow);

    // ---- Owners ----
    owners_ = new QListWidget;
    owners_->setDragDropMode(QAbstractItemView::InternalMove);  // drag to reorder
    owners_->setMinimumHeight(90);

    upBtn_ = new QPushButton(tr("Move Up"));
    downBtn_ = new QPushButton(tr("Move Down"));
    removeOwnerBtn_ = new QPushButton(tr("Remove"));
    auto* orderButtons = new QVBoxLayout;
    orderButtons->addWidget(upBtn_);
    orderButtons->addWidget(downBtn_);
    orderButtons->addWidget(removeOwnerBtn_);
    orderButtons->addStretch();

    auto* ownerRow = new QHBoxLayout;
    ownerRow->addWidget(owners_, 1);
    ownerRow->addLayout(orderButtons);

    peopleChoice_ = new QComboBox;
    addOwnerBtn_ = new QPushButton(tr("Add"));
    auto* newPersonBtn = new QPushButton(tr("New Person..."));
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(peopleChoice_, 1);
    addRow->addWidget(addOwnerBtn_);
    addRow->addWidget(newPersonBtn);

    auto* ownersHint = new QLabel(tr("Names appear in the filename in this order. Optional."));
    ownersHint->setStyleSheet("color: palette(placeholder-text);");

    ownersBox_ = new QGroupBox(tr("Owners"));
    auto* ownersBox = ownersBox_;
    auto* ownersLayout = new QVBoxLayout(ownersBox);
    ownersLayout->addWidget(ownersHint);
    ownersLayout->addLayout(ownerRow);
    ownersLayout->addLayout(addRow);

    // ---- Preview + errors ----
    preview_ = new QLabel;
    preview_->setWordWrap(true);
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_->setMinimumHeight(preview_->fontMetrics().lineSpacing() * 3 + 6);  // file, note, folder
    auto* previewBox = new QGroupBox(tr("Names"));
    auto* previewLayout = new QVBoxLayout(previewBox);
    previewLayout->addWidget(preview_);

    error_ = new QLabel;
    error_->setWordWrap(true);
    error_->setStyleSheet("color: #c42b1c;");
    error_->hide();

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(kindForm);
    layout->addWidget(singlePage_);
    layout->addWidget(combinedPage_);
    layout->addWidget(ownersBox, 1);
    layout->addWidget(previewBox);
    layout->addWidget(error_);
    layout->addWidget(buttons_);

    // ---- Fill in an existing account ----
    if (editingCombined) {
        loadPeople();
        for (const CombinedPart& p : existing_->parts) addPart(qstr(p.accountType), qstr(p.lastFour), p.ownerIds);
    } else if (!existing_) {
        loadPeople();
        addPart();
        addPart();  // a new statement starts with two empty rows
    }
    if (existing_) {
        institution_->setText(qstr(existing_->institution));
        // Blank when it's just the institution, so editing the institution carries through.
        if (existing_->institutionDisplay != existing_->institution) {
            institutionDisplay_->setText(qstr(existing_->institutionDisplay));
        }
        type_->setText(qstr(existing_->accountType));
        openedOn_->setDate(existing_->openedOn);
        closedOn_->setDate(existing_->closedOn);
        addNumberItem(numbers_, qstr(existing_->lastFour));
        for (const auto& n : existing_->previousLastFour) addNumberItem(numbers_, qstr(n));
        runGuarded(this, [&] {
            const auto people = db_.listPeople(caseId_);
            for (const auto id : existing_->ownerIds) {
                for (const auto& p : people)
                    if (p.id == id) addOwnerItem(owners_, p);
            }
        });
    }
    if (numbers_->count() == 0) addNumberItem(numbers_, QString());  // a new account: type its number here
    numbers_->setCurrentRow(0);
    styleNumbers();
    reloadPeopleChoices();

    // ---- Wiring ----
    connect(kind_, &QComboBox::currentIndexChanged, this, &AccountDialog::updateKind);
    connect(parts_, &QTableWidget::currentCellChanged, this, &AccountDialog::updateState);
    connect(parts_, &QTableWidget::itemChanged, this, &AccountDialog::updateState);
    connect(removePartBtn_, &QPushButton::clicked, this, &AccountDialog::removePart);
    connect(partOwnersBtn_, &QPushButton::clicked, this, [this] { editPartOwners(parts_->currentRow()); });
    connect(parts_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == 2) editPartOwners(row);
    });
    connect(partUpBtn_, &QPushButton::clicked, this, [this] { movePart(-1); });
    connect(partDownBtn_, &QPushButton::clicked, this, [this] { movePart(+1); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &AccountDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto* edit : {institution_, institutionDisplay_, type_})
        connect(edit, &QLineEdit::textChanged, this, &AccountDialog::updateState);
    connect(openedOn_, &DateField::changed, this, &AccountDialog::updateState);
    connect(closedOn_, &DateField::changed, this, &AccountDialog::updateState);
    connect(numbers_, &QListWidget::itemChanged, this, &AccountDialog::updateState);
    connect(numbers_, &QListWidget::currentRowChanged, this, &AccountDialog::updateState);
    connect(addNumberBtn, &QPushButton::clicked, this, &AccountDialog::addNumber);
    connect(removeNumberBtn_, &QPushButton::clicked, this, &AccountDialog::removeNumber);
    connect(numberUpBtn_, &QPushButton::clicked, this, [this] { moveNumber(-1); });
    connect(numberDownBtn_, &QPushButton::clicked, this, [this] { moveNumber(+1); });
    connect(owners_, &QListWidget::currentRowChanged, this, &AccountDialog::updateState);
    connect(owners_->model(), &QAbstractItemModel::rowsMoved, this, &AccountDialog::updateState);
    connect(addOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::addSelectedPerson);
    connect(newPersonBtn, &QPushButton::clicked, this, &AccountDialog::addNewPerson);
    connect(removeOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::removeOwner);
    connect(upBtn_, &QPushButton::clicked, this, [this] { moveOwner(-1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveOwner(+1); });

    resize(560, 640);
    updateKind();
}

AccountDialog::~AccountDialog()
{
    disconnectChildren(this);
}

bool AccountDialog::isCombined() const
{
    return kind_->currentIndex() == 1;
}

void AccountDialog::updateKind()
{
    singlePage_->setVisible(!isCombined());
    combinedPage_->setVisible(isCombined());
    ownersBox_->setVisible(!isCombined());
    if (!existing_) setWindowTitle(isCombined() ? tr("New Combined Statement") : tr("New Account"));
    updateState();
}

void AccountDialog::addPart(const QString& type, const QString& number, const std::vector<std::int64_t>& owners)
{
    const QSignalBlocker block(parts_);
    const int row = parts_->rowCount();
    // A new account starts with the owners of the one above it: they are usually the same.
    const bool fresh = type.isEmpty() && number.isEmpty() && owners.empty();
    const std::vector<std::int64_t> initial = fresh && row > 0 ? partOwners(row - 1) : owners;
    parts_->insertRow(row);
    parts_->setItem(row, 0, new QTableWidgetItem(type));
    parts_->setItem(row, 1, new QTableWidgetItem(number));
    auto* ownersItem = new QTableWidgetItem;
    ownersItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    parts_->setItem(row, 2, ownersItem);
    setPartOwners(row, initial);
    parts_->setCurrentCell(row, 0);
    if (isVisible()) parts_->editItem(parts_->item(row, 0));
    updateState();
}

void AccountDialog::loadPeople()
{
    people_.clear();
    runGuarded(this, [&] {
        for (const auto& p : db_.listPeople(caseId_)) people_[p.id] = p;
    });
}

void AccountDialog::setPartOwners(int row, const std::vector<std::int64_t>& ids)
{
    QTableWidgetItem* item = parts_->item(row, 2);
    if (!item) return;
    QVariantList list;
    QStringList names;
    for (const auto id : ids) {
        list << QVariant::fromValue<qlonglong>(id);
        const auto it = people_.find(id);
        if (it != people_.end()) names << qstr(it->second.displayName);
    }
    item->setData(Qt::UserRole, list);
    item->setText(names.isEmpty() ? tr("(none)") : names.join(QStringLiteral("; ")));
}

std::vector<std::int64_t> AccountDialog::partOwners(int row) const
{
    std::vector<std::int64_t> ids;
    if (QTableWidgetItem* item = parts_->item(row, 2))
        for (const QVariant& v : item->data(Qt::UserRole).toList()) ids.push_back(v.toLongLong());
    return ids;
}

void AccountDialog::editPartOwners(int row)
{
    if (row < 0 || row >= parts_->rowCount()) return;
    std::vector<Person> people;
    for (const auto& [id, person] : people_) people.push_back(person);
    const QString type = parts_->item(row, 0) ? parts_->item(row, 0)->text().trimmed() : QString();
    const QString number = parts_->item(row, 1) ? parts_->item(row, 1)->text().trimmed() : QString();
    const QString what = (type + (number.isEmpty() ? QString() : QStringLiteral(" x") + number)).trimmed();
    PartOwnersDialog dialog(people, partOwners(row), what.isEmpty() ? tr("this account") : what, this);
    if (dialog.exec() != QDialog::Accepted) return;
    setPartOwners(row, dialog.owners());
    updateState();
}

void AccountDialog::removePart()
{
    const int row = parts_->currentRow();
    if (row < 0) return;
    parts_->removeRow(row);
    updateState();
}

void AccountDialog::movePart(int delta)
{
    const int row = parts_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= parts_->rowCount()) return;
    const QSignalBlocker block(parts_);
    for (int col = 0; col < 3; ++col) {
        QTableWidgetItem* a = parts_->takeItem(row, col);
        QTableWidgetItem* b = parts_->takeItem(target, col);
        parts_->setItem(row, col, b);
        parts_->setItem(target, col, a);
    }
    parts_->setCurrentCell(target, parts_->currentColumn() < 0 ? 0 : parts_->currentColumn());
    updateState();
}

std::vector<CombinedPart> AccountDialog::parts() const
{
    // Rows left completely blank are ignored.
    std::vector<CombinedPart> out;
    for (int row = 0; row < parts_->rowCount(); ++row) {
        const QString type = parts_->item(row, 0) ? parts_->item(row, 0)->text().trimmed() : QString();
        const QString number = parts_->item(row, 1) ? parts_->item(row, 1)->text().trimmed() : QString();
        if (type.isEmpty() && number.isEmpty()) continue;
        CombinedPart part{stdstr(type), stdstr(number), {}, partOwners(row)};
        for (const auto id : part.ownerIds) {
            const auto it = people_.find(id);
            if (it != people_.end()) part.owners.push_back(it->second.displayName);
        }
        out.push_back(std::move(part));
    }
    return out;
}

void AccountDialog::reloadPeopleChoices()
{
    std::set<qlonglong> taken;
    for (int i = 0; i < owners_->count(); ++i)
        taken.insert(owners_->item(i)->data(kIdRole).toLongLong());

    peopleChoice_->clear();
    runGuarded(this, [&] {
        for (const auto& p : db_.listPeople(caseId_)) {
            if (taken.count(p.id)) continue;
            peopleChoice_->addItem(personLabel(p), QVariant::fromValue<qlonglong>(p.id));
            peopleChoice_->setItemData(peopleChoice_->count() - 1, qstr(p.displayName), kDisplayRole);
        }
    });
    if (peopleChoice_->count() == 0)
        peopleChoice_->setPlaceholderText(tr("Everyone on this case is already an owner"));
}

void AccountDialog::addSelectedPerson()
{
    const int index = peopleChoice_->currentIndex();
    if (index < 0) return;
    auto* item = new QListWidgetItem(peopleChoice_->itemText(index), owners_);
    item->setData(kIdRole, peopleChoice_->itemData(index));
    item->setData(kDisplayRole, peopleChoice_->itemData(index, kDisplayRole));
    reloadPeopleChoices();
    updateState();
}

void AccountDialog::addNewPerson()
{
    PersonDialog dialog(tr("New Person"), {}, {}, this);
    if (dialog.exec() != QDialog::Accepted) return;

    Person p;
    if (!runGuarded(this, [&] {
            p.id = db_.addPerson(caseId_, stdstr(dialog.fullName()), stdstr(dialog.displayName()));
            for (const auto& existing : db_.listPeople(caseId_))
                if (existing.id == p.id) p = existing;  // pick up the cleaned-up names
        }))
        return;
    addedPeople_ = true;

    addOwnerItem(owners_, p);
    reloadPeopleChoices();
    updateState();
}

void AccountDialog::removeOwner()
{
    delete owners_->takeItem(owners_->currentRow());
    reloadPeopleChoices();
    updateState();
}

void AccountDialog::moveOwner(int delta)
{
    const int row = owners_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= owners_->count()) return;
    QListWidgetItem* item = owners_->takeItem(row);
    owners_->insertItem(target, item);
    owners_->setCurrentRow(target);
    updateState();
}

Account AccountDialog::currentAccount() const
{
    Account a;
    for (int i = 0; i < owners_->count(); ++i)
        a.owners.push_back(stdstr(owners_->item(i)->data(kDisplayRole).toString()));
    a.institution = stdstr(institution_->text());
    a.institutionDisplay = stdstr(institutionDisplay_->text().trimmed());
    if (isCombined()) {
        a.owners.clear();  // the accounts on it carry the owners
        a.combined = parts();
        return a;
    }
    a.accountType = stdstr(type_->text());
    const auto all = numbers();
    if (!all.empty()) {
        a.lastFour = all.front();
        a.previousLastFour.assign(all.begin() + 1, all.end());
    }
    return a;
}

std::vector<std::string> AccountDialog::numbers() const
{
    std::vector<std::string> out;
    for (int i = 0; i < numbers_->count(); ++i) {
        const QString n = numbers_->item(i)->text().trimmed();
        if (!n.isEmpty()) out.push_back(stdstr(n));
    }
    return out;
}

Database::NumberCorrections AccountDialog::numberCorrections() const
{
    // An existing number whose text was edited is a correction (a typo fix):
    // files named with the old text should get the new one.
    Database::NumberCorrections out;
    for (int i = 0; i < numbers_->count(); ++i) {
        const QString saved = numbers_->item(i)->data(kSavedNumberRole).toString().trimmed();
        const QString now = numbers_->item(i)->text().trimmed();
        if (!saved.isEmpty() && !now.isEmpty() && saved.compare(now, Qt::CaseInsensitive) != 0)
            out.push_back({stdstr(saved), stdstr(now)});
    }
    return out;
}

void AccountDialog::addNumber()
{
    QListWidgetItem* item = addNumberItem(numbers_, QString(), 0);  // newest goes on top
    numbers_->setCurrentItem(item);
    numbers_->editItem(item);
    styleNumbers();
    updateState();
}

void AccountDialog::removeNumber()
{
    delete numbers_->takeItem(numbers_->currentRow());
    if (numbers_->count() == 0) numbers_->setCurrentItem(addNumberItem(numbers_, QString()));
    styleNumbers();
    updateState();
}

void AccountDialog::moveNumber(int delta)
{
    const int row = numbers_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= numbers_->count()) return;
    QListWidgetItem* item = numbers_->takeItem(row);
    numbers_->insertItem(target, item);
    numbers_->setCurrentRow(target);
    styleNumbers();
    updateState();
}

void AccountDialog::styleNumbers()
{
    // The top number is the current one: show it in bold.
    const QSignalBlocker block(numbers_);
    for (int i = 0; i < numbers_->count(); ++i) {
        QListWidgetItem* item = numbers_->item(i);
        QFont font = item->font();
        font.setBold(i == 0);
        item->setFont(font);
        item->setToolTip(i == 0 ? tr("Current number") : tr("Earlier number"));
    }
}

void AccountDialog::updateState()
{
    const int row = owners_->currentRow();
    upBtn_->setEnabled(row > 0);
    downBtn_->setEnabled(row >= 0 && row < owners_->count() - 1);
    removeOwnerBtn_->setEnabled(row >= 0);
    addOwnerBtn_->setEnabled(peopleChoice_->count() > 0);

    const int numberRow = numbers_->currentRow();
    numberUpBtn_->setEnabled(numberRow > 0);
    numberDownBtn_->setEnabled(numberRow >= 0 && numberRow < numbers_->count() - 1);
    removeNumberBtn_->setEnabled(numberRow >= 0 && numbers_->count() > 1);

    const int partRow = parts_->currentRow();
    partUpBtn_->setEnabled(partRow > 0);
    partDownBtn_->setEnabled(partRow >= 0 && partRow < parts_->rowCount() - 1);
    removePartBtn_->setEnabled(partRow >= 0);
    partOwnersBtn_->setEnabled(partRow >= 0);

    const Account a = currentAccount();
    const bool valid = !validateAccount(a);
    // An opening/closing date may be left blank, but not half-typed or impossible.
    const bool datesOk = isCombined() || ((openedOn_->isBlank() || openedOn_->date()) &&
                                          (closedOn_->isBlank() || closedOn_->date()));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid && datesOk);

    // Shows the cleaned-up label, so illegal characters are visibly replaced.
    if (valid && a.isCombined()) {
        preview_->setTextFormat(Qt::PlainText);
        preview_->setText(tr("File:    YYYY.MM.DD %1.pdf\nFolder:  %1").arg(qstr(accountLabel(a))));
    } else if (valid) {
        preview_->setTextFormat(Qt::PlainText);
        QString text = tr("File:    YYYY.MM.DD %1.pdf").arg(qstr(accountLabel(a)));
        if (!a.previousLastFour.empty())
            text += tr("\n          (older statements: pick the number they show when renaming)");
        text += tr("\nFolder:  %1").arg(qstr(accountFolderLabel(a)));
        preview_->setText(text);
    } else {
        preview_->setTextFormat(Qt::RichText);
        preview_->setText(isCombined() ? tr("<i>Fill in the institution and at least two accounts, each with a number.</i>")
                                       : tr("<i>Fill in the institution and an account number (the type is optional).</i>"));
    }
}

void AccountDialog::accept()
{
    AccountRecord r;
    r.id = existing_ ? existing_->id : 0;
    r.caseId = caseId_;
    if (!isCombined()) {
        for (int i = 0; i < owners_->count(); ++i)
            r.ownerIds.push_back(owners_->item(i)->data(kIdRole).toLongLong());
    } else {
        r.parts = parts();  // a combined statement's owners belong to its accounts
    }
    r.institution = stdstr(institution_->text());
    r.institutionDisplay = stdstr(institutionDisplay_->text());
    r.accountType = stdstr(type_->text());
    const Account shown = currentAccount();
    r.lastFour = shown.lastFour;
    r.previousLastFour = shown.previousLastFour;
    if (!isCombined()) {
        r.openedOn = openedOn_->date();
        r.closedOn = closedOn_->date();
    }

    try {
        if (existing_) {
            db_.updateAccount(r, isCombined() ? Database::NumberCorrections{} : numberCorrections());
            savedId_ = r.id;
        } else {
            savedId_ = db_.createAccount(r);
        }
    } catch (const DatabaseError& e) {
        error_->setText(qstr(e.what()));  // keep the dialog open so it can be fixed
        error_->show();
        return;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Database error"),
                              tr("Something went wrong saving the account:\n\n%1").arg(qstr(e.what())));
        return;
    }
    QDialog::accept();
}
