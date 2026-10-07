#pragma once

#include <QDialog>

#include <cstdint>
#include <optional>

#include "finrenamer/Database.h"

class QLineEdit;
class QListWidget;
class QPushButton;

// "Choose a case": a filterable list of cases, for the home screen's File Renamer
// and Deficiency List. Starts on the case used last time.
class CasePickerDialog : public QDialog {
    Q_OBJECT
public:
    CasePickerDialog(finrenamer::Database& db, const QString& purpose, QWidget* parent = nullptr);
    ~CasePickerDialog() override;

    std::optional<std::int64_t> selectedCaseId() const;
    void selectCase(std::int64_t caseId);

    // Remembered between runs (settings.ini, "home/lastCase").
    static std::optional<std::int64_t> lastCase();

public slots:
    void accept() override;

private:
    void filter(const QString& text);
    void updateButtons();

    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* ok_ = nullptr;
};
