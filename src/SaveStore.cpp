#include "SaveStore.h"

#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
bool replaceFile(const std::filesystem::path& temp, const std::filesystem::path& path,
                 const std::filesystem::path& backup, bool hasCurrent, std::string& error, bool preserveBackup) {
#ifdef _WIN32
    if (hasCurrent) {
        std::error_code ec;
        if(!preserveBackup) std::filesystem::remove(backup, ec);
        if (ec) {
            error = "cannot remove previous backup: " + ec.message();
            return false;
        }
        if (!ReplaceFileW(path.c_str(), temp.c_str(), preserveBackup ? nullptr : backup.c_str(), 0,
                          nullptr, nullptr)) {
            error = "cannot atomically replace save: "
                + std::system_category().message(static_cast<int>(GetLastError()));
            return false;
        }
    } else if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = "cannot install new save: "
            + std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
#else
    if(hasCurrent && !preserveBackup){
        std::error_code copyError;
        std::filesystem::copy_file(path,backup,std::filesystem::copy_options::overwrite_existing,copyError);
        if(copyError){error="cannot create save backup: "+copyError.message();return false;}
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = "cannot install new save: " + ec.message();
        return false;
    }
#endif
    return true;
}
}

bool SaveStore::readAll(const std::filesystem::path& path, std::string& contents, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "cannot inspect file: " + ec.message();
        return false;
    }
    constexpr std::uintmax_t maxSaveSize = 1024 * 1024;
    if (size > maxSaveSize) {
        error = "save file exceeds 1 MiB";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open file for reading";
        return false;
    }
    std::string read(static_cast<std::size_t>(size), '\0');
    file.read(read.data(), static_cast<std::streamsize>(read.size()));
    if (file.bad() || file.gcount() != static_cast<std::streamsize>(read.size())) {
        error = "I/O error while reading file";
        return false;
    }
    if (file.peek() != std::char_traits<char>::eof()) {
        error = "save file grew beyond the 1 MiB limit while being read";
        return false;
    }
    contents = std::move(read);
    error.clear();
    return true;
}

bool SaveStore::writeAtomically(const std::filesystem::path& path, const std::string& contents,
                                std::string& error, bool preserveBackup) {
    error.clear();
    std::filesystem::path temp = path;
    temp += std::filesystem::path::string_type{'.', 't', 'm', 'p'};
    std::filesystem::path backup = path;
    backup += std::filesystem::path::string_type{'.', 'b', 'a', 'k'};
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "cannot create save directory: " + ec.message();
            return false;
        }
    }

    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open temporary save file";
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.flush();
    if (!file) {
        file.close();
        std::filesystem::remove(temp, ec);
        error = "write or flush failed";
        return false;
    }
    file.close();
    if (file.fail()) {
        std::filesystem::remove(temp, ec);
        error = "close failed";
        return false;
    }

    const bool hasCurrent = std::filesystem::exists(path, ec);
    if (ec) {
        const std::string detail = ec.message();
        std::filesystem::remove(temp, ec);
        error = "cannot inspect existing save: " + detail;
        return false;
    }
    if (!replaceFile(temp, path, backup, hasCurrent, error, preserveBackup)) {
        std::error_code cleanupError;
        std::filesystem::remove(temp, cleanupError);
        if (cleanupError) error += "; temporary cleanup failed: " + cleanupError.message();
        return false;
    }

    error.clear();
    return true;
}

