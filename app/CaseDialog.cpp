#include "CaseDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace ui;
using finrenamer::ClientCase;

CaseDialog::CaseDialog(const ClientCase& initial, QWidget* parent) : QDialog(parent), initial_(initial)
{
    setWindowTitle(initial.id == 0 ? tr("New Case") : tr("Edit Case"));

    auto line = [&](QFormLayout* form, const QString& label, const char* objectName, std::string ClientCase::*member,
                    const QString& hint = {}) {
        auto* e = new QLineEdit(qstr(initial.*member));
        e->setObjectName(objectName);
        e->setPlaceholderText(hint.isEmpty() ? tr("Optional") : hint);
        form->addRow(label, e);
        edits_.push_back({e, member});
        return e;
    };

    auto* caseBox = new QGroupBox(tr("Case"));
    auto* caseForm = new QFormLayout(caseBox);
    line(caseForm, tr("Client name:"), "clientName", &ClientCase::clientName, tr("Optional (the name in the Case List)"));
    line(caseForm, tr("Type:"), "caseType", &ClientCase::caseType, tr("Optional, e.g. Divorce, Modification"));
    line(caseForm, tr("Cause number:"), "causeNumber", &ClientCase::causeNumber);
    line(caseForm, tr("County:"), "county", &ClientCase::county);
    line(caseForm, tr("Court:"), "court", &ClientCase::court,
         tr("Optional, as it should read, e.g. In the District Court, 345th Judicial District"));

    auto* marriageBox = new QGroupBox(tr("Marriage"));
    auto* marriageForm = new QFormLayout(marriageBox);
    line(marriageForm, tr("Date of marriage:"), "marriageDate", &ClientCase::marriageDate);
    line(marriageForm, tr("Place of marriage:"), "marriagePlace", &ClientCase::marriagePlace);
    line(marriageForm, tr("Date of separation:"), "separationDate", &ClientCase::separationDate);

    auto* insuranceBox = new QGroupBox(tr("Insurance"));
    auto* insuranceForm = new QFormLayout(insuranceBox);
    line(insuranceForm, tr("Insurance:"), "insurance", &ClientCase::insurance);
    line(insuranceForm, tr("Carrier:"), "insuranceCarrier", &ClientCase::insuranceCarrier);
    line(insuranceForm, tr("Policy no.:"), "policyNumber", &ClientCase::policyNumber);
    line(insuranceForm, tr("Group no.:"), "groupNumber", &ClientCase::groupNumber);

    notes_ = new QPlainTextEdit(qstr(initial.notes));
    notes_->setObjectName("notes");
    notes_->setPlaceholderText(tr("Optional"));
    notes_->setTabChangesFocus(true);
    auto* notesBox = new QGroupBox(tr("Notes"));
    auto* notesLayout = new QVBoxLayout(notesBox);
    notesLayout->addWidget(notes_);

    auto* right = new QVBoxLayout;
    right->addWidget(marriageBox);
    right->addWidget(insuranceBox);
    auto* top = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    left->addWidget(caseBox);
    left->addWidget(notesBox, 1);
    top->addLayout(left, 3);
    top->addLayout(right, 2);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);
    resize(820, 420);
}

ClientCase CaseDialog::result() const
{
    ClientCase c = initial_;
    for (const auto& [edit, member] : edits_) c.*member = stdstr(edit->text().trimmed());
    c.notes = stdstr(notes_->toPlainText().trimmed());
    return c;
}
