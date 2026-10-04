#include "AccountDialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <set>

#include "QtHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;
using namespace ui;

namespace {
constexpr int kIdRole = Qt::UserRole;
}

AccountDialog::AccountDialog(Database& db, std::int64_t caseId,
                             std::optional<AccountRecord> existing, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId), existing_(std::move(existing))
{
    setWindowTitle(existing_ ? tr("Edit Account") : tr("New Account"));

    // ---- Account fields ----
    institution_ = new QLineEdit;
    institution_->setPlaceholderText(tr("e.g. Chase"));

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

    lastFour_ = new QLineEdit;
    lastFour_->setPlaceholderText(tr("1234"));
    lastFour_->setMaxLength(12);

    auto* form = new QFormLayout;
    form->addRow(tr("Institution:"), institution_);
    form->addRow(tr("Account type:"), type_);
    form->addRow(tr("Last four:"), lastFour_);

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
    auto* previewBox = new QGroupBox(tr("Files will be named"));
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
        type_->setText(qstr(existing_->accountType));
        lastFour_->setText(qstr(existing_->lastFour));
        runGuarded(this, [&] {
            const auto people = db_.listPeople(caseId_);
            for (const auto id : existing_->ownerIds) {
                for (const auto& p : people) {
                    if (p.id != id) continue;
                    auto* item = new QListWidgetItem(qstr(p.fullName), owners_);
                    item->setData(kIdRole, QVariant::fromValue<qlonglong>(p.id));
                }
            }
        });
    }
    reloadPeopleChoices();

    // ---- Wiring ----
    connect(buttons_, &QDialogButtonBox::accepted, this, &AccountDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto* edit : {institution_, type_, lastFour_})
        connect(edit, &QLineEdit::textChanged, this, &AccountDialog::updateState);
    connect(owners_, &QListWidget::currentRowChanged, this, &AccountDialog::updateState);
    connect(owners_->model(), &QAbstractItemModel::rowsMoved, this, &AccountDialog::updateState);
    connect(addOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::addSelectedPerson);
    connect(newPersonBtn, &QPushButton::clicked, this, &AccountDialog::addNewPerson);
    connect(removeOwnerBtn_, &QPushButton::clicked, this, &AccountDialog::removeOwner);
    connect(upBtn_, &QPushButton::clicked, this, [this] { moveOwner(-1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveOwner(+1); });

    resize(520, 520);
    updateState();
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
            peopleChoice_->addItem(qstr(p.fullName), QVariant::fromValue<qlonglong>(p.id));
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
    reloadPeopleChoices();
    updateState();
}

void AccountDialog::addNewPerson()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Person"),
                                               tr("Full name as it should appear in filenames:"),
                                               QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    std::int64_t id = 0;
    if (!runGuarded(this, [&] { id = db_.addPerson(caseId_, stdstr(name)); })) return;
    addedPeople_ = true;

    auto* item = new QListWidgetItem(name, owners_);
    item->setData(kIdRole, QVariant::fromValue<qlonglong>(id));
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
    a.institution = stdstr(institution_->text());
    a.accountType = stdstr(type_->text());
    a.lastFour = stdstr(lastFour_->text());
    for (int i = 0; i < owners_->count(); ++i) a.owners.push_back(stdstr(owners_->item(i)->text()));
    return a;
}

void AccountDialog::updateState()
{
    const int row = owners_->currentRow();
    upBtn_->setEnabled(row > 0);
    downBtn_->setEnabled(row >= 0 && row < owners_->count() - 1);
    removeOwnerBtn_->setEnabled(row >= 0);
    addOwnerBtn_->setEnabled(peopleChoice_->count() > 0);

    const Account a = currentAccount();
    const bool valid = !validateAccount(a);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);

    // Shows the cleaned-up label, so illegal characters are visibly replaced.
    if (valid) {
        preview_->setTextFormat(Qt::PlainText);
        preview_->setText(tr("YYYY.MM.DD %1.pdf").arg(qstr(accountLabel(a))));
    } else {
        preview_->setTextFormat(Qt::RichText);
        preview_->setText(tr("<i>Fill in institution, account type and last four.</i>"));
    }
}

void AccountDialog::accept()
{
    AccountRecord r;
    r.id = existing_ ? existing_->id : 0;
    r.caseId = caseId_;
    r.institution = stdstr(institution_->text());
    r.accountType = stdstr(type_->text());
    r.lastFour = stdstr(lastFour_->text());
    for (int i = 0; i < owners_->count(); ++i)
        r.ownerIds.push_back(owners_->item(i)->data(kIdRole).toLongLong());

    try {
        if (existing_) {
            db_.updateAccount(r);
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
