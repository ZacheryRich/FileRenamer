#pragma once

#include <QDialog>
#include <QEvent>
#include <QObject>

namespace ui {

// The tools are separate windows, so a popup opened from one (a message box, an editor, a
// folder picker; most are shown with exec()) must block only its own window, not every tool.
// exec() makes dialogs application-modal; just before such a dialog first appears this makes
// it window-modal instead. A dialog with no parent window stays application-modal.
class WindowModalFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish) {
            if (auto* dialog = qobject_cast<QDialog*>(watched)) {
                if (dialog->parentWidget() && dialog->windowModality() == Qt::ApplicationModal)
                    dialog->setWindowModality(Qt::WindowModal);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace ui
