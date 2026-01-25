///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Tomáš Mészáros @tamasmeszaros, Roman Beránek @zavorka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef EXECUTIONTBB_HPP
#define EXECUTIONTBB_HPP

#include <mutex>

#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include "Execution.hpp"

namespace Slic3r
{

struct ExecutionTBB
{
};
template<>
struct IsExecutionPolicy_<ExecutionTBB> : public std::true_type
{
};

// Execution policy using Intel TBB library under the hood.
static constexpr ExecutionTBB ex_tbb = {};

template<>
struct execution::Traits<ExecutionTBB>
{
private:
    template<class Fn, class It>
    static IteratorOnly<It, void> loop_(const tbb::blocked_range<It> &range, Fn &&fn)
    {
        for (auto &el : range)
            fn(el);
    }

    template<class Fn, class I>
    static IntegerOnly<I, void> loop_(const tbb::blocked_range<I> &range, Fn &&fn)
    {
        for (I i = range.begin(); i < range.end(); ++i)
            fn(i);
    }

public:
    using SpinningMutex = tbb::spin_mutex;
    using BlockingMutex = std::mutex;

    template<class It, class Fn>
    static void for_each(const ExecutionTBB &, It from, It to, Fn &&fn, size_t granularity)
    {
        tbb::parallel_for(tbb::blocked_range{from, to, granularity},
                          [&fn](const auto &range) { loop_(range, std::forward<Fn>(fn)); });
    }

    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(const ExecutionTBB &, I from, I to, const T &init, MergeFn &&mergefn, AccessFn &&accessfn,
                    size_t granularity = 1)
    {
        return tbb::parallel_reduce(
            tbb::blocked_range{from, to, granularity}, init,
            [&](const auto &range, T subinit)
            {
                T acc = subinit;
                loop_(range, [&](auto &i) { acc = mergefn(acc, accessfn(i)); });
                return acc;
            },
            std::forward<MergeFn>(mergefn));
    }

    static size_t max_concurrency(const ExecutionTBB &) { return tbb::this_task_arena::max_concurrency(); }
};

} // namespace Slic3r

#endif // EXECUTIONTBB_HPP
