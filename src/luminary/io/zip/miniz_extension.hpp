///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2020 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>
#include <miniz.h>

namespace Luminary
{

bool open_zip_reader(mz_zip_archive *zip, const std::string &fname_utf8);
bool open_zip_writer(mz_zip_archive *zip, const std::string &fname_utf8);
bool close_zip_reader(mz_zip_archive *zip);
bool close_zip_writer(mz_zip_archive *zip);

class MZ_Archive
{
public:
    mz_zip_archive arch;

    MZ_Archive();

    static std::string get_errorstr(mz_zip_error mz_err);

    std::string get_errorstr() const { return get_errorstr(arch.m_last_error) + "!"; }

    bool is_alive() const { return arch.m_zip_mode != MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED; }
};

} // namespace Luminary
