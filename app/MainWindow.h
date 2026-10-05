#pragma once

#include <QMainWindow>

#include <cstdint>
#include <optional>

#include "finrenamer/Database.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

// Case / people / account management. The rename screen opens from here.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(finrenamer::Database& db, const QString& dataFolder, QWidget* parent = nullptr);

    void selectCase(std::int64_t caseId);

private:
    void buildUi();
    void buildMenus();

    void reloadCases(std::optional<std::int64_t> select = std::nullopt);
    void filterCases(const QString& text);
    void showSelectedCase();
    void reloadPeople();
    void reloadAccounts(std::optional<std::int64_t> select = std::nullopt);
    void updateButtons();

    std::optional<std::int64_t> currentCaseId() const;
    std::optional<std::int64_t> currentPersonId() const;
    std::optional<std::int64_t> currentAccountId() const;

    void newCase();
    void editCase();
    void deleteCase();
    void addPerson();
    void editPerson();
    void deletePerson();
    void addAccount();
    void editAccount();
    void deleteAccount();

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
    QPushButton* renameFilesBtn_ = nullptr;

    QTableWidget* peopleTable_ = nullptr;
    QPushButton* editPersonBtn_ = nullptr;
    QPushButton* deletePersonBtn_ = nullptr;

    QTableWidget* accountTable_ = nullptr;
    QPushButton* editAccountBtn_ = nullptr;
    QPushButton* deleteAccountBtn_ = nullptr;
};
