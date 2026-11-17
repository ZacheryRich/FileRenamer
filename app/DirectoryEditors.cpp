#include "DirectoryEditors.h"

namespace ui {

std::optional<Firm> createFirmInteractive(finrenamer::Database& db, QWidget* parent, bool ours)
{
    Firm blank;
    blank.ours = ours;
    FirmEditDialog dlg(blank, parent);
    while (dlg.exec() == QDialog::Accepted) {
        Firm f = dlg.result();
        if (runGuarded(parent, [&] { f.id = db.createFirm(f); })) return f;
    }
    return std::nullopt;
}

std::optional<Firm> editFirmInteractive(finrenamer::Database& db, QWidget* parent, const Firm& firm)
{
    FirmEditDialog dlg(firm, parent);
    while (dlg.exec() == QDialog::Accepted) {
        const Firm f = dlg.result();
        if (runGuarded(parent, [&] { db.updateFirm(f); })) return f;
    }
    return std::nullopt;
}

std::optional<Attorney> createAttorneyInteractive(finrenamer::Database& db, QWidget* parent, std::int64_t firmId)
{
    std::vector<Firm> firms;
    if (!runGuarded(parent, [&] { firms = db.listFirms(); })) return std::nullopt;
    Attorney blank;
    blank.firmId = firmId;
    AttorneyEditDialog dlg(blank, firms, parent);
    while (dlg.exec() == QDialog::Accepted) {
        Attorney a = dlg.result();
        if (runGuarded(parent, [&] { a.id = db.createAttorney(a); })) return a;
    }
    return std::nullopt;
}

std::optional<Attorney> editAttorneyInteractive(finrenamer::Database& db, QWidget* parent, const Attorney& attorney)
{
    std::vector<Firm> firms;
    if (!runGuarded(parent, [&] { firms = db.listFirms(); })) return std::nullopt;
    AttorneyEditDialog dlg(attorney, firms, parent);
    while (dlg.exec() == QDialog::Accepted) {
        const Attorney a = dlg.result();
        if (runGuarded(parent, [&] { db.updateAttorney(a); })) return a;
    }
    return std::nullopt;
}

}  // namespace ui
