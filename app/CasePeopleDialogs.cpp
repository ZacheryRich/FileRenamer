#include "CasePeopleDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

template <class T>
QLineEdit* addLine(QFormLayout* form, const QString& label, const char* objectName, const T& initial,
                   std::string T::*member, std::vector<std::pair<QLineEdit*, std::string T::*>>& edits)
{
    auto* e = new QLineEdit(qstr(initial.*member));
    e->setObjectName(objectName);
    e->setPlaceholderText(QObject::tr("Optional"));
    form->addRow(label, e);
    edits.push_back({e, member});
    return e;
}

QDialogButtonBox* okCancel(QDialog* d)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return buttons;
}

}  // namespace

// ---- PartyDialog ------------------------------------------------------------------------------------

PartyDialog::PartyDialog(const CaseParty& initial, QWidget* parent) : QDialog(parent), party_(initial)
{
    setWindowTitle(initial.id == 0 ? tr("Add Party") : tr("Edit Party"));

    auto* top = new QFormLayout;
    auto* name = addLine(top, tr("Name:"), "partyName", initial, &CaseParty::name, edits_);
    role_ = new QComboBox;
    role_->setObjectName("partyRole");
    role_->addItem(tr("Petitioner"));
    role_->addItem(tr("Respondent"));
    role_->setCurrentIndex(initial.role == PartyRole::Respondent ? 1 : 0);
    top->addRow(tr("Role:"), role_);
    client_ = new QCheckBox(tr("Our client"));
    client_->setObjectName("ourClient");
    client_->setChecked(initial.ourClient);
    client_->setToolTip(tr("Documents fill \"Client\" with this party, and \"Opposing Party\" with the other side"));
    top->addRow(QString(), client_);

    auto* details = new QGroupBox(tr("Details"));
    auto* cols = new QHBoxLayout(details);
    auto* left = new QFormLayout;
    auto* right = new QFormLayout;
    cols->addLayout(left);
    cols->addLayout(right);
    addLine(left, tr("Address:"), "partyAddress", initial, &CaseParty::address, edits_);
    addLine(left, tr("City, State, ZIP:"), "partyCityStateZip", initial, &CaseParty::cityStateZip, edits_);
    addLine(left, tr("Phone:"), "partyPhone", initial, &CaseParty::phone, edits_);
    addLine(left, tr("Email:"), "partyEmail", initial, &CaseParty::email, edits_);
    addLine(left, tr("Employer:"), "partyEmployer", initial, &CaseParty::employer, edits_);
    addLine(left, tr("Name change to:"), "partyNameChange", initial, &CaseParty::nameChange, edits_);
    addLine(right, tr("Date of birth:"), "partyDob", initial, &CaseParty::dob, edits_);
    addLine(right, tr("Place of birth:"), "partyPob", initial, &CaseParty::pob, edits_);
    addLine(right, tr("SSN:"), "partySsn", initial, &CaseParty::ssn, edits_);
    addLine(right, tr("Driver's license:"), "partyDl", initial, &CaseParty::driversLicense, edits_);
    addLine(right, tr("Car:"), "partyCar", initial, &CaseParty::car, edits_);

    auto* note = new QLabel(tr("Everything is optional. These details are kept only in this program's database on "
                               "this computer."));
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(placeholder-text);");

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(details);
    layout->addWidget(note);
    layout->addWidget(okCancel(this));
    resize(680, 380);
    name->setFocus();
}

CaseParty PartyDialog::result() const
{
    CaseParty p = party_;
    for (const auto& [edit, member] : edits_) p.*member = stdstr(edit->text().trimmed());
    p.role = role_->currentIndex() == 1 ? PartyRole::Respondent : PartyRole::Petitioner;
    p.ourClient = client_->isChecked();
    return p;
}

// ---- ChildDialog ------------------------------------------------------------------------------------

ChildDialog::ChildDialog(const CaseChild& initial, QWidget* parent) : QDialog(parent), child_(initial)
{
    setWindowTitle(initial.id == 0 ? tr("Add Child") : tr("Edit Child"));
    auto* form = new QFormLayout;
    auto* name = addLine(form, tr("Name:"), "childName", initial, &CaseChild::name, edits_);
    addLine(form, tr("Date of birth:"), "childDob", initial, &CaseChild::dob, edits_);
    auto* age = addLine(form, tr("Age:"), "childAge", initial, &CaseChild::age, edits_);
    age->setPlaceholderText(tr("Optional; worked out from the date of birth when left blank"));
    addLine(form, tr("Sex:"), "childSex", initial, &CaseChild::sex, edits_);
    addLine(form, tr("Place of birth:"), "childPob", initial, &CaseChild::pob, edits_);
    addLine(form, tr("School:"), "childSchool", initial, &CaseChild::school, edits_);
    addLine(form, tr("SSN:"), "childSsn", initial, &CaseChild::ssn, edits_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
    resize(460, 300);
    name->setFocus();
}

CaseChild ChildDialog::result() const
{
    CaseChild c = child_;
    for (const auto& [edit, member] : edits_) c.*member = stdstr(edit->text().trimmed());
    return c;
}

// ---- ContactDialog ----------------------------------------------------------------------------------

ContactDialog::ContactDialog(const CaseContact& initial, QWidget* parent) : QDialog(parent), contact_(initial)
{
    setWindowTitle(initial.id == 0 ? tr("Add Person") : tr("Edit Person"));
    auto* form = new QFormLayout;
    role_ = new QComboBox;
    role_->setObjectName("contactRole");
    role_->setEditable(true);
    role_->addItems({tr("Prior Counsel"), tr("Third Party"), tr("Expert"), tr("Guardian ad Litem"), tr("Amicus Attorney"),
                     tr("Mediator"), tr("Other")});
    role_->setCurrentText(qstr(initial.role));
    if (initial.id == 0 && initial.role.empty()) role_->setCurrentIndex(0);
    role_->setToolTip(tr("Choose one or type your own"));
    form->addRow(tr("Role:"), role_);
    auto* name = addLine(form, tr("Name:"), "contactName", initial, &CaseContact::name, edits_);
    addLine(form, tr("Firm / organization:"), "contactFirm", initial, &CaseContact::firm, edits_);
    addLine(form, tr("Bar number:"), "contactBar", initial, &CaseContact::barNumber, edits_);
    address_ = new QPlainTextEdit(qstr(initial.address));
    address_->setObjectName("contactAddress");
    address_->setPlaceholderText(tr("Optional; the last line is read as city, state, ZIP"));
    address_->setTabChangesFocus(true);
    address_->setFixedHeight(address_->fontMetrics().lineSpacing() * 4 + 14);
    form->addRow(tr("Address:"), address_);
    addLine(form, tr("Phone:"), "contactPhone", initial, &CaseContact::phone, edits_);
    addLine(form, tr("Fax:"), "contactFax", initial, &CaseContact::fax, edits_);
    addLine(form, tr("Email:"), "contactEmail", initial, &CaseContact::email, edits_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
    resize(480, 380);
    name->setFocus();
}

CaseContact ContactDialog::result() const
{
    CaseContact c = contact_;
    for (const auto& [edit, member] : edits_) c.*member = stdstr(edit->text().trimmed());
    c.role = stdstr(role_->currentText().trimmed());
    c.address = stdstr(address_->toPlainText().trimmed());
    return c;
}
