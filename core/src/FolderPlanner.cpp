#include "finrenamer/FolderPlanner.h"

#include <string>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace finrenamer {

std::filesystem::path subfolderFor(const Account& account, const DateSpec& date,
                                   const SortOptions& options)
{
    const std::filesystem::path accountDir = pathFromUtf8(accountLabel(account));
    const std::filesystem::path yearDir = pathFromUtf8(std::to_string(filingYear(date)));

    if (options.byAccount && options.byYear) {
        return options.order == FolderOrder::AccountThenYear ? accountDir / yearDir
                                                             : yearDir / accountDir;
    }
    if (options.byAccount) return accountDir;
    if (options.byYear) return yearDir;
    return {};
}

}  // namespace finrenamer
