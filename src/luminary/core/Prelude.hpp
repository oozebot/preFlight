///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2016 Miro Hrončok @hroncok
///|/ Copyright (c) 2014 Kamil Kwolek
///|/
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Král @vojtechkral, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2016 Miro Hrončok @hroncok
///|/ Copyright (c) 2014 Kamil Kwolek
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "PreFlightVersion.h"

// Profiles for the alpha are stored into the preFlight-alpha directory to not mix with the current release.
#define PREFLIGHT_APP_FULL_NAME PREFLIGHT_APP_KEY
// #define PREFLIGHT_APP_FULL_NAME PREFLIGHT_APP_KEY "-alpha"
// #define PREFLIGHT_APP_FULL_NAME PREFLIGHT_APP_KEY "-beta"

#define GCODEVIEWER_APP_NAME "preFlight G-code Viewer"
#define GCODEVIEWER_APP_KEY "preFlightGcodeViewer"

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <memory>
#include <array>
#include <algorithm>
#include <ostream>
#include <iostream>
#include <math.h>
#include <queue>
#include <sstream>
#include <cstdio>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
#include <cassert>
#include <cmath>
#include <type_traits>
#include <optional>

#ifdef _WIN32
// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
// https://github.com/microsoft/STL/issues/147#issuecomment-1090148740
// Thus it is recommended to use boost::container::deque instead.
#include <boost/container/deque.hpp>
#endif // _WIN32

#include "Semver.hpp"

// Clipper2 uses int64_t internally (Path64), so coord_t is int64_t as well. An int32_t coordinate
// truncates silently and overflows on high-detail models (1M+ triangles), which breaks
// boost::polygon Voronoi; it would save 32% of the memory the point arrays take.
//
// Note: Original comment said "boost Voronoi requires 32bit" but this may be outdated.
// We'll test if boost::polygon works with int64_t. If not, we'll replace boost::polygon.
using coord_t = int64_t;

using coordf_t = double;

// One tolerance shared by unrelated tests: squared Euclidean distance, a difference of radians,
// a cross product of two non-normalized vectors and others.
static constexpr double EPSILON = 1e-4;
// Scaling factor for a conversion from coord_t to coordf_t: 10e-6
// This scaling generates a following fixed point representation with for a 32bit integer:
// 0..4294mm with 1nm resolution
// int32_t fits an interval of (-2147.48mm, +2147.48mm)
// with int64_t we don't have to worry anymore about the size of the int.
static constexpr double SCALING_FACTOR = 0.000001;
static constexpr double PI = 3.141592653589793238;
// Number of sides for circular brim ears
static constexpr size_t POLY_SIDE_COUNT = 128; // Was 24, then 64 - now ultra smooth
// When extruding a closed loop, the loop is interrupted and shortened a bit to reduce the seam.
static constexpr double LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER = 0.15;
static constexpr double INSET_OVERLAP_TOLERANCE = 0.4;
// 3mm ring around the top / bottom / bridging areas.
static constexpr double EXTERNAL_INFILL_MARGIN = 3.;
// Yields a double; the conversion to coord_t happens at the call site and truncates.
#define scale_(val) ((val) / SCALING_FACTOR)

#define SCALED_EPSILON scale_(EPSILON)

#ifndef UNUSED
#define UNUSED(x) (void) (x)
#endif /* UNUSED */

// Write slices as SVG images into out directory during the 2D processing of the slices.
// #define PREFLIGHT_DEBUG_SLICE_PROCESSING

namespace Luminary
{

extern Semver SEMVER;

// On MSVC, std::deque degenerates to a list of pointers, which defeats its purpose of reducing allocator load and memory fragmentation.
template<class T, class Allocator = std::allocator<T>>
using deque =
#ifdef _WIN32
    // Use boost implementation, which allocates blocks of 512 bytes instead of blocks of 8 bytes.
    boost::container::deque<T, Allocator>;
#else  // _WIN32
    std::deque<T, Allocator>;
#endif // _WIN32

template<typename T, typename Q>
inline T unscale(Q v)
{
    return T(v) * T(SCALING_FACTOR);
}

constexpr size_t MAX_NUMBER_OF_BEDS = 9;

enum Axis
{
    X = 0,
    Y,
    Z,
    E,
    F,
    NUM_AXES,
    // For the GCodeReader to mark a parsed axis, which is not in "XYZEF", it was parsed correctly.
    UNKNOWN_AXIS = NUM_AXES,
    NUM_AXES_WITH_UNKNOWN,
};

template<typename T, typename Alloc, typename Alloc2>
inline void append(std::vector<T, Alloc> &dest, const std::vector<T, Alloc2> &src)
{
    if (dest.empty())
        dest = src; // copy
    else
        dest.insert(dest.end(), src.begin(), src.end());
}

template<typename T, typename Alloc>
inline void append(std::vector<T, Alloc> &dest, std::vector<T, Alloc> &&src)
{
    if (dest.empty())
        dest = std::move(src);
    else
    {
        dest.insert(dest.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
        // Release memory of the source contour now.
        src.clear();
        src.shrink_to_fit();
    }
}

template<class T, class... Args> // Arbitrary allocator can be used
void clear_and_shrink(std::vector<T, Args...> &vec)
{
    // shrink_to_fit does not garantee the release of memory nor does it clear()
    std::vector<T, Args...> tmp;
    vec.swap(tmp);
    assert(vec.capacity() == 0);
}

// Append the source in reverse.
template<typename T>
inline void append_reversed(std::vector<T> &dest, const std::vector<T> &src)
{
    if (dest.empty())
        dest = {src.rbegin(), src.rend()};
    else
        dest.insert(dest.end(), src.rbegin(), src.rend());
}

// Append the source in reverse.
template<typename T>
inline void append_reversed(std::vector<T> &dest, std::vector<T> &&src)
{
    if (dest.empty())
        dest = {std::make_move_iterator(src.rbegin), std::make_move_iterator(src.rend)};
    else
        dest.insert(dest.end(), std::make_move_iterator(src.rbegin()), std::make_move_iterator(src.rend()));
    // Release memory of the source contour now.
    src.clear();
    src.shrink_to_fit();
}

// Casting an std::vector<> from one type to another type without warnings about a loss of accuracy.
template<typename T_TO, typename T_FROM>
std::vector<T_TO> cast(const std::vector<T_FROM> &src)
{
    std::vector<T_TO> dst;
    dst.reserve(src.size());
    for (const T_FROM &a : src)
        dst.emplace_back((T_TO) a);
    return dst;
}

template<typename T>
inline void remove_nulls(std::vector<T *> &vec)
{
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const T *ptr) { return ptr == nullptr; }), vec.end());
}

template<typename T>
inline void sort_remove_duplicates(std::vector<T> &vec)
{
    std::sort(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

// Older compilers do not provide a std::make_unique template. Provide a simple one.
template<typename T, typename... Args>
inline std::unique_ptr<T> make_unique(Args &&...args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Variant of std::lower_bound() with compare predicate, but without the key.
// This variant is very useful in case that the T type is large or it does not even have a public constructor.
template<class ForwardIt, class LowerThanKeyPredicate>
ForwardIt lower_bound_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_than_key)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);

    while (count > 0)
    {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (lower_than_key(*it))
        {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class T, class Compare = std::less<>>
ForwardIt binary_find(ForwardIt first, ForwardIt last, const T &value, Compare comp = {})
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare.
    // This is stricter than lower_bound requirement (see above)

    first = std::lower_bound(first, last, value, comp);
    return first != last && !comp(value, *first) ? first : last;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class LowerThanKeyPredicate, class EqualToKeyPredicate>
ForwardIt binary_find_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_thank_key,
                                   EqualToKeyPredicate equal_to_key)
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare.
    // This is stricter than lower_bound requirement (see above)

    first = lower_bound_by_predicate(first, last, lower_thank_key);
    return first != last && equal_to_key(*first) ? first : last;
}

template<typename ContainerType, typename ValueType>
inline bool contains(const ContainerType &c, const ValueType &v)
{
    return std::find(c.begin(), c.end(), v) != c.end();
}
template<typename T>
inline bool contains(const std::initializer_list<T> &il, const T &v)
{
    return std::find(il.begin(), il.end(), v) != il.end();
}

template<typename ContainerType, typename ValueType>
inline bool one_of(const ValueType &v, const ContainerType &c)
{
    return contains(c, v);
}
template<typename T>
inline bool one_of(const T &v, const std::initializer_list<T> &il)
{
    return contains(il, v);
}

template<typename T>
constexpr inline T sqr(T x)
{
    return x * x;
}

template<typename T, typename Number>
constexpr inline T lerp(const T &a, const T &b, Number t)
{
    assert((t >= Number(-EPSILON)) && (t <= Number(1) + Number(EPSILON)));
    return (Number(1) - t) * a + t * b;
}

template<typename Number>
constexpr inline bool is_approx(Number value, Number test_value, Number precision = EPSILON)
{
    return std::fabs(double(value) - double(test_value)) < double(precision);
}

template<typename Number>
constexpr inline bool is_approx(const std::optional<Number> &value, const std::optional<Number> &test_value)
{
    return (!value.has_value() && !test_value.has_value()) ||
           (value.has_value() && test_value.has_value() && is_approx<Number>(*value, *test_value));
}

// A meta-predicate which is true for integers wider than or equal to coord_t
template<class I>
struct is_scaled_coord
{
    static const constexpr bool value = std::is_integral<I>::value &&
                                        std::numeric_limits<I>::digits >= std::numeric_limits<coord_t>::digits;
};

// Meta predicates for floating, 'scaled coord' and generic arithmetic types
// Can be used to restrict templates to work for only the specified set of types.
// parameter T is the type we want to restrict
// parameter O (Optional defaults to T) is the type that the whole expression
// will be evaluated to.
// e.g. template<class T> FloatingOnly<T, bool> is_nan(T val);
// The whole template will be defined only for floating point types and the
// return type will be bool.
// For more info how to use, see docs for std::enable_if
//
template<class T, class O = T>
using FloatingOnly = std::enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class O = T>
using ScaledCoordOnly = std::enable_if_t<is_scaled_coord<T>::value, O>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

template<class T, class O = T>
using ArithmeticOnly = std::enable_if_t<std::is_arithmetic<T>::value, O>;

template<class T, class O = T>
using IteratorOnly = std::enable_if_t<!std::is_same_v<typename std::iterator_traits<T>::value_type, void>, O>;

template<class T, class I, class... Args> // Arbitrary allocator can be used
IntegerOnly<I, std::vector<T, Args...>> reserve_vector(I capacity)
{
    std::vector<T, Args...> ret;
    if (capacity > I(0))
        ret.reserve(size_t(capacity));

    return ret;
}

// Borrowed from C++20
template<class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

namespace detail_strip_ref_wrappers
{
template<class T>
struct StripCVRef_
{
    using type = remove_cvref_t<T>;
};
template<class T>
struct StripCVRef_<std::reference_wrapper<T>>
{
    using type = std::remove_cv_t<T>;
};
} // namespace detail_strip_ref_wrappers

// Removes reference wrappers as well
template<class T>
using StripCVRef = typename detail_strip_ref_wrappers::StripCVRef_<remove_cvref_t<T>>::type;

// A very simple range concept implementation with iterator-like objects.
// This should be replaced by std::ranges::subrange (C++20)
template<class It>
class Range
{
    It from, to;

public:
    // The class is ready for range based for loops.
    It begin() const { return from; }
    It end() const { return to; }

    // The iterator type can be obtained this way.
    using iterator = It;
    using value_type = typename std::iterator_traits<It>::value_type;

    Range() = default;
    Range(It b, It e) : from(std::move(b)), to(std::move(e)) {}

    // Some useful container-like methods...
    inline size_t size() const { return std::distance(from, to); }
    inline bool empty() const { return from == to; }
};

template<class Cont>
auto range(Cont &&cont)
{
    return Range{std::begin(cont), std::end(cont)};
}

template<class Cont>
auto crange(Cont &&cont)
{
    return Range{std::cbegin(cont), std::cend(cont)};
}

template<class IntType = int, class = IntegerOnly<IntType, void>>
class IntIterator
{
    IntType m_val;

public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = IntType;
    using pointer = IntType *;   // or also value_type*
    using reference = IntType &; // or also value_type&

    IntIterator(IntType v) : m_val{v} {}

    IntIterator &operator++()
    {
        ++m_val;
        return *this;
    }
    IntIterator operator++(int)
    {
        auto cpy = *this;
        ++m_val;
        return cpy;
    }
    IntIterator &operator--()
    {
        --m_val;
        return *this;
    }
    IntIterator operator--(int)
    {
        auto cpy = *this;
        --m_val;
        return cpy;
    }

    IntType operator*() const { return m_val; }
    IntType operator->() const { return m_val; }

    bool operator==(const IntIterator &other) const { return m_val == other.m_val; }

    bool operator!=(const IntIterator &other) const { return !(*this == other); }
};

template<class IntType, class = IntegerOnly<IntType>>
auto range(IntType from, IntType to)
{
    return Range{IntIterator{from}, IntIterator{to}};
}

template<class T, class = FloatingOnly<T>>
constexpr T NaN = std::numeric_limits<T>::quiet_NaN();

constexpr float NaNf = NaN<float>;
constexpr double NaNd = NaN<double>;

// Rounding up.
// 1.5 is rounded to 2
// 1.49 is rounded to 1
// 0.5 is rounded to 1,
// 0.49 is rounded to 0
// -0.5 is rounded to 0,
// -0.51 is rounded to -1,
// -1.5 is rounded to -1.
// -1.51 is rounded to -2.
// If input is not a valid float (it is infinity NaN or if it does not fit)
// the float to int conversion produces a max int on Intel and +-max int on ARM.
template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

template<class T>
using SamePair = std::pair<T, T>;

// Helper to be used in static_assert.
template<class T>
struct always_false
{
    enum
    {
        value = false
    };
};

// Map a generic function to each argument following the mapping function
template<class Fn, class... Args>
Fn for_each_argument(Fn &&fn, Args &&...args)
{
    // see https://www.fluentcpp.com/2019/03/05/for_each_arg-applying-a-function-to-each-argument-of-a-function-in-cpp/
    (fn(std::forward<Args>(args)), ...);

    return fn;
}

// Call fn on each element of the input tuple tup.
template<class Fn, class Tup>
Fn for_each_in_tuple(Fn fn, Tup &&tup)
{
    auto mpfn = [&fn](auto &...pack)
    {
        for_each_argument(fn, pack...);
    };

    std::apply(mpfn, tup);

    return fn;
}

template<typename T>
inline bool is_in_range(const T &value, const T &low, const T &high)
{
    return low <= value && value <= high;
}

} // namespace Luminary
