#pragma once

#include <QDialog>

#include <string>
#include <utility>
#include <vector>

#include "finrenamer/Models.h"

class QLineEdit;
class QPlainTextEdit;

// New/edit dialog for a client case: name, type, cause number, county, court, marriage and
// insurance details, notes. Every field is optional (user's rule: a case can be saved with
// whatever is known). Only collects input; the caller saves. Parties, children, other people and
// attorneys are edited on the Case List.
class CaseDialog : public QDialog {
    Q_OBJECT
public:
    explicit CaseDialog(const finrenamer::ClientCase& initial, QWidget* parent = nullptr);

    finrenamer::ClientCase result() const;

private:
    finrenamer::ClientCase initial_;
    std::vector<std::pair<QLineEdit*, std::string finrenamer::ClientCase::*>> edits_;
    QPlainTextEdit* notes_ = nullptr;
};
