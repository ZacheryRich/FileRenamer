#include "PersonDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PersonDialog::PersonDialog(const QString& title, const QString& fullName,
                           const QString& displayName, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);

    fullName_ = new QLineEdit(fullName);

    // When the display name is just the full name, leave the box empty so that
    // renaming the person also updates what shows in filenames.
    displayName_ = new QLineEdit(displayName == fullName ? QString() : displayName);
    displayName_->setPlaceholderText(tr("Same as full name"));

    auto* hint = new QLabel(tr("The display name is what appears in filenames, "
                               "for example initials like \"H\" or \"JS\"."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");

    auto* form = new QFormLayout;
    form->addRow(tr("Full name:"), fullName_);
    form->addRow(tr("Display name:"), displayName_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(fullName_, &QLineEdit::textChanged, this, &PersonDialog::updateOk);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons_);

    resize(400, sizeHint().height());
    updateOk();
}

QString PersonDialog::fullName() const { return fullName_->text().trimmed(); }

QString PersonDialog::displayName() const { return displayName_->text().trimmed(); }

void PersonDialog::updateOk()
{
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!fullName().isEmpty());
}
