#include "DateWidgets.h"

#include <QCalendarWidget>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLocale>
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

namespace {

constexpr int kMaxDigits[3] = {4, 2, 2};

bool isSeparator(QChar c)
{
    return c == u'/' || c == u'.' || c == u'-' || c == u' ' || c == u',';
}

// "20260131" (the filename order) -- parseUserDate reads 8 digits as MMDDYYYY.
std::optional<year_month_day> parseYmdDigits(const QString& digits)
{
    if (digits.size() != 8) return std::nullopt;
    const year_month_day d = makeDate(digits.left(4).toInt(), digits.mid(4, 2).toUInt(), digits.mid(6, 2).toUInt());
    if (!d.ok() || static_cast<int>(d.year()) < 1900) return std::nullopt;
    return d;
}

}  // namespace

DateField::DateField(QWidget* parent) : QWidget(parent)
{
    const char* names[3] = {"year", "month", "day"};
    const QString placeholders[3] = {tr("YYYY"), tr("MM"), tr("DD")};
    const QString tips[3] = {tr("Year (4 digits; 26 becomes 2026)"), tr("Month (1-12)"), tr("Day")};
    for (int i = 0; i < 3; ++i) {
        auto* e = new QLineEdit;
        e->setObjectName(QString::fromLatin1(names[i]));
        e->setPlaceholderText(placeholders[i]);
        e->setToolTip(tips[i]);
        e->setProperty("baseTip", tips[i]);
        e->setAlignment(Qt::AlignCenter);
        e->setMaxLength(16);  // room to paste a whole date
        e->setFixedWidth(e->fontMetrics().horizontalAdvance(i == Year ? QStringLiteral("YYYYY")
                                                                     : QStringLiteral("MMM")) + 14);
        e->installEventFilter(this);
        parts_[i] = e;
    }

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
    calendarButton_->setFocusPolicy(Qt::NoFocus);  // Tab goes box to box

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

    for (int i = 0; i < 3; ++i) {
        const Part part = static_cast<Part>(i);
        connect(parts_[i], &QLineEdit::textEdited, this, [this, part](const QString& t) { edited(part, t); });
        connect(parts_[i], &QLineEdit::textChanged, this, [this] {
            updateStyle();
            emit changed();
        });
        connect(parts_[i], &QLineEdit::editingFinished, this, [this, part] { tidy(part); });
    }

    auto separator = [] {
        auto* l = new QLabel(QStringLiteral("."));
        l->setContentsMargins(0, 0, 0, 0);
        return l;
    };
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(parts_[Year]);
    layout->addWidget(separator());
    layout->addWidget(parts_[Month]);
    layout->addWidget(separator());
    layout->addWidget(parts_[Day]);
    layout->addSpacing(2);
    layout->addWidget(calendarButton_);
    setFocusProxy(parts_[Year]);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);  // boxes and button stay together
}

DateField::~DateField()
{
    ui::disconnectChildren(this);
}

int DateField::value(Part part) const
{
    bool ok = false;
    const int v = parts_[part]->text().trimmed().toInt(&ok);
    return ok ? v : -1;
}

std::optional<year_month_day> DateField::date() const
{
    const QString y = parts_[Year]->text().trimmed();
    int year = value(Year), month = value(Month), day = value(Day);
    if (year < 0 || month < 0 || day < 0) return std::nullopt;
    if (y.size() == 2) year += 2000;  // "26" before the box is tidied
    else if (y.size() != 4 || year < 1900) return std::nullopt;
    if (month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
    const year_month_day d = makeDate(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    if (!d.ok()) return std::nullopt;
    return d;
}

QString DateField::problem() const
{
    const QString y = parts_[Year]->text().trimmed();
    const int month = value(Month), day = value(Day);
    if (!parts_[Year]->hasFocus() && !y.isEmpty() && y.size() != 2 && y.size() != 4)
        return tr("Type the year with 4 digits.");
    if (y.size() == 4 && value(Year) < 1900) return tr("That year is too early.");
    if (!parts_[Month]->text().trimmed().isEmpty() && (month > 12 || (month == 0 && parts_[Month]->text().size() == 2)))
        return tr("The month must be 1 to 12.");
    if (!parts_[Day]->text().trimmed().isEmpty() && (day > 31 || (day == 0 && parts_[Day]->text().size() == 2)))
        return tr("The day must be 1 to 31.");
    if (month >= 1 && month <= 12 && day >= 1 && day <= 31 && !date() && value(Year) >= 0 &&
        (y.size() == 2 || y.size() == 4)) {
        const int year = y.size() == 2 ? value(Year) + 2000 : value(Year);
        return tr("%1 %2 has no day %3.").arg(QLocale(QLocale::English).monthName(month)).arg(year).arg(day);
    }
    return {};
}

bool DateField::isBlank() const
{
    for (const QLineEdit* part : parts_)
        if (!part->text().trimmed().isEmpty()) return false;
    return true;
}

bool DateField::hasInvalidText() const
{
    return !problem().isEmpty();
}

void DateField::setDate(std::optional<year_month_day> d)
{
    const QSignalBlocker b0(parts_[Year]), b1(parts_[Month]), b2(parts_[Day]);
    if (d) {
        parts_[Year]->setText(QString::number(static_cast<int>(d->year())));
        parts_[Month]->setText(QStringLiteral("%1").arg(static_cast<unsigned>(d->month()), 2, 10, QLatin1Char('0')));
        parts_[Day]->setText(QStringLiteral("%1").arg(static_cast<unsigned>(d->day()), 2, 10, QLatin1Char('0')));
    } else {
        for (auto* e : parts_) e->clear();
    }
    updateStyle();
}

void DateField::focusAndSelect()
{
    focusPart(parts_[Year]->text().trimmed().size() == 4 ? Month : Year);
}

void DateField::focusPart(Part part)
{
    parts_[part]->setFocus();
    parts_[part]->selectAll();
}

// Called for the user's own typing and pasting only (not setText).
void DateField::edited(Part part, const QString& text)
{
    QString digits;
    bool trailingSeparator = false, other = false;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isDigit()) digits += text[i];
        else if (isSeparator(text[i]) && i == text.size() - 1) trailingSeparator = true;
        else other = true;
    }

    // A whole date typed or pasted into one box: fill all three.
    if (other || digits.size() > kMaxDigits[part]) {
        std::optional<year_month_day> whole = parseUserDate(ui::stdstr(text));
        if (!whole) whole = parseYmdDigits(digits);
        if (whole) {
            setDate(*whole);
            emit changed();
            parts_[Day]->setFocus();
            parts_[Day]->end(false);
            return;
        }
    }

    QLineEdit* e = parts_[part];
    const QString kept = digits.left(kMaxDigits[part]);
    if (kept != text) {
        const QSignalBlocker block(e);  // one changed() below, not two
        e->setText(kept);
    }
    if (kept != text) {
        updateStyle();
        emit changed();
    }

    // Move on once the box is complete, or the user typed a separator.
    const int n = static_cast<int>(kept.size());
    bool advance = false;
    switch (part) {
    case Year: advance = n == 4 || (trailingSeparator && n == 2); break;
    case Month: advance = n == 2 || (n == 1 && kept[0] >= u'2') || (trailingSeparator && n == 1); break;
    case Day: break;
    }
    if (advance) {
        tidy(part);
        focusPart(static_cast<Part>(part + 1));
    }
}

// "26" -> "2026", "1" -> "01" once the user leaves a box.
void DateField::tidy(Part part)
{
    QLineEdit* e = parts_[part];
    const QString t = e->text().trimmed();
    bool ok = false;
    const int v = t.toInt(&ok);
    if (!ok) {
        updateStyle();
        return;
    }
    QString tidied = t;
    if (part == Year && t.size() == 2) tidied = QString::number(2000 + v);
    else if (part != Year && t.size() == 1 && v >= 1) tidied = QStringLiteral("0") + t;
    if (tidied != e->text()) e->setText(tidied);  // emits changed()
    else updateStyle();
}

// Backspace in an empty box, or the arrow keys at its edge, move between boxes.
bool DateField::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        int part = -1;
        for (int i = 0; i < 3; ++i)
            if (watched == parts_[i]) part = i;
        if (part >= 0) {
            QLineEdit* e = parts_[part];
            const auto* key = static_cast<QKeyEvent*>(event);
            const bool atStart = e->cursorPosition() == 0 && !e->hasSelectedText();
            const bool atEnd = e->cursorPosition() == e->text().size() && !e->hasSelectedText();
            if (part > 0 && ((key->key() == Qt::Key_Backspace && e->text().isEmpty()) ||
                             (key->key() == Qt::Key_Left && atStart))) {
                parts_[part - 1]->setFocus();
                parts_[part - 1]->end(false);
                return true;
            }
            if (part < 2 && key->key() == Qt::Key_Right && atEnd) {
                parts_[part + 1]->setFocus();
                parts_[part + 1]->home(false);
                return true;
            }
        }
    } else if (event->type() == QEvent::FocusOut) {
        updateStyle();  // a short year is only flagged once the user leaves it
    }
    return QWidget::eventFilter(watched, event);
}

void DateField::updateStyle()
{
    const QString why = problem();
    const QString red = QStringLiteral("QLineEdit { border: 1px solid #c42b1c; }");
    for (auto* e : parts_) {
        e->setStyleSheet(why.isEmpty() ? QString() : red);
        e->setToolTip(why.isEmpty() ? e->property("baseTip").toString() : why);
    }
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

QString DateSpecEditor::problem() const
{
    switch (mode_->currentIndex()) {
    case Single: return single_->problem();
    case PeriodMode: {
        const QString a = periodStart_->problem();
        return a.isEmpty() ? periodEnd_->problem() : a;
    }
    default: return {};
    }
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
