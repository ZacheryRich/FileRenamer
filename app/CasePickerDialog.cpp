#include "CasePickerDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "CaseDialog.h"
#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {
constexpr int kIdRole = Qt::UserRole;
}

CasePickerDialog::CasePickerDialog(Database& db, const QString& purpose, QWidget* parent) : QDialog(parent), db_(db)
{
    setWindowTitle(tr("Choose a Case"));

    auto* label = new QLabel(tr("Which case is the %1 for?").arg(purpose));
    filter_ = new QLineEdit;
    filter_->setObjectName("filter");
    filter_->setPlaceholderText(tr("Type to filter cases"));
    filter_->setClearButtonEnabled(true);

    list_ = new QListWidget;
    list_->setObjectName("cases");
    runGuarded(this, [&] {
        for (const ClientCase& c : db.listCases()) addItem(c);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* newBtn = new QPushButton(tr("New Case..."));
    newBtn->setObjectName("newCase");
    newBtn->setAutoDefault(false);
    buttons->addButton(newBtn, QDialogButtonBox::ActionRole);
    ok_ = buttons->button(QDialogButtonBox::Ok);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(filter_);
    layout->addWidget(list_, 1);
    layout->addWidget(buttons);

    connect(filter_, &QLineEdit::textChanged, this, &CasePickerDialog::filter);
    connect(list_, &QListWidget::currentItemChanged, this, &CasePickerDialog::updateButtons);
    connect(list_, &QListWidget::itemDoubleClicked, this, &CasePickerDialog::accept);
    connect(newBtn, &QPushButton::clicked, this, &CasePickerDialog::newCase);
    connect(buttons, &QDialogButtonBox::accepted, this, &CasePickerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (const auto last = lastCase()) selectCase(*last);
    if (!list_->currentItem() && list_->count() > 0) list_->setCurrentRow(0);
    updateButtons();
    resize(380, 420);
    filter_->setFocus();
    // Nothing to choose from yet: go straight to making the first case.
    if (list_->count() == 0) QTimer::singleShot(0, this, &CasePickerDialog::newCase);
}

CasePickerDialog::~CasePickerDialog()
{
    disconnectChildren(this);
}

std::optional<std::int64_t> CasePickerDialog::lastCase()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    if (!settings.contains("home/lastCase")) return std::nullopt;
    return settings.value("home/lastCase").toLongLong();
}

std::optional<std::int64_t> CasePickerDialog::selectedCaseId() const
{
    const QListWidgetItem* item = list_->currentItem();
    if (!item || item->isHidden()) return std::nullopt;
    return item->data(kIdRole).toLongLong();
}

void CasePickerDialog::selectCase(std::int64_t caseId)
{
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kIdRole).toLongLong() == caseId) list_->setCurrentRow(i);
}

void CasePickerDialog::addItem(const ClientCase& c)
{
    auto* item = new QListWidgetItem(qstr(c.clientName), list_);
    item->setData(kIdRole, QVariant::fromValue<qlonglong>(c.id));
}

void CasePickerDialog::newCase()
{
    CaseDialog dialog(ClientCase{}, this);
    if (dialog.exec() != QDialog::Accepted) return;
    std::int64_t id = 0;
    if (!runGuarded(this, [&] { id = db_.createCase(dialog.result(), defaultCasePeople()); })) return;
    filter_->clear();
    list_->clear();
    runGuarded(this, [&] {
        for (const ClientCase& c : db_.listCases()) addItem(c);
    });
    selectCase(id);
    updateButtons();
    accept();  // carry on with the new case
}

void CasePickerDialog::filter(const QString& text)
{
    for (int i = 0; i < list_->count(); ++i)
        list_->item(i)->setHidden(!list_->item(i)->text().contains(text.trimmed(), Qt::CaseInsensitive));
    // Keep a visible case selected.
    if (!list_->currentItem() || list_->currentItem()->isHidden()) {
        for (int i = 0; i < list_->count(); ++i)
            if (!list_->item(i)->isHidden()) {
                list_->setCurrentRow(i);
                break;
            }
    }
    updateButtons();
}

void CasePickerDialog::updateButtons()
{
    ok_->setEnabled(selectedCaseId().has_value());
}

void CasePickerDialog::accept()
{
    const auto id = selectedCaseId();
    if (!id) return;
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("home/lastCase", static_cast<qlonglong>(*id));
    QDialog::accept();
}
