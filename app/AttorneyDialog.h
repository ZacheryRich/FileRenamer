#pragma once

#include <QDialog>

#include <cstdint>
#include <optional>
#include <vector>

#include "finrenamer/Database.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// "Attorney Directory" (no case needed): law firms -- ours and opposing counsel -- with their
// phone, fax and address, and the attorneys in each firm with name, bar number and email.
// Only the firm name and the attorney's name are required. An attorney who changes firms is
// moved (the same record, not a copy): "Move to Another Firm...", or pick another firm in the
// attorney's edit window. Everything saves at once to the program's database.
class AttorneyDialog : public QDialog {
    Q_OBJECT
public:
    explicit AttorneyDialog(finrenamer::Database& db, QWidget* parent = nullptr);
    ~AttorneyDialog() override;

    // Reloads from the database, keeping the chosen firm/attorney when they still exist.
    void refresh();
    void selectFirm(std::int64_t firmId);
    std::optional<std::int64_t> currentFirmId() const;
    std::optional<std::int64_t> currentAttorneyId() const;

private:
    void populateFirms();
    void populateAttorneys();
    void showFirmDetails();
    void updateButtons();

    void newFirm();
    void editFirm();
    void deleteFirm();
    void moveAllAttorneys();
    void newAttorney();
    void editAttorney();
    void moveAttorney();
    void deleteAttorney();

    // Asks for a firm other than `exceptFirm`; nullopt if cancelled or there is none.
    std::optional<std::int64_t> chooseOtherFirm(std::int64_t exceptFirm, const QString& title, const QString& prompt);
    const finrenamer::Firm* findFirm(std::int64_t id) const;
    bool firmMatches(const finrenamer::Firm& f) const;
    bool attorneyMatches(const finrenamer::Attorney& a) const;

    finrenamer::Database& db_;
    std::vector<finrenamer::Firm> firms_;
    std::vector<finrenamer::Attorney> attorneys_;  // all of them

    QLineEdit* search_ = nullptr;
    QComboBox* show_ = nullptr;
    QTableWidget* firmTable_ = nullptr;
    QTableWidget* attorneyTable_ = nullptr;
    QLabel* firmTitle_ = nullptr;
    QLabel* firmDetails_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* editFirmBtn_ = nullptr;
    QPushButton* deleteFirmBtn_ = nullptr;
    QPushButton* moveAllBtn_ = nullptr;
    QPushButton* newAttorneyBtn_ = nullptr;
    QPushButton* editAttorneyBtn_ = nullptr;
    QPushButton* moveAttorneyBtn_ = nullptr;
    QPushButton* deleteAttorneyBtn_ = nullptr;
    bool populating_ = false;
};
