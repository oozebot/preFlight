///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "DebugOutput.hpp"

// Registry of fallback / event counters. Every guarded path that logs a per-fire
// line also bumps a named counter here, so the total of every counter is dumped
// once per slice as a machine-readable block:
//     [0.000][COUNTER] NAME=<total>
// Per-fire lines keep the where/why payload; the block carries the totals the
// harness compares. Counting costs one relaxed atomic add per fire and happens
// whether or not --debug is active; the dump only prints when it is.
namespace Luminary
{

class DbgCounters
{
public:
    static DbgCounters &inst()
    {
        static DbgCounters c;
        return c;
    }

    // Registration is one static line per counter at the emitting site (the
    // DBG_COUNT macro below does it): the lock is taken once per site, the
    // returned atomic is bumped lock-free afterwards.
    // `sticky` marks a row that reset() leaves alone. A counter whose only fire happens while a
    // project is being loaded is bumped before Print::process() begins, so a per-slice row would be
    // zeroed before either dump and could never appear. Sticky rows are per process, not per slice.
    std::atomic<uint64_t> &reg(const char *name, bool sticky = false)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](const Row &r) { return r.first == name; });
        if (it == m_rows.end())
        {
            m_rows.emplace_back(name, std::make_unique<std::atomic<uint64_t>>(0));
            m_sites.push_back(1);
            m_sticky.push_back(sticky);
            return *m_rows.back().second;
        }
        // A second site registering the same name shares the counter; the dump says so. A name
        // registered sticky from any site stays sticky, so one load-stage site cannot be undone by a
        // second site that forgot the distinction.
        const size_t i = size_t(it - m_rows.begin());
        ++m_sites[i];
        m_sticky[i] = m_sticky[i] || sticky;
        return *it->second;
    }

    // Called at the start of Print::process() so GUI re-slices produce clean per-print blocks.
    // Sticky rows survive, because their fires happen at load time and would otherwise be erased
    // between the fire and the dump.
    void reset()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (!m_sticky[i])
                m_rows[i].second->store(0, std::memory_order_relaxed);
    }

    // One line per counter, sorted by name, only for counters that fired. `stage` names the block
    // (PROCESS after Print::process(), EXPORT after the G-code export) so a reader can tell which
    // blocks a run produced.
    void dump(const char *stage)
    {
        std::vector<std::pair<std::string, uint64_t>> rows;
        std::vector<std::pair<std::string, unsigned>> shared;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (size_t i = 0; i < m_rows.size(); ++i)
            {
                rows.emplace_back(m_rows[i].first, m_rows[i].second->load(std::memory_order_relaxed));
                if (m_sites[i] > 1)
                    shared.emplace_back(m_rows[i].first, m_sites[i]);
            }
        }
        std::sort(rows.begin(), rows.end());
        unsigned long long printed = 0;
        for (const auto &[name, v] : rows)
            if (v != 0)
            {
                dbg_log(DBG_ALL, 0., "COUNTER", "%s=%llu", name.c_str(), (unsigned long long) v);
                ++printed;
            }
        // Terminates the block with its stage and row count, so a reader can tell a stream cut short
        // from counters that never fired, and a run that lost a whole block from one that never had it:
        // a block whose last line is not an _END line is incomplete, a run without _END_EXPORT lost its
        // export-stage counters.
        dbg_log(DBG_ALL, 0., "COUNTER", "_END_%s=%llu", stage, printed);
        // A name registered from more than one site is one counter with several meanings.
        for (const auto &[name, n] : shared)
            dbg_log(DBG_ALL, 0., "META", "counter_shared=%s:%u", name.c_str(), n);
    }

private:
    using Row = std::pair<std::string, std::unique_ptr<std::atomic<uint64_t>>>;
    std::mutex m_mutex;
    std::vector<Row> m_rows;
    std::vector<unsigned> m_sites; // registrations per row
    std::vector<bool> m_sticky;    // rows reset() leaves alone; see reg()
};

} // namespace Luminary

// Bump the named counter by one (or by n). Registers on first use at this site.
#define DBG_COUNT(name)                                                                             \
    do                                                                                              \
    {                                                                                               \
        static std::atomic<uint64_t> &dbg_counter_ref_ = ::Luminary::DbgCounters::inst().reg(name); \
        dbg_counter_ref_.fetch_add(1, std::memory_order_relaxed);                                   \
    } while (0)

#define DBG_COUNT_ADD(name, n)                                                                      \
    do                                                                                              \
    {                                                                                               \
        static std::atomic<uint64_t> &dbg_counter_ref_ = ::Luminary::DbgCounters::inst().reg(name); \
        dbg_counter_ref_.fetch_add(uint64_t(n), std::memory_order_relaxed);                         \
    } while (0)

// For a counter whose fire happens while a project is being loaded, before Print::process() resets
// the per-slice rows. The row survives the reset and totals over the process, so a GUI session that
// opens several projects reports their sum.
#define DBG_COUNT_LOAD(name)                                                                              \
    do                                                                                                    \
    {                                                                                                     \
        static std::atomic<uint64_t> &dbg_counter_ref_ = ::Luminary::DbgCounters::inst().reg(name, true); \
        dbg_counter_ref_.fetch_add(1, std::memory_order_relaxed);                                         \
    } while (0)
