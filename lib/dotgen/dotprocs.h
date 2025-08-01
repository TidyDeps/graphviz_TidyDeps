/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <dotgen/aspect.h>
#include <stdbool.h>
#include <util/list.h>

DEFINE_LIST(node_queue, Agnode_t *)

    extern void acyclic(Agraph_t *);
    extern void allocate_ranks(Agraph_t *);
    /// @return 0 on success
    extern int build_ranks(Agraph_t *, int);
    extern void build_skeleton(Agraph_t *, Agraph_t *);
    extern void checkLabelOrder (graph_t* g);
    extern void class1(Agraph_t *);
    extern void class2(Agraph_t *);
    extern void decompose(Agraph_t *, int);
    extern void delete_fast_edge(Agedge_t *);
    extern void delete_fast_node(Agraph_t *, Agnode_t *);
    extern void delete_flat_edge(Agedge_t *);
    extern void dot_cleanup(graph_t * g);
    extern void dot_layout(Agraph_t * g);
    extern void dot_init_node_edge(graph_t * g);
    extern void dot_scan_ranks(graph_t * g);
    extern void enqueue_neighbors(node_queue_t *q, node_t *n0, int pass);
    /// @return 0 on success
    extern int expand_cluster(Agraph_t *);
    extern Agedge_t *fast_edge(Agedge_t *);
    extern void fast_node(Agraph_t *, Agnode_t *);
    extern Agedge_t *find_fast_edge(Agnode_t *, Agnode_t *);
    extern Agedge_t *find_flat_edge(Agnode_t *, Agnode_t *);
    extern void flat_edge(Agraph_t *, Agedge_t *);
    extern int flat_edges(Agraph_t *);
    /// @return 0 on success
    extern int install_cluster(Agraph_t *, Agnode_t *, int, node_queue_t *);
    /// @return 0 on success
    extern int install_in_rank(Agraph_t *, Agnode_t *);
    extern bool is_cluster(Agraph_t *);
    extern void dot_compoundEdges(Agraph_t *);
    extern Agedge_t *make_aux_edge(Agnode_t *, Agnode_t *, double, int);
    extern void mark_clusters(Agraph_t *);
    extern void mark_lowclusters(Agraph_t *);
    extern bool mergeable(edge_t *e, edge_t *f);
    extern void merge_chain(Agraph_t*, Agedge_t*, Agedge_t*, bool);
    extern void merge_oneway(Agedge_t *, Agedge_t *);
    extern Agedge_t *new_virtual_edge(Agnode_t *, Agnode_t *, Agedge_t *);
    extern bool nonconstraint_edge(Agedge_t *);
    extern void other_edge(Agedge_t *);
    extern void rank1(graph_t * g);
    extern int portcmp(port p0, port p1);
    extern int ports_eq(edge_t *, edge_t *);
    extern void rec_reset_vlists(Agraph_t *);
    extern void rec_save_vlists(Agraph_t *);
    extern void reverse_edge(Agedge_t *);
    extern void safe_other_edge(Agedge_t *);
    extern void save_vlist(Agraph_t *);
    extern Agedge_t *virtual_edge(Agnode_t *, Agnode_t *, Agedge_t *);
    extern Agnode_t *virtual_node(Agraph_t *);
    extern void virtual_weight(Agedge_t *);
    extern void zapinlist(elist *, Agedge_t *);

    extern Agraph_t* dot_root(void *);
    extern void dot_concentrate(Agraph_t *);
    /// @return 0 on success
    extern int dot_mincross(Agraph_t *);
    extern void dot_position(Agraph_t *);
    extern void dot_rank(Agraph_t *);
    extern void dot_sameports(Agraph_t *);
    /// @return 0 on success
    extern int dot_splines(Agraph_t *);

#ifdef __cplusplus
}
#endif
