/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/* layout.c:
 * Written by Emden R. Gansner
 *
 * This module provides the main bookkeeping for the fdp layout.
 * In particular, it handles the recursion and the creation of
 * ports and auxiliary graphs.
 * 
 * TODO : can we use ports to aid in layout of edges? Note that
 * at present, they are deleted.
 *
 *   Can we delay all repositioning of nodes until evalPositions, so
 * finalCC only sets the bounding boxes?
 *
 * Make sure multiple edges have an effect.
 */

/* uses PRIVATE interface */
#define FDP_PRIVATE 1

#include "config.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <inttypes.h>
#include <assert.h>
#include <common/render.h>
#include <common/utils.h>
#include <fdpgen/tlayout.h>
#include <math.h>
#include <neatogen/neatoprocs.h>
#include <neatogen/adjust.h>
#include <fdpgen/comp.h>
#include <pack/pack.h>
#include <fdpgen/clusteredges.h>
#include <fdpgen/dbg.h>
#include <stddef.h>
#include <stdbool.h>
#include <util/alloc.h>
#include <util/list.h>

typedef struct {
    graph_t*  rootg;  /* logical root; graph passed in to fdp_layout */
    attrsym_t *G_coord;
    attrsym_t *G_width;
    attrsym_t *G_height;
    int gid;
    pack_info pack;
} layout_info;

typedef struct {
    edge_t *e;
    double alpha;
    double dist2;
} erec;

#define NEW_EDGE(e) (ED_to_virt(e) == 0)

/* finalCC:
 * Set graph bounding box given list of connected
 * components, each with its bounding box set.
 * If c_cnt > 1, then pts != NULL and gives translations for components.
 * Add margin about whole graph unless isRoot is true.
 * Reposition nodes based on final position of
 * node's connected component.
 * Also, entire layout is translated to origin.
 */
static void finalCC(graph_t *g, size_t c_cnt, graph_t **cc, pointf *pts,
                    graph_t *rg, layout_info* infop) {
    attrsym_t * G_width = infop->G_width;
    attrsym_t * G_height = infop->G_height;
    graph_t *cg;
    boxf bb;
    boxf bbf;
    pointf pt;
    int margin;
    graph_t **cp = cc;
    pointf *pp = pts;
    int isRoot = rg == infop->rootg;
    int isEmpty = 0;

    /* compute graph bounding box in points */
    if (c_cnt) {
	cg = *cp++;
	bb = GD_bb(cg);
	if (c_cnt > 1) {
	    pt = *pp++;
	    bb.LL.x += pt.x;
	    bb.LL.y += pt.y;
	    bb.UR.x += pt.x;
	    bb.UR.y += pt.y;
	    while ((cg = *cp++)) {
		boxf b = GD_bb(cg);
		pt = *pp++;
		b.LL.x += pt.x;
		b.LL.y += pt.y;
		b.UR.x += pt.x;
		b.UR.y += pt.y;
		bb.LL.x = fmin(bb.LL.x, b.LL.x);
		bb.LL.y = fmin(bb.LL.y, b.LL.y);
		bb.UR.x = fmax(bb.UR.x, b.UR.x);
		bb.UR.y = fmax(bb.UR.y, b.UR.y);
	    }
	}
    } else {			/* empty graph */
	bb.LL.x = 0;
	bb.LL.y = 0;
	bb.UR.x = late_int(rg, G_width, POINTS(DEFAULT_NODEWIDTH), 3);
	bb.UR.y = late_int(rg, G_height, POINTS(DEFAULT_NODEHEIGHT), 3);
	isEmpty = 1;
    }

    if (GD_label(rg)) {
	isEmpty = 0;
	double d = round(GD_label(rg)->dimen.x) - (bb.UR.x - bb.LL.x);
	if (d > 0) {		/* height of label added below */
	    d /= 2;
	    bb.LL.x -= d;
	    bb.UR.x += d;
	}
    }

    if (isRoot || isEmpty)
	margin = 0;
    else
	margin = late_int (rg, G_margin, CL_OFFSET, 0);
    pt.x = -bb.LL.x + margin;
    pt.y = -bb.LL.y + margin + GD_border(rg)[BOTTOM_IX].y;
    bb.LL.x = 0;
    bb.LL.y = 0;
    bb.UR.x += pt.x + margin;
    bb.UR.y += pt.y + margin + GD_border(rg)[TOP_IX].y;

    /* translate nodes */
    if (c_cnt) {
	cp = cc;
	pp = pts;
	while ((cg = *cp++)) {
	    pointf p;
	    node_t *n;
	    pointf del;

	    if (pp) {
		p = *pp++;
		p.x += pt.x;
		p.y += pt.y;
	    } else {
		p = pt;
	    }
	    del.x = PS2INCH(p.x);
	    del.y = PS2INCH(p.y);
	    for (n = agfstnode(cg); n; n = agnxtnode(cg, n)) {
		ND_pos(n)[0] += del.x;
		ND_pos(n)[1] += del.y;
	    }
	}
    }

    bbf.LL.x = PS2INCH(bb.LL.x);
    bbf.LL.y = PS2INCH(bb.LL.y);
    bbf.UR.x = PS2INCH(bb.UR.x);
    bbf.UR.y = PS2INCH(bb.UR.y);
    BB(g) = bbf;

}

/* mkDeriveNode:
 * Constructor for a node in a derived graph.
 * Allocates dndata.
 */
static node_t *mkDeriveNode(graph_t * dg, char *name)
{
    node_t *dn;

    dn = agnode(dg, name,1);
    agbindrec(dn, "Agnodeinfo_t", sizeof(Agnodeinfo_t), true);	//node custom data
    ND_alg(dn) = gv_alloc(sizeof(dndata)); // free in freeDeriveNode
    ND_pos(dn) = gv_calloc(GD_ndim(dg), sizeof(double));
    /* fprintf (stderr, "Creating %s\n", dn->name); */
    return dn;
}

static void freeDeriveNode(node_t * n)
{
    free(ND_alg(n));
    free(ND_pos(n));
    agdelrec(n, "Agnodeinfo_t");
}

static void freeGData(graph_t * g)
{
    free(GD_alg(g));
}

static void freeDerivedGraph(graph_t * g, graph_t ** cc)
{
    graph_t *cg;
    node_t *dn;
    node_t *dnxt;
    edge_t *e;

    while ((cg = *cc++)) {
	freeGData(cg);
	agdelrec(cg, "Agraphinfo_t");
    }
    if (PORTS(g))
	free(PORTS(g));
    freeGData(g);
    agdelrec(g, "Agraphinfo_t");
    for (dn = agfstnode(g); dn; dn = dnxt) {
	dnxt = agnxtnode(g, dn);
	for (e = agfstout(g, dn); e; e = agnxtout(g, e)) {
	    free (ED_to_virt(e));
	    agdelrec(e, "Agedgeinfo_t");
	}
	freeDeriveNode(dn);
    }
    agclose(g);
}

/* evalPositions:
 * The input is laid out, but node coordinates
 * are relative to smallest containing cluster.
 * Walk through all nodes and clusters, translating
 * the positions to absolute coordinates.
 * Assume that when called, g's bounding box is
 * in absolute coordinates and that box of root graph
 * has LL at origin.
 */
static void evalPositions(graph_t * g, graph_t* rootg)
{
    int i;
    graph_t *subg;
    node_t *n;
    boxf bb;
    boxf sbb;

    bb = BB(g);

    /* translate nodes in g */
    if (g != rootg) {
	for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	    if (PARENT(n) != g)
		continue;
	    ND_pos(n)[0] += bb.LL.x;
	    ND_pos(n)[1] += bb.LL.y;
	}
    }

    /* translate top-level clusters and recurse */
    for (i = 1; i <= GD_n_cluster(g); i++) {
	subg = GD_clust(g)[i];
	if (g != rootg) {
	    sbb = BB(subg);
	    sbb.LL.x += bb.LL.x;
	    sbb.LL.y += bb.LL.y;
	    sbb.UR.x += bb.LL.x;
	    sbb.UR.y += bb.LL.y;
	    BB(subg) = sbb;
	}
	evalPositions(subg, rootg);
    }
}

DEFINE_LIST(clist, graph_t*)

#define BSZ 1000

/* portName:
 * Generate a name for a port.
 * We use the ids of the nodes.
 * This is for debugging. For production, just use edge id and some
 * id for the graph. Note that all the graphs are subgraphs of the
 * root graph.
 */
static char *portName(graph_t * g, bport_t * p)
{
    edge_t *e = p->e;
    node_t *h = aghead(e);
    node_t *t = agtail(e);
    static char buf[BSZ + 1];

	snprintf(buf, sizeof(buf), "_port_%s_(%d)_(%d)_%u",agnameof(g),
		ND_id(t), ND_id(h), AGSEQ(e));
    return buf;
}

/* chkPos:
 * If cluster has coords attribute, use to supply initial position
 * of derived node.
 * Only called if G_coord is defined.
 * We also look at the parent graph's G_coord attribute. If this
 * is identical to the child graph, we have to assume the child
 * inherited it.
 */
static void chkPos(graph_t* g, node_t* n, layout_info* infop, boxf* bbp)
{
    char *p;
    char *pp;
    boxf bb;
    char c;
    graph_t *parent;
    attrsym_t *G_coord = infop->G_coord;

    p = agxget(g, G_coord);
    if (p[0]) {
	if (g != infop->rootg) {
	    parent =agparent(g);
	    pp = agxget(parent, G_coord);
	    if (!strcmp(p, pp))
		return;
	}
	c = '\0';
	if (sscanf(p, "%lf,%lf,%lf,%lf%c",
		   &bb.LL.x, &bb.LL.y, &bb.UR.x, &bb.UR.y, &c) >= 4) {
	    if (PSinputscale > 0.0) {
		bb.LL.x /= PSinputscale;
		bb.LL.y /= PSinputscale;
		bb.UR.x /= PSinputscale;
		bb.UR.y /= PSinputscale;
	    }
	    if (c == '!')
		ND_pinned(n) = P_PIN;
	    else if (c == '?')
		ND_pinned(n) = P_FIX;
	    else
		ND_pinned(n) = P_SET;
	    *bbp = bb;
	} else
	    agwarningf("graph %s, coord %s, expected four doubles\n",
		  agnameof(g), p);
    }
}

/* addEdge:
 * Add real edge e to its image de in the derived graph.
 * We use the to_virt and count fields to store the list.
 */
static void addEdge(edge_t * de, edge_t * e)
{
    short cnt = ED_count(de);
    edge_t **el;

    el = (edge_t**)ED_to_virt(de);
    el = gv_recalloc(el, cnt, cnt + 1, sizeof(edge_t*));
    el[cnt] = e;
    ED_to_virt(de) = (edge_t *) el;
    ED_count(de)++;
}

/* copyAttr:
 * Copy given attribute from g to dg.
 */
static void
copyAttr (graph_t* g, graph_t* dg, char* attr)
{
    char*     ov_val;
    Agsym_t*  ov;

    if ((ov = agattr_text(g,AGRAPH, attr, NULL))) {
	ov_val = agxget(g,ov);
	ov = agattr_text(dg,AGRAPH, attr, NULL);
	if (ov)
	    agxset (dg, ov, ov_val);
	else {
	    const bool is_html = aghtmlstr(ov_val);
	    is_html ? agattr_html(dg, AGRAPH, attr, ov_val)
	            : agattr_text(dg, AGRAPH, attr, ov_val);
	}
    }
}

/* deriveGraph:
 * Create derived graph of g by collapsing clusters into
 * nodes. An edge is created between nodes if there is
 * an edge between two nodes in the clusters of the base graph.
 * Such edges record all corresponding real edges.
 * In addition, we add a node and edge for each port.
 */
static graph_t *deriveGraph(graph_t * g, layout_info * infop)
{
    graph_t *dg;
    node_t *dn;
    graph_t *subg;
    bport_t *pp;
    node_t *n;
    edge_t *de;
    int i, id = 0;

    if (Verbose >= 2)
	fprintf(stderr, "derive graph _dg_%d of %s\n", infop->gid, agnameof(g));
    infop->gid++;

    dg = agopen("derived", Agstrictdirected,NULL);
    agbindrec(dg, "Agraphinfo_t", sizeof(Agraphinfo_t), true);
    GD_alg(dg) = gv_alloc(sizeof(gdata)); // freed in freeDeriveGraph
#ifdef DEBUG
    GORIG(dg) = g;
#endif
    GD_ndim(dg) = GD_ndim(agroot(g));

    /* Copy attributes from g.
     */
    copyAttr(g,dg,"overlap");
    copyAttr(g,dg,"sep");
    copyAttr(g,dg,"K");

    /* create derived nodes from clusters */
    for (i = 1; i <= GD_n_cluster(g); i++) {
	boxf fix_bb = {{DBL_MAX, DBL_MAX}, {-DBL_MAX, -DBL_MAX}};
	subg = GD_clust(g)[i];

	do_graph_label(subg);
	dn = mkDeriveNode(dg, agnameof(subg));
	ND_clust(dn) = subg;
	ND_id(dn) = id++;
	if (infop->G_coord)
		chkPos(subg, dn, infop, &fix_bb);
	for (n = agfstnode(subg); n; n = agnxtnode(subg, n)) {
	    DNODE(n) = dn;
	}
	if (ND_pinned(dn)) {
	    ND_pos(dn)[0] = (fix_bb.LL.x + fix_bb.UR.x) / 2;
	    ND_pos(dn)[1] = (fix_bb.LL.y + fix_bb.UR.y) / 2;
	}
    }

    /* create derived nodes from remaining nodes */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (!DNODE(n)) {
	    if (PARENT(n) && PARENT(n) != GPARENT(g)) {
		agerrorf("node \"%s\" is contained in two non-comparable clusters \"%s\" and \"%s\"\n", agnameof(n), agnameof(g), agnameof(PARENT(n)));
		return NULL;
	    }
	    PARENT(n) = g;
	    if (IS_CLUST_NODE(n))
		continue;
	    dn = mkDeriveNode(dg, agnameof(n));
	    DNODE(n) = dn;
	    ND_id(dn) = id++;
	    ND_width(dn) = ND_width(n);
	    ND_height(dn) = ND_height(n);
	    ND_lw(dn) = ND_lw(n);
	    ND_rw(dn) = ND_rw(n);
	    ND_ht(dn) = ND_ht(n);
	    ND_shape(dn) = ND_shape(n);
	    ND_shape_info(dn) = ND_shape_info(n);
	    if (ND_pinned(n)) {
		ND_pos(dn)[0] = ND_pos(n)[0];
		ND_pos(dn)[1] = ND_pos(n)[1];
		ND_pinned(dn) = ND_pinned(n);
	    }
	    ANODE(dn) = n;
	}
    }

    /* add edges */
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	edge_t *e;
	node_t *hd;
	node_t *tl = DNODE(n);
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
	    hd = DNODE(aghead(e));
	    if (hd == tl)
		continue;
	    if (hd > tl)
		de = agedge(dg, tl, hd, NULL,1);
	    else
		de = agedge(dg, hd, tl, NULL,1);
	    agbindrec(de, "Agedgeinfo_t", sizeof(Agedgeinfo_t), true);
	    ED_dist(de) = ED_dist(e);
	    ED_factor(de) = ED_factor(e);
	    /* fprintf (stderr, "edge %s -- %s\n", tl->name, hd->name); */
	    WDEG(hd)++;
	    WDEG(tl)++;
	    if (NEW_EDGE(de)) {
		DEG(hd)++;
		DEG(tl)++;
	    }
	    addEdge(de, e);
	}
    }

    /* transform ports */
    if ((pp = PORTS(g))) {
	bport_t *pq;
	node_t *m;
	int sz = NPORTS(g);

	/* freed in freeDeriveGraph */
	PORTS(dg) = pq = gv_calloc(sz + 1, sizeof(bport_t));
	sz = 0;
	while (pp->e) {
	    m = DNODE(pp->n);
	    /* Create port in derived graph only if hooks to internal node */
	    if (m) {
		dn = mkDeriveNode(dg, portName(g, pp));
		sz++;
		ND_id(dn) = id++;
		if (dn > m)
		    de = agedge(dg, m, dn, NULL,1);
		else
		    de = agedge(dg, dn, m, NULL,1);
		agbindrec(de, "Agedgeinfo_t", sizeof(Agedgeinfo_t), true);
		ED_dist(de) = ED_dist(pp->e);
		ED_factor(de) = ED_factor(pp->e);
		addEdge(de, pp->e);
		WDEG(dn)++;
		WDEG(m)++;
		DEG(dn)++;	/* ports are unique, so this will be the first and */
		DEG(m)++;	/* only time the edge is touched. */
		pq->n = dn;
		pq->alpha = pp->alpha;
		pq->e = de;
		pq++;
	    }
	    pp++;
	}
	NPORTS(dg) = sz;
    }

    return dg;
}

/* ecmp:
 * Sort edges by angle, then distance.
 */
static int ecmp(const void *v1, const void *v2)
{
    const erec *e1 = v1;
    const erec *e2 = v2;
    if (e1->alpha > e2->alpha)
	return 1;
    else if (e1->alpha < e2->alpha)
	return -1;
    else if (e1->dist2 > e2->dist2)
	return 1;
    else if (e1->dist2 < e2->dist2)
	return -1;
    else
	return 0;
}

#define ANG (M_PI/90)		/* Maximum angular change: 2 degrees */

/* getEdgeList:
 * Generate list of edges in derived graph g using
 * node n. The list is in counterclockwise order.
 * This, of course, assumes we have an initial layout for g.
 */
static erec *getEdgeList(node_t * n, graph_t * g)
{
    int deg = DEG(n);
    int i;
    double dx, dy;
    edge_t *e;
    node_t *m;

    /* freed in expandCluster */
    erec *erecs = gv_calloc(deg + 1, sizeof(erec));
    i = 0;
    for (e = agfstedge(g, n); e; e = agnxtedge(g, e, n)) {
	if (aghead(e) == n)
	    m = agtail(e);
	else
	    m = aghead(e);
	dx = ND_pos(m)[0] - ND_pos(n)[0];
	dy = ND_pos(m)[1] - ND_pos(n)[1];
	erecs[i].e = e;
	erecs[i].alpha = atan2(dy, dx);
	erecs[i].dist2 = dx * dx + dy * dy;
	i++;
    }
    assert(i == deg);
    qsort(erecs, deg, sizeof(erec), ecmp);

    /* ensure no two angles are equal */
    if (deg >= 2) {
	int j;
	double a, inc, delta, bnd;

	i = 0;
	while (i < deg - 1) {
	    a = erecs[i].alpha;
	    j = i + 1;
	    while (j < deg && erecs[j].alpha == a)
		j++;
	    if (j == i + 1)
		i = j;
	    else {
		if (j == deg)
		    bnd = M_PI;	/* all values equal up to end */
		else
		    bnd = erecs[j].alpha;
		delta = fmin((bnd - a) / (j - i), ANG);
		inc = 0;
		for (; i < j; i++) {
		    erecs[i].alpha += inc;
		    inc += delta;
		}
	    }
	}
    }

    return erecs;
}

/* genPorts:
 * Given list of edges with node n in derived graph, add corresponding
 * ports to port list pp, starting at index idx. Return next index.
 * If an edge in the derived graph corresponds to multiple real edges,
 * add them in order if address of n is smaller than other node address.
 * Otherwise, reverse order.
 * Attach angles. The value bnd gives next angle after er->alpha.
 */
static int
genPorts(node_t * n, erec * er, bport_t * pp, int idx, double bnd)
{
    node_t *other;
    int cnt;
    edge_t *e = er->e;
    edge_t *el;
    edge_t **ep;
    double angle, delta;
    int i, j, inc;

    cnt = ED_count(e);

    if (aghead(e) == n)
	other = agtail(e);
    else
	other = aghead(e);

    delta = fmin((bnd - er->alpha) / cnt, ANG);
    angle = er->alpha;

    if (n < other) {
	i = idx;
	inc = 1;
    } else {
	i = idx + cnt - 1;
	inc = -1;
	angle += delta * (cnt - 1);
	delta = -delta;
    }

    ep = (edge_t **)ED_to_virt(e);
    for (j = 0; j < ED_count(e); j++, ep++) {
	el = *ep;
	pp[i].e = el;
	pp[i].n = DNODE(agtail(el)) == n ? agtail(el) : aghead(el);
	pp[i].alpha = angle;
	i += inc;
	angle += delta;
    }
    return (idx + cnt);
}

/* expandCluster;
 * Given positioned derived graph cg with node n which corresponds
 * to a cluster, generate a graph containing the interior of the
 * cluster, plus port information induced by the layout of cg.
 * Basically, we use the cluster subgraph to which n corresponds,
 * attached with port information.
 */
static graph_t *expandCluster(node_t * n, graph_t * cg)
{
    erec *es;
    erec *ep;
    erec *next;
    graph_t *sg = ND_clust(n);
    int sz = WDEG(n);
    int idx = 0;
    double bnd;

    if (sz != 0) {
	/* freed in cleanup_subgs */
	bport_t *pp = gv_calloc(sz + 1, sizeof(bport_t));

	/* create sorted list of edges of n */
	es = ep = getEdgeList(n, cg);

	/* generate ports from edges */
	while (ep->e) {
	    next = ep + 1;
	    if (next->e)
		bnd = next->alpha;
	    else
		bnd = 2 * M_PI + es->alpha;
	    idx = genPorts(n, ep, pp, idx, bnd);
	    ep = next;
	}
	assert(idx == sz);

	PORTS(sg) = pp;
	NPORTS(sg) = sz;
	free(es);
    }
    return sg;
}

/* setClustNodes:
 * At present, cluster nodes are not assigned a position during layout,
 * but positioned in the center of its associated cluster. Because the
 * dummy edge associated with a cluster node may not occur at a sufficient
 * level of cluster, the edge may not be used during layout and we cannot
 * therefore rely find these nodes via ports.
 *
 * In this implementation, we just do a linear pass over all nodes in the
 * root graph. At some point, we may use a better method, like having each
 * cluster contain its list of cluster nodes, or have the graph keep a list.
 * 
 * As nodes, we need to assign cluster nodes the coordinates in the
 * coordinates of its cluster p. Note that p's bbox is in its parent's
 * coordinates. 
 * 
 * If routing, we may decide to place on cluster boundary,
 * and use polyline.
 */
static void 
setClustNodes(graph_t* root)
{
    boxf bb;
    graph_t* p;
    pointf ctr;
    node_t *n;
    double w, h, h_pts;
    double h2, w2;
    pointf *vertices;

    for (n = agfstnode(root); n; n = agnxtnode(root, n)) {
	if (!IS_CLUST_NODE(n)) continue;

	p = PARENT(n);
	bb = BB(p);  /* bbox in parent cluster's coordinates */
	w = bb.UR.x - bb.LL.x;
	h = bb.UR.y - bb.LL.y;
	ctr.x = w / 2.0;
	ctr.y = h / 2.0;
	w2 = INCH2PS(w / 2.0);
	h2 = INCH2PS(h / 2.0);
	h_pts = INCH2PS(h);
	ND_pos(n)[0] = ctr.x;
	ND_pos(n)[1] = ctr.y;
	ND_width(n) = w;
	ND_height(n) = h;
	const double penwidth = late_double(n, N_penwidth, DEFAULT_NODEPENWIDTH,
	                                    MIN_NODEPENWIDTH);
	ND_outline_width(n) = w + penwidth;
	ND_outline_height(n) = h + penwidth;
	/* ND_xsize(n) = POINTS(w); */
	ND_lw(n) = ND_rw(n) = w2;
	ND_ht(n) = h_pts;

	vertices = ((polygon_t *) ND_shape_info(n))->vertices;
	vertices[0].x = ND_rw(n);
	vertices[0].y = h2;
	vertices[1].x = -ND_lw(n);
	vertices[1].y = h2;
	vertices[2].x = -ND_lw(n);
	vertices[2].y = -h2;
	vertices[3].x = ND_rw(n);
	vertices[3].y = -h2;
	// allocate extra vertices representing the outline, i.e., the outermost
	// periphery with penwidth taken into account
	vertices[4].x = ND_rw(n) + penwidth / 2;
	vertices[4].y = h2 + penwidth / 2;
	vertices[5].x = -ND_lw(n) - penwidth / 2;
	vertices[5].y = h2 + penwidth / 2;
	vertices[6].x = -ND_lw(n) - penwidth / 2;
	vertices[6].y = -h2 - penwidth / 2;
	vertices[7].x = ND_rw(n) + penwidth / 2;
	vertices[7].y = -h2 - penwidth / 2;
    }
}

/* layout:
 * Given g with ports:
 *  Derive g' from g by reducing clusters to points (deriveGraph)
 *  Compute connected components of g' (findCComp)
 *  For each cc of g': 
 *    Layout cc (tLayout)
 *    For each node n in cc of g' <-> cluster c in g:
 *      Add ports based on layout of cc to get c' (expandCluster)
 *      Layout c' (recursion)
 *    Remove ports from cc
 *    Expand nodes of cc to reflect size of c'  (xLayout)
 *  Pack connected components to get layout of g (putGraphs)
 *  Translate layout so that bounding box of layout + margin 
 *  has the origin as LL corner. 
 *  Set position of top level clusters and real nodes.
 *  Set bounding box of graph
 * 
 * TODO:
 * 
 * Possibly should modify so that only do connected components
 * on top-level derived graph. Unconnected parts of a cluster
 * could just rattle within cluster boundaries. This may mix
 * up components but give a tighter packing.
 * 
 * Add edges per components to get better packing, rather than
 * wait until the end.
 */
static int layout(graph_t * g, layout_info * infop)
{
    pointf *pts = NULL;
    graph_t *dg;
    node_t *dn;
    node_t *n;
    graph_t *cg;
    graph_t *sg;
    graph_t **cc;
    graph_t **pg;
    int pinned;
    xparams xpms;

#ifdef DEBUG
    incInd();
#endif
    if (Verbose) {
#ifdef DEBUG
	prIndent();
#endif
	fprintf (stderr, "layout %s\n", agnameof(g));
    }
    /* initialize derived node pointers */
    for (n = agfstnode(g); n; n = agnxtnode(g, n))
	DNODE(n) = 0;

    dg = deriveGraph(g, infop);
    if (dg == NULL) {
	return -1;
    }
    size_t c_cnt;
    cc = pg = findCComp(dg, &c_cnt, &pinned);

    while ((cg = *pg++)) {
	node_t* nxtnode;
	fdp_tLayout(cg, &xpms);
	for (n = agfstnode(cg); n; n = nxtnode) {
	    nxtnode = agnxtnode(cg, n);
	    if (ND_clust(n)) {
		pointf pt;
		sg = expandCluster(n, cg);	/* attach ports to sg */
		int r = layout(sg, infop);
		if (r != 0) {
                    return r;
		}
		ND_width(n) = BB(sg).UR.x;
		ND_height(n) = BB(sg).UR.y;
		pt.x = POINTS_PER_INCH * BB(sg).UR.x;
		pt.y = POINTS_PER_INCH * BB(sg).UR.y;
		ND_rw(n) = ND_lw(n) = pt.x/2;
		ND_ht(n) = pt.y;
	    } else if (IS_PORT(n))
		agdelete(cg, n);	/* remove ports from component */
	}

	/* Remove overlaps */
	if (agnnodes(cg) >= 2) {
	    if (g == infop->rootg)
		normalize (cg);
	    fdp_xLayout(cg, &xpms);
	}
    }

    /* At this point, each connected component has its nodes correctly
     * positioned. If we have multiple components, we pack them together.
     * All nodes will be moved to their new positions.
     * NOTE: packGraphs uses nodes in components, so if port nodes are
     * not removed, it won't work.
     */
    /* Handle special cases well: no ports to real internal nodes
     *   Place cluster edges separately, after layout.
     * How to combine parts, especially with disparate components?
     */
    if (c_cnt > 1) {
	bool *bp;
	if (pinned) {
	    bp = gv_calloc(c_cnt, sizeof(bool));
	    bp[0] = true;
	} else
	    bp = NULL;
	infop->pack.fixed = bp;
	pts = putGraphs(c_cnt, cc, NULL, &infop->pack);
	free(bp);
    } else {
	pts = NULL;
	if (c_cnt == 1)
	    compute_bb(cc[0]);
    }

    /* set bounding box of dg and reposition nodes */
    finalCC(dg, c_cnt, cc, pts, g, infop);
    free (pts);

    /* record positions from derived graph to input graph */
    /* At present, this does not record port node info */
    /* In fact, as noted above, we have removed port nodes */
    for (dn = agfstnode(dg); dn; dn = agnxtnode(dg, dn)) {
	if ((sg = ND_clust(dn))) {
	    BB(sg).LL.x = ND_pos(dn)[0] - ND_width(dn) / 2;
	    BB(sg).LL.y = ND_pos(dn)[1] - ND_height(dn) / 2;
	    BB(sg).UR.x = BB(sg).LL.x + ND_width(dn);
	    BB(sg).UR.y = BB(sg).LL.y + ND_height(dn);
	} else if ((n = ANODE(dn))) {
	    ND_pos(n)[0] = ND_pos(dn)[0];
	    ND_pos(n)[1] = ND_pos(dn)[1];
	}
    }
    BB(g) = BB(dg);
#ifdef DEBUG
    if (g == infop->rootg)
	dump(g, 1);
#endif

    /* clean up temp graphs */
    freeDerivedGraph(dg, cc);
    free(cc);
    if (Verbose) {
#ifdef DEBUG
	prIndent ();
#endif
	fprintf (stderr, "end %s\n", agnameof(g));
    }
#ifdef DEBUG
    decInd();
#endif

    return 0;
}

/* setBB;
 * Set point box g->bb from inch box BB(g).
 */
static void setBB(graph_t * g)
{
    int i;
    boxf bb;

    bb.LL.x = POINTS_PER_INCH * BB(g).LL.x;
    bb.LL.y = POINTS_PER_INCH * BB(g).LL.y;
    bb.UR.x = POINTS_PER_INCH * BB(g).UR.x;
    bb.UR.y = POINTS_PER_INCH * BB(g).UR.y;
    GD_bb(g) = bb;
    for (i = 1; i <= GD_n_cluster(g); i++) {
	setBB(GD_clust(g)[i]);
    }
}

/* init_info:
 * Initialize graph-dependent information and
 * state variable.s
 */
static void init_info(graph_t * g, layout_info * infop)
{
    infop->G_coord = agattr_text(g, AGRAPH, "coords", NULL);
    infop->G_width = agattr_text(g, AGRAPH, "width", NULL);
    infop->G_height = agattr_text(g, AGRAPH, "height", NULL);
    infop->rootg = g;
    infop->gid = 0;
    infop->pack.mode = getPackInfo(g, l_node, CL_OFFSET / 2, &infop->pack);
}

/* mkClusters:
 * Attach list of immediate child clusters.
 * NB: By convention, the indexing starts at 1.
 * If pclist is NULL, the graph is the root graph or a cluster
 * If pclist is non-NULL, we are recursively scanning a non-cluster
 * subgraph for cluster children.
 */
static void
mkClusters (graph_t * g, clist_t* pclist, graph_t* parent)
{
    graph_t* subg;
    clist_t  list = {0};
    clist_t* clist;

    if (pclist == NULL) {
	// [0] is empty. The clusters are in [1..cnt].
	clist_append(&list, NULL);
	clist = &list;
    }
    else
	clist = pclist;

    for (subg = agfstsubg(g); subg; subg = agnxtsubg(subg))
	{
	if (is_a_cluster(subg)) {
	    agbindrec(subg, "Agraphinfo_t", sizeof(Agraphinfo_t), true);
	    GD_alg(subg) = gv_alloc(sizeof(gdata)); // freed in cleanup_subgs
	    GD_ndim(subg) = GD_ndim(agroot(parent));
	    LEVEL(subg) = LEVEL(parent) + 1;
	    GPARENT(subg) = parent;
	    clist_append(clist, subg);
	    mkClusters(subg, NULL, subg);
	}
	else {
	    mkClusters(subg, clist, parent);
	}
    }
    if (pclist == NULL) {
	assert(clist_size(&list) - 1 <= INT_MAX);
	GD_n_cluster(g) = (int)(clist_size(&list) - 1);
	if (clist_size(&list) > 1) {
	    clist_shrink_to_fit(&list);
	    GD_clust(g) = clist_detach(&list);
	} else {
	    clist_free(&list);
	}
    }
}

static void fdp_init_graph(Agraph_t * g)
{
    setEdgeType (g, EDGETYPE_LINE);
    GD_alg(g) = gv_alloc(sizeof(gdata)); // freed in cleanup_graph
    GD_ndim(agroot(g)) = late_int(g, agattr_text(g,AGRAPH, "dim", NULL), 2, 2);
    Ndim = GD_ndim(agroot(g)) = MIN(GD_ndim(agroot(g)), MAXDIM);

    mkClusters (g, NULL, g);
    fdp_initParams(g);
    fdp_init_node_edge(g);
}

static int fdpLayout(graph_t * g)
{
    layout_info info;

    init_info(g, &info);
    int r = layout(g, &info);
    if (r != 0) {
        return r;
    }
    setClustNodes(g);
    evalPositions(g,g);

    /* Set bbox info for g and all clusters. This is needed for
     * spline drawing. We already know the graph bbox is at the origin.
     * On return from spline drawing, all bounding boxes should be correct.
     */
    setBB(g);

    return 0;
}

static void
fdpSplines (graph_t * g)
{
    int trySplines = 0;
    int et = EDGE_TYPE(g);

    if (et > EDGETYPE_ORTHO) {
	if (et == EDGETYPE_COMPOUND) {
	    trySplines = splineEdges(g, compoundEdges, EDGETYPE_SPLINE);
	    /* When doing the edges again, accept edges done by compoundEdges */
	    if (trySplines)
		Nop = 2;
	}
	if (trySplines || et != EDGETYPE_COMPOUND) {
	    if (HAS_CLUST_EDGE(g)) {
		agwarningf(
		      "splines and cluster edges not supported - using line segments\n");
		et = EDGETYPE_LINE;
	    } else {
		spline_edges1(g, et);
	    }
	}
	Nop = 0;
    }
    if (State < GVSPLINES)
	spline_edges1(g, et);
}

void fdp_layout(graph_t * g)
{
    double save_scale = PSinputscale;
        
    PSinputscale = get_inputscale (g);
    fdp_init_graph(g);
    if (fdpLayout(g) != 0) {
	return;
    }
    neato_set_aspect(g);

    if (EDGE_TYPE(g) != EDGETYPE_NONE) fdpSplines (g);

    gv_postprocess(g, 0);
    PSinputscale = save_scale;
}
