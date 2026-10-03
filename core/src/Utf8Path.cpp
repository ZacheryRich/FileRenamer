#include "finrenamer/Utf8Path.h"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace finrenamer {

std::filesystem::path pathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

std::size_t utf16Length(std::string_view utf8)
{
    std::size_t units = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80)       { units += 1; i += 1; }
        else if (c < 0xE0)  { units += 1; i += 2; }
        else if (c < 0xF0)  { units += 1; i += 3; }
        else                { units += 2; i += 4; }  // surrogate pair
    }
    return units;
}

#ifdef _WIN32

std::string caseFoldKey(const std::filesystem::path& path)
{
    // native() is UTF-16 on Windows. Uppercasing with the invariant locale is
    // the same mapping NTFS uses to decide whether two names collide.
    std::wstring wide = path.lexically_normal().native();
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    if (wide.empty()) return {};

    const int len = static_cast<int>(wide.size());
    const int needed = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.c_str(), len,
                                     nullptr, 0, nullptr, nullptr, 0);
    if (needed <= 0) return utf8FromPath(std::filesystem::path(wide));

    std::wstring upper(static_cast<std::size_t>(needed), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.c_str(), len,
                  upper.data(), needed, nullptr, nullptr, 0);
    return utf8FromPath(std::filesystem::path(upper));
}

#else

std::string caseFoldKey(const std::filesystem::path& path)
{
    const std::string utf8 = utf8FromPath(path.lexically_normal());
    std::string out;
    out.reserve(utf8.size());

    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        if (c == '\\') {
            out += '/';
        } else if (c < 0x80) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        } else if (c == 0xC3 && i + 1 < utf8.size()) {
            // U+00C0..U+00DE (À..Þ, except × U+00D7) lowercase to +0x20.
            auto c2 = static_cast<unsigned char>(utf8[++i]);
            if (c2 >= 0x80 && c2 <= 0x9E && c2 != 0x97) c2 += 0x20;
            out += static_cast<char>(c);
            out += static_cast<char>(c2);
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

#endif

}  // namespace finrenamer
