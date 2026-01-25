///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_VirtualGCodeFile_hpp_
#define slic3r_VirtualGCodeFile_hpp_

#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace Slic3r
{

class VirtualGCodeFile
{
private:
    static constexpr size_t INITIAL_BUFFER_SIZE = 100 * 1024 * 1024; // 100MB
    static constexpr size_t INITIAL_LINE_CAPACITY = 1000000;         // 1M lines

public:
    VirtualGCodeFile()
    {
        // Reserve initial space to reduce reallocations
        m_buffer.reserve(INITIAL_BUFFER_SIZE);
        m_line_offsets.reserve(INITIAL_LINE_CAPACITY);
        m_line_offsets.push_back(0); // First line starts at 0
    }

    ~VirtualGCodeFile()
    {
        m_buffer.clear();
        m_buffer.shrink_to_fit();
        m_line_offsets.clear();
        m_line_offsets.shrink_to_fit();
    }

    // Stage 1: Basic write interface
    void write(const char *data)
    {
        if (!data)
            return;

        size_t len = strlen(data);
        size_t old_size = m_buffer.size();
        m_buffer.append(data, len);

        // Track new lines
        for (size_t i = old_size; i < m_buffer.size(); ++i)
        {
            if (m_buffer[i] == '\n')
            {
                m_line_offsets.push_back(i + 1);
            }
        }
    }

    void write_line(const std::string &line)
    {
        size_t old_size = m_buffer.size();
        m_buffer.append(line);
        if (line.empty() || line.back() != '\n')
        {
            m_buffer.push_back('\n');
        }
        m_line_offsets.push_back(m_buffer.size());
    }

    // Stage 2: Basic read interface
    size_t line_count() const { return m_line_offsets.empty() ? 0 : m_line_offsets.size() - 1; }

    std::string get_line(size_t line_num) const
    {
        if (line_num >= line_count())
        {
            return "";
        }

        if (m_buffer.empty() || m_line_offsets.size() <= line_num + 1)
        {
            return "";
        }

        size_t start = m_line_offsets[line_num];
        size_t end = (line_num + 1 < m_line_offsets.size()) ? m_line_offsets[line_num + 1] // Include the newline!
                                                            : m_buffer.size();

        // Return line including newline if present
        if (end > start && m_buffer[end - 1] == '\n')
        {
            return m_buffer.substr(start, end - start);
        }
        else
        {
            // Last line might not have newline
            return m_buffer.substr(start, end - start) + '\n';
        }
    }

    // Clear all data
    void clear()
    {
        m_buffer.clear();
        m_buffer.shrink_to_fit();
        m_line_offsets.clear();
        m_line_offsets.shrink_to_fit();
        m_line_offsets.push_back(0);
    }

    // Get total size
    size_t total_size() const { return m_buffer.size(); }

    // Provide vector-like interface for compatibility
    size_t size() const { return line_count(); }
    std::string operator[](size_t index) const { return get_line(index); }

    // Get direct access to buffer for bulk writes (avoids line-by-line iteration)
    const std::string &get_buffer() const { return m_buffer; }

    // Efficient line-by-line iteration
    class LineIterator
    {
        const VirtualGCodeFile *m_file;
        size_t m_line_idx;

    public:
        LineIterator(const VirtualGCodeFile *file, size_t idx) : m_file(file), m_line_idx(idx) {}

        std::string operator*() const { return m_file->get_line(m_line_idx); }
        LineIterator &operator++()
        {
            ++m_line_idx;
            return *this;
        }
        bool operator!=(const LineIterator &other) const { return m_line_idx != other.m_line_idx; }
    };

    LineIterator begin() const { return LineIterator(this, 0); }
    LineIterator end() const { return LineIterator(this, line_count()); }

    // Efficient append from another virtual file
    void append_from(const VirtualGCodeFile &source, size_t start_line, size_t count)
    {
        size_t end_line = std::min(start_line + count, source.line_count());
        for (size_t i = start_line; i < end_line; ++i)
        {
            write_line(source.get_line(i));
        }
    }

    // Reserve capacity for expected size
    void reserve_lines(size_t expected_lines)
    {
        m_line_offsets.reserve(expected_lines + 1);
        // Estimate avg 50 chars per line
        m_buffer.reserve(expected_lines * 50);
    }

private:
    std::string m_buffer;               // Single contiguous buffer
    std::vector<size_t> m_line_offsets; // Line start positions
};

} // namespace Slic3r

#endif // slic3r_VirtualGCodeFile_hpp_