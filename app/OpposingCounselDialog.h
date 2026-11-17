#pragma once

#include <QDialog>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "finrenamer/Database.h"
#include "finrenamer/VacationCounsel.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Vacation Letters' "Opposing Counsel..." window: picks a case's opposing counsel from the
// Attorney Directory. Any number of opposing firms (Add Firm / Remove Firm, Move Up / Move Down:
// the first one fills OPC / OPC Firm Name / OPC Address, the others OPC 2 Address); for the
// chosen firm, a check box per attorney, with New/Edit buttons for firms and attorneys (saved to
// the directory at once). "Use These" hands the choice back; the case's columns are filled in by
// VacationDialog (opposingCounselText) and saved to the sheet only when the user saves the case.
// "Pro Se (Type by Hand)" (the other party represents themselves) closes the window with result kProSe and
// changes nothing: the user types the party's details into the case's boxes by hand.
class OpposingCounselDialog : public QDialog {
    Q_OBJECT
public:
    enum { kProSe = 2 };  // exec() result of the Pro Se button

    OpposingCounselDialog(finrenamer::Database& db, const QString& caseTitle,
                          const std::vector<finrenamer::OpposingFirmChoice>& initial,
                          const std::vector<std::string>& unmatched, QWidget* parent = nullptr);
    ~OpposingCounselDialog() override;

    std::vector<finrenamer::OpposingFirmChoice> choices() const;

    // Also used by tests.
    void addFirmEntry(std::int64_t firmId = 0);   // appends and selects it
    void setEntryFirm(std::int64_t firmId);       // the selected entry
    void setAttorneyChecked(std::int64_t attorneyId, bool on);  // in the selected entry
    int entryCount() const { return static_cast<int>(entries_.size()); }

private:
    struct Entry {
        std::int64_t firmId = 0;  // 0 = not chosen yet
        std::set<std::int64_t> checked;
    };

    void reloadDirectory();  // re-reads firms/attorneys; drops ticks that no longer apply
    void populateEntries();
    void showEntry();        // the firm combo, details and attorney list of the selected entry
    void updatePreview();
    void updateButtons();
    int currentEntry() const;
    const finrenamer::Firm* findFirm(std::int64_t id) const;
    const finrenamer::Attorney* findAttorney(std::int64_t id) const;

    void removeEntry();
    void moveEntry(int delta);
    void newFirm();
    void editFirm();
    void newAttorney();
    void editAttorney();

    finrenamer::Database& db_;
    std::vector<finrenamer::Firm> firms_;
    std::vector<finrenamer::Attorney> attorneys_;
    std::vector<Entry> entries_;

    QListWidget* entryList_ = nullptr;
    QPushButton* removeEntryBtn_ = nullptr;
    QPushButton* upBtn_ = nullptr;
    QPushButton* downBtn_ = nullptr;
    QComboBox* firmCombo_ = nullptr;
    QPushButton* editFirmBtn_ = nullptr;
    QLabel* firmDetails_ = nullptr;
    QListWidget* attorneyList_ = nullptr;
    QPushButton* newAttorneyBtn_ = nullptr;
    QPushButton* editAttorneyBtn_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QPushButton* useBtn_ = nullptr;
    bool populating_ = false;
};
