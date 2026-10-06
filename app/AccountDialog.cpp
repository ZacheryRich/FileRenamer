#include "AccountDialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <set>

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
}

AccountDialog::AccountDialog(Database& db, std::int64_t caseId,
                             std::optional<AccountRecord> existing, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId), existing_(std::move(existing))
{
    setWindowTitle(existing_ ? tr("Edit Account") : tr("New Account"));

    // ---- Account fields ----
    institution_ = new QLineEdit;
    institution_->setPlaceholderText(tr("e.g. Chase"));

    institutionDisplay_ = new QLineEdit;
    institutionDisplay_->setPlaceholderText(tr("Same as institution"));
    institutionDisplay_->setToolTip(tr("What appears in filenames, e.g. \"BofA\" for Bank of America."));

    // Institutions used before (any case) autocomplete, and bring their
    // abbreviation with them.
    QStringList institutions;
    runGuarded(this, [&] {
        for (const auto& known : db_.institutionSuggestions()) {
            const QString name = qstr(known.institution);
            institutions << name;
            if (known.displayName != known.institution)
                knownDisplayNames_.insert(name.toLower(), qstr(known.displayName));
        }
    });
    auto* institutionCompleter = new QCompleter(institutions, this);
    institutionCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    institutionCompleter->setFilterMode(Qt::MatchContains);
    institution_->setCompleter(institutionCompleter);

    type_ = new QLineEdit;
    type_->setPlaceholderText(tr("e.g. Checking, Roth IRA, Credit Card"));
    QStringList types;
    runGuarded(this, [&] {
        for (const auto& t : db_.accountTypeSuggestions()) types << qstr(t);
    });
    auto* completer = new QCompleter(types, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    type_->setCompleter(completer);

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

    auto* form = new QFormLayout;
    form->addRow(tr("Institution:"), institution_);
    form->addRow(tr("Display name:"), institutionDisplay_);
    form->addRow(tr("Account type:"), type_);
    form->addRow(tr("Account numbers:"), numbersBox);

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

    auto* ownersBox = new QGroupBox(tr("Owners"));
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
    layout->addLayout(form);
    layout->addWidget(ownersBox, 1);
    layout->addWidget(previewBox);
    layout->addWidget(error_);
    layout->addWidget(buttons_);

    // ---- Fill in an existing account ----
    if (existing_) {
        institution_->setText(qstr(existing_->institution));
        // Blank when it's just the institution, so editing the institution carries through.
        if (existing_->institutionDisplay != existing_->institution) {
            institutionDisplay_->setText(qstr(existing_->institutionDisplay));
            displayEditedByUser_ = true;
        }
        type_->setText(qstr(existing_->accountType));
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
    connect(buttons_, &QDialogButtonBox::accepted, this, &AccountDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto* edit : {institution_, institutionDisplay_, type_})
        connect(edit, &QLineEdit::textChanged, this, &AccountDialog::updateState);
    connect(numbers_, &QListWidget::itemChanged, this, &AccountDialog::updateState);
    connect(numbers_, &QListWidget::currentRowChanged, this, &AccountDialog::updateState);
    connect(addNumberBtn, &QPushButton::clicked, this, &AccountDialog::addNumber);
    connect(removeNumberBtn_, &QPushButton::clicked, this, &AccountDialog::removeNumber);
    connect(numberUpBtn_, &QPushButton::clicked, this, [this] { moveNumber(-1); });
    connect(numberDownBtn_, &QPushButton::clicked, this, [this] { moveNumber(+1); });
    connect(institution_, &QLineEdit::textChanged, this, &AccountDialog::autofillInstitutionDisplay);
    // textEdited fires only for the user's own typing, not for autofill.
    connect(institutionDisplay_, &QLineEdit::textEdited, this, [this](const QString& text) {
        displayEditedByUser_ = !text.trimmed().isEmpty();
    });
    connect(owners_, &QListWidget::currentRowChanged, this, &AccountDialog::updateState);
    connect(owners_->model(), &QAbstractItemModel::rowsMoved, this, &AccountDialog::updateState);
    connect(addOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::addSelectedPerson);
    connect(newPersonBtn, &QPushButton::clicked, this, &AccountDialog::addNewPerson);
    connect(removeOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::removeOwner);
    connect(upBtn_, &QPushButton::clicked, this, [this] { moveOwner(-1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveOwner(+1); });

    resize(560, 640);
    updateState();
}

AccountDialog::~AccountDialog()
{
    disconnectChildren(this);
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

void AccountDialog::autofillInstitutionDisplay()
{
    if (displayEditedByUser_) return;
    // Known institution -> its usual abbreviation; anything else -> blank (= full name).
    institutionDisplay_->setText(
        knownDisplayNames_.value(institution_->text().trimmed().toLower()));
}

Account AccountDialog::currentAccount() const
{
    Account a;
    a.institution = stdstr(institution_->text());
    a.institutionDisplay = stdstr(institutionDisplay_->text().trimmed());
    a.accountType = stdstr(type_->text());
    const auto all = numbers();
    if (!all.empty()) {
        a.lastFour = all.front();
        a.previousLastFour.assign(all.begin() + 1, all.end());
    }
    for (int i = 0; i < owners_->count(); ++i)
        a.owners.push_back(stdstr(owners_->item(i)->data(kDisplayRole).toString()));
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

    const Account a = currentAccount();
    const bool valid = !validateAccount(a);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);

    // Shows the cleaned-up label, so illegal characters are visibly replaced.
    if (valid) {
        preview_->setTextFormat(Qt::PlainText);
        QString text = tr("File:    YYYY.MM.DD %1.pdf").arg(qstr(accountLabel(a)));
        if (!a.previousLastFour.empty())
            text += tr("\n          (older statements: pick the number they show when renaming)");
        text += tr("\nFolder:  %1").arg(qstr(accountFolderLabel(a)));
        preview_->setText(text);
    } else {
        preview_->setTextFormat(Qt::RichText);
        preview_->setText(tr("<i>Fill in institution, account type and an account number.</i>"));
    }
}

void AccountDialog::accept()
{
    AccountRecord r;
    r.id = existing_ ? existing_->id : 0;
    r.caseId = caseId_;
    r.institution = stdstr(institution_->text());
    r.institutionDisplay = stdstr(institutionDisplay_->text());
    r.accountType = stdstr(type_->text());
    const Account shown = currentAccount();
    r.lastFour = shown.lastFour;
    r.previousLastFour = shown.previousLastFour;
    for (int i = 0; i < owners_->count(); ++i)
        r.ownerIds.push_back(owners_->item(i)->data(kIdRole).toLongLong());

    try {
        if (existing_) {
            db_.updateAccount(r, numberCorrections());
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
