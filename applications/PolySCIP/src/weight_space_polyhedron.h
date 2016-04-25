/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*        This file is part of the program PolySCIP                          */
/*                                                                           */
/*    Copyright (C) 2012-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  PolySCIP is distributed under the terms of the ZIB Academic License.     */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with PolySCIP; see the file LICENCE.                               */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/** @brief  The (partial) weight space polyhedron
 *
 * This class represents the (partial) weight space polyhedron P =
 * {(w,a) \in \Lambda \times R : w \cdot y >= a \forall y \in Y'}
 * where Y' is the set of non-dominated points computed so far and
 * \Lambda is the set of normalized weights
 */

#ifndef POLYSCIP_SRC_WEIGHT_SPACE_POLYHEDRON_H_INCLUDED 
#define POLYSCIP_SRC_WEIGHT_SPACE_POLYHEDRON_H_INCLUDED 

#include <list>
#include <memory> // std::shared_ptr
#include <tuple>
#include <unordered_map>
#include <utility> // std::pair
#include <vector>

#undef GCC_VERSION /* lemon/core.h redefines GCC_VERSION additionally to scip/def.h */
#include "lemon/list_graph.h"

#include "polyscip.h"
#include "weight_space_facet.h"
#include "weight_space_vertex.h"

namespace polyscip {

  /** 1-skeleton of the (partial) weight space polyhedron. */
  class WeightSpacePolyhedron {
  public:
    /** Container used to store the marked weight space vertices
     *  Needs to support: push_back, size() 
     */
    using MarkedVertexContainer = std::vector<std::shared_ptr<WeightSpaceVertex>>;

    /** Container used to store the unmarked weight space vertices
     *  Needs to support: empty(), size()
     */
    using UnmarkedVertexContainer = std::list<std::shared_ptr<WeightSpaceVertex>>; 

    using RayInfoType = std::pair<std::shared_ptr<Polyscip::RayType>, 
				  std::shared_ptr<Polyscip::WeightType>>;
    
    /** Creates the skeleton of the initial (partial) weight
	space polyhedron P = {(w,a) \in \Lambda \times R : w \cdot y^1
	>= a}
	@param num_objs number of objectives of given problem
	@param point first computed (weakly non-dominated) point
	@param point_weighted_obj_val weighted objective value of first point
	@param unit_weight_info pair indicating whether first point was computed by using 
	a unit weight; if unit_weight_info.first is true, then unit_weight_info.second contains the
	index with value 1; note: first index is 0
    */
    explicit WeightSpacePolyhedron(unsigned num_objs,
				   Polyscip::PointType point, 
				   Polyscip::ValueType point_weighted_obj_val,
				   std::pair<bool,Polyscip::WeightType::size_type> unit_weight_info);

    /** Destructor */
    ~WeightSpacePolyhedron();
  
    /** Checks whether there is an unmarked weight space vertex
     *  @return true if there is an unmarked weight space vertex; false otherwise
     */
    bool hasUnmarkedVertex() const;

    /** Returns an unmarked weight space vertex
     *  @return an untested weight space vertex
     */
    std::unique_ptr<WeightSpaceVertex> getUnmarkedVertex() = delete;

    /** Incorporates a newly found non-dominated point into the
     * (partial) weight space polyhedron 
     *  @param old_vertex the vertex (yielding weight and weight
     * objective value) that was considered in last computation 
     *  @param new_point the newly found non-dominated point that was 
     * computed by considering the weight and weighted objective 
     * value given by old_vertex
     */
    void addNondomPoint(std::shared_ptr<WeightSpaceVertex> old_vertex, 
			std::shared_ptr<Polyscip::PointType> new_point) = delete;

    /** Incorporates an newly found unbounded non-dominated ray
     * into the (partial) weight space polyhedron
     *  @param old_vertex the vertex (yielding weight and weight
     * objective value) that was considered in last computation 
     *  @param new_ray the newly found non-dominated ray that was 
     * computed by considering the weight and weighted objective 
     * value given by old_vertex
     */
    void addNondomRay(std::shared_ptr<WeightSpaceVertex> old_vertex,
		      std::shared_ptr<Polyscip::RayType> new_ray) = delete;

    /** Prints unmarked vertices to standard output 
     *  @param printFacets if true, facet information of unmarked vertices is also printed
     */
    void printUnmarkedVertices(bool printFacets) const;
    
    /** Print marked vertices to standard output 
     *  @param printFacets if true, facet information of marked vertices is also printed
     */
    void printMarkedVertices(bool printFacets) const;

  private:
    using Graph = lemon::ListGraph;
    using Node = Graph::Node;
    using NodeMap = Graph::NodeMap<std::shared_ptr<const WeightSpaceVertex>>;
    using VertexMap = std::unordered_map<std::shared_ptr<const WeightSpaceVertex>, Node>;

    /** Creates initial weight space vertices
     *  @param num_objs number of objectives of given problem
     *  @param point first computed (weakly non-dominated) point
     *	@param weighted_obj_val weighted objective value of given point
     *  @param boundary_facets initial boundary facets of the weight space polyhedron
    */
    void createInitialVertices(unsigned num_objs,
			       Polyscip::PointType point,
			       Polyscip::ValueType weighted_obj_val,
			       WeightSpaceVertex::FacetContainer boundary_facets);

    /** Creates initial 1-skeleton of complete graph with number of
	objectives many vertices
     */
    void createInitialSkeleton();

    /** Makes unmarked vertex with unit weight (1 in unit weight is at
     *  unit_weight_index) an marked vertex 
     *  @param unit_weight_index index of 1 in unit weight
     */
    void setMarkedVertex(Polyscip::WeightType::size_type unit_weight_index);

    /** Incorporates values of computed rays and updates weight values of weight space vertices
     *  @param computed_rays container with computed rays
     *  @param zero value used to compare whether some values are strictly less than zero
     */
    void updateVertexWeightsWithRayInfo(const Polyscip::RayContainer& computed_rays,
					double zero = 0.) = delete;

    MarkedVertexContainer marked_vertices_;      /**< all marked weight space vertices */
    UnmarkedVertexContainer unmarked_vertices_;  /**< all unmarked weight space vertices  */
    Graph skeleton_;                             /**< 1-skeleton of the weight space polyhedron */
    NodeMap nodes_to_vertices_;                  /**< maps nodes to vertices */
    VertexMap vertices_to_nodes_;                /**< maps vertices to nodes */
    
  };

}

#endif // POLYSCIP_SRC_WEIGHT_SPACE_POLYHEDRON_H_INCLUDED 
