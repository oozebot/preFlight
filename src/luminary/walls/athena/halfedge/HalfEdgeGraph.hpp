///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2020 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#pragma once

#include <list>
#include <cassert>

#include "HalfEdge.hpp"
#include "HalfEdgeNode.hpp"

namespace Luminary::Athena
{
template<class node_data_t, class edge_data_t, class derived_node_t,
         class derived_edge_t> // types of data contained in nodes and edges
class HalfEdgeGraph
{
public:
    using edge_t = derived_edge_t;
    using node_t = derived_node_t;
    using Edges = std::list<edge_t>;
    using Nodes = std::list<node_t>;
    Edges edges;
    Nodes nodes;
};

} // namespace Luminary::Athena
