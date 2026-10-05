#include "CaseDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace ui;

CaseDialog::CaseDialog(const finrenamer::ClientCase& initial, QWidget* parent)
    : QDialog(parent), initial_(initial)
{
    setWindowTitle(initial.id == 0 ? tr("New Case") : tr("Edit Case"));

    name_ = new QLineEdit(qstr(initial.clientName));

    notes_ = new QPlainTextEdit(qstr(initial.notes));
    notes_->setPlaceholderText(tr("Optional"));
    notes_->setTabChangesFocus(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Client name:"), name_);
    form->addRow(tr("Notes:"), notes_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, &CaseDialog::updateOk);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons_);

    resize(420, 260);
    updateOk();
}

finrenamer::ClientCase CaseDialog::result() const
{
    finrenamer::ClientCase c = initial_;
    c.clientName = stdstr(name_->text().trimmed());
    c.notes = stdstr(notes_->toPlainText().trimmed());
    return c;
}

void CaseDialog::updateOk()
{
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name_->text().trimmed().isEmpty());
}
