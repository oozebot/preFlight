///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <string>

namespace Luminary
{

// Returns next utf8 sequence length. =number of bytes in string, that creates together one utf-8 character.
// Starting at pos. ASCII characters returns 1. Works also if pos is in the middle of the sequence.
extern size_t get_utf8_sequence_length(const std::string &text, size_t pos = 0);
extern size_t get_utf8_sequence_length(const char *seq, size_t size);

std::string string_printf(const char *format, ...);

extern std::string xml_escape(std::string text, bool is_marked = false);
extern std::string xml_escape_double_quotes_attribute_value(std::string text);

} // namespace Luminary
