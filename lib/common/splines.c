/// @file
/// @brief  [edge splines](https://graphviz.org/docs/attrs/splines/)
/// @ingroup common_render
/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/* Functions related to creating a spline and attaching it to
 * an edge, starting from a list of control points.
 */

#include <math.h>
#include <common/render.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/agxbuf.h>
#include <util/alloc.h>
#include <util/gv_math.h>
#include <util/unreachable.h>

#ifdef DEBUG
static int debugleveln(edge_t* e, int i)
{
    return GD_showboxes(agraphof(aghead(e))) == i ||
	    GD_showboxes(agraphof(agtail(e))) == i ||
	    ED_showboxes(e) == i ||
	    ND_showboxes(aghead(e)) == i ||
	    ND_showboxes(agtail(e)) == i;
}

static void showPoints(pointf ps[], int pn)
{
    int bi;

    show_boxes_append(&Show_boxes, gv_strdup("%% self list"));
    show_boxes_append(&Show_boxes, gv_strdup("dbgstart"));
    for (bi = 0; bi < pn; bi++) {
	agxbuf buf = {0};
	agxbprint(&buf, "%.5g %.5g point", ps[bi].x, ps[bi].y);
	show_boxes_append(&Show_boxes, agxbdisown(&buf));
    }
    show_boxes_append(&Show_boxes, gv_strdup("grestore"));
}
#endif

/* arrow_clip:
 * Clip arrow to node boundary.
 * The real work is done elsewhere. Here we get the real edge,
 * check that the edge has arrowheads, and that an endpoint
 * isn't a merge point where several parts of an edge meet.
 * (e.g., with edge concentrators).
 */
static void
arrow_clip(edge_t * fe, node_t * hn,
	   pointf * ps, size_t *startp, size_t *endp,
	   bezier * spl, splineInfo * info)
{
    edge_t *e;
    bool j;

    for (e = fe; ED_to_orig(e); e = ED_to_orig(e));

    if (info->ignoreSwap)
	j = false;
    else
	j = info->swapEnds(e);
    uint32_t sflag, eflag;
    arrow_flags(e, &sflag, &eflag);
    if (info->splineMerge(hn))
	eflag = ARR_NONE;
    if (info->splineMerge(agtail(fe)))
	sflag = ARR_NONE;
    /* swap the two ends */
    if (j) {
	uint32_t i = sflag;
	sflag = eflag;
	eflag = i;
    }
    if (info->isOrtho) {
	if (eflag || sflag)
	    arrowOrthoClip(e, ps, *startp, *endp, spl, sflag, eflag);
    }
    else {
	if (sflag)
	    *startp = arrowStartClip(e, ps, *startp, *endp, spl, sflag);
	if (eflag)
	    *endp = arrowEndClip(e, ps, *startp, *endp, spl, eflag);
    }
}

/* bezier_clip
 * Clip bezier to shape using binary search.
 * The details of the shape are passed in the inside_context;
 * The function providing the inside test is passed as a parameter.
 * left_inside specifies that sp[0] is inside the node,
 * else sp[3] is taken as inside.
 * The points p are in node coordinates.
 */
void bezier_clip(inside_t * inside_context,
		 bool(*inside) (inside_t * inside_context, pointf p),
		 pointf * sp, bool left_inside)
{
    pointf seg[4], best[4], pt, opt, *left, *right;
    double low, high, t, *idir, *odir;
    bool found;
    int i;

    if (left_inside) {
	left = NULL;
	right = seg;
	pt = sp[0];
	idir = &low;
	odir = &high;
    } else {
	left = seg;
	right = NULL;
	pt = sp[3];
	idir = &high;
	odir = &low;
    }
    found = false;
    low = 0.0;
    high = 1.0;
    do {
	opt = pt;
	t = (high + low) / 2.0;
	pt = Bezier(sp, t, left, right);
	if (inside(inside_context, pt)) {
	    *idir = t;
	    for (i = 0; i < 4; i++)
		best[i] = seg[i];
	    found = true;
	} else {
	    *odir = t;
	}
    } while (fabs(opt.x - pt.x) > .5 || fabs(opt.y - pt.y) > .5);
    if (found)
	for (i = 0; i < 4; i++)
	    sp[i] = best[i];
    else
	for (i = 0; i < 4; i++)
	    sp[i] = seg[i];
}

/* shape_clip0:
 * Clip Bezier to node shape using binary search.
 * left_inside specifies that curve[0] is inside the node, else
 * curve[3] is taken as inside.
 * Assumes ND_shape(n) and ND_shape(n)->fns->insidefn are non-NULL.
 * See note on shape_clip.
 */
static void
shape_clip0(inside_t * inside_context, node_t * n, pointf curve[4],
	    bool left_inside)
{
    int i;
    double save_real_size;
    pointf c[4];

    save_real_size = ND_rw(n);
    for (i = 0; i < 4; i++) {
	c[i].x = curve[i].x - ND_coord(n).x;
	c[i].y = curve[i].y - ND_coord(n).y;
    }

    bezier_clip(inside_context, ND_shape(n)->fns->insidefn, c, left_inside);

    for (i = 0; i < 4; i++) {
	curve[i].x = c[i].x + ND_coord(n).x;
	curve[i].y = c[i].y + ND_coord(n).y;
    }
    ND_rw(n) = save_real_size;
}

/* shape_clip:
 * Clip Bezier to node shape
 * Uses curve[0] to determine which which side is inside the node.
 * NOTE: This test is bad. It is possible for previous call to
 * shape_clip to produce a Bezier with curve[0] moved to the boundary
 * for which insidefn(curve[0]) is true. Thus, if the new Bezier is
 * fed back to shape_clip, it will again assume left_inside is true.
 * To be safe, shape_clip0 should guarantee that the computed boundary
 * point fails insidefn.
 * The edge e is used to provide a port box. If NULL, the spline is
 * clipped to the node shape.
 */
void shape_clip(node_t * n, pointf curve[4])
{
    double save_real_size;
    bool left_inside;
    pointf c;

    if (ND_shape(n) == NULL || ND_shape(n)->fns->insidefn == NULL)
	return;

    inside_t inside_context = {.s = {.n = n}};
    save_real_size = ND_rw(n);
    c.x = curve[0].x - ND_coord(n).x;
    c.y = curve[0].y - ND_coord(n).y;
    left_inside = ND_shape(n)->fns->insidefn(&inside_context, c);
    ND_rw(n) = save_real_size;
    shape_clip0(&inside_context, n, curve, left_inside);
}

/* new_spline:
 * Create and attach a new bezier of size sz to the edge d
 */
bezier *new_spline(edge_t *e, size_t sz) {
    bezier *rv;
    while (ED_to_orig(e) != NULL && ED_edge_type(e) != NORMAL)
	e = ED_to_orig(e);
    if (ED_spl(e) == NULL)
	ED_spl(e) = gv_alloc(sizeof(splines));
    ED_spl(e)->list = gv_recalloc(ED_spl(e)->list, ED_spl(e)->size,
                                  ED_spl(e)->size + 1, sizeof(bezier));
    rv = &(ED_spl(e)->list[ED_spl(e)->size++]);
    rv->list = gv_calloc(sz, sizeof(pointf));
    rv->size = sz;
    rv->sflag = rv->eflag = 0;
    rv->sp.x = rv->sp.y = rv->ep.x = rv->ep.y = 0;
    return rv;
}

/* clip_and_install:
 * Given a raw spline (pn control points in ps), representing
 * a path from edge agtail(fe) ending in node hn, clip the ends to
 * the node boundaries and attach the resulting spline to the
 * edge.
 */
void
clip_and_install(edge_t *fe, node_t *hn, pointf *ps, size_t pn,
		 splineInfo * info)
{
    int clipTail, clipHead;
    size_t start, end;
    edge_t *orig;
    boxf *tbox, *hbox;

    node_t *tn = agtail(fe);
    graph_t *const g = agraphof(tn);
    bezier *const newspl = new_spline(fe, pn);

    for (orig = fe; ED_to_orig(orig) != NULL && ED_edge_type(orig) != NORMAL;
         orig = ED_to_orig(orig));

    /* may be a reversed flat edge */
    if (!info->ignoreSwap && ND_rank(tn) == ND_rank(hn) && ND_order(tn) > ND_order(hn)) {
	SWAP(&hn, &tn);
    }
    if (tn == agtail(orig)) {
	clipTail = ED_tail_port(orig).clip;
	clipHead = ED_head_port(orig).clip;
	tbox = ED_tail_port(orig).bp;
	hbox = ED_head_port(orig).bp;
    }
    else { /* fe and orig are reversed */
	clipTail = ED_head_port(orig).clip;
	clipHead = ED_tail_port(orig).clip;
	hbox = ED_tail_port(orig).bp;
	tbox = ED_head_port(orig).bp;
    }

    /* spline may be interior to node */
    if(clipTail && ND_shape(tn) && ND_shape(tn)->fns->insidefn) {
	inside_t inside_context = {.s = {.n = tn, .bp = tbox}};
	for (start = 0; start < pn - 4; start += 3) {
	    const pointf p2 = {.x = ps[start + 3].x - ND_coord(tn).x,
	                       .y = ps[start + 3].y - ND_coord(tn).y};
	    if (!ND_shape(tn)->fns->insidefn(&inside_context, p2))
		break;
	}
	shape_clip0(&inside_context, tn, &ps[start], true);
    } else
	start = 0;
    if(clipHead && ND_shape(hn) && ND_shape(hn)->fns->insidefn) {
	inside_t inside_context = {.s = {.n = hn, .bp = hbox}};
	for (end = pn - 4; end > 0; end -= 3) {
	    const pointf p2 = {.x = ps[end].x - ND_coord(hn).x,
	                       .y = ps[end].y - ND_coord(hn).y};
	    if (!ND_shape(hn)->fns->insidefn(&inside_context, p2))
		break;
	}
	shape_clip0(&inside_context, hn, &ps[end], false);
    } else
	end = pn - 4;
    for (; start < pn - 4; start += 3)
	if (! APPROXEQPT(ps[start], ps[start + 3], MILLIPOINT))
	    break;
    for (; end > 0; end -= 3)
	if (! APPROXEQPT(ps[end], ps[end + 3], MILLIPOINT))
	    break;
    arrow_clip(fe, hn, ps, &start, &end, newspl, info);
    for (size_t i = start; i < end + 4; ) {
	pointf cp[4];
	newspl->list[i - start] = ps[i];
	cp[0] = ps[i];
	i++;
	if ( i >= end + 4)
	    break;
	newspl->list[i - start] = ps[i];
	cp[1] = ps[i];
	i++;
	newspl->list[i - start] = ps[i];
	cp[2] = ps[i];
	i++;
	cp[3] = ps[i];
	update_bb_bz(&GD_bb(g), cp);
    }
    newspl->size = end - start + 4;
}

static double
conc_slope(node_t* n)
{
    double s_in, s_out, m_in, m_out;
    int cnt_in, cnt_out;
    edge_t *e;

    s_in = s_out = 0.0;
    for (cnt_in = 0; (e = ND_in(n).list[cnt_in]); cnt_in++)
	s_in += ND_coord(agtail(e)).x;
    for (cnt_out = 0; (e = ND_out(n).list[cnt_out]); cnt_out++)
	s_out += ND_coord(aghead(e)).x;
    const double x1 = ND_coord(n).x - s_in / cnt_in;
    const double y1 = ND_coord(n).y - ND_coord(agtail(ND_in(n).list[0])).y;
    m_in = atan2(y1, x1);
    const double x2 = s_out / cnt_out - ND_coord(n).x;
    const double y2 = ND_coord(aghead(ND_out(n).list[0])).y - ND_coord(n).y;
    m_out = atan2(y2, x2);
    return (m_in + m_out) / 2.0;
}

void add_box(path * P, boxf b)
{
    if (b.LL.x < b.UR.x && b.LL.y < b.UR.y)
	P->boxes[P->nbox++] = b;
}

/* beginpath:
 * Set up boxes near the tail node.
 * For regular nodes, the result should be a list of contiguous rectangles
 * such that the last one has the smallest LL.y and its LL.y is above
 * the bottom of the rank (rank.ht1).
 *
 * For flat edges, we assume endp->sidemask has been set. For regular
 * edges, we set this, but it doesn't appear to be needed any more.
 *
 * In many cases, we tweak the x or y coordinate of P->start.p by 1.
 * This is because of a problem in the path routing code. If the starting
 * point actually lies on the polygon, in some cases, the router gets
 * confused and routes the path outside the polygon. So, the offset ensures
 * the starting point is in the polygon.
 *
 * FIX: Creating the initial boxes only really works for rankdir=TB and
 * rankdir=LR. For the others, we rely on compassPort flipping the side
 * and then assume that the node shape has top-bottom symmetry. Since we
 * at present only put compass points on the bounding box, this works.
 * If we attempt to implement compass points on actual node perimeters,
 * something major will probably be necessary. Doing the coordinate
 * flip (postprocess) before spline routing will be too disruptive. The
 * correct solution is probably to have beginpath/endpath create the
 * boxes assuming an inverted node. Note that compassPort already does
 * some flipping. Even better would be to allow the *_path function
 * to provide a polygon.
 *
 * The extra space provided by FUDGE-2 prevents the edge from getting
 * too close the side of the node.
 *
 */
#define FUDGE 2
#define HT2(n) (ND_ht(n)/2)

void
beginpath(path * P, edge_t * e, int et, pathend_t * endp, bool merge)
{
    int side, mask;
    node_t *n;
    int (*pboxfn) (node_t*, port*, int, boxf*, int*);

    n = agtail(e);

    if (ED_tail_port(e).dyna)
	ED_tail_port(e) = resolvePort(agtail(e), aghead(e), &ED_tail_port(e));
    if (ND_shape(n))
	pboxfn = ND_shape(n)->fns->pboxfn;
    else
	pboxfn = NULL;
    P->start.p = add_pointf(ND_coord(n), ED_tail_port(e).p);
    if (merge) {
	/*P->start.theta = - M_PI / 2; */
	P->start.theta = conc_slope(agtail(e));
	P->start.constrained = true;
    } else {
	if (ED_tail_port(e).constrained) {
	    P->start.theta = ED_tail_port(e).theta;
	    P->start.constrained = true;
	} else
	    P->start.constrained = false;
    }
    P->nbox = 0;
    P->data = e;
    endp->np = P->start.p;
    if (et == REGULAREDGE && ND_node_type(n) == NORMAL && (side = ED_tail_port(e).side)) {
	edge_t* orig;
	boxf b0, b = endp->nb;
	if (side & TOP) {
	    endp->sidemask = TOP;
	    if (P->start.p.x < ND_coord(n).x) { /* go left */
		b0.LL.x = b.LL.x - 1;
		/* b0.LL.y = ND_coord(n).y + HT2(n); */
		b0.LL.y = P->start.p.y;
		b0.UR.x = b.UR.x;
		b0.UR.y = ND_coord(n).y + HT2(n) + GD_ranksep(agraphof(n))/2;
		b.UR.x = ND_coord(n).x - ND_lw(n) - (FUDGE-2);
		b.UR.y = b0.LL.y;
		b.LL.y = ND_coord(n).y - HT2(n);
		--b.LL.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
	    }
	    else {
		b0.LL.x = b.LL.x;
		b0.LL.y = P->start.p.y;
		/* b0.LL.y = ND_coord(n).y + HT2(n); */
		b0.UR.x = b.UR.x+1;
		b0.UR.y = ND_coord(n).y + HT2(n) + GD_ranksep(agraphof(n))/2;
		b.LL.x = ND_coord(n).x + ND_rw(n) + (FUDGE-2);
		b.UR.y = b0.LL.y;
		b.LL.y = ND_coord(n).y - HT2(n);
		++b.UR.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
	    }
	    ++P->start.p.y;
	    endp->boxn = 2;
	}
	else if (side & BOTTOM) {
	    endp->sidemask = BOTTOM;
	    b.UR.y = MAX(b.UR.y,P->start.p.y);
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    --P->start.p.y;
	}
	else if (side & LEFT) {
	    endp->sidemask = LEFT;
	    b.UR.x = P->start.p.x;
	    b.LL.y = ND_coord(n).y - HT2(n);
	    b.UR.y = P->start.p.y;
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    --P->start.p.x;
	}
	else {
	    endp->sidemask = RIGHT;
	    b.LL.x = P->start.p.x;
	    b.LL.y = ND_coord(n).y - HT2(n);
	    b.UR.y = P->start.p.y;
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->start.p.x;
	}
	for (orig = e; ED_to_orig(orig) != NULL && ED_edge_type(orig) != NORMAL;
	     orig = ED_to_orig(orig));
	if (n == agtail(orig))
	    ED_tail_port(orig).clip = false;
	else
	    ED_head_port(orig).clip = false;
	return;
    }
    if (et == FLATEDGE && (side = ED_tail_port(e).side)) {
	boxf b0, b = endp->nb;
	edge_t* orig;
	if (side & TOP) {
	    b.LL.y = MIN(b.LL.y,P->start.p.y);
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->start.p.y;
	}
	else if (side & BOTTOM) {
	    if (endp->sidemask == TOP) {
		b0.UR.y = ND_coord(n).y - HT2(n);
		b0.UR.x = b.UR.x+1;
		b0.LL.x = P->start.p.x;
		b0.LL.y = b0.UR.y - GD_ranksep(agraphof(n))/2;
		b.LL.x = ND_coord(n).x + ND_rw(n) + (FUDGE-2);
		b.LL.y = b0.UR.y;
		b.UR.y = ND_coord(n).y + HT2(n);
		++b.UR.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
		endp->boxn = 2;
	    }
	    else {
		b.UR.y = MAX(b.UR.y,P->start.p.y);
		endp->boxes[0] = b;
		endp->boxn = 1;
	    }
	    --P->start.p.y;
	}
	else if (side & LEFT) {
	    b.UR.x = P->start.p.x+1;
	    if (endp->sidemask == TOP) {
		b.UR.y = ND_coord(n).y + HT2(n);
		b.LL.y = P->start.p.y-1;
	    }
	    else {
		b.LL.y = ND_coord(n).y - HT2(n);
		b.UR.y = P->start.p.y+1;
	    }
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    --P->start.p.x;
	}
	else {
	    b.LL.x = P->start.p.x;
	    if (endp->sidemask == TOP) {
		b.UR.y = ND_coord(n).y + HT2(n);
		b.LL.y = P->start.p.y;
	    }
	    else {
		b.LL.y = ND_coord(n).y - HT2(n);
		b.UR.y = P->start.p.y+1;
	    }
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->start.p.x;
	}
	for (orig = e; ED_to_orig(orig) != NULL && ED_edge_type(orig) != NORMAL;
	     orig = ED_to_orig(orig));
	if (n == agtail(orig))
	    ED_tail_port(orig).clip = false;
	else
	    ED_head_port(orig).clip = false;
	endp->sidemask = side;
	return;
    }

    if (et == REGULAREDGE) side = BOTTOM;
    else side = endp->sidemask;  /* for flat edges */
    if (pboxfn
	&& (mask = pboxfn(n, &ED_tail_port(e), side, &endp->boxes[0], &endp->boxn)))
	endp->sidemask = mask;
    else {
	endp->boxes[0] = endp->nb;
	endp->boxn = 1;

	switch (et) {
	case SELFEDGE:
	/* moving the box UR.y by + 1 avoids colinearity between
	   port point and box that confuses Proutespline().  it's
	   a bug in Proutespline() but this is the easiest fix. */
	    assert(0);  /* at present, we don't use beginpath for selfedges */
	    endp->boxes[0].UR.y = P->start.p.y - 1;
	    endp->sidemask = BOTTOM;
	    break;
	case FLATEDGE:
	    if (endp->sidemask == TOP)
		endp->boxes[0].LL.y = P->start.p.y;
	    else
		endp->boxes[0].UR.y = P->start.p.y;
	    break;
	case REGULAREDGE:
	    endp->boxes[0].UR.y = P->start.p.y;
	    endp->sidemask = BOTTOM;
	    --P->start.p.y;
	    break;
	}
    }
}

void endpath(path * P, edge_t * e, int et, pathend_t * endp, bool merge)
{
    int side, mask;
    node_t *n;
    int (*pboxfn) (node_t* n, port*, int, boxf*, int*);

    n = aghead(e);

    if (ED_head_port(e).dyna)
	ED_head_port(e) = resolvePort(aghead(e), agtail(e), &ED_head_port(e));
    if (ND_shape(n))
	pboxfn = ND_shape(n)->fns->pboxfn;
    else
	pboxfn = NULL;
    P->end.p = add_pointf(ND_coord(n), ED_head_port(e).p);
    if (merge) {
	/*P->end.theta = M_PI / 2; */
	P->end.theta = conc_slope(aghead(e)) + M_PI;
	assert(P->end.theta < 2 * M_PI);
	P->end.constrained = true;
    } else {
	if (ED_head_port(e).constrained) {
	    P->end.theta = ED_head_port(e).theta;
	    P->end.constrained = true;
	} else
	    P->end.constrained = false;
    }
    endp->np = P->end.p;
    if (et == REGULAREDGE && ND_node_type(n) == NORMAL && (side = ED_head_port(e).side)) {
	edge_t* orig;
	boxf b0, b = endp->nb;
	if (side & TOP) {
	    endp->sidemask = TOP;
	    b.LL.y = MIN(b.LL.y,P->end.p.y);
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->end.p.y;
	}
	else if (side & BOTTOM) {
	    endp->sidemask = BOTTOM;
	    if (P->end.p.x < ND_coord(n).x) { /* go left */
		b0.LL.x = b.LL.x-1;
		/* b0.UR.y = ND_coord(n).y - HT2(n); */
		b0.UR.y = P->end.p.y;
		b0.UR.x = b.UR.x;
		b0.LL.y = ND_coord(n).y - HT2(n) - GD_ranksep(agraphof(n))/2;
		b.UR.x = ND_coord(n).x - ND_lw(n) - (FUDGE-2);
		b.LL.y = b0.UR.y;
		b.UR.y = ND_coord(n).y + HT2(n);
		--b.LL.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
	    }
	    else {
		b0.LL.x = b.LL.x;
		b0.UR.y = P->end.p.y;
		/* b0.UR.y = ND_coord(n).y - HT2(n); */
		b0.UR.x = b.UR.x+1;
		b0.LL.y = ND_coord(n).y - HT2(n) - GD_ranksep(agraphof(n))/2;
		b.LL.x = ND_coord(n).x + ND_rw(n) + (FUDGE-2);
		b.LL.y = b0.UR.y;
		b.UR.y = ND_coord(n).y + HT2(n);
		++b.UR.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
	    }
	    endp->boxn = 2;
	    --P->end.p.y;
	}
	else if (side & LEFT) {
	    endp->sidemask = LEFT;
	    b.UR.x = P->end.p.x;
	    b.UR.y = ND_coord(n).y + HT2(n);
	    b.LL.y = P->end.p.y;
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    --P->end.p.x;
	}
	else {
	    endp->sidemask = RIGHT;
	    b.LL.x = P->end.p.x;
	    b.UR.y = ND_coord(n).y + HT2(n);
	    b.LL.y = P->end.p.y;
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->end.p.x;
	}
	for (orig = e; ED_to_orig(orig) != NULL && ED_edge_type(orig) != NORMAL;
	     orig = ED_to_orig(orig));
	if (n == aghead(orig))
	    ED_head_port(orig).clip = false;
	else
	    ED_tail_port(orig).clip = false;
	endp->sidemask = side;
	return;
    }

    if (et == FLATEDGE && (side = ED_head_port(e).side)) {
	boxf b0, b = endp->nb;
	edge_t* orig;
	if (side & TOP) {
	    b.LL.y = MIN(b.LL.y,P->end.p.y);
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->end.p.y;
	}
	else if (side & BOTTOM) {
	    if (endp->sidemask == TOP) {
		b0.LL.x = b.LL.x-1;
		b0.UR.y = ND_coord(n).y - HT2(n);
		b0.UR.x = P->end.p.x;
		b0.LL.y = b0.UR.y - GD_ranksep(agraphof(n))/2;
		b.UR.x = ND_coord(n).x - ND_lw(n) - 2;
		b.LL.y = b0.UR.y;
		b.UR.y = ND_coord(n).y + HT2(n);
		--b.LL.x;
		endp->boxes[0] = b0;
		endp->boxes[1] = b;
		endp->boxn = 2;
	    }
	    else {
		b.UR.y = MAX(b.UR.y,P->start.p.y);
		endp->boxes[0] = b;
		endp->boxn = 1;
	    }
	    --P->end.p.y;
	}
	else if (side & LEFT) {
	    b.UR.x = P->end.p.x+1;
	    if (endp->sidemask == TOP) {
		b.UR.y = ND_coord(n).y + HT2(n);
		b.LL.y = P->end.p.y-1;
	    }
	    else {
		b.LL.y = ND_coord(n).y - HT2(n);
		b.UR.y = P->end.p.y+1;
	    }
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    --P->end.p.x;
	}
	else {
	    b.LL.x = P->end.p.x-1;
	    if (endp->sidemask == TOP) {
		b.UR.y = ND_coord(n).y + HT2(n);
		b.LL.y = P->end.p.y-1;
	    }
	    else {
		b.LL.y = ND_coord(n).y - HT2(n);
		b.UR.y = P->end.p.y;
	    }
	    endp->boxes[0] = b;
	    endp->boxn = 1;
	    ++P->end.p.x;
	}
	for (orig = e; ED_to_orig(orig) != NULL && ED_edge_type(orig) != NORMAL;
	     orig = ED_to_orig(orig));
	if (n == aghead(orig))
	    ED_head_port(orig).clip = false;
	else
	    ED_tail_port(orig).clip = false;
	endp->sidemask = side;
	return;
    }

    if (et == REGULAREDGE) side = TOP;
    else side = endp->sidemask;  /* for flat edges */
    if (pboxfn
	&& (mask = pboxfn(n, &ED_head_port(e), side, &endp->boxes[0], &endp->boxn)))
	endp->sidemask = mask;
    else {
	endp->boxes[0] = endp->nb;
	endp->boxn = 1;

	switch (et) {
	case SELFEDGE:
	    /* offset of -1 is symmetric w.r.t. beginpath()
	     * FIXME: is any of this right?  what if self-edge
	     * doesn't connect from BOTTOM to TOP??? */
	    assert(0);  /* at present, we don't use endpath for selfedges */
	    endp->boxes[0].LL.y = P->end.p.y + 1;
	    endp->sidemask = TOP;
	    break;
	case FLATEDGE:
	    if (endp->sidemask == TOP)
		endp->boxes[0].LL.y = P->end.p.y;
	    else
		endp->boxes[0].UR.y = P->end.p.y;
	    break;
	case REGULAREDGE:
	    endp->boxes[0].LL.y = P->end.p.y;
	    endp->sidemask = TOP;
	    ++P->end.p.y;
	    break;
	}
    }
}


static int convert_sides_to_points(int tail_side, int head_side)
{
int vertices[] = {12,4,6,2,3,1,9,8};  //the cumulative side value of each node point
int i, tail_i, head_i;
int pair_a[8][8] = {	    //array of possible node point pairs
{11,12,13,14,15,16,17,18},
{21,22,23,24,25,26,27,28},
{31,32,33,34,35,36,37,38},
{41,42,43,44,45,46,47,48},
{51,52,53,54,55,56,57,58},
{61,62,63,64,65,66,67,68},
{71,72,73,74,75,76,77,78},
{81,82,83,84,85,86,87,88}
};

 tail_i = head_i = -1;
	for(i=0;i< 8; i++){
		if(head_side == vertices[i]){
			head_i = i;
			break;
		}
	}
	for(i=0;i< 8; i++){
		if(tail_side == vertices[i]){
			tail_i = i;
			break;
		}
	}
	
if( tail_i < 0 || head_i < 0)
  return 0;
else
  return pair_a[tail_i][head_i];
}

static void selfBottom(edge_t *edges[], size_t ind, size_t cnt, double sizex,
                       double stepy, splineInfo *sinfo) {
    pointf tp, hp, np;
    node_t *n;
    edge_t *e;
    int sgn, point_pair;
    double hy, ty, stepx, dx, dy, height;

    e = edges[ind];
    n = agtail(e);

    stepx = fmax(sizex / 2.0 / (double)cnt, 2.0);
    np = ND_coord(n);
    tp = ED_tail_port(e).p;
    tp.x += np.x;
    tp.y += np.y;
    hp = ED_head_port(e).p;
    hp.x += np.x;
    hp.y += np.y;
    if (tp.x >= hp.x) sgn = 1;
    else sgn = -1;
    dy = ND_ht(n) / 2.0;
    dx = 0.0;
    // certain adjustments are required for some point_pairs in order to improve the
    // display of the edge path between them
    point_pair = convert_sides_to_points(ED_tail_port(e).side,ED_head_port(e).side);
    switch(point_pair){
      case 67:  sgn = -sgn;
		break;
      default:
		break;
    }
    ty = fmin(dy, 3 * (tp.y + dy - np.y));
    hy = fmin(dy, 3 * (hp.y + dy - np.y));
    for (size_t i = 0; i < cnt; i++) {
        e = edges[ind++];
        dy += stepy;
        ty += stepy;
        hy += stepy;
        dx += sgn * stepx;
        pointf points[] = {
    	    tp,
    	    {tp.x + dx, tp.y - ty / 3},
    	    {tp.x + dx, np.y - dy},
    	    {(tp.x + hp.x) / 2, np.y - dy},
    	    {hp.x - dx, np.y - dy},
    	    {hp.x - dx, hp.y - hy / 3},
    	    hp
        };
        if (ED_label(e)) {
	if (GD_flip(agraphof(agtail(e)))) {
    	    height = ED_label(e)->dimen.x;
    	} else {
    	    height = ED_label(e)->dimen.y;
    	}
    	ED_label(e)->pos.y = ND_coord(n).y - dy - height / 2.0;
    	ED_label(e)->pos.x = ND_coord(n).x;
    	ED_label(e)->set = true;
    	if (height > stepy)
    	    dy += height - stepy;
        }
        const size_t pointn = sizeof(points) / sizeof(points[0]);
        clip_and_install(e, aghead(e), points, pointn, sinfo);
#ifdef DEBUG
        if (debugleveln(e,1))
	    showPoints (points, pointn);
#endif
    }
}

static void selfTop(edge_t *edges[], size_t ind, size_t cnt, double sizex,
                    double stepy, splineInfo *sinfo) {
    int sgn, point_pair;
    double hy, ty,  stepx, dx, dy, height;
    pointf tp, hp, np;
    node_t *n;
    edge_t *e;

    e = edges[ind];
    n = agtail(e);

    stepx = fmax(sizex / 2.0 / (double)cnt, 2.0);
    np = ND_coord(n);
    tp = ED_tail_port(e).p;
    tp.x += np.x;
    tp.y += np.y;
    hp = ED_head_port(e).p;
    hp.x += np.x;
    hp.y += np.y;
    if (tp.x >= hp.x) sgn = 1;
    else sgn = -1;
    dy = ND_ht(n)/2., dx = 0.;
    // certain adjustments are required for some point_pairs in order to improve the
    // display of the edge path between them
    point_pair = convert_sides_to_points(ED_tail_port(e).side,ED_head_port(e).side);
    switch(point_pair){
	case 15:	
		dx = sgn*(ND_rw(n) - (hp.x-np.x) + stepx);
		break;

	case 38:
		dx = sgn*(ND_lw(n)-(np.x-hp.x) + stepx);
		break;
	case 41:
		dx = sgn*(ND_rw(n)-(tp.x-np.x) + stepx);
		break;
	case 48:
		dx = sgn*(ND_rw(n)-(tp.x-np.x) + stepx);
		break;
	
	case 14:
	case 37:
	case 47:
	case 51:
	case 57:
	case 58:
		dx = sgn * ((ND_lw(n) - (np.x - tp.x) + (ND_rw(n) - (hp.x - np.x))) / 3.0);
		break;
	case 73:
 		dx = sgn*(ND_lw(n)-(np.x-tp.x) + stepx);
		break;
	case 83:
		dx = sgn*(ND_lw(n)-(np.x-tp.x));
		break;
	case 84:
          dx = sgn *
               ((ND_lw(n) - (np.x - tp.x) + (ND_rw(n) - (hp.x - np.x))) / 2.0 +
                stepx);
          break;
	case 74:
	case 75:
	case 85:
          dx = sgn *
               ((ND_lw(n) - (np.x - tp.x) + (ND_rw(n) - (hp.x - np.x))) / 2.0 +
                2 * stepx);
          break;
	default:
		break;
    }
    ty = fmin(dy, 3 * (np.y + dy - tp.y));
    hy = fmin(dy, 3 * (np.y + dy - hp.y));
    for (size_t i = 0; i < cnt; i++) {
        e = edges[ind++];
        dy += stepy;
        ty += stepy;
        hy += stepy;
        dx += sgn * stepx;
        pointf points[] = {
	    tp,
	    {tp.x + dx, tp.y + ty / 3},
	    {tp.x + dx, np.y + dy},
	    {(tp.x + hp.x) / 2, np.y + dy},
	    {hp.x - dx, np.y + dy},
	    {hp.x - dx, hp.y + hy / 3},
	    hp,
        };
        if (ED_label(e)) {
	    if (GD_flip(agraphof(agtail(e)))) {
		height = ED_label(e)->dimen.x;
	    } else {
		height = ED_label(e)->dimen.y;
	    }
	    ED_label(e)->pos.y = ND_coord(n).y + dy + height / 2.0;
	    ED_label(e)->pos.x = ND_coord(n).x;
	    ED_label(e)->set = true;
	    if (height > stepy)
		dy += height - stepy;
        }
        const size_t pointn = sizeof(points) / sizeof(points[0]);
       clip_and_install(e, aghead(e), points, pointn, sinfo);
#ifdef DEBUG
        if (debugleveln(e,1))
	    showPoints (points, pointn);
#endif
    }
}

static void selfRight(edge_t *edges[], size_t ind, size_t cnt, double stepx,
                      double sizey, splineInfo *sinfo) {
    int sgn, point_pair;
    double hx, tx, stepy, dx, dy, width;
    pointf tp, hp, np;
    node_t *n;
    edge_t *e;

    e = edges[ind];
    n = agtail(e);

    stepy = fmax(sizey / 2.0 / (double)cnt, 2.0);
    np = ND_coord(n);
    tp = ED_tail_port(e).p;
    tp.x += np.x;
    tp.y += np.y;
    hp = ED_head_port(e).p;
    hp.x += np.x;
    hp.y += np.y;
    if (tp.y >= hp.y) sgn = 1;
    else sgn = -1;
    dx = ND_rw(n), dy = 0;
    // certain adjustments are required for some point_pairs in order to improve the
    // display of the edge path between them
    point_pair = convert_sides_to_points(ED_tail_port(e).side,ED_head_port(e).side);
    switch(point_pair){
      case 32:
      case 65:	if(tp.y == hp.y)
		  sgn = -sgn;
		break;
      default:
		break;
    }
    tx = fmin(dx, 3 * (np.x + dx - tp.x));
    hx = fmin(dx, 3 * (np.x + dx - hp.x));
    for (size_t i = 0; i < cnt; i++) {
        e = edges[ind++];
        dx += stepx;
        tx += stepx;
        hx += stepx;
        dy += sgn * stepy;
        pointf points[] = {
	    tp,
	    {tp.x + tx / 3, tp.y + dy},
	    {np.x + dx, tp.y + dy},
	    {np.x + dx, (tp.y + hp.y) / 2},
	    {np.x + dx, hp.y - dy},
	    {hp.x + hx / 3, hp.y - dy},
	    hp,
        };
        if (ED_label(e)) {
	    if (GD_flip(agraphof(agtail(e)))) {
		width = ED_label(e)->dimen.y;
	    } else {
		width = ED_label(e)->dimen.x;
	    }
	    ED_label(e)->pos.x = ND_coord(n).x + dx + width / 2.0;
	    ED_label(e)->pos.y = ND_coord(n).y;
	    ED_label(e)->set = true;
	    if (width > stepx)
		dx += width - stepx;
        }
        const size_t pointn = sizeof(points) / sizeof(points[0]);
	clip_and_install(e, aghead(e), points, pointn, sinfo);
#ifdef DEBUG
        if (debugleveln(e,1))
	    showPoints (points, pointn);
#endif
    }
}

static void selfLeft(edge_t *edges[], size_t ind, size_t cnt, double stepx,
                     double sizey, splineInfo *sinfo) {
    int sgn,point_pair;
    double hx, tx, stepy, dx, dy, width;
    pointf tp, hp, np;
    node_t *n;
    edge_t *e;

    e = edges[ind];
    n = agtail(e);

    stepy = fmax(sizey / 2.0 / (double)cnt, 2.0);
    np = ND_coord(n);
    tp = ED_tail_port(e).p;
    tp.x += np.x;
    tp.y += np.y;
    hp = ED_head_port(e).p;
    hp.x += np.x;
    hp.y += np.y;


    if (tp.y >= hp.y) sgn = 1;
    else sgn = -1;
    dx = ND_lw(n), dy = 0.;
    // certain adjustments are required for some point_pairs in order to improve the
    // display of the edge path between them
    point_pair = convert_sides_to_points(ED_tail_port(e).side,ED_head_port(e).side);
    switch(point_pair){
      case 12:
      case 67:
		if(tp.y == hp.y)
		  sgn = -sgn;
		break;
      default:
		break;
    }
    tx = fmin(dx, 3 * (tp.x + dx - np.x));
    hx = fmin(dx, 3 * (hp.x + dx - np.x));
    for (size_t i = 0; i < cnt; i++) {
        e = edges[ind++];
        dx += stepx;
        tx += stepx;
        hx += stepx;
        dy += sgn * stepy;
        pointf points[] = {
    	    tp,
    	    {tp.x - tx / 3, tp.y + dy},
    	    {np.x - dx, tp.y + dy},
    	    {np.x - dx, (tp.y + hp.y) / 2},
    	    {np.x - dx, hp.y - dy},
    	    {hp.x - hx / 3, hp.y - dy},
    	    hp,
        };
        if (ED_label(e)) {
    	if (GD_flip(agraphof(agtail(e)))) {
    	    width = ED_label(e)->dimen.y;
    	} else {
    	    width = ED_label(e)->dimen.x;
    	}
    	ED_label(e)->pos.x = ND_coord(n).x - dx - width / 2.0;
    	ED_label(e)->pos.y = ND_coord(n).y;
    	ED_label(e)->set = true;
    	if (width > stepx)
    	    dx += width - stepx;
        }

        const size_t pointn = sizeof(points) / sizeof(points[0]);
        clip_and_install(e, aghead(e), points, pointn, sinfo);
#ifdef DEBUG
        if (debugleveln(e,1))
	    showPoints (points, pointn);
#endif
    }
}

/* selfRightSpace:
 * Assume e is self-edge.
 * Return extra space necessary on the right for this edge.
 * If the edge does not go on the right, return 0.
 * NOTE: the actual space is determined dynamically by GD_nodesep,
 * so using the constant SELF_EDGE_SIZE is going to be wrong.
 * Fortunately, the default nodesep is the same as SELF_EDGE_SIZE.
 */
double selfRightSpace(edge_t *e) {
    double sw;
    double label_width;
    textlabel_t* l = ED_label(e);

    if ((!ED_tail_port(e).defined && !ED_head_port(e).defined) ||
        (!(ED_tail_port(e).side & LEFT) &&
         !(ED_head_port(e).side & LEFT) &&
          (ED_tail_port(e).side != ED_head_port(e).side ||
          !(ED_tail_port(e).side & (TOP|BOTTOM))))) {
	sw = SELF_EDGE_SIZE;
	if (l) {
	    label_width = GD_flip(agraphof(aghead(e))) ? l->dimen.y : l->dimen.x;
	    sw += label_width;
	}
    }
    else sw = 0;
    return sw;
}

/* makeSelfEdge:
 * The routing is biased towards the right side because this is what
 * dot supports, and leaves room for.
 * FIX: With this bias, labels tend to be placed on top of each other.
 * Perhaps for self-edges, the label should be centered.
 */
void makeSelfEdge(edge_t *edges[], size_t ind, size_t cnt, double sizex,
                  double sizey, splineInfo * sinfo) {
    edge_t *e = edges[ind];

    /* self edge without ports or
     * self edge with all ports inside, on the right, or at most 1 on top
     * and at most 1 on bottom
     */

    if ((!ED_tail_port(e).defined && !ED_head_port(e).defined) ||
        (!(ED_tail_port(e).side & LEFT) &&
         !(ED_head_port(e).side & LEFT) &&
          (ED_tail_port(e).side != ED_head_port(e).side ||
          !(ED_tail_port(e).side & (TOP|BOTTOM))))) {
	selfRight(edges, ind, cnt, sizex, sizey, sinfo);
    }

    /* self edge with port on left side */
    else if ((ED_tail_port(e).side & LEFT) || (ED_head_port(e).side & LEFT)) {

	/* handle L-R specially */
	if ((ED_tail_port(e).side & RIGHT) || (ED_head_port(e).side & RIGHT)) {
	    selfTop(edges, ind, cnt, sizex, sizey, sinfo);
	}
	else {
	    selfLeft(edges, ind, cnt, sizex, sizey, sinfo);
	}
    }

    /* self edge with both ports on top side */
    else if (ED_tail_port(e).side & TOP) {
	selfTop(edges, ind, cnt, sizex, sizey, sinfo);
    }
    else if (ED_tail_port(e).side & BOTTOM) {
	selfBottom(edges, ind, cnt, sizex, sizey, sinfo);
    }

    else assert(0);
}

/* makePortLabels:
 * Add head and tail labels if necessary and update bounding box.
 */
void makePortLabels(edge_t * e)
{
    /* Only use this if labelangle or labeldistance is set for the edge;
     * otherwise, handle with external labels.
     */
    if (!E_labelangle && !E_labeldistance) return;

    if (ED_head_label(e) && !ED_head_label(e)->set) {
	if (place_portlabel(e, true))
	    updateBB(agraphof(agtail(e)), ED_head_label(e));
    }
    if (ED_tail_label(e) && !ED_tail_label(e)->set) {
	if (place_portlabel(e, false))
	    updateBB(agraphof(agtail(e)), ED_tail_label(e));
    }
}

/* endPoints:
 * Extract the actual end points of the spline, where
 * they touch the node.
 */
static void endPoints(splines * spl, pointf * p, pointf * q)
{
    bezier bz;

    bz = spl->list[0];
    if (bz.sflag) {
	*p = bz.sp;
    }
    else {
	*p = bz.list[0];
    }
    bz = spl->list[spl->size - 1];
    if (bz.eflag) {
	*q = bz.ep;
    }
    else {
	*q = bz.list[bz.size - 1];
    }
}

/* polylineMidpoint;
 * Find midpoint of polyline.
 * pp and pq are set to the endpoints of the line segment containing it.
 */
static pointf
polylineMidpoint (splines* spl, pointf* pp, pointf* pq)
{
    bezier bz;
    double d, dist = 0;
    pointf pf, qf, mf;

    for (size_t i = 0; i < spl->size; i++) {
	bz = spl->list[i];
	for (size_t j = 0, k = 3; k < bz.size; j += 3, k += 3) {
	    pf = bz.list[j];
	    qf = bz.list[k];
	    dist += DIST(pf, qf);
	}
    }
    dist /= 2;
    for (size_t i = 0; i < spl->size; i++) {
	bz = spl->list[i];
	for (size_t j = 0, k = 3; k < bz.size; j += 3, k += 3) {
	    pf = bz.list[j];
	    qf = bz.list[k];
	    d = DIST(pf,qf);
	    if (d >= dist) {
		*pp = pf;
		*pq = qf;
		mf.x = (qf.x * dist + pf.x * (d - dist)) / d;
		mf.y = (qf.y * dist + pf.y * (d - dist)) / d;
		return mf;
	    }
	    else
		dist -= d;
	}
    }
    UNREACHABLE();
}

pointf
edgeMidpoint (graph_t* g, edge_t * e)
{
    int et = EDGE_TYPE (g);
    pointf d, spf, p, q;

    endPoints(ED_spl(e), &p, &q);
    if (APPROXEQPT(p, q, MILLIPOINT)) { /* degenerate spline */
	spf = p;
    }
    else if (et == EDGETYPE_SPLINE || et == EDGETYPE_CURVED) {
	d.x = (q.x + p.x) / 2.;
	d.y = (p.y + q.y) / 2.;
	spf = dotneato_closest(ED_spl(e), d);
    }
    else {   /* EDGETYPE_PLINE, EDGETYPE_ORTHO or EDGETYPE_LINE */
	spf = polylineMidpoint (ED_spl(e), &p, &q);
    }

    return spf;
}

#define LEFTOF(a,b,c) (((a.y - b.y)*(c.x - b.x) - (c.y - b.y)*(a.x - b.x)) > 0)
#define MAXLABELWD  (POINTS_PER_INCH/2.0)

/* addEdgeLabels:
 * Adds label, headlabel and taillabel.
 * Updates bounding box.
 * We use the endpoints of the spline.
 */
void addEdgeLabels(edge_t *e) {
    makePortLabels(e);
}

#define AGXGET(o,a) agxget(o,a)

/* place_portlabel:
 * place the {head,tail}label (depending on HEAD_P) of edge E
 * N.B. Assume edges are normalized, so tail is at spl->list[0].list[0]
 * and head is at spl->list[spl->size-l].list[bez->size-1]
 * Return 1 on success
 */
int place_portlabel(edge_t * e, bool head_p)
{
    textlabel_t *l;
    splines *spl;
    bezier *bez;
    double dist, angle;
    pointf c[4], pe, pf;
    char* la;
    char* ld;

    if (ED_edge_type(e) == IGNORED)
	return 0;
    /* add label here only if labelangle or labeldistance is defined; else, use external label */
    if ((!E_labelangle || *(la = AGXGET(e,E_labelangle)) == '\0') &&
	(!E_labeldistance || *(ld = AGXGET(e,E_labeldistance)) == '\0')) {
	return 0;
    }

    l = head_p ? ED_head_label(e) : ED_tail_label(e);
    if ((spl = getsplinepoints(e)) == NULL) return 0;
    if (!head_p) {
	bez = &spl->list[0];
	if (bez->sflag) {
	    pe = bez->sp;
	    pf = bez->list[0];
	} else {
	    pe = bez->list[0];
	    for (size_t i = 0; i < 4; i++)
		c[i] = bez->list[i];
	    pf = Bezier(c, 0.1, NULL, NULL);
	}
    } else {
	bez = &spl->list[spl->size - 1];
	if (bez->eflag) {
	    pe = bez->ep;
	    pf = bez->list[bez->size - 1];
	} else {
	    pe = bez->list[bez->size - 1];
	    for (size_t i = 0; i < 4; i++)
		c[i] = bez->list[bez->size - 4 + i];
	    pf = Bezier(c, 0.9, NULL, NULL);
	}
    }
    angle = atan2(pf.y - pe.y, pf.x - pe.x) +
	RADIANS(late_double(e, E_labelangle, PORT_LABEL_ANGLE, -180.0));
    dist = PORT_LABEL_DISTANCE * late_double(e, E_labeldistance, 1.0, 0.0);
    l->pos.x = pe.x + dist * cos(angle);
    l->pos.y = pe.y + dist * sin(angle);
    l->set = true;
    return 1;
}

splines *getsplinepoints(edge_t * e)
{
    edge_t *le;
    splines *sp;

    for (le = e; !(sp = ED_spl(le)) && ED_edge_type(le) != NORMAL;
	 le = ED_to_orig(le));
    if (sp == NULL)
	agerrorf("getsplinepoints: no spline points available for edge (%s,%s)\n",
	    agnameof(agtail(e)), agnameof(aghead(e)));
    return sp;
}

