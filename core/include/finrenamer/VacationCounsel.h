#pragma once

#include <string>
#include <vector>

#include "finrenamer/Models.h"

namespace finrenamer {

// Opposing counsel on the Vacation Letters case list, chosen from the Attorney Directory.
//
// The shared sheet keeps plain text in its own columns (others use it without this program and
// the letter template reads those columns), so a choice of firms and attorneys is written as:
//   OPC            the first firm's chosen attorneys, one per line
//   OPC Firm Name  the first firm's name
//   OPC Address    the first firm's address
//   OPC 2 Address  every further firm as a block -- its attorneys (one per line), the firm name,
//                  the address -- with a blank line between blocks
//   eService Email every chosen attorney's email (all firms; repeats dropped), joined with "; "


struct OpposingFirmChoice {
    Firm firm;
    std::vector<Attorney> attorneys;  // the ticked ones, in the order they are to be listed
};

struct OpposingCounselText {
    std::string names;      // OPC
    std::string firmName;   // OPC Firm Name
    std::string address;    // OPC Address
    std::string others;     // OPC 2 Address
    std::string emails;     // eService Email ("" if no chosen attorney has an email)
};

OpposingCounselText opposingCounselText(const std::vector<OpposingFirmChoice>& choices);

// Reads a case's current text back into choices, so the window opens with them ticked.
// Only exact matches count (spaces at the ends and letter case ignored): the firm by its name,
// attorneys by their name within that firm. Text that matches nothing in the directory is listed
// in `unmatched` (it is replaced if the user applies a new choice); nothing is guessed.
struct OpposingCounselMatch {
    std::vector<OpposingFirmChoice> choices;
    std::vector<std::string> unmatched;  // for the user, e.g. "OPC Firm Name: Smith Law"
};

// The arguments are the case's OPC, OPC Firm Name and OPC 2 Address.
OpposingCounselMatch matchOpposingCounsel(const std::string& names, const std::string& firmName,
                                          const std::string& others, const std::vector<Firm>& firms,
                                          const std::vector<Attorney>& attorneys);

}  // namespace finrenamer
