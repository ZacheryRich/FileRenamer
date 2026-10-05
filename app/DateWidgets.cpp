#include "DateWidgets.h"

#include <QCalendarWidget>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "QtHelpers.h"

using namespace finrenamer;
using namespace std::chrono;

namespace {

QDate toQDate(const year_month_day& d)
{
    return QDate(static_cast<int>(d.year()), static_cast<int>(static_cast<unsigned>(d.month())),
                 static_cast<int>(static_cast<unsigned>(d.day())));
}

year_month_day fromQDate(const QDate& d)
{
    return makeDate(d.year(), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

QWidget* row(std::initializer_list<QWidget*> widgets)
{
    auto* w = new QWidget;
    auto* layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    for (auto* child : widgets) layout->addWidget(child);
    layout->addStretch();
    return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// DateField

DateField::DateField(QWidget* parent) : QWidget(parent)
{
    edit_ = new QLineEdit;
    edit_->setPlaceholderText(tr("MM/DD/YYYY"));
    edit_->setMaxLength(12);
    edit_->setFixedWidth(edit_->fontMetrics().horizontalAdvance(QStringLiteral("00/00/00000")) + 16);

    // Calendar in a drop-down menu.
    auto* calendar = new QCalendarWidget;
    calendar->setGridVisible(true);
    auto* menu = new QMenu(this);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(calendar);
    menu->addAction(action);

    calendarButton_ = new QToolButton;
    calendarButton_->setText(QStringLiteral("..."));
    calendarButton_->setToolTip(tr("Pick from a calendar"));
    calendarButton_->setPopupMode(QToolButton::InstantPopup);
    calendarButton_->setMenu(menu);
    calendarButton_->setFocusPolicy(Qt::NoFocus);  // Tab goes field to field

    connect(menu, &QMenu::aboutToShow, this, [this, calendar] {
        const auto d = date();
        const QDate shown = d ? toQDate(*d) : QDate::currentDate();
        calendar->setSelectedDate(shown);
        calendar->setCurrentPage(shown.year(), shown.month());
    });
    connect(calendar, &QCalendarWidget::clicked, this, [this, menu](const QDate& picked) {
        setDate(fromQDate(picked));
        menu->close();
        emit changed();
    });

    connect(edit_, &QLineEdit::textChanged, this, [this] {
        updateStyle();
        emit changed();
    });
    // Tidy the text once the user leaves the box: "13126" stays red, "1/31/26" -> "01/31/2026".
    connect(edit_, &QLineEdit::editingFinished, this, [this] {
        if (const auto d = date()) {
            const QString tidy = ui::qstr(formatUserDate(*d));
            if (edit_->text() != tidy) edit_->setText(tidy);
        }
    });

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(edit_);
    layout->addWidget(calendarButton_);
    setFocusProxy(edit_);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);  // box and button stay together
}

DateField::~DateField()
{
    ui::disconnectChildren(this);
}

std::optional<year_month_day> DateField::date() const
{
    return parseUserDate(ui::stdstr(edit_->text()));
}

bool DateField::hasInvalidText() const
{
    return !edit_->text().trimmed().isEmpty() && !date();
}

void DateField::setDate(std::optional<year_month_day> d)
{
    const QSignalBlocker block(edit_);
    edit_->setText(d ? ui::qstr(formatUserDate(*d)) : QString());
    updateStyle();
}

void DateField::focusAndSelect()
{
    edit_->setFocus();
    edit_->selectAll();
}

void DateField::updateStyle()
{
    edit_->setStyleSheet(hasInvalidText() ? QStringLiteral("QLineEdit { border: 1px solid #c42b1c; }")
                                          : QString());
    edit_->setToolTip(hasInvalidText() ? tr("Not a date. Try 1/31/2026 or 013126.") : QString());
}

// ---------------------------------------------------------------------------
// DateSpecEditor

DateSpecEditor::DateSpecEditor(QWidget* parent) : QWidget(parent)
{
    mode_ = new QComboBox;
    mode_->addItems({tr("Single date"), tr("Period"), tr("Quarter")});

    single_ = new DateField;
    periodStart_ = new DateField;
    periodEnd_ = new DateField;

    quarter_ = new QComboBox;
    quarter_->addItems({QString(), QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3"),
                        QStringLiteral("Q4")});
    quarterYear_ = new QLineEdit;
    quarterYear_->setPlaceholderText(tr("YYYY"));
    quarterYear_->setValidator(new QIntValidator(1900, 9999, quarterYear_));
    quarterYear_->setMaxLength(4);
    quarterYear_->setFixedWidth(quarterYear_->fontMetrics().horizontalAdvance(QStringLiteral("00000")) + 16);

    // Names for automated GUI tests.
    mode_->setObjectName("mode");
    single_->setObjectName("singleDate");
    periodStart_->setObjectName("periodStart");
    periodEnd_->setObjectName("periodEnd");
    quarter_->setObjectName("quarter");
    quarterYear_->setObjectName("quarterYear");

    pages_ = new QStackedWidget;
    pages_->addWidget(row({single_}));
    pages_->addWidget(row({periodStart_, new QLabel(tr("to")), periodEnd_}));
    pages_->addWidget(row({quarter_, quarterYear_}));
    // Only as tall as the current page.
    pages_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mode_);
    layout->addWidget(pages_, 1);

    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        pages_->setCurrentIndex(index);
        emit changed();
    });
    for (DateField* f : {single_, periodStart_, periodEnd_})
        connect(f, &DateField::changed, this, &DateSpecEditor::changed);
    connect(quarter_, &QComboBox::currentIndexChanged, this, &DateSpecEditor::changed);
    connect(quarterYear_, &QLineEdit::textChanged, this, &DateSpecEditor::changed);
}

DateSpecEditor::~DateSpecEditor()
{
    ui::disconnectChildren(this);
}

std::optional<DateSpec> DateSpecEditor::value() const
{
    switch (mode_->currentIndex()) {
    case Single:
        if (const auto d = single_->date()) return SingleDate{*d};
        return std::nullopt;
    case PeriodMode: {
        const auto a = periodStart_->date();
        const auto b = periodEnd_->date();
        if (a && b) return Period{*a, *b};
        return std::nullopt;
    }
    case QuarterMode: {
        bool ok = false;
        const int year = quarterYear_->text().toInt(&ok);
        if (!ok || quarterYear_->text().size() != 4 || quarter_->currentIndex() < 1) return std::nullopt;
        return Quarter{year, quarter_->currentIndex()};
    }
    }
    return std::nullopt;
}

bool DateSpecEditor::hasInvalidText() const
{
    switch (mode_->currentIndex()) {
    case Single: return single_->hasInvalidText();
    case PeriodMode: return periodStart_->hasInvalidText() || periodEnd_->hasInvalidText();
    default: return false;
    }
}

void DateSpecEditor::setValue(const std::optional<DateSpec>& spec)
{
    const QSignalBlocker b1(mode_), b2(quarter_), b3(quarterYear_), b4(single_),
        b5(periodStart_), b6(periodEnd_);

    single_->setDate(std::nullopt);
    periodStart_->setDate(std::nullopt);
    periodEnd_->setDate(std::nullopt);
    quarter_->setCurrentIndex(0);
    quarterYear_->clear();

    if (spec) {
        if (const auto* s = std::get_if<SingleDate>(&*spec)) {
            mode_->setCurrentIndex(Single);
            single_->setDate(s->date);
        } else if (const auto* p = std::get_if<Period>(&*spec)) {
            mode_->setCurrentIndex(PeriodMode);
            periodStart_->setDate(p->start);
            periodEnd_->setDate(p->end);
        } else if (const auto* q = std::get_if<Quarter>(&*spec)) {
            mode_->setCurrentIndex(QuarterMode);
            quarter_->setCurrentIndex(q->quarter);
            quarterYear_->setText(QString::number(q->year));
        }
    }
    pages_->setCurrentIndex(mode_->currentIndex());
}

void DateSpecEditor::focusFirstField()
{
    switch (mode_->currentIndex()) {
    case Single: single_->focusAndSelect(); break;
    case PeriodMode: periodStart_->focusAndSelect(); break;
    case QuarterMode: quarter_->setFocus(); break;
    }
}
