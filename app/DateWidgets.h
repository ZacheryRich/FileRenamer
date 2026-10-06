#pragma once

#include <QWidget>

#include <chrono>
#include <optional>

#include "finrenamer/DateSpec.h"

class QComboBox;
class QLineEdit;
class QStackedWidget;
class QToolButton;

// A date typed as separate Year, Month and Day boxes (in filename order), with a
// calendar button. Typing moves on by itself once a box is complete ("2026" ->
// Month, "1/" or "01" -> Day); Backspace in an empty box goes back. Pasting a
// whole date (1/31/2026, 013126, 2026-01-31) into any box fills all three.
// Blank is allowed.
class DateField : public QWidget {
    Q_OBJECT
public:
    explicit DateField(QWidget* parent = nullptr);
    ~DateField() override;

    std::optional<std::chrono::year_month_day> date() const;
    bool hasInvalidText() const;  // a month/day out of range, or a day the month doesn't have
    QString problem() const;      // why, for the status line ("" when fine)
    void setDate(std::optional<std::chrono::year_month_day> date);
    void focusAndSelect();        // the Month box once a year is filled, else Year

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Part { Year = 0, Month = 1, Day = 2 };
    void edited(Part part, const QString& text);
    void tidy(Part part);
    void focusPart(Part part);
    void updateStyle();
    int value(Part part) const;  // -1 when blank

    QLineEdit* parts_[3] = {};
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
    QString problem() const;  // why the typed date isn't valid ("" when fine)

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
