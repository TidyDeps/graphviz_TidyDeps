/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/* classify edges for mincross/nodepos/splines, using given ranks */

#include <dotgen/dot.h>
#include <stdbool.h>
#include <stdlib.h>
#include <util/alloc.h>
#include <util/gv_math.h>

static node_t*
label_vnode(graph_t * g, edge_t * orig)
{
    const pointf dimen = ED_label(orig)->dimen;
    node_t *const v = virtual_node(g);
    ND_label(v) = ED_label(orig);
    ND_lw(v) = GD_nodesep(agroot(v));
    if (!ED_label_ontop(orig)) {
	if (GD_flip(agroot(g))) {
	    ND_ht(v) = dimen.x;
	    ND_rw(v) = dimen.y;
	} else {
	    ND_ht(v) = dimen.y;
	    ND_rw(v) = dimen.x;
	}
    }
    return v;
}

static void 
incr_width(graph_t * g, node_t * v)
{
    int width = GD_nodesep(g) / 2;
    ND_lw(v) += width;
    ND_rw(v) += width;
}

static node_t *plain_vnode(graph_t *g) {
    node_t *const v = virtual_node(g);
    incr_width(g, v);
    return v;
}

static node_t *leader_of(node_t * v) {
    graph_t *clust;
    node_t *rv;

    if (ND_ranktype(v) != CLUSTER) {
	rv = UF_find(v);
    } else {
	clust = ND_clust(v);
	rv = GD_rankleader(clust)[ND_rank(v)];
    }
    return rv;
}

/// create chain of dummy nodes for edge orig
static void 
make_chain(graph_t * g, node_t * from, node_t * to, edge_t * orig)
{
    int r, label_rank;
    node_t *u, *v;
    edge_t *e;

    u = from;
    if (ED_label(orig))
	label_rank = (ND_rank(from) + ND_rank(to)) / 2;
    else
	label_rank = -1;
    assert(ED_to_virt(orig) == NULL);
    for (r = ND_rank(from) + 1; r <= ND_rank(to); r++) {
	if (r < ND_rank(to)) {
	    if (r == label_rank)
		v = label_vnode(g, orig);
	    else
		v = plain_vnode(g);
	    ND_rank(v) = r;
	} else
	    v = to;
	e = virtual_edge(u, v, orig);
	virtual_weight(e);
	u = v;
    }
    assert(ED_to_virt(orig) != NULL);
}

static void 
interclrep(graph_t * g, edge_t * e)
{
    node_t *t, *h;
    edge_t *ve;

    t = leader_of(agtail(e));
    h = leader_of(aghead(e));
    if (ND_rank(t) > ND_rank(h)) {
	SWAP(&t, &h);
    }
    if (ND_clust(t) != ND_clust(h)) {
	if ((ve = find_fast_edge(t, h))) {
	    merge_chain(g, e, ve, true);
	    return;
	}
	if (ND_rank(t) == ND_rank(h))
	    return;
	make_chain(g, t, h, e);

	/* mark as cluster edge */
	for (ve = ED_to_virt(e); ve && ND_rank(aghead(ve)) <= ND_rank(h);
	     ve = ND_out(aghead(ve)).list[0])
	    ED_edge_type(ve) = CLUSTER_EDGE;
    }
    /* else ignore intra-cluster edges at this point */
}

static bool is_cluster_edge(edge_t *e) {
  return ND_ranktype(agtail(e)) == CLUSTER || ND_ranktype(aghead(e)) == CLUSTER;
}

void merge_chain(graph_t *g, edge_t *e, edge_t *f, bool update_count) {
    edge_t *rep;
    int lastrank = MAX(ND_rank(agtail(e)), ND_rank(aghead(e)));

    assert(ED_to_virt(e) == NULL);
    ED_to_virt(e) = f;
    rep = f;
    do {
	/* interclust multi-edges are not counted now */
	if (update_count)
	    ED_count(rep) += ED_count(e);
	ED_xpenalty(rep) += ED_xpenalty(e);
	ED_weight(rep) += ED_weight(e);
	if (ND_rank(aghead(rep)) == lastrank)
	    break;
	incr_width(g, aghead(rep));
	rep = ND_out(aghead(rep)).list[0];
    } while (rep);
}

bool mergeable(edge_t *e, edge_t *f) {
  return e && f && agtail(e) == agtail(f) && aghead(e) == aghead(f) &&
         ED_label(e) == ED_label(f) && ports_eq(e, f);
}

void class2(graph_t * g)
{
    int c;
    node_t *n, *t, *h;
    edge_t *e, *prev, *opp;

    GD_nlist(g) = NULL;

    mark_clusters(g);
    for (c = 1; c <= GD_n_cluster(g); c++)
	build_skeleton(g, GD_clust(g)[c]);
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    if (ND_weight_class(aghead(e)) <= 2)
		ND_weight_class(aghead(e))++;
	    if (ND_weight_class(agtail(e)) <= 2)
		ND_weight_class(agtail(e))++;
	}

    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (ND_clust(n) == NULL && n == UF_find(n)) {
	    fast_node(g, n);
	}
	prev = NULL;
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {

	    /* already processed */
	    if (ED_to_virt(e)) {
		prev = e;
		continue;
	    }

	    /* edges involving sub-clusters of g */
	    if (is_cluster_edge(e)) {
		/* following is new cluster multi-edge code */
		if (mergeable(prev, e)) {
		    if (ED_to_virt(prev)) {
			merge_chain(g, e, ED_to_virt(prev), false);
			other_edge(e);
		    } else if (ND_rank(agtail(e)) == ND_rank(aghead(e))) {
			merge_oneway(e, prev);
			other_edge(e);
		    }
		    /* else is an intra-cluster edge */
		    continue;
		}
		interclrep(g, e);
		prev = e;
		continue;
	    }
	    /* merge multi-edges */
	    if (prev && agtail(e) == agtail(prev) && aghead(e) == aghead(prev)) {
		if (ND_rank(agtail(e)) == ND_rank(aghead(e))) {
		    merge_oneway(e, prev);
		    other_edge(e);
		    continue;
		}
		if (ED_label(e) == NULL && ED_label(prev) == NULL
		    && ports_eq(e, prev)) {
		    if (Concentrate)
			ED_edge_type(e) = IGNORED;
		    else {
			merge_chain(g, e, ED_to_virt(prev), true);
			other_edge(e);
		    }
		    continue;
		}
		/* parallel edges with different labels fall through here */
	    }

	    /* self edges */
	    if (agtail(e) == aghead(e)) {
		other_edge(e);
		prev = e;
		continue;
	    }

	    t = UF_find(agtail(e));
	    h = UF_find(aghead(e));

	    /* non-leader leaf nodes */
	    if (agtail(e) != t || aghead(e) != h) {
		/* FIX need to merge stuff */
		continue;
	    }


	    /* flat edges */
	    if (ND_rank(agtail(e)) == ND_rank(aghead(e))) {
		flat_edge(g, e);
		prev = e;
		continue;
	    }

	    /* forward edges */
	    if (ND_rank(aghead(e)) > ND_rank(agtail(e))) {
		make_chain(g, agtail(e), aghead(e), e);
		prev = e;
		continue;
	    }

	    /* backward edges */
	    else {
		/* avoid when opp==e in undirected graph */
		for (opp = agfstout(g, aghead(e)); opp != NULL; opp = agnxtout(g, opp)) {
		    if (aghead(opp) != agtail(e) || aghead(opp) == aghead(e) ||
		        ED_edge_type(opp) == IGNORED) {
			continue;
		    }
		    /* shadows a forward edge */
		    if (ED_to_virt(opp) == NULL)
			make_chain(g, agtail(opp), aghead(opp), opp);
		    if (ED_label(e) == NULL && ED_label(opp) == NULL
			&& ports_eq(e, opp)) {
			if (Concentrate) {
			    ED_edge_type(e) = IGNORED;
			    ED_conc_opp_flag(opp) = true;
			} else {	/* see above.  this is getting out of hand */
			    other_edge(e);
			    merge_chain(g, e, ED_to_virt(opp), true);
			}
			break;
		    }
		}
		if (opp != NULL) {
		    continue;
		}
		make_chain(g, aghead(e), agtail(e), e);
		prev = e;
	    }
	}
    }
    /* since decompose() is not called on subgraphs */
    if (g != dot_root(g)) {
	free(GD_comp(g).list);
	GD_comp(g).list = gv_alloc(sizeof(node_t *));
	GD_comp(g).list[0] = GD_nlist(g);
    }
}

