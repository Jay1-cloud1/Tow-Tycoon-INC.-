#pragma once

#include <filesystem>
#include <string>

class SaveStore {
public:
    static bool readAll(const std::filesystem::path& path, std::string& contents, std::string& error);
    static bool writeAtomically(const std::filesystem::path& path, const std::string& contents,
                                std::string& error, bool preserveBackup = false);
};

