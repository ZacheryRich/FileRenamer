#pragma once

// The firm and attorney edit windows of the Attorney Directory, shared with Vacation Letters'
// Opposing Counsel window, plus helpers that open them and save to the database.

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdint>
#include <optional>
#include <vector>

#include "QtHelpers.h"
#include "finrenamer/Database.h"

namespace ui {

using finrenamer::Attorney;
using finrenamer::Firm;

inline QString typeText(bool ours) { return ours ? QObject::tr("Our firm") : QObject::tr("Opposing counsel"); }

// ---- Firm editor ---------------------------------------------------------------------------------

class FirmEditDialog : public QDialog {
public:
    FirmEditDialog(const Firm& initial, QWidget* parent) : QDialog(parent), firm_(initial)
    {
        setWindowTitle(initial.id == 0 ? tr("New Firm") : tr("Edit Firm"));
        name_ = new QLineEdit(qstr(initial.name));
        name_->setObjectName("firmName");
        type_ = new QComboBox;
        type_->setObjectName("firmType");
        type_->addItem(typeText(false));
        type_->addItem(typeText(true));
        type_->setCurrentIndex(initial.ours ? 1 : 0);
        phone_ = new QLineEdit(qstr(initial.phone));
        phone_->setObjectName("firmPhone");
        fax_ = new QLineEdit(qstr(initial.fax));
        fax_->setObjectName("firmFax");
        address_ = new QPlainTextEdit(qstr(initial.address));
        address_->setObjectName("firmAddress");
        address_->setTabChangesFocus(true);
        address_->setFixedHeight(address_->fontMetrics().lineSpacing() * 4 + 14);
        for (auto* e : {phone_, fax_}) e->setPlaceholderText(tr("Optional"));
        address_->setPlaceholderText(tr("Optional"));

        auto* form = new QFormLayout;
        form->addRow(tr("Firm name:"), name_);
        form->addRow(tr("Type:"), type_);
        form->addRow(tr("Phone:"), phone_);
        form->addRow(tr("Fax:"), fax_);
        form->addRow(tr("Address:"), address_);

        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(name_, &QLineEdit::textChanged, this, [this] { updateOk(); });
        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons_);
        resize(440, 300);
        updateOk();
    }

    Firm result() const
    {
        Firm f = firm_;
        f.name = stdstr(name_->text().trimmed());
        f.ours = type_->currentIndex() == 1;
        f.phone = stdstr(phone_->text().trimmed());
        f.fax = stdstr(fax_->text().trimmed());
        f.address = stdstr(address_->toPlainText().trimmed());
        return f;
    }

private:
    void updateOk() { buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name_->text().trimmed().isEmpty()); }

    Firm firm_;
    QLineEdit* name_;
    QComboBox* type_;
    QLineEdit* phone_;
    QLineEdit* fax_;
    QPlainTextEdit* address_;
    QDialogButtonBox* buttons_;
};

// ---- Attorney editor -----------------------------------------------------------------------------

class AttorneyEditDialog : public QDialog {
public:
    AttorneyEditDialog(const Attorney& initial, const std::vector<Firm>& firms, QWidget* parent)
        : QDialog(parent), attorney_(initial)
    {
        setWindowTitle(initial.id == 0 ? tr("New Attorney") : tr("Edit Attorney"));
        name_ = new QLineEdit(qstr(initial.name));
        name_->setObjectName("attorneyName");
        firm_ = new QComboBox;
        firm_->setObjectName("attorneyFirm");
        for (const auto& f : firms) {
            firm_->addItem(qstr(f.name), QVariant::fromValue<qlonglong>(f.id));
            if (f.id == initial.firmId) firm_->setCurrentIndex(firm_->count() - 1);
        }
        bar_ = new QLineEdit(qstr(initial.barNumber));
        bar_->setObjectName("attorneyBar");
        email_ = new QLineEdit(qstr(initial.email));
        email_->setObjectName("attorneyEmail");
        for (auto* e : {bar_, email_}) e->setPlaceholderText(tr("Optional"));
        signOff_ = new QLineEdit(qstr(initial.signOff));
        signOff_->setObjectName("attorneySignOff");
        signOff_->setPlaceholderText(tr("Sincerely,"));
        signOff_->setToolTip(tr("How this attorney's letters close (for our attorneys). Blank = \"Sincerely,\""));

        auto* form = new QFormLayout;
        form->addRow(tr("Name:"), name_);
        form->addRow(tr("Firm:"), firm_);
        form->addRow(tr("Bar number:"), bar_);
        form->addRow(tr("Email:"), email_);
        form->addRow(tr("Letter sign-off:"), signOff_);
        if (initial.id != 0) {
            auto* hint = new QLabel(tr("Choosing another firm moves the attorney there."));
            hint->setStyleSheet("color: palette(placeholder-text);");
            form->addRow(QString(), hint);
        }

        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(name_, &QLineEdit::textChanged, this, [this] { updateOk(); });
        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons_);
        resize(440, 250);
        updateOk();
    }

    Attorney result() const
    {
        Attorney a = attorney_;
        a.name = stdstr(name_->text().trimmed());
        a.firmId = firm_->currentData().toLongLong();
        a.barNumber = stdstr(bar_->text().trimmed());
        a.email = stdstr(email_->text().trimmed());
        a.signOff = stdstr(signOff_->text().trimmed());
        return a;
    }

private:
    void updateOk()
    {
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name_->text().trimmed().isEmpty() && firm_->count() > 0);
    }

    Attorney attorney_;
    QLineEdit* name_;
    QComboBox* firm_;
    QLineEdit* bar_;
    QLineEdit* email_;
    QLineEdit* signOff_;
    QDialogButtonBox* buttons_;
};


// Open the editor and save; asked again (with the typing kept) if the database refuses.
// Return the saved record, or nothing if cancelled.
std::optional<Firm> createFirmInteractive(finrenamer::Database& db, QWidget* parent, bool ours);
std::optional<Firm> editFirmInteractive(finrenamer::Database& db, QWidget* parent, const Firm& firm);
std::optional<Attorney> createAttorneyInteractive(finrenamer::Database& db, QWidget* parent, std::int64_t firmId);
std::optional<Attorney> editAttorneyInteractive(finrenamer::Database& db, QWidget* parent, const Attorney& attorney);

}  // namespace ui
