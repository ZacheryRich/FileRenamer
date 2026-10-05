#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLineEdit;

// Add/edit a person: full name, plus the (usually shorter) name used in filenames.
// Only collects input; the caller saves.
class PersonDialog : public QDialog {
    Q_OBJECT
public:
    PersonDialog(const QString& title, const QString& fullName, const QString& displayName,
                 QWidget* parent = nullptr);

    QString fullName() const;
    QString displayName() const;  // empty = use the full name

private:
    void updateOk();

    QLineEdit* fullName_ = nullptr;
    QLineEdit* displayName_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
