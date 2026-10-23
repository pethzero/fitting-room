#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace OS::PathText {

    // A path as narrow text, WITHOUT THROWING.
    //
    // ⚠⚠ path::string() CONVERTS THROUGH THE ACTIVE CODE PAGE AND THROWS
    // std::system_error WHEN A CHARACTER HAS NO MAPPING. A file or folder named
    // in Thai, Chinese or Japanese under a player's Data tree is enough, and on
    // a worker thread that throw is std::terminate: a fail-fast with no crash
    // log (WorkerGuard.h has the field history). Field 2026-10-23, SE 1.5.97:
    // the Bodies page's BodyMorphCatalog scan of Data/CalienteTools died this
    // way twice, ucrtbase 0xC0000409, with ZhizhenMakeup in the load order.
    //
    // ⚠ THE CODE-PAGE FORM WHEN IT EXISTS, so every name that converted before
    // converts the same now and a string that is later turned back into a path
    // still opens the same file. Only a name that used to throw comes back as
    // UTF-8 instead: good enough to compare an extension or log a file name,
    // which is all the scanners do with it.
    [[nodiscard]] inline std::string Narrow(const std::filesystem::path& a_path) {
        try {
            return a_path.string();
        } catch (const std::system_error&) {
            const auto u8 = a_path.u8string();
            return std::string{ reinterpret_cast<const char*>(u8.data()), u8.size() };
        }
    }

    // The code-page form only, for a string that will be turned BACK INTO A
    // PATH later. ⚠ NOT Narrow: its UTF-8 fallback read back through the code
    // page names a different file, and can itself throw on the way in (bytes
    // 0x81/0x8D/0x8F/0x90/0x9D have no mapping in 1252). Empty means this name
    // cannot travel as narrow text at all, and the caller skips it.
    [[nodiscard]] inline std::optional<std::string> Native(const std::filesystem::path& a_path) {
        try {
            return a_path.string();
        } catch (const std::system_error&) {
            return std::nullopt;
        }
    }

}  // namespace OS::PathText
