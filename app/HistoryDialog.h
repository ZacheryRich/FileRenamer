#pragma once

#include <QDialog>

#include <vector>

#include "finrenamer/Database.h"

class QPushButton;
class QTableWidget;

// File > Rename History: every Apply, newest first, with Undo.
class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryDialog(finrenamer::Database& db, QWidget* parent = nullptr);
    ~HistoryDialog() override;

private:
    void reload();
    void updateButtons();
    void undoSelected();

    finrenamer::Database& db_;
    std::vector<finrenamer::BatchSummary> batches_;
    QTableWidget* table_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
};
