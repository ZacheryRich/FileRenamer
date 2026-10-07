#pragma once

#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <vector>

class QListWidget;
class QPushButton;
class QVBoxLayout;

// The folders to search for one case's statements: Add Folder..., Remove, and
// drag-and-drop from File Explorer. Remembered per case in settings.ini, and
// shared by Update File Names and the Deficiency List.
class CaseFolderList : public QWidget {
    Q_OBJECT
public:
    explicit CaseFolderList(std::int64_t caseId, QWidget* parent = nullptr);
    ~CaseFolderList() override;

    QStringList folders() const;  // with forward slashes
    std::vector<std::filesystem::path> paths() const;
    bool hasFolders() const;

    // Shows the folder picker and adds the chosen folder. False if cancelled.
    bool promptForFolder();
    void addFolder(const QString& folder);  // ignores one already listed
    // A listed folder was renamed: follow it.
    void replaceFolder(const std::filesystem::path& from, const std::filesystem::path& to);

    // Puts a widget (e.g. an option check box) under the buttons.
    void addSideWidget(QWidget* widget);

signals:
    void changed();  // folders added, removed or replaced

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void removeSelected();
    void save() const;

    std::int64_t caseId_;
    QListWidget* list_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QVBoxLayout* side_ = nullptr;
};
