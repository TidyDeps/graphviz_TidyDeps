/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

///////////////////////////////////////
//                                   // 
// This file contains the functions  //
// for constructing and managing the //
// hierarchy structure               //
//                                   // 
///////////////////////////////////////

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <common/arith.h>
#include <topfish/hierarchy.h>
#include <util/alloc.h>

/////////////////////////
// Some utilities for  //
// 'maxmatch(..)'      //
/////////////////////////

static double
unweighted_common_fraction(v_data * graph, int v, int u, float *v_vector)
{
// returns: |N(v) & N(u)| / |N(v) or N(u)|
// v_vector[i]>0 <==> i is neighbor of v or is v itself

    int neighbor;
    int num_shared_neighbors = 0;
    int j;
    for (j = 0; j < graph[u].nedges; j++) {
	neighbor = graph[u].edges[j];
	if (v_vector[neighbor] > 0) {
	    // a shared neighobr
	    num_shared_neighbors++;
	}
    }
    // parallel to the weighted version:
    //return 2*num_shared_neighbors/(graph[v].nedges+graph[u].nedges);  

    // more natural
    return ((double) num_shared_neighbors) / (graph[v].nedges +
					      graph[u].nedges -
					      num_shared_neighbors);
}

static void fill_neighbors_vec(v_data *graph, int vtx, float *vtx_vec) {
    int j;
    if (graph[0].ewgts != NULL) {
	for (j = 0; j < graph[vtx].nedges; j++) {
	    vtx_vec[graph[vtx].edges[j]] = fabsf(graph[vtx].ewgts[j]);	// use fabsf for the self loop
	}
    } else {
	for (j = 0; j < graph[vtx].nedges; j++) {
	    vtx_vec[graph[vtx].edges[j]] = 1;
	}
    }
}

static void
fill_neighbors_vec_unweighted(v_data * graph, int vtx, float *vtx_vec)
{
    // a node is a neighbor of itself!
    int j;
    for (j = 0; j < graph[vtx].nedges; j++) {
	vtx_vec[graph[vtx].edges[j]] = 1;
    }
}

static void empty_neighbors_vec(v_data * graph, int vtx, float *vtx_vec)
{
    int j;
    for (j = 0; j < graph[vtx].nedges; j++) {
	vtx_vec[graph[vtx].edges[j]] = 0;
    }
}


static int dist3(v_data * graph, int node1, int node2)
{
// succeeds if the graph theoretic distance between the nodes is no more than 3
    int i, j, k;
    int u, v;
    for (i = 1; i < graph[node1].nedges; i++) {
	u = graph[node1].edges[i];
	if (u == node2) {
	    return 1;
	}
	for (j = 1; j < graph[u].nedges; j++) {
	    v = graph[u].edges[j];
	    if (v == node2) {
		return 1;
	    }
	    for (k = 1; k < graph[v].nedges; k++) {
		if (graph[v].edges[k] == node2) {
		    return 1;
		}
	    }
	}
    }
    return 0;
}

#define A 1.0
#define B 1.0
#define C 3.0
#define D 1.0

static double ddist(ex_vtx_data * geom_graph, int v, int u)
{
// Euclidean distance between nodes 'v' and 'u'
    double x_v = geom_graph[v].x_coord, y_v = geom_graph[v].y_coord,
	x_u = geom_graph[u].x_coord, y_u = geom_graph[u].y_coord;

    return hypot(x_v - x_u, y_v - y_u);
}

extern void quicksort_place(double *, int *, size_t size);

static int 
maxmatch(v_data * graph,	/* array of vtx data for graph */
	ex_vtx_data * geom_graph,	/* array of vtx data for graph */
	int nvtxs,	/* number of vertices in graph */
	int *mflag,	/* flag indicating vtx selected or not */
	bool dist2_limit
    )
/* 
    Compute a matching of the nodes set. 
    The matching is not based only on the edge list of 'graph', 
    which might be too small, 
    but on the wider edge list of 'geom_graph' (which includes 'graph''s edges)

    We match nodes that are close both in the graph-theoretical sense and 
    in the geometry sense  (in the layout)
*/
{
    int *order;			/* random ordering of vertices */
    int *iptr, *jptr;		/* loops through integer arrays */
    int vtx;			/* vertex to process next */
    int neighbor;		/* neighbor of a vertex */
    int nmerged = 0;		/* number of edges in matching */
    int i, j;			/* loop counters */
    float max_norm_edge_weight;
    double inv_size;
    double *matchability = gv_calloc(nvtxs, sizeof(double));
    double min_edge_len;
    double closest_val = -1, val;
    int closest_neighbor;
    float *vtx_vec = gv_calloc(nvtxs, sizeof(float));
    float *weighted_vtx_vec = gv_calloc(nvtxs, sizeof(float));

    // gather statistics, to enable normalizing the values
    double avg_edge_len = 0, avg_deg_2 = 0;
    int nedges = 0;

    for (i = 0; i < nvtxs; i++) {
	avg_deg_2 += graph[i].nedges;
	for (j = 1; j < graph[i].nedges; j++) {
	    avg_edge_len += ddist(geom_graph, i, graph[i].edges[j]);
	    nedges++;
	}
    }
    avg_edge_len /= nedges;
    avg_deg_2 /= nvtxs;
    avg_deg_2 *= avg_deg_2;

    // the normalized edge weight of edge <v,u> is defined as:
    // weight(<v,u>)/sqrt(size(v)*size(u))
    // Now we compute the maximal normalized weight
    if (graph[0].ewgts != NULL) {
	max_norm_edge_weight = -1;
	for (i = 0; i < nvtxs; i++) {
	    inv_size = sqrt(1.0 / geom_graph[i].size);
	    for (j = 1; j < graph[i].nedges; j++) {
		if (graph[i].ewgts[j] * inv_size /
		    sqrt((float) geom_graph[graph[i].edges[j]].size) >
		    max_norm_edge_weight) {
		    max_norm_edge_weight =
			(float) (graph[i].ewgts[j] * inv_size /
				 sqrt((double)
				      geom_graph[graph[i].edges[j]].size));
		}
	    }
	}
    } else {
	max_norm_edge_weight = 1;
    }

    /* Now determine the order of the vertices. */
    iptr = order = gv_calloc(nvtxs, sizeof(int));
    jptr = mflag;
    for (i = 0; i < nvtxs; i++) {
	*(iptr++) = i;
	*(jptr++) = -1;
    }

    // Option 2: sort the nodes beginning with the ones highly approriate for matching

#ifdef DEBUG
    srand(0);
#endif
    for (i = 0; i < nvtxs; i++) {
	vtx = order[i];
	matchability[vtx] = graph[vtx].nedges;	// we less want to match high degree nodes
	matchability[vtx] += geom_graph[vtx].size;	// we less want to match large sized nodes
	min_edge_len = 1e99;
	for (j = 1; j < graph[vtx].nedges; j++) {
	    min_edge_len =
		MIN(min_edge_len,
		    ddist(geom_graph, vtx,
			 graph[vtx].edges[j]) / avg_edge_len);
	}
	matchability[vtx] += min_edge_len;	// we less want to match distant nodes
	matchability[vtx] += ((double) rand()) / RAND_MAX;	// add some randomness 
    }
    quicksort_place(matchability, order, nvtxs);
    free(matchability);

    // Start determining the matched pairs
    for (i = 0; i < nvtxs; i++) {
	vtx_vec[i] = 0;
    }
    for (i = 0; i < nvtxs; i++) {
	weighted_vtx_vec[i] = 0;
    }

    // relative weights of the different criteria
    for (i = 0; i < nvtxs; i++) {
	vtx = order[i];
	if (mflag[vtx] >= 0) {	/*  already matched. */
	    continue;
	}
	inv_size = sqrt(1.0 / geom_graph[vtx].size);
	fill_neighbors_vec(graph, vtx, weighted_vtx_vec);
	fill_neighbors_vec_unweighted(graph, vtx, vtx_vec);
	closest_neighbor = -1;

	/*
	   We match node i with the "closest" neighbor, based on 4 criteria:
	   (1) (Weighted) fraction of common neighbors  (measured on orig. graph)
	   (2) AvgDeg*AvgDeg/(deg(vtx)*deg(neighbor)) (degrees measured on orig. graph)
	   (3) AvgEdgeLen/dist(vtx,neighbor)
	   (4) Weight of normalized direct connection between nodes (measured on orig. graph)
	 */

	for (j = 1; j < geom_graph[vtx].nedges; j++) {
	    neighbor = geom_graph[vtx].edges[j];
	    if (mflag[neighbor] >= 0) {	/*  already matched. */
		continue;
	    }
	    // (1): 
	    val =
		A * unweighted_common_fraction(graph, vtx, neighbor,
					       vtx_vec);

	    if (val == 0 && (dist2_limit || !dist3(graph, vtx, neighbor))) {
		// graph theoretical distance is larger than 3 (or 2 if '!dist3(graph, vtx, neighbor)' is commented)
		// nodes cannot be matched
		continue;
	    }
	    // (2)
	    val +=
		B * avg_deg_2 / (graph[vtx].nedges *
				 graph[neighbor].nedges);


	    // (3)
	    val += C * avg_edge_len / ddist(geom_graph, vtx, neighbor);

	    // (4)
	    val +=
		(weighted_vtx_vec[neighbor] * inv_size /
		 sqrt((float) geom_graph[neighbor].size)) /
		max_norm_edge_weight;



	    if (val > closest_val || closest_neighbor == -1) {
		closest_neighbor = neighbor;
		closest_val = val;
	    }

	}
	if (closest_neighbor != -1) {
	    mflag[vtx] = closest_neighbor;
	    mflag[closest_neighbor] = vtx;
	    nmerged++;
	}
	empty_neighbors_vec(graph, vtx, vtx_vec);
	empty_neighbors_vec(graph, vtx, weighted_vtx_vec);
    }

    free(order);
    free(vtx_vec);
    free(weighted_vtx_vec);
    return (nmerged);
}

/* Construct mapping from original graph nodes to coarsened graph nodes */
static void makev2cv(int *mflag, /* flag indicating vtx selected or not */
		     int nvtxs,	/* number of vtxs in original graph */
		     int *v2cv,	/* mapping from vtxs to coarsened vtxs */
		     int *cv2v	/* mapping from coarsened vtxs to vtxs */
    )
{
    int i, j;			/* loop counters */

    j = 0;
    for (i = 0; i < nvtxs; i++) {
	if (mflag[i] < 0) {	// unmatched node
	    v2cv[i] = j;
	    cv2v[2 * j] = i;
	    cv2v[2 * j + 1] = -1;
	    j++;
	} else if (mflag[i] > i) {	// matched node
	    v2cv[i] = j;
	    v2cv[mflag[i]] = j;
	    cv2v[2 * j] = i;
	    cv2v[2 * j + 1] = mflag[i];
	    j++;
	}
    }
}

static int make_coarse_graph(v_data * graph,	/* array of vtx data for graph */
			     int nedges,	/* number of edges in graph */
			     v_data ** cgp,	/* coarsened version of graph */
			     int cnvtxs,	/* number of vtxs in coarsened graph */
			     int *v2cv,	/* mapping from vtxs to coarsened vtxs */
			     int *cv2v	/* mapping from coarsened vtxs to vtxs */
    )
// This function takes the information about matched pairs
// and use it to contract these pairs and build a coarse graph
{
    int j, cv, v, neighbor, cv_nedges;
    int cnedges = 0;		/* number of edges in coarsened graph */
    v_data *cgraph;		/* coarsened version of graph */
    int *index = gv_calloc(cnvtxs, sizeof(int));
    float intra_weight;
    /* An upper bound on the number of coarse graph edges. */
    int maxCnedges = nedges;	// do not subtract (nvtxs-cnvtxs) because we do not contract only along edges
    int *edges;
    float *eweights;

    /* Now allocate space for the new graph.  Overeallocate and realloc later. */
    cgraph = gv_calloc(cnvtxs, sizeof(v_data));
    edges = gv_calloc(2 * maxCnedges + cnvtxs, sizeof(int));
    eweights = gv_calloc(2 * maxCnedges + cnvtxs, sizeof(float));

    if (graph[0].ewgts != NULL) {
	// use edge weights
	for (cv = 0; cv < cnvtxs; cv++) {

	    intra_weight = 0;

	    cgraph[cv].edges = edges;
	    cgraph[cv].ewgts = eweights;

	    cv_nedges = 1;
	    v = cv2v[2 * cv];
	    for (j = 1; j < graph[v].nedges; j++) {
		neighbor = v2cv[graph[v].edges[j]];
		if (neighbor == cv) {
		    intra_weight = 2 * graph[v].ewgts[j];	// count both directions of the intra-edge
		    continue;
		}
		if (index[neighbor] == 0) {	// new neighbor
		    index[neighbor] = cv_nedges;
		    cgraph[cv].edges[cv_nedges] = neighbor;
		    cgraph[cv].ewgts[cv_nedges] = graph[v].ewgts[j];
		    cv_nedges++;
		} else {
		    cgraph[cv].ewgts[index[neighbor]] += graph[v].ewgts[j];
		}
	    }

	    cgraph[cv].ewgts[0] = graph[v].ewgts[0];

	    if ((v = cv2v[2 * cv + 1]) != -1) {
		for (j = 1; j < graph[v].nedges; j++) {
		    neighbor = v2cv[graph[v].edges[j]];
		    if (neighbor == cv)
			continue;
		    if (index[neighbor] == 0) {	// new neighbor
			index[neighbor] = cv_nedges;
			cgraph[cv].edges[cv_nedges] = neighbor;
			cgraph[cv].ewgts[cv_nedges] = graph[v].ewgts[j];
			cv_nedges++;
		    } else {
			cgraph[cv].ewgts[index[neighbor]] +=
			    graph[v].ewgts[j];
		    }
		}
		cgraph[cv].ewgts[0] += graph[v].ewgts[0] + intra_weight;
	    }
	    cgraph[cv].nedges = cv_nedges;
	    cgraph[cv].edges[0] = cv;
	    edges += cv_nedges;
	    eweights += cv_nedges;
	    cnedges += cv_nedges;

	    for (j = 1; j < cgraph[cv].nedges; j++)
		index[cgraph[cv].edges[j]] = 0;
	}
    } else {			// fine graph is unweighted
	int internal_weight = 0;

	for (cv = 0; cv < cnvtxs; cv++) {

	    cgraph[cv].edges = edges;
	    cgraph[cv].ewgts = eweights;

	    cv_nedges = 1;
	    v = cv2v[2 * cv];
	    for (j = 1; j < graph[v].nedges; j++) {
		neighbor = v2cv[graph[v].edges[j]];
		if (neighbor == cv) {
		    internal_weight = 2;
		    continue;
		}
		if (index[neighbor] == 0) {	// new neighbor
		    index[neighbor] = cv_nedges;
		    cgraph[cv].edges[cv_nedges] = neighbor;
		    cgraph[cv].ewgts[cv_nedges] = -1;
		    cv_nedges++;
		} else {
		    cgraph[cv].ewgts[index[neighbor]]--;
		}
	    }
	    cgraph[cv].ewgts[0] = (float) graph[v].edges[0];	// this is our trick to store the weights on the diag in an unweighted graph
	    if ((v = cv2v[2 * cv + 1]) != -1) {
		for (j = 1; j < graph[v].nedges; j++) {
		    neighbor = v2cv[graph[v].edges[j]];
		    if (neighbor == cv)
			continue;
		    if (index[neighbor] == 0) {	// new neighbor
			index[neighbor] = cv_nedges;
			cgraph[cv].edges[cv_nedges] = neighbor;
			cgraph[cv].ewgts[cv_nedges] = -1;
			cv_nedges++;
		    } else {
			cgraph[cv].ewgts[index[neighbor]]--;
		    }
		}
		// we subtract the weight of the intra-edge that was counted twice 
		cgraph[cv].ewgts[0] +=
		    (float) graph[v].edges[0] - internal_weight;
		// In a case the edge weights are defined as positive:
		//cgraph[cv].ewgts[0] += (float) graph[v].edges[0]+internal_weight; 
	    }

	    cgraph[cv].nedges = cv_nedges;
	    cgraph[cv].edges[0] = cv;
	    edges += cv_nedges;
	    eweights += cv_nedges;
	    cnedges += cv_nedges;

	    for (j = 1; j < cgraph[cv].nedges; j++)
		index[cgraph[cv].edges[j]] = 0;
	}
    }
    cnedges -= cnvtxs;
    cnedges /= 2;
    free(index);
    *cgp = cgraph;
    return cnedges;
}

static int 
make_coarse_ex_graph (
    ex_vtx_data * graph, /* array of vtx data for graph */
    int nedges,	/* number of edges in graph */
    ex_vtx_data ** cgp,	/* coarsened version of graph */
    int cnvtxs,	/* number of vtxs in coarsened graph */
    int *v2cv,	/* mapping from vtxs to coarsened vtxs */
    int *cv2v	/* mapping from coarsened vtxs to vtxs */
)
// This function takes the information about matched pairs
// and use it to contract these pairs and build a coarse ex_graph
{
    int cnedges;		/* number of edges in coarsened graph */
    ex_vtx_data *cgraph;	/* coarsened version of graph */
    int j, cv, v, neighbor, cv_nedges;
    int *index = gv_calloc(cnvtxs, sizeof(int));
    int *edges;

    /* An upper bound on the number of coarse graph edges. */
    cnedges = nedges;

    /* Now allocate space for the new graph.  Overeallocate and realloc later. */
    cgraph = gv_calloc(cnvtxs, sizeof(ex_vtx_data));
    edges = gv_calloc(2 * cnedges + cnvtxs, sizeof(int));

    for (cv = 0; cv < cnvtxs; cv++) {

	cgraph[cv].edges = edges;

	cv_nedges = 1;
	v = cv2v[2 * cv];
	for (j = 1; j < graph[v].nedges; j++) {
	    neighbor = v2cv[graph[v].edges[j]];
	    if (neighbor == cv) {
		continue;
	    }
	    if (index[neighbor] == 0) {	// new neighbor
		index[neighbor] = cv_nedges;
		cgraph[cv].edges[cv_nedges] = neighbor;
		cv_nedges++;
	    }
	}
	cgraph[cv].size = graph[v].size;
	cgraph[cv].x_coord = graph[v].x_coord;
	cgraph[cv].y_coord = graph[v].y_coord;
	if ((v = cv2v[2 * cv + 1]) != -1) {
	    for (j = 1; j < graph[v].nedges; j++) {
		neighbor = v2cv[graph[v].edges[j]];
		if (neighbor == cv)
		    continue;
		if (index[neighbor] == 0) {	// new neighbor
		    index[neighbor] = cv_nedges;
		    cgraph[cv].edges[cv_nedges] = neighbor;
		    cv_nedges++;
		}
	    }
	    // compute new coord's as a weighted average of the old ones
	    cgraph[cv].x_coord =
		(cgraph[cv].size * cgraph[cv].x_coord +
		 graph[v].size * graph[v].x_coord) / (cgraph[cv].size +
						      graph[v].size);
	    cgraph[cv].y_coord =
		(cgraph[cv].size * cgraph[cv].y_coord +
		 graph[v].size * graph[v].y_coord) / (cgraph[cv].size +
						      graph[v].size);
	    cgraph[cv].size += graph[v].size;
	}
	cgraph[cv].nedges = cv_nedges;
	cgraph[cv].edges[0] = cv;
	edges += cv_nedges;

	for (j = 1; j < cgraph[cv].nedges; j++)
	    index[cgraph[cv].edges[j]] = 0;
    }
    free(index);
    *cgp = cgraph;
    return cnedges;
}

static void 
coarsen_match (
    v_data * graph,	/* graph to be matched */
    ex_vtx_data* geom_graph, /* another graph (with coords) on the same nodes */
    int nvtxs,	/* number of vertices in graph */
    int nedges,	/* number of edges in graph */
    int geom_nedges,	/* number of edges in geom_graph */
    v_data ** cgraph,	/* coarsened version of graph */
    ex_vtx_data ** cgeom_graph,	/* coarsened version of geom_graph */
    int *cnp,	/* number of vtxs in coarsened graph */
    int *cnedges,	/* number of edges in coarsened graph */
    int *cgeom_nedges,	/* number of edges in coarsened geom_graph */
    int **v2cvp,	/* reference from vertices to coarse vertices */
    int **cv2vp,	/* reference from vertices to coarse vertices */
    bool dist2_limit
)

/*
 * This function gets two graphs with the same node set and
 * constructs two corresponding coarsened graphs of about 
 * half the size
 */
{
    int *mflag;			/* flag indicating vtx matched or not */
    int nmerged;		/* number of edges contracted */
    int *v2cv;			/* reference from vertices to coarse vertices */
    int *cv2v;			/* reference from vertices to coarse vertices */
    int cnvtxs;

    /* Allocate and initialize space. */
    mflag = gv_calloc(nvtxs, sizeof(int));

    /* Find a maximal matching in the graphs */
    nmerged = maxmatch(graph, geom_graph, nvtxs, mflag, dist2_limit);

    /* Now construct coarser graph by contracting along matching edges. */
    /* Pairs of values in mflag array indicate matched vertices. */
    /* A negative value indicates that vertex is unmatched. */

    *cnp = cnvtxs = nvtxs - nmerged;

    *v2cvp = v2cv = gv_calloc(nvtxs, sizeof(int));
    *cv2vp = cv2v = gv_calloc(2 * cnvtxs, sizeof(int));
    makev2cv(mflag, nvtxs, v2cv, cv2v);

    free(mflag);

    *cnedges = make_coarse_graph(graph, nedges, cgraph, cnvtxs, v2cv, cv2v);
    *cgeom_nedges = make_coarse_ex_graph(geom_graph, geom_nedges, cgeom_graph,
			     cnvtxs, v2cv, cv2v);
}

static v_data *cpGraph(v_data * graph, int n, int nedges)
{
    float *ewgts = NULL;
    int i, j;

    if (graph == NULL || n == 0) {
	return NULL;
    }
    v_data *cpGraph = gv_calloc(n, sizeof(v_data));
    int *edges = gv_calloc(2 * nedges + n, sizeof(int));
    if (graph[0].ewgts != NULL) {
	ewgts = gv_calloc(2 * nedges + n, sizeof(float));
    }

    for (i = 0; i < n; i++) {
	cpGraph[i] = graph[i];
	cpGraph[i].edges = edges;
	cpGraph[i].ewgts = ewgts;
	for (j = 0; j < graph[i].nedges; j++) {
	    edges[j] = graph[i].edges[j];
	}
	edges += graph[i].nedges;
	if (ewgts != NULL) {
	    for (j = 0; j < graph[i].nedges; j++) {
		ewgts[j] = graph[i].ewgts[j];
	    }
	    ewgts += graph[i].nedges;
	}
    }
    return cpGraph;
}

static ex_vtx_data *cpExGraph(ex_vtx_data * graph, int n, int nedges)
{
    int i, j;

    if (graph == NULL || n == 0) {
	return NULL;
    }
    ex_vtx_data *cpGraph = gv_calloc(n, sizeof(ex_vtx_data));
    int *edges = gv_calloc(2 * nedges + n, sizeof(int));

    for (i = 0; i < n; i++) {
	cpGraph[i] = graph[i];
	cpGraph[i].edges = edges;
	for (j = 0; j < graph[i].nedges; j++) {
	    edges[j] = graph[i].edges[j];
	}
	edges += graph[i].nedges;
    }
    return cpGraph;
}

Hierarchy *create_hierarchy(v_data *graph, int nvtxs, int nedges,
                            ex_vtx_data *geom_graph, int ngeom_edges,
                            bool dist2_limit) {
    int cur_level;
    Hierarchy *hierarchy = gv_alloc(sizeof(Hierarchy));
    int cngeom_edges = ngeom_edges;
    ex_vtx_data *geom_graph_level;
    int nodeIndex = 0;
    int i, j;
    static const int min_nvtxs = 20;
    int nlevels = MAX(5, 10 * (int) log((float) (nvtxs / min_nvtxs)));	// just an estimate

    hierarchy->graphs = gv_calloc(nlevels, sizeof(v_data*));
    hierarchy->geom_graphs = gv_calloc(nlevels, sizeof(ex_vtx_data*));
    hierarchy->nvtxs = gv_calloc(nlevels, sizeof(int));
    hierarchy->nedges = gv_calloc(nlevels, sizeof(int));
    hierarchy->v2cv = gv_calloc(nlevels, sizeof(int*));
    hierarchy->cv2v = gv_calloc(nlevels, sizeof(int*));

    hierarchy->graphs[0] = cpGraph(graph, nvtxs, nedges);
    hierarchy->geom_graphs[0] = cpExGraph(geom_graph, nvtxs, ngeom_edges);
    hierarchy->nvtxs[0] = nvtxs;
    hierarchy->nedges[0] = nedges;

    for (cur_level = 0;
	 hierarchy->nvtxs[cur_level] > min_nvtxs
	 && cur_level < 50 /*nvtxs/10 */ ; cur_level++) {
	if (cur_level == nlevels - 1) {	// we have to allocate more space
	    hierarchy->graphs =
		gv_recalloc(hierarchy->graphs, nlevels, nlevels * 2, sizeof(v_data*));
	    hierarchy->geom_graphs =
		gv_recalloc(hierarchy->geom_graphs, nlevels, nlevels * 2, sizeof(ex_vtx_data*));
	    hierarchy->nvtxs = gv_recalloc(hierarchy->nvtxs, nlevels, nlevels * 2, sizeof(int));
	    hierarchy->nedges = gv_recalloc(hierarchy->nedges, nlevels, nlevels * 2, sizeof(int));
	    hierarchy->v2cv = gv_recalloc(hierarchy->v2cv, nlevels, nlevels * 2, sizeof(int*));
	    hierarchy->cv2v = gv_recalloc(hierarchy->cv2v, nlevels, nlevels * 2, sizeof(int*));
	    nlevels *= 2;
	}

	ngeom_edges = cngeom_edges;
	coarsen_match
	    (hierarchy->graphs[cur_level],
	     hierarchy->geom_graphs[cur_level],
	     hierarchy->nvtxs[cur_level], hierarchy->nedges[cur_level],
	     ngeom_edges, &hierarchy->graphs[cur_level + 1],
	     &hierarchy->geom_graphs[cur_level + 1],
	     &hierarchy->nvtxs[cur_level + 1],
	     &hierarchy->nedges[cur_level + 1], &cngeom_edges,
	     &hierarchy->v2cv[cur_level], &hierarchy->cv2v[cur_level + 1],
	     dist2_limit);
    }

    hierarchy->nlevels = cur_level + 1;

    // assign consecutive global identifiers to all nodes on hierarchy
    for (i = 0; i < hierarchy->nlevels; i++) {
	geom_graph_level = hierarchy->geom_graphs[i];
	for (j = 0; j < hierarchy->nvtxs[i]; j++) {
	    geom_graph_level[j].globalIndex = nodeIndex;
	    nodeIndex++;
	}
    }
    hierarchy->maxNodeIndex = nodeIndex;
    return hierarchy;
}

static double
dist_from_foci(ex_vtx_data * graph, int node, int *foci, int num_foci)
{
// compute minimum distance of 'node' from the set 'foci'
    int i;
    double distance = ddist(graph, node, foci[0]);
    for (i = 1; i < num_foci; i++) {
	distance = MIN(distance, ddist(graph, node, foci[i]));
    }

    return distance;
}

/* set_active_levels:
 * Compute the "active level" field of each node in the hierarchy.
 * Note that if the active level is lower than the node's level, the node 
 * is "split" in the presentation; if the active level is higher than 
 * the node's level, then the node is aggregated into a coarser node.
 * If the active level equals the node's level then the node is currently shown
 */
void
set_active_levels(Hierarchy * hierarchy, int *foci_nodes, int num_foci,
    levelparms_t* parms)
{
    int min_level = 0;

    ex_vtx_data *graph = hierarchy->geom_graphs[min_level]; // finest graph
    int n = hierarchy->nvtxs[min_level];

    // compute distances from foci nodes
    int *nodes = gv_calloc(n, sizeof(int));
    double *distances = gv_calloc(n, sizeof(double));
    for (int i = 0; i < n; i++) {
	nodes[i] = i;
	distances[i] = dist_from_foci(graph, i, foci_nodes, num_foci);
    }

    // sort nodes according to their distance from foci
    quicksort_place(distances, nodes, n);

    /* compute *desired* levels of fine nodes by distributing them into buckets
     * The sizes of the buckets is a geometric series with 
     * factor: 'coarsening_rate'
     */
    int level = min_level;
    int group_size = parms->num_fine_nodes * num_foci;
    int thresh = group_size;
    for (int i = 0; i < n; i++) {
	const int vtx = nodes[i];
	if (i > thresh && level < hierarchy->nlevels - 1) {
	    level++;
	    group_size = (int) (group_size * parms->coarsening_rate);
	    thresh += group_size;
	}
	graph[vtx].active_level = level;
    }

    // Fine-to-coarse sweep:
    //----------------------
    // Propagate levels to all coarse nodes and determine final levels 
    // at lowest meeting points. Note that nodes can be active in 
    // lower (finer) levels than what originally desired, since if 'u' 
    // and 'v' are merged, than the active level of '{u,v}' will be 
    // the minimum of the active levels of 'u' and 'v'
    for (level = min_level + 1; level < hierarchy->nlevels; level++) {
	ex_vtx_data *const cgraph = hierarchy->geom_graphs[level];
	graph = hierarchy->geom_graphs[level - 1];
	const int *const cv2v = hierarchy->cv2v[level];
	n = hierarchy->nvtxs[level];
	for (int i = 0; i < n; i++) {
	    const int v = cv2v[2 * i];
	    const int u = cv2v[2 * i + 1];
	    if (u >= 0) {	// cv is decomposed from 2 fine nodes
		if (graph[v].active_level < level
		    || graph[u].active_level < level) {
		    // At least one of the nodes should be active at a lower level,
		    // in this case both children are active at a lower level
		    // and we don't wait till they are merged
		    graph[v].active_level =
			MIN(graph[v].active_level, level - 1);
		    graph[u].active_level =
			MIN(graph[u].active_level, level - 1);
		}
		// The node with the finer (lower) active level determines the coarse active level
		cgraph[i].active_level =
		    MIN(graph[v].active_level, graph[u].active_level);
	    } else {
		cgraph[i].active_level = graph[v].active_level;
	    }
	}
    }

    // Coarse-to-fine sweep:
    //----------------------
    // Propagate final levels all the way to fine nodes
    for (level = hierarchy->nlevels - 1; level > 0; level--) {
	ex_vtx_data *const cgraph = hierarchy->geom_graphs[level];
	graph = hierarchy->geom_graphs[level - 1];
	const int *const cv2v = hierarchy->cv2v[level];
	n = hierarchy->nvtxs[level];
	for (int i = 0; i < n; i++) {
	    if (cgraph[i].active_level < level) {
		continue;
	    }
	    // active level has been already reached, copy level to children
	    const int v = cv2v[2 * i];
	    const int u = cv2v[2 * i + 1];
	    graph[v].active_level = cgraph[i].active_level;
	    if (u >= 0) {
		graph[u].active_level = cgraph[i].active_level;
	    }
	}
    }
    free(nodes);
    free(distances);
}

/* findClosestActiveNode:
 * Given (x,y) in physical coords, check if node is closer to this point
 * than previous setting. If so, reset values.
 * If node is not active, recurse down finer levels.
 * Return closest distance squared. 
 */
static double
findClosestActiveNode(Hierarchy * hierarchy, int node,
		      int level, double x, double y,
		      double closest_dist, int *closest_node,
		      int *closest_node_level)
{
    ex_vtx_data *graph;

    graph = hierarchy->geom_graphs[level];

    if (graph[node].active_level == level)
	{	// node is active
		double delx = x - graph[node].physical_x_coord;
		double dely = y - graph[node].physical_y_coord;
		double dist = delx*delx + dely*dely;

		if (dist < closest_dist) 
		{
			closest_dist = dist;
			*closest_node = node;
			*closest_node_level = level;


		}
		return closest_dist;
    }

    closest_dist =
	findClosestActiveNode(hierarchy, hierarchy->cv2v[level][2 * node],
			      level - 1, x, y, closest_dist, closest_node,
			      closest_node_level);

    if (hierarchy->cv2v[level][2 * node + 1] >= 0) {
	closest_dist =
	    findClosestActiveNode(hierarchy,
				  hierarchy->cv2v[level][2 * node + 1],
				  level - 1, x, y, closest_dist,
				  closest_node, closest_node_level);
    }
    return closest_dist;
}

/* find_leftmost_descendant:
 * Given coarse node in given level, return representative node
 * in lower level cur_level.
 */
static int
find_leftmost_descendant(Hierarchy * hierarchy, int node, int level,
			 int cur_level)
{
    while (level > cur_level) 
	{
		node = hierarchy->cv2v[level--][2 * node];
    }
    return node;
}

/* find_closest_active_node:
 * Given x and y in physical coordinate system, determine closest
 * actual node in graph. Store this in closest_fine_node, and return
 * distance squared.
 */
double
find_closest_active_node(Hierarchy * hierarchy, double x, double y,
			 int *closest_fine_node)
{
    int i, closest_node, closest_node_level;
    int top_level = hierarchy->nlevels - 1;
    double min_dist = 1e20;

    for (i = 0; i < hierarchy->nvtxs[top_level]; i++) 
	{
		min_dist = findClosestActiveNode(hierarchy, i, top_level, x, y,min_dist, &closest_node, &closest_node_level);
    }
    *closest_fine_node =find_leftmost_descendant(hierarchy, closest_node,closest_node_level, 0);

    return min_dist;
}

int
init_ex_graph(v_data * graph1, v_data * graph2, int n,
	      double *x_coords, double *y_coords, ex_vtx_data ** gp)
{
    // build ex_graph from the union of edges in 'graph1' and 'graph2'
    // note that this function does not destroy the input graphs

    ex_vtx_data *geom_graph;
    int nedges1 = 0, nedges2 = 0;
    int nedges = 0;
    int i, j, k, l, first_nedges;
    int neighbor;
    for (i = 0; i < n; i++) {
	nedges1 += graph1[i].nedges;
	nedges2 += graph2[i].nedges;
    }
    int *edges = gv_calloc(nedges1 + nedges2, sizeof(int));
    *gp = geom_graph = gv_calloc(n, sizeof(ex_vtx_data));

    for (i = 0; i < n; i++) {
	geom_graph[i].edges = edges;
	geom_graph[i].size = 1;
	geom_graph[i].x_coord = (float) x_coords[i];
	geom_graph[i].y_coord = (float) y_coords[i];
	geom_graph[i].edges[0] = i;
	for (j = 1; j < graph1[i].nedges; j++) {
	    edges[j] = graph1[i].edges[j];
	}
	first_nedges = k = graph1[i].nedges;
	for (j = 1; j < graph2[i].nedges; j++) {
	    neighbor = graph2[i].edges[j];
	    for (l = 1; l < first_nedges; l++) {
		if (edges[l] == neighbor) {	// already existed neighbor
		    break;
		}
	    }
	    if (l == first_nedges) {	// neighbor wasn't found
		edges[k++] = neighbor;
	    }
	}
	geom_graph[i].nedges = k;
	edges += k;
	nedges += k;
    }
    nedges /= 2;
    return nedges;
}

/* extract_active_logical_coords:
 * Preorder scan the hierarchy tree, and extract the logical coordinates of 
 * all active nodes
 * Store (universal) coords in x_coords and y_coords and increment index.
 * Return index.
 */
size_t extract_active_logical_coords(Hierarchy *hierarchy, int node, int level,
                                     double *x_coords, double *y_coords,
                                     size_t counter) {

    ex_vtx_data *graph = hierarchy->geom_graphs[level];

    if (graph[node].active_level == level) {	// node is active
	x_coords[counter] = graph[node].x_coord;
	y_coords[counter++] = graph[node].y_coord;
	return counter;
    }

    counter =
	extract_active_logical_coords(hierarchy,
				      hierarchy->cv2v[level][2 * node],
				      level - 1, x_coords, y_coords,
				      counter);

    if (hierarchy->cv2v[level][2 * node + 1] >= 0) {
	counter =
	    extract_active_logical_coords(hierarchy,
					  hierarchy->cv2v[level][2 * node +
								 1],
					  level - 1, x_coords, y_coords,
					  counter);
    }
    return counter;
}

/* set_active_physical_coords:
 * Preorder scan the hierarchy tree, and set the physical coordinates 
 * of all active nodes
 */
int
set_active_physical_coords(Hierarchy * hierarchy, int node, int level,
			   double *x_coords, double *y_coords, int counter)
{

    ex_vtx_data *graph = hierarchy->geom_graphs[level];

    if (graph[node].active_level == level) {	// node is active
	graph[node].physical_x_coord = (float) x_coords[counter];
	graph[node].physical_y_coord = (float) y_coords[counter++];
	return counter;
    }

    counter =
	set_active_physical_coords(hierarchy,
				   hierarchy->cv2v[level][2*node],
				   level - 1, x_coords, y_coords, counter);

    if (hierarchy->cv2v[level][2 * node + 1] >= 0) {
	counter =
	    set_active_physical_coords(hierarchy,
				       hierarchy->cv2v[level][2*node + 1],
				       level - 1, x_coords, y_coords,
				       counter);
    }
    return counter;
}

/* find_physical_coords:
 * find the 'physical_coords' of the active-ancestor of 'node'
 */
void find_physical_coords(Hierarchy *hierarchy, int level, int node, float *x,
                          float *y) {
    int active_level = hierarchy->geom_graphs[level][node].active_level;
    while (active_level > level) {
	node = hierarchy->v2cv[level][node];
	level++;
    }

    *x = hierarchy->geom_graphs[level][node].physical_x_coord;
    *y = hierarchy->geom_graphs[level][node].physical_y_coord;
}

void
find_active_ancestor_info(Hierarchy * hierarchy, int level, int node, int *levell,int *nodee)
{
    int active_level = hierarchy->geom_graphs[level][node].active_level;
    while (active_level > level) {
	node = hierarchy->v2cv[level][node];
	level++;
    }

    *nodee = node;
    *levell = level;
}




/* find_old_physical_coords:
 * find the 'old_physical_coords' of the old active-ancestor of 'node'
 */
void find_old_physical_coords(Hierarchy *hierarchy, int level, int node,
                              float *x, float *y) {
    int active_level = hierarchy->geom_graphs[level][node].old_active_level;
    while (active_level > level) {
	node = hierarchy->v2cv[level][node];
	level++;
    }

    *x = hierarchy->geom_graphs[level][node].old_physical_x_coord;
    *y = hierarchy->geom_graphs[level][node].old_physical_y_coord;
}

/* find_active_ancestor:
 * find the 'ancestorIndex' of the active-ancestor of 'node'
 * Return negative if node's active_level < level.
 */
int
find_active_ancestor(Hierarchy * hierarchy, int level, int node)
{
    int active_level = hierarchy->geom_graphs[level][node].active_level;
    while (active_level > level) {
	node = hierarchy->v2cv[level][node];
	level++;
    }

    if (active_level == level) 
	return hierarchy->geom_graphs[level][node].globalIndex;
    else
	return -1;
}

void init_active_level(Hierarchy* hierarchy, int level) 
{
    int i,j;
    ex_vtx_data* graph;
    for (i=0; i<hierarchy->nlevels; i++) {
        graph = hierarchy->geom_graphs[i];
        for (j=0; j<hierarchy->nvtxs[i]; j++) {
            graph->active_level = level;
	    graph++;
        }
    }
}

