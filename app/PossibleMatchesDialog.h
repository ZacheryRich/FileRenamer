#pragma once

#include <QDialog>

#include <vector>

class QPushButton;
class QTableWidget;

// "Possible Matches": files whose names show a date, an account number and the institution
// or type, but also other words ("Email from Chase to John re acct 1234"). The user ticks
// the ones that really are statements; only ticked files are counted by the Deficiency List.
class PossibleMatchesDialog : public QDialog {
    Q_OBJECT
public:
    struct Row {
        QString file;      // the file name
        QString folder;
        QString countsAs;  // "Chase Checking x1234 (H) - May 2023"
        QString words;     // the words that made it doubtful
        bool included = false;
    };

    explicit PossibleMatchesDialog(std::vector<Row> rows, QWidget* parent = nullptr);
    ~PossibleMatchesDialog() override;

    std::vector<bool> included() const;  // one per row, in order
    void setAll(bool on);                // also used by tests

private:
    QTableWidget* table_ = nullptr;
};
