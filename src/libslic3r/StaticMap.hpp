///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef PREFLIGHT_STATICMAP_HPP
#define PREFLIGHT_STATICMAP_HPP

#include <optional>
#include <array>
#include <string_view>
#include <functional>
#include <stdexcept>

namespace Slic3r
{

// This module provides std::map and std::set like structures with fixed number
// of elements and usable at compile or in time constexpr contexts without
// any memory allocations.

// C++20 emulation utilities to get the missing constexpr functionality in C++17
namespace static_set_detail
{

// Simple bubble sort but constexpr
template<class T, size_t N, class Cmp = std::less<T>>
constexpr void sort_array(std::array<T, N> &arr, Cmp cmp = {})
{
    // A bubble sort will do the job, C++20 will have constexpr std::sort
    for (size_t i = 0; i < N - 1; ++i)
    {
        for (size_t j = 0; j < N - i - 1; ++j)
        {
            if (!cmp(arr[j], arr[j + 1]))
            {
                T temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

// Simple emulation of lower_bound with constexpr
template<class It, class V, class Cmp = std::less<V>>
constexpr auto array_lower_bound(It from, It to, const V &val, Cmp cmp)
{
    auto N = std::distance(from, to);
    std::size_t middle = N / 2;

    if (N == 0)
    {
        return from; // Key not found, return the beginning of the array
    }
    else if (cmp(val, *(from + middle)))
    {
        return array_lower_bound(from, from + middle, val, cmp);
    }
    else if (cmp(*(from + middle), val))
    {
        return array_lower_bound(from + middle + 1, to, val, cmp);
    }
    else
    {
        return from + middle; // Key found, return an iterator to it
    }

    return to;
}

template<class T, size_t N, class Cmp = std::less<T>>
constexpr auto array_lower_bound(const std::array<T, N> &arr, const T &val, Cmp cmp = {})
{
    return array_lower_bound(arr.begin(), arr.end(), val, cmp);
}

template<class T, std::size_t N, std::size_t... I>
constexpr std::array<std::remove_cv_t<T>, N> to_array_impl(T (&a)[N], std::index_sequence<I...>)
{
    return {{a[I]...}};
}

template<class T, std::size_t N>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N])
{
    return to_array_impl(a, std::make_index_sequence<N>{});
}

// Emulating constexpr std::pair
template<class K, class V>
struct StaticMapElement
{
    using KeyType = K;

    constexpr StaticMapElement(const K &k, const V &v) : first{k}, second{v} {}

    K first;
    V second;
};

} // namespace static_set_detail
} // namespace Slic3r

namespace Slic3r
{

// std::set like set structure
template<class T, size_t N, class Cmp = std::less<T>>
class StaticSet
{
    std::array<T, N> m_vals; // building on top of std::array
    Cmp m_cmp;

public:
    using value_type = T;

    constexpr StaticSet(const std::array<T, N> &arr, Cmp cmp = {}) : m_vals{arr}, m_cmp{cmp}
    {
        // TODO: C++20 can use std::sort(vals.begin(), vals.end())
        static_set_detail::sort_array(m_vals, m_cmp);
    }

    template<class... Ts>
    constexpr StaticSet(Ts &&...args) : m_vals{std::forward<Ts>(args)...}
    {
        static_set_detail::sort_array(m_vals, m_cmp);
    }

    template<class... Ts>
    constexpr StaticSet(Cmp cmp, Ts &&...args) : m_vals{std::forward<Ts>(args)...}, m_cmp{cmp}
    {
        static_set_detail::sort_array(m_vals, m_cmp);
    }

    constexpr auto find(const T &val) const
    {
        // TODO: C++20 can use std::lower_bound
        auto it = static_set_detail::array_lower_bound(m_vals, val, m_cmp);
        if (it != m_vals.end() && !m_cmp(*it, val) && !m_cmp(val, *it))
            return it;

        return m_vals.cend();
    }

    constexpr bool empty() const { return m_vals.empty(); }
    constexpr size_t size() const { return m_vals.size(); }

    // Can be iterated over
    constexpr auto begin() const { return m_vals.begin(); }
    constexpr auto end() const { return m_vals.end(); }
};

// These are "deduction guides", a C++17 feature.
// Reason is to be able to deduce template arguments from constructor arguments
// e.g.: StaticSet{1, 2, 3} is deduced as StaticSet<int, 3, std::less<int>>, no
// need to state the template types explicitly.
template<class T, class... Vals>
StaticSet(T, Vals...) -> StaticSet<std::enable_if_t<(std::is_same_v<T, Vals> && ...), T>, 1 + sizeof...(Vals)>;

// Same as above, only with the first argument being a comparison functor
template<class Cmp, class T, class... Vals>
StaticSet(Cmp, T, Vals...) -> StaticSet<std::enable_if_t<(std::is_same_v<T, Vals> && ...), T>, 1 + sizeof...(Vals),
                                        std::enable_if_t<std::is_invocable_r_v<bool, Cmp, T, T>, Cmp>>;

// Specialization for the empty set case.
template<class T>
class StaticSet<T, size_t{0}>
{
public:
    constexpr StaticSet() = default;
    constexpr auto find(const T &val) const { return nullptr; }
    constexpr bool empty() const { return true; }
    constexpr size_t size() const { return 0; }
    constexpr auto begin() const { return nullptr; }
    constexpr auto end() const { return nullptr; }
};

// Constructor with no arguments need to be deduced as the specialization for
// empty sets (see above)
StaticSet() -> StaticSet<int, 0>;

// StaticMap definition:

template<class K, class V>
using SMapEl = static_set_detail::StaticMapElement<K, V>;

template<class K, class V>
struct DefaultCmp
{
    constexpr bool operator()(const SMapEl<K, V> &el1, const SMapEl<K, V> &el2) const
    {
        return std::less<K>{}(el1.first, el2.first);
    }
};

// Overriding the default comparison for C style strings, as std::less<const char*>
// doesn't do the lexicographic comparisons, only the pointer values would be
// compared. Fortunately we can wrap the C style strings with string_views and
// do the comparison with those.
template<class V>
struct DefaultCmp<const char *, V>
{
    constexpr bool operator()(const SMapEl<const char *, V> &el1, const SMapEl<const char *, V> &el2) const
    {
        return std::string_view{el1.first} < std::string_view{el2.first};
    }
};

template<class K, class V, size_t N, class Cmp = DefaultCmp<K, V>>
class StaticMap
{
    std::array<SMapEl<K, V>, N> m_vals;
    Cmp m_cmp;

public:
    using value_type = SMapEl<K, V>;

    constexpr StaticMap(const std::array<SMapEl<K, V>, N> &arr, Cmp cmp = {}) : m_vals{arr}, m_cmp{cmp}
    {
        static_set_detail::sort_array(m_vals, cmp);
    }

    constexpr auto find(const K &key) const
    {
        auto ret = m_vals.end();

        SMapEl<K, V> vkey{key, V{}};

        auto it = static_set_detail::array_lower_bound(std::begin(m_vals), std::end(m_vals), vkey, m_cmp);

        if (it != std::end(m_vals) && !m_cmp(*it, vkey) && !m_cmp(vkey, *it))
            ret = it;

        return ret;
    }

    constexpr const V &at(const K &key) const
    {
        if (auto it = find(key); it != end())
            return it->second;

        throw std::out_of_range{"No such element"};
    }

    constexpr bool empty() const { return m_vals.empty(); }
    constexpr size_t size() const { return m_vals.size(); }

    constexpr auto begin() const { return m_vals.begin(); }
    constexpr auto end() const { return m_vals.end(); }
};

template<class K, class V>
class StaticMap<K, V, size_t{0}>
{
public:
    constexpr StaticMap() = default;
    constexpr auto find(const K &key) const { return nullptr; }
    constexpr bool empty() const { return true; }
    constexpr size_t size() const { return 0; }
    [[noreturn]] constexpr const V &at(const K &) const { throw std::out_of_range{"Map is empty"}; }
    constexpr auto begin() const { return nullptr; }
    constexpr auto end() const { return nullptr; }
};

// Deducing template arguments from the StaticMap constructors is not easy,
// so there is a helper "make" function to be used instead:
// e.g.: auto map = make_staticmap<const char*, int>({ {"one", 1}, {"two", 2}})
// will work, and only the key and value type needs to be specified. No need
// to state the number of elements, that is deduced automatically.
template<class K, class V, size_t N>
constexpr auto make_staticmap(const SMapEl<K, V> (&arr)[N])
{
    return StaticMap<K, V, N>{static_set_detail ::to_array(arr), DefaultCmp<K, V>{}};
}

template<class K, class V, size_t N, class Cmp>
constexpr auto make_staticmap(const SMapEl<K, V> (&arr)[N], Cmp cmp)
{
    return StaticMap<K, V, N, Cmp>{static_set_detail ::to_array(arr), cmp};
}

// Override for empty maps
template<class K, class V, class Cmp = DefaultCmp<K, V>>
constexpr auto make_staticmap()
{
    return StaticMap<K, V, 0, Cmp>{};
}

// Override which uses a c++ array as the initializer
template<class K, class V, size_t N, class Cmp = DefaultCmp<K, V>>
constexpr auto make_staticmap(const std::array<SMapEl<K, V>, N> &arr, Cmp cmp = {})
{
    return StaticMap<K, V, N, Cmp>{arr, cmp};
}

// Helper function to get a specific element from a set, returning a std::optional
// which is more convinient than working with iterators
template<class V, size_t N, class Cmp, class T>
constexpr std::enable_if_t<std::is_convertible_v<T, V>, std::optional<V>> query(const StaticSet<V, N, Cmp> &sset,
                                                                                const T &val)
{
    std::optional<V> ret;
    if (auto it = sset.find(val); it != sset.end())
        ret = *it;

    return ret;
}

template<class K, class V, size_t N, class Cmp, class KeyT>
constexpr std::enable_if_t<std::is_convertible_v<K, KeyT>, std::optional<V>> query(const StaticMap<K, V, N, Cmp> &sset,
                                                                                   const KeyT &val)
{
    std::optional<V> ret;

    if (auto it = sset.find(val); it != sset.end())
        ret = it->second;

    return ret;
}

template<class V, size_t N, class Cmp, class T>
constexpr std::enable_if_t<std::is_convertible_v<T, V>, bool> contains(const StaticSet<V, N, Cmp> &sset, const T &val)
{
    return sset.find(val) != sset.end();
}

template<class K, class V, size_t N, class Cmp, class KeyT>
constexpr std::enable_if_t<std::is_convertible_v<K, KeyT>, bool> contains(const StaticMap<K, V, N, Cmp> &smap,
                                                                          const KeyT &key)
{
    return smap.find(key) != smap.end();
}

} // namespace Slic3r

#endif // STATICMAP_HPP
