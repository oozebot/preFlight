///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef> // size_t, which the precompiled header had been supplying
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Luminary
{

struct GCodeObjectLine
{
    size_t text_offset{0};
    size_t text_length{0};
};

class GCodeObject
{
public:
    static constexpr size_t INITIAL_BUFFER_SIZE = 100 * 1024 * 1024;
    static constexpr size_t INITIAL_LINE_CAPACITY = 1'000'000;

    GCodeObject()
    {
        m_text_buffer.reserve(INITIAL_BUFFER_SIZE);
        m_lines.reserve(INITIAL_LINE_CAPACITY);
    }

    void append_text(const char *data)
    {
        if (data)
            this->append_text(data, strlen(data));
    }

    void append_text(const char *data, size_t len)
    {
        if (!data || len == 0)
            return;

        size_t old_size = m_text_buffer.size();
        m_text_buffer.append(data, len);

        const char *search_start = m_text_buffer.data() + old_size;
        const char *search_end = m_text_buffer.data() + m_text_buffer.size();
        while (search_start < search_end)
        {
            const char *nl = static_cast<const char *>(memchr(search_start, '\n', search_end - search_start));
            if (!nl)
                break;

            size_t line_start = m_lines.empty() ? 0 : m_lines.back().text_offset + m_lines.back().text_length;
            size_t line_end = static_cast<size_t>(nl - m_text_buffer.data()) + 1;
            m_lines.push_back({line_start, line_end - line_start});
            search_start = nl + 1;
        }
    }

    size_t line_count() const { return m_lines.size(); }

    std::string_view get_line_text(size_t line_idx) const
    {
        if (line_idx >= m_lines.size())
            return {};
        const GCodeObjectLine &l = m_lines[line_idx];
        return std::string_view(m_text_buffer.data() + l.text_offset, l.text_length);
    }

    const std::string &text_buffer() const { return m_text_buffer; }
    const std::vector<GCodeObjectLine> &lines() const { return m_lines; }
    std::vector<GCodeObjectLine> &lines() { return m_lines; }

    void clear()
    {
        m_text_buffer.clear();
        m_text_buffer.shrink_to_fit();
        m_lines.clear();
        m_lines.shrink_to_fit();
    }

private:
    std::string m_text_buffer;
    std::vector<GCodeObjectLine> m_lines;
};

} // namespace Luminary
