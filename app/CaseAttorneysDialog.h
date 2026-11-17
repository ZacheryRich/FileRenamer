#pragma once

#include <QDialog>

#include <cstdint>
#include <set>
#include <vector>

#include "finrenamer/Database.h"

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Assign Attorneys" for a case: every firm in the Attorney Directory (our firm first, then the
// opposing firms by name), each with a check box per attorney. New Firm... / New Attorney... /
// Edit... save to the directory at once (a new attorney is ticked). OK hands back the ticked
// attorneys; the caller saves them (Database::setCaseAttorneys).
class CaseAttorneysDialog : public QDialog {
    Q_OBJECT
public:
    CaseAttorneysDialog(finrenamer::Database& db, const QString& caseName, const std::set<std::int64_t>& assigned,
                        QWidget* parent = nullptr);
    ~CaseAttorneysDialog() override;

    std::vector<std::int64_t> chosen() const;   // the ticked attorneys, in directory order
    void setChecked(std::int64_t attorneyId, bool on);  // also used by tests

private:
    void reload();          // re-reads the directory, keeping the ticks
    void applyFilter();
    void updateButtons();
    std::int64_t selectedFirmId() const;      // the firm of the selected row (firm or attorney), or 0
    std::int64_t selectedAttorneyId() const;  // 0 if a firm row is selected
    void newFirm();
    void newAttorney();
    void editSelected();

    finrenamer::Database& db_;
    std::vector<finrenamer::Firm> firms_;
    std::vector<finrenamer::Attorney> attorneys_;
    std::set<std::int64_t> checked_;

    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPushButton* newAttorneyBtn_ = nullptr;
    QPushButton* editBtn_ = nullptr;
    bool populating_ = false;
};
