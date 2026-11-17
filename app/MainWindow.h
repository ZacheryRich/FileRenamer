#pragma once

#include <QMainWindow>

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include "finrenamer/Database.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;

// Case management: court details, parties, attorneys on the case, people (account owners) and
// accounts. The rename screen opens from here.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(finrenamer::Database& db, const QString& dataFolder, QWidget* parent = nullptr);
    ~MainWindow() override;

    void selectCase(std::int64_t caseId);

    // Re-reads people and accounts after another window (the File Renamer) may have changed them.
    void refreshAfterTool();

protected:
    // Coming back to the window re-reads the case's attorneys (the directory may have changed).
    bool event(QEvent* e) override;

signals:
    // The File Renamer and Deficiency List are single windows owned by the start screen.
    void fileRenamerRequested(std::int64_t caseId);
    void deficiencyListRequested(std::int64_t caseId);

private:
    void buildUi();
    void buildMenus();

    void reloadCases(std::optional<std::int64_t> select = std::nullopt);
    void filterCases(const QString& text);
    void showSelectedCase();
    void reloadPeople();
    void reloadParties();
    void reloadContacts();
    void reloadChildren();
    void reloadCaseAttorneys();
    void reloadAccounts(std::optional<std::int64_t> select = std::nullopt);
    void updateButtons();

    std::optional<std::int64_t> currentCaseId() const;
    std::optional<std::int64_t> currentPersonId() const;
    std::optional<std::int64_t> currentPartyId() const;
    std::optional<std::int64_t> currentCaseAttorneyId() const;
    std::optional<std::int64_t> currentAccountId() const;

    void newCase();
    void importForm(bool intoCurrentCase);  // New from Form / Fill from Form
    void editCase();
    void deleteCase();
    void addContact();
    void editContact();
    void deleteContact();
    void addChild();
    void editChild();
    void deleteChild();
    void createDocument();
    void addParty();
    void editParty();
    void deleteParty();
    void assignAttorneys();
    void editCaseAttorney();
    void removeCaseAttorney();
    void addPerson();
    void editPerson();
    void deletePerson();
    void addAccount();
    void editAccount();
    void deleteAccount();
    void renameFiles();
    void updateFileNames();
    void deficiencyList();
    std::set<std::string> currentNames();
    void offerNameUpdate(const std::set<std::string>& namesBefore);

    finrenamer::Database& db_;
    QString dataFolder_;

    // Left: cases
    QLineEdit* caseFilter_ = nullptr;
    QListWidget* caseList_ = nullptr;
    QPushButton* editCaseBtn_ = nullptr;
    QPushButton* deleteCaseBtn_ = nullptr;

    // Right: details of the selected case
    QStackedWidget* detailStack_ = nullptr;
    QLabel* caseTitle_ = nullptr;
    QLabel* caseNotes_ = nullptr;
    QLabel* caseCourt_ = nullptr;   // "Cause No. ... | ... County | court"
    QPushButton* renameFilesBtn_ = nullptr;
    QPushButton* updateNamesBtn_ = nullptr;
    QPushButton* deficiencyBtn_ = nullptr;

    QTabWidget* caseTabs_ = nullptr;      // Case Information | Accounts
    QPushButton* createDocBtn_ = nullptr;
    QPushButton* fillFromFormBtn_ = nullptr;
    QTableWidget* contactTable_ = nullptr;
    QPushButton* editContactBtn_ = nullptr;
    QPushButton* deleteContactBtn_ = nullptr;
    QTableWidget* childTable_ = nullptr;
    QPushButton* editChildBtn_ = nullptr;
    QPushButton* deleteChildBtn_ = nullptr;

    QTableWidget* partyTable_ = nullptr;
    QPushButton* editPartyBtn_ = nullptr;
    QPushButton* deletePartyBtn_ = nullptr;

    QTableWidget* caseAttorneyTable_ = nullptr;
    QPushButton* editCaseAttorneyBtn_ = nullptr;
    QPushButton* removeCaseAttorneyBtn_ = nullptr;

    QTableWidget* peopleTable_ = nullptr;
    QPushButton* editPersonBtn_ = nullptr;
    QPushButton* deletePersonBtn_ = nullptr;

    QTableWidget* accountTable_ = nullptr;
    QPushButton* editAccountBtn_ = nullptr;
    QPushButton* deleteAccountBtn_ = nullptr;
};
