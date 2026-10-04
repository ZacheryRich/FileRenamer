#pragma once

#include <QDialog>

#include "finrenamer/Models.h"

class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;

// New/edit dialog for a client case. Only collects input; the caller saves.
class CaseDialog : public QDialog {
    Q_OBJECT
public:
    explicit CaseDialog(const finrenamer::ClientCase& initial, QWidget* parent = nullptr);

    finrenamer::ClientCase result() const;

private:
    void updateOk();

    finrenamer::ClientCase initial_;
    QLineEdit* name_ = nullptr;
    QPlainTextEdit* notes_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
