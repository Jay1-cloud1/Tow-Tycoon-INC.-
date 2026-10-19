#include "SaveStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("TowTycoonSaveStoreTests-" + std::to_string(stamp));
    const auto savePath = root / "savegame.tt";
    std::string error;
    std::string contents;

    if (!SaveStore::writeAtomically(savePath, "first-save", error)) return 1;
    if (!SaveStore::readAll(savePath, contents, error) || contents != "first-save") return 2;
    if (!SaveStore::writeAtomically(savePath, "second-save", error)) return 3;
    if (!SaveStore::readAll(savePath, contents, error) || contents != "second-save") return 4;

    const auto blockingFile = root / "not-a-directory";
    {
        std::ofstream blocker(blockingFile);
        if (!blocker) return 5;
    }
    if (SaveStore::writeAtomically(blockingFile / "savegame.tt", "invalid", error)) return 6;
    if (error.empty()) return 7;
    if (!SaveStore::readAll(savePath, contents, error) || contents != "second-save") return 8;

    const auto oversizedPath = root / "oversized.tt";
    if (!SaveStore::writeAtomically(oversizedPath, std::string(std::size_t{1024} * 1024 + 1, 'x'), error)) return 9;
    if (SaveStore::readAll(oversizedPath, contents, error) || error.empty()) return 10;

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    if (cleanupError) return 11;
    return 0;
}
