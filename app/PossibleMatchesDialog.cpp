#include "PossibleMatchesDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace ui;

PossibleMatchesDialog::PossibleMatchesDialog(std::vector<Row> rows, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Possible Matches"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);

    auto* intro = new QLabel(tr(
        "These files show a date, an account's last four digits and its institution or type, but "
        "their names also have other words, so they may not be statements (an email or letter about "
        "the account, say). Tick the ones that are statements; only ticked files are counted. "
        "Months found only through such files are starred in the report."));
    intro->setWordWrap(true);

    table_ = new QTableWidget(static_cast<int>(rows.size()), 4);
    table_->setObjectName("possibleFiles");
    table_->setHorizontalHeaderLabels({tr("File"), tr("Folder"), tr("Would count as"), tr("Other words in the name")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const Row& r = rows[static_cast<std::size_t>(i)];
        auto* file = new QTableWidgetItem(r.file);
        file->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        file->setCheckState(r.included ? Qt::Checked : Qt::Unchecked);
        table_->setItem(i, 0, file);
        table_->setItem(i, 1, new QTableWidgetItem(r.folder));
        table_->setItem(i, 2, new QTableWidgetItem(r.countsAs));
        table_->setItem(i, 3, new QTableWidgetItem(r.words));
    }
    table_->resizeColumnsToContents();
    table_->setColumnWidth(0, qMin(table_->columnWidth(0), 420));
    table_->setColumnWidth(1, qMin(table_->columnWidth(1), 260));

    auto* checkAll = new QPushButton(tr("Check All"));
    checkAll->setObjectName("checkAll");
    auto* uncheckAll = new QPushButton(tr("Uncheck All"));
    uncheckAll->setObjectName("uncheckAll");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->addButton(checkAll, QDialogButtonBox::ActionRole);
    buttons->addButton(uncheckAll, QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(table_, 1);
    layout->addWidget(buttons);
    for (auto* b : findChildren<QPushButton*>()) b->setAutoDefault(false);

    connect(checkAll, &QPushButton::clicked, this, [this] { setAll(true); });
    connect(uncheckAll, &QPushButton::clicked, this, [this] { setAll(false); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(1000, 480);
}

PossibleMatchesDialog::~PossibleMatchesDialog()
{
    disconnectChildren(this);
}

std::vector<bool> PossibleMatchesDialog::included() const
{
    std::vector<bool> out;
    for (int i = 0; i < table_->rowCount(); ++i) out.push_back(table_->item(i, 0)->checkState() == Qt::Checked);
    return out;
}

void PossibleMatchesDialog::setAll(bool on)
{
    for (int i = 0; i < table_->rowCount(); ++i) table_->item(i, 0)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
}
