///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>
#include <system_error>

#include <boost/system/error_code.hpp>

namespace boost
{
namespace filesystem
{
class directory_entry;
}
} // namespace boost

namespace Luminary
{

// Safely rename a file even if the target exists.
// On Windows, the file explorer (or anti-virus or whatever else) often locks the file
// for a short while, so the file may not be movable. Retry while we see recoverable errors.
extern std::error_code rename_file(const std::string &from, const std::string &to);

enum CopyFileResult
{
    SUCCESS = 0,
    FAIL_COPY_FILE,
    FAIL_FILES_DIFFERENT,
    FAIL_RENAMING,
    FAIL_CHECK_ORIGIN_NOT_OPENED,
    FAIL_CHECK_TARGET_NOT_OPENED
};
// Copy a file, adjust the access attributes, so that the target is writable.
CopyFileResult copy_file_inner(const std::string &from, const std::string &to, std::string &error_message);
// Copy file to a temp file first, then rename it to the final file name.
// If with_check is true, then the content of the copied file is compared to the content
// of the source file before renaming.
// Additional error info is passed in error message.
extern CopyFileResult copy_file(const std::string &from, const std::string &to, std::string &error_message,
                                const bool with_check = false);

// Compares two files if identical.
extern CopyFileResult check_copy(const std::string &origin, const std::string &copy);

// Ignore system and hidden files, which may be created by the DropBox synchronisation process.
// Fix for string escaping issue
extern bool is_plain_file(const boost::filesystem::directory_entry &path);
extern bool is_ini_file(const boost::filesystem::directory_entry &path);
extern bool is_idx_file(const boost::filesystem::directory_entry &path);
extern bool is_gcode_file(const std::string &path);
extern bool is_img_file(const std::string &path);
extern bool is_gallery_file(const boost::filesystem::directory_entry &path, char const *type);
extern bool is_gallery_file(const std::string &path, char const *type);
extern bool is_shapes_dir(const std::string &dir);

} // namespace Luminary
