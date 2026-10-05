#pragma once

#include <QWidget>

#include <chrono>
#include <optional>

#include "finrenamer/DateSpec.h"

class QComboBox;
class QLineEdit;
class QStackedWidget;
class QToolButton;

// A typed date (1/31/2026, 013126, ...) with a calendar button. Blank is allowed.
class DateField : public QWidget {
    Q_OBJECT
public:
    explicit DateField(QWidget* parent = nullptr);
    ~DateField() override;

    std::optional<std::chrono::year_month_day> date() const;
    bool hasInvalidText() const;  // something typed that isn't a date
    void setDate(std::optional<std::chrono::year_month_day> date);
    void focusAndSelect();

signals:
    void changed();

private:
    void updateStyle();

    QLineEdit* edit_ = nullptr;
    QToolButton* calendarButton_ = nullptr;
};

// Mode (single date / period / quarter) plus the matching inputs.
class DateSpecEditor : public QWidget {
    Q_OBJECT
public:
    enum Mode { Single = 0, PeriodMode = 1, QuarterMode = 2 };

    explicit DateSpecEditor(QWidget* parent = nullptr);
    ~DateSpecEditor() override;

    // Empty until every input for the current mode is filled in correctly.
    std::optional<finrenamer::DateSpec> value() const;
    bool hasInvalidText() const;

    // Shows `spec` (switching mode to match). nullopt clears the inputs but
    // keeps the current mode, so a run of quarterly statements stays quarterly.
    void setValue(const std::optional<finrenamer::DateSpec>& spec);

    void focusFirstField();

signals:
    void changed();

private:
    QComboBox* mode_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    DateField* single_ = nullptr;
    DateField* periodStart_ = nullptr;
    DateField* periodEnd_ = nullptr;
    QComboBox* quarter_ = nullptr;
    QLineEdit* quarterYear_ = nullptr;
};
