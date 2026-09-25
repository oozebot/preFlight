///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Roman Beránek @zavorka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Justin Schuh @jschuh
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/core/text/Encoding.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>

namespace Luminary
{

size_t get_utf8_sequence_length(const std::string &text, size_t pos)
{
    assert(pos < text.size());
    return get_utf8_sequence_length(text.c_str() + pos, text.size() - pos);
}

size_t get_utf8_sequence_length(const char *seq, size_t size)
{
    size_t length = 0;
    unsigned char c = seq[0];
    if (c < 0x80)
    { // 0x00-0x7F
        // is ASCII letter
        length++;
    }
    // Bytes 0x80 to 0xBD are trailer bytes in a multibyte sequence.
    // pos is in the middle of a utf-8 sequence. Add the utf-8 trailer bytes.
    else if (c < 0xC0)
    { // 0x80-0xBF
        length++;
        while (length < size)
        {
            c = seq[length];
            if (c < 0x80 || c >= 0xC0)
            {
                break; // prevent overrun
            }
            length++; // add a utf-8 trailer byte
        }
    }
    // Bytes 0xC0 to 0xFD are header bytes in a multibyte sequence.
    // The number of one bits above the topmost zero bit indicates the number of bytes (including this one) in the whole sequence.
    else if (c < 0xE0)
    { // 0xC0-0xDF
        // add a utf-8 sequence (2 bytes)
        if (2 > size)
        {
            return size; // prevent overrun
        }
        length += 2;
    }
    else if (c < 0xF0)
    { // 0xE0-0xEF
        // add a utf-8 sequence (3 bytes)
        if (3 > size)
        {
            return size; // prevent overrun
        }
        length += 3;
    }
    else if (c < 0xF8)
    { // 0xF0-0xF7
        // add a utf-8 sequence (4 bytes)
        if (4 > size)
        {
            return size; // prevent overrun
        }
        length += 4;
    }
    else if (c < 0xFC)
    { // 0xF8-0xFB
        // add a utf-8 sequence (5 bytes)
        if (5 > size)
        {
            return size; // prevent overrun
        }
        length += 5;
    }
    else if (c < 0xFE)
    { // 0xFC-0xFD
        // add a utf-8 sequence (6 bytes)
        if (6 > size)
        {
            return size; // prevent overrun
        }
        length += 6;
    }
    else
    { // 0xFE-0xFF
        // not a utf-8 sequence
        length++;
    }
    return length;
}

std::string string_printf(const char *format, ...)
{
    va_list args1;
    va_start(args1, format);
    va_list args2;
    va_copy(args2, args1);

    static const size_t INITIAL_LEN = 200;
    std::string buffer(INITIAL_LEN, '\0');

    int bufflen = ::vsnprintf(buffer.data(), INITIAL_LEN - 1, format, args1);

    if (bufflen >= int(INITIAL_LEN))
    {
        buffer.resize(size_t(bufflen) + 1);
        ::vsnprintf(buffer.data(), buffer.size(), format, args2);
    }

    va_end(args1);
    va_end(args2);

    buffer.resize(bufflen);
    return buffer;
}

// Replaces in place one character at a time, so the cost is quadratic in the number of escapes.
// Inputs are short attribute strings.
std::string xml_escape(std::string text, bool is_marked /* = false*/)
{
    std::string::size_type pos = 0;
    for (;;)
    {
        pos = text.find_first_of("\"\'&<>", pos);
        if (pos == std::string::npos)
            break;

        std::string replacement;
        switch (text[pos])
        {
        case '\"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = is_marked ? "<" : "&lt;";
            break;
        case '>':
            replacement = is_marked ? ">" : "&gt;";
            break;
        default:
            break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}

// Definition of escape symbols https://www.w3.org/TR/REC-xml/#AVNormalize
// During the read of xml attribute normalization of white spaces is applied
// Soo for not lose white space character it is escaped before store
std::string xml_escape_double_quotes_attribute_value(std::string text)
{
    std::string::size_type pos = 0;
    for (;;)
    {
        pos = text.find_first_of("\"&<\r\n\t", pos);
        if (pos == std::string::npos)
            break;

        std::string replacement;
        switch (text[pos])
        {
        case '\"':
            replacement = "&quot;";
            break;
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '\r':
            replacement = "&#xD;";
            break;
        case '\n':
            replacement = "&#xA;";
            break;
        case '\t':
            replacement = "&#x9;";
            break;
        default:
            break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}

} // namespace Luminary
