#include "HistoryDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

#include "QtHelpers.h"
#include "finrenamer/RenameEngine.h"

using namespace finrenamer;
using namespace ui;

HistoryDialog::HistoryDialog(Database& db, QWidget* parent) : QDialog(parent), db_(db)
{
    setWindowTitle(tr("Rename History"));

    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({tr("When"), tr("Case"), tr("Folder"), tr("Files"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto* hint = new QLabel(tr("Undo puts every file from that Apply back to its original name and "
                               "folder. Files that have been moved or renamed since are left alone "
                               "and listed."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");

    undoBtn_ = new QPushButton(tr("Undo Selected"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(undoBtn_, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(undoBtn_, &QPushButton::clicked, this, &HistoryDialog::undoSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &HistoryDialog::updateButtons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_, 1);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    resize(900, 460);
    reload();
}

HistoryDialog::~HistoryDialog()
{
    disconnectChildren(this);
}

void HistoryDialog::reload()
{
    std::map<std::int64_t, QString> caseNames;
    runGuarded(this, [&] {
        batches_ = db_.listBatches(500);
        for (const auto& c : db_.listCases()) caseNames[c.id] = qstr(c.clientName);
    });

    table_->setRowCount(static_cast<int>(batches_.size()));
    for (int row = 0; row < static_cast<int>(batches_.size()); ++row) {
        const BatchSummary& b = batches_[static_cast<std::size_t>(row)];
        const QDateTime when = QDateTime::fromString(qstr(b.createdAt), Qt::ISODate).toLocalTime();
        const QString caseName = b.caseId && caseNames.count(*b.caseId) ? caseNames[*b.caseId]
                                                                        : tr("(deleted case)");
        const QStringList cells{when.toString(QStringLiteral("MMM d, yyyy  h:mm AP")), caseName,
                                QDir::toNativeSeparators(qpath(b.root)),
                                QString::number(b.moveCount), b.undone ? tr("Undone") : QString()};
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            if (b.undone) item->setForeground(QColor(0x70, 0x70, 0x70));
            table_->setItem(row, col, item);
        }
    }
    updateButtons();
}

void HistoryDialog::updateButtons()
{
    const int row = table_->currentRow();
    const bool canUndo = row >= 0 && row < static_cast<int>(batches_.size()) &&
                         table_->selectionModel()->hasSelection() &&
                         !batches_[static_cast<std::size_t>(row)].undone;
    undoBtn_->setEnabled(canUndo);
}

void HistoryDialog::undoSelected()
{
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(batches_.size())) return;
    const BatchSummary summary = batches_[static_cast<std::size_t>(row)];

    if (QMessageBox::question(this, tr("Undo"),
                              tr("Put the %1 file(s) from this Apply back to their original names?")
                                  .arg(summary.moveCount),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    std::optional<BatchRecord> record;
    if (!runGuarded(this, [&] { record = db_.loadBatch(summary.batchId); }) || !record) return;

    const UndoResult result = undo(*record);
    runGuarded(this, [&] { db_.markBatchUndone(summary.batchId); });
    reload();

    if (result.failures.empty()) {
        QMessageBox::information(this, tr("Undo"), tr("%1 file(s) restored.").arg(result.restored));
    } else {
        QStringList lines;
        for (const auto& f : result.failures)
            lines << QStringLiteral("• %1: %2").arg(QDir::toNativeSeparators(qpath(f.path)),
                                                          qstr(f.reason));
        QMessageBox::warning(this, tr("Undo"),
                             tr("%1 file(s) restored. These were left alone:\n\n%2")
                                 .arg(result.restored)
                                 .arg(lines.join('\n')));
    }
}
