/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


#include "config.h"
#include "gdgen_text.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>

#include <gvc/gvplugin_render.h>
#include <gvc/gvio.h>
#include <gd.h>

#ifdef HAVE_GD_PNG

/* for gvcolor_t */
#include <common/color.h>

#include <cgraph/cgraph.h>
#include <common/render.h>
#include <util/agxbuf.h>
#include <util/alloc.h>
#include <util/strview.h>

/* for wind() */
#include <pathplan/pathutil.h>

enum { FORMAT_VRML, };

#define BEZIERSUBDIVISION 10

typedef struct {
  double Scale;
  double MinZ;
  bool Saw_skycolor;

  gdImagePtr im;
  FILE *PNGfile;
  int    IsSegment;   /* set true if edge is line segment */
  double CylHt;       /* height of cylinder part of edge */
  double EdgeLen;     /* length between centers of endpoints */
  double HeadHt, TailHt;  /* height of arrows */
  double Fstz, Sndz;  /* z values of tail and head points */
} state_t;

static void vrml_begin_job(GVJ_t *job) {
  job->context = gv_alloc(sizeof(state_t));
}

static void vrml_end_job(GVJ_t *job) {
  free(job->context);
}

/* gdirname:
 * Returns directory pathname prefix
 * Code adapted from dgk
 */
static strview_t gdirname(const char *pathname){
    /* go to end of path */
    size_t last = strlen(pathname);
    /* back over trailing '/' */
    while (last > 0 && pathname[--last] == '/');
    /* back over non-slash chars */
    for (; last > 0 && pathname[last] != '/'; last--);
    if (last == 0) {
	/* all '/' or "" */
	if (*pathname != '/')
	    return strview(".", '\0');
	/* preserve // */
	else if (pathname[1] == '/')
	    last++;
    } else {
	/* back over trailing '/' */
	for (; pathname[last] == '/' && last > 0; last--);
	/* preserve // */
	if (last == 0 && *pathname == '/' && pathname[1] == '/')
	    last++;
    }
    last++;

    return (strview_t){.data = pathname, .size = last};
}

static char *nodefilename(const char *filename, node_t *n, agxbuf *buf) {
    strview_t dir;

    if (filename)
	dir = gdirname(filename);
    else
	dir = strview(".", '\0');
    agxbprint(buf, "%.*s/node%d.png", (int)dir.size, dir.data, AGSEQ(n));
    return agxbuse(buf);
}

static FILE *nodefile(const char *filename, node_t * n)
{
    FILE *rv;
    agxbuf buf = {0};

    rv = fopen(nodefilename(filename, n, &buf), "wb");
    agxbfree(&buf);
    return rv;
}

#define NODE_PAD 1

static pointf vrml_node_point(GVJ_t *job, node_t *n, pointf p)
{
    pointf rv;
    state_t *state = job->context;

    /* make rv relative to PNG canvas */
    if (job->rotation) {
	rv.x = ( (p.y - job->pad.y) - ND_coord(n).y + ND_lw(n)     ) * state->Scale + NODE_PAD;
	rv.y = (-(p.x - job->pad.x) + ND_coord(n).x + ND_ht(n) / 2.) * state->Scale + NODE_PAD;
    } else {
	rv.x = ( (p.x - job->pad.x) - ND_coord(n).x + ND_lw(n)     ) * state->Scale + NODE_PAD;
	rv.y = (-(p.y - job->pad.y) + ND_coord(n).y + ND_ht(n) / 2.) * state->Scale + NODE_PAD;
    }
    return rv;
}

static int color_index(gdImagePtr im, gvcolor_t color)
{
    int alpha;

    /* convert alpha (normally an "opacity" value) to gd's "transparency" */
    alpha = (255 - color.u.rgba[3]) * gdAlphaMax / 255;

    if(alpha == gdAlphaMax)
        return gdImageGetTransparent(im);
    else
        return gdImageColorResolveAlpha(im,
		    color.u.rgba[0],
		    color.u.rgba[1],
		    color.u.rgba[2],
		    alpha);
}

static int set_penstyle(GVJ_t * job, gdImagePtr im, gdImagePtr brush)
{
    obj_state_t *obj = job->obj;
    int i, pen, pencolor, transparent, width, dashstyle[20];

    pen = pencolor = color_index(im, obj->pencolor);
    transparent = gdImageGetTransparent(im);
    if (obj->pen == PEN_DASHED) {
        for (i = 0; i < 10; i++)
            dashstyle[i] = pencolor;
        for (; i < 20; i++)
            dashstyle[i] = transparent;
        gdImageSetStyle(im, dashstyle, 20);
        pen = gdStyled;
    } else if (obj->pen == PEN_DOTTED) {
        for (i = 0; i < 2; i++)
            dashstyle[i] = pencolor;
        for (; i < 12; i++)
            dashstyle[i] = transparent;
        gdImageSetStyle(im, dashstyle, 12);
        pen = gdStyled;
    }
    width = obj->penwidth * job->scale.x;
    if (width < PENWIDTH_NORMAL)
        width = PENWIDTH_NORMAL;  /* gd can't do thin lines */
    gdImageSetThickness(im, width);
    /* use brush instead of Thickness to improve end butts */
    if (width != (int)PENWIDTH_NORMAL) {
        brush = gdImageCreate(width, width);
        gdImagePaletteCopy(brush, im);
        gdImageFilledRectangle(brush, 0, 0, width - 1, width - 1, pencolor);
        gdImageSetBrush(im, brush);
        if (pen == gdStyled)
            pen = gdStyledBrushed;
        else
            pen = gdBrushed;
    }
    return pen;
}

/* warmed over VRML code starts here */

static void vrml_begin_page(GVJ_t *job)
{
    state_t *state = job->context;
    state->Scale = (double) DEFAULT_DPI / POINTS_PER_INCH;
    gvputs(job,   "#VRML V2.0 utf8\n");

    state->Saw_skycolor = false;
    state->MinZ = DBL_MAX;
    gvputs(job,   "Group { children [\n"
                  "  Transform {\n");
    gvprintf(job, "    scale %.3f %.3f %.3f\n", .0278, .0278, .0278);
    gvputs(job,   "    children [\n");
}

static void vrml_end_page(GVJ_t *job)
{
    double d, z;
    box bb = job->boundingBox;
    state_t *state = job->context;

    d = MAX(bb.UR.x - bb.LL.x,bb.UR.y - bb.LL.y);
    /* Roughly fill 3/4 view assuming FOV angle of M_PI/4.
     * Small graphs and non-square aspect ratios will upset this.
     */
    z = (0.6667*d)/tan(M_PI/8.0) + state->MinZ;  /* fill 3/4 of view */

    if (!state->Saw_skycolor)
	gvputs(job,   " Background { skyColor 1 1 1 }\n");
    gvputs(job,   "  ] }\n");
    gvprintf(job, "  Viewpoint {position %.3f %.3f %.3f}\n",
	    state->Scale * (bb.UR.x + bb.LL.x) / 72.,
	    state->Scale * (bb.UR.y + bb.LL.y) / 72.,
	    state->Scale * 2 * z / 72.);
    gvputs(job,   "] }\n");
}

static void vrml_begin_node(GVJ_t *job)
{
    obj_state_t *obj = job->obj;
    node_t *n = obj->u.n;
    double z = obj->z;
    int width, height;
    int transparent;
    state_t *state = job->context;

    gvprintf(job, "# node %s\n", agnameof(n));
    if (z < state->MinZ)
	state->MinZ = z;
    if (shapeOf(n) != SH_POINT) {
	state->PNGfile = nodefile(job->output_filename, n);
	if (state->PNGfile == NULL) {
		agerrorf("failed to open file for writing PNG node image\n");
	}

	width  = (ND_lw(n) + ND_rw(n)) * state->Scale + 2 * NODE_PAD;
	height = (ND_ht(n)           ) * state->Scale + 2 * NODE_PAD;
	state->im = gdImageCreate(width, height);

	/* make background transparent */
	transparent = gdImageColorResolveAlpha(state->im,
                                           gdRedMax - 1, gdGreenMax,
                                           gdBlueMax, gdAlphaTransparent);
	gdImageColorTransparent(state->im, transparent);
    }
}

static void vrml_end_node(GVJ_t *job)
{
    state_t *state = job->context;
    if (state->im) {
	if (state->PNGfile != NULL) {
		gdImagePng(state->im, state->PNGfile);
		fclose(state->PNGfile);
	}
	gdImageDestroy(state->im);
	state->im = NULL;
    }
}

static void vrml_begin_edge(GVJ_t *job)
{
    obj_state_t *obj = job->obj;
    edge_t *e = obj->u.e;
    state_t *state = job->context;

    state->IsSegment = 0;
    gvprintf(job, "# edge %s -> %s\n", agnameof(agtail(e)), agnameof(aghead(e)));
    gvputs(job,   " Group { children [\n");
}

static void
finishSegment (GVJ_t *job, edge_t *e)
{
    pointf p0 = gvrender_ptf(job, ND_coord(agtail(e)));
    pointf p1 = gvrender_ptf(job, ND_coord(aghead(e)));
    double o_x, o_y, o_z;
    double x, y, y0, z, theta;
    state_t *state = job->context;

    o_x = ((double)(p0.x + p1.x))/2;
    o_y = ((double)(p0.y + p1.y))/2;
    o_z = (state->Fstz + state->Sndz) / 2;
    /* Compute rotation */
    /* Pick end point with highest y */
    if (p0.y > p1.y) {
	x = p0.x;
	y = p0.y;
        z = state->Fstz;
    }
    else {
	x = p1.x;
	y = p1.y;
        z = state->Sndz;
    }
    /* Translate center to the origin */
    x -= o_x;
    y -= o_y;
    z -= o_z;
    if (p0.y > p1.y)
	theta = acos(2 * y / state->EdgeLen) + M_PI;
    else
	theta = acos(2 * y / state->EdgeLen);
    if (!x && !z)   /* parallel  to y-axis */
	x = 1;

    y0 = (state->HeadHt - state->TailHt) / 2.0;
    gvputs(job,   "      ]\n");
    gvprintf(job, "      center 0 %.3f 0\n", y0);
    gvprintf(job, "      rotation %.3f 0 %.3f %.3f\n", -z, x, -theta);
    gvprintf(job, "      translation %.3f %.3f %.3f\n", o_x, o_y - y0, o_z);
    gvputs(job,   "    }\n");
}

static void vrml_end_edge(GVJ_t *job)
{
    state_t *state = job->context;

    if (state->IsSegment)
	finishSegment(job, job->obj->u.e);
    gvputs(job,   "] }\n");
}

static void vrml_textspan(GVJ_t *job, pointf p, textspan_t * span)
{
    obj_state_t *obj = job->obj;
    pointf spf, epf, q;
    state_t *state = job->context;

    if (!obj->u.n || !state->im)   /* if not a node - or if no im (e.g. for cluster) */
	return;

    switch (span->just) {
    case 'l':
	break;
    case 'r':
	p.x -= span->size.x;
	break;
    default:
    case 'n':
	p.x -= span->size.x / 2;
	break;
    }
    q.x = p.x + span->size.x;
    q.y = p.y;

    spf = vrml_node_point(job, obj->u.n, p);
    epf = vrml_node_point(job, obj->u.n, q);

    gdgen_text(state->im, spf, epf,
	color_index(state->im, obj->pencolor),
	span->font->size,
        DEFAULT_DPI,
	job->rotation ? (M_PI / 2) : 0,
	span->font->name,
	span->str);
}

/* interpolate_zcoord:
 * Given 2 points in 3D p = (fst.x,fst.y,fstz) and q = (snd.x, snd.y, sndz),
 * and a point p1 in the xy plane lying on the line segment connecting 
 * the projections of the p and q, find the z coordinate of p1 when it
 * is projected up onto the segment (p,q) in 3-space. 
 *
 * Why the special case for ranks? Is the arithmetic really correct?
 */
static double 
interpolate_zcoord(GVJ_t *job, pointf p1, pointf fst, double fstz, pointf snd, double sndz)
{
    obj_state_t *obj = job->obj;
    edge_t *e = obj->u.e;
    double len, d, rv;

    if (fstz == sndz)
	return fstz;
    if (ND_rank(agtail(e)) != ND_rank(aghead(e))) {
	if (snd.y == fst.y)
	    rv = (fstz + sndz) / 2.0;
	else
	    rv = fstz + (sndz - fstz) * (p1.y - fst.y) / (snd.y - fst.y);
    } 
    else {
	len = DIST(fst, snd);
	d = DIST(p1, fst)/len;
	rv = fstz + d*(sndz - fstz);
    }
    return rv;
}

/* collinear:
 * Return true if the 3 points starting at A are collinear.
 */
static int
collinear (pointf * A)
{
    double w;

    w = wind(A[0],A[1],A[2]);
    return fabs(w) <= 1;
}

/* straight:
 * Return true if bezier points are collinear
 * At present, just check with 4 points, the common case.
 */
static int straight(pointf *A, size_t n) {
    if (n != 4) return 0;
    return collinear(A) && collinear(A + 1);
}

static void
doSegment (GVJ_t *job, pointf* A, pointf p0, double z0, pointf p1, double z1)
{
    obj_state_t *obj = job->obj;
    double d1, d0;
    double delx, dely, delz;
    state_t *state = job->context;

    delx = p0.x - p1.x;
    dely = p0.y - p1.y;
    delz = z0 - z1;
    state->EdgeLen = sqrt(delx*delx + dely*dely + delz*delz);
    d0 = DIST(A[0],p0);
    d1 = DIST(A[3],p1);
    state->CylHt = state->EdgeLen - d0 - d1;
    state->TailHt = state->HeadHt = 0;

    state->IsSegment = 1;
    gvputs(job,   "Transform {\n"
                  "  children [\n"
                  "    Shape {\n"
                  "      geometry Cylinder {\n"
                  "        bottom FALSE top FALSE\n");
    gvprintf(job, "        height %.3f radius %.3f }\n", state->CylHt, obj->penwidth);
    gvputs(job,   "      appearance Appearance {\n"
                  "        material Material {\n"
                  "          ambientIntensity 0.33\n");
    gvprintf(job, "          diffuseColor %.3f %.3f %.3f\n",
	    obj->pencolor.u.rgba[0] / 255.,
	    obj->pencolor.u.rgba[1] / 255.,
	    obj->pencolor.u.rgba[2] / 255.);
    gvputs(job,   "        }\n"
                  "      }\n"
                  "    }\n");
}

/* nearTail:
 * Given a point a and edge e, return true if a is closer to the
 * tail of e than the head.
 */
static int
nearTail (GVJ_t* job, pointf a, Agedge_t* e)
{
    pointf tp = gvrender_ptf(job, ND_coord(agtail(e)));
    pointf hp = gvrender_ptf(job, ND_coord(aghead(e)));

    return (DIST2(a, tp) < DIST2(a, hp));
}

    /* this is gruesome, but how else can we get z coord */
#define GETZ(jp,op,p,e) (nearTail(jp,p,e)?op->tail_z:op->head_z) 

static void vrml_bezier(GVJ_t *job, pointf *A, size_t n, int filled) {
    (void)filled;

    obj_state_t *obj = job->obj;
    edge_t *e = obj->u.e;
    double fstz, sndz;
    pointf p1, V[4];
    int step;
    state_t *state = job->context;

    assert(e);

    fstz = state->Fstz = obj->tail_z; 
    sndz = state->Sndz = obj->head_z;
    if (straight(A, n)) {
	doSegment (job, A, gvrender_ptf(job, ND_coord(agtail(e))),state->Fstz,gvrender_ptf(job, ND_coord(aghead(e))),state->Sndz);
	return;
    }

    gvputs(job,   "Shape { geometry Extrusion  {\n"
                  "  spine [");
    V[3] = A[0];
    for (size_t i = 0; i + 3 < n; i += 3) {
	V[0] = V[3];
	for (size_t j = 1; j <= 3; j++)
	    V[j] = A[i + j];
	for (step = 0; step <= BEZIERSUBDIVISION; step++) {
	    p1 = Bezier(V, (double)step / BEZIERSUBDIVISION, NULL, NULL);
	    gvprintf(job, " %.3f %.3f %.3f", p1.x, p1.y,
		    interpolate_zcoord(job, p1, A[0], fstz, A[n - 1], sndz));
	}
    }
    gvputs(job,   " ]\n");
    gvprintf(job, "  crossSection [ %.3f %.3f, %.3f %.3f, %.3f %.3f, %.3f %.3f ]\n",
	    (obj->penwidth), (obj->penwidth), -(obj->penwidth),
	    (obj->penwidth), -(obj->penwidth), -(obj->penwidth),
	    (obj->penwidth), -(obj->penwidth));
    gvputs(job,   "}\n");
    gvprintf(job, " appearance DEF E%d Appearance {\n", AGSEQ(e));
    gvputs(job,   "   material Material {\n"
                  "   ambientIntensity 0.33\n");
    gvprintf(job, "   diffuseColor %.3f %.3f %.3f\n",
	    obj->pencolor.u.rgba[0] / 255.,
	    obj->pencolor.u.rgba[1] / 255.,
	    obj->pencolor.u.rgba[2] / 255.);
    gvputs(job,   "   }\n"
                  " }\n"
                  "}\n");
}

/* doArrowhead:
 * If edge is straight, we attach a cone to the edge as a group.
 */
static void doArrowhead (GVJ_t *job, pointf * A)
{
    obj_state_t *obj = job->obj;
    edge_t *e = obj->u.e;
    double rad, ht, y;
    pointf p0;      /* center of triangle base */
    state_t *state = job->context;

    p0.x = (A[0].x + A[2].x)/2.0;
    p0.y = (A[0].y + A[2].y)/2.0;
    rad = DIST(A[0],A[2])/2.0;
    ht = DIST(p0,A[1]);

    y = (state->CylHt + ht)/2.0;

    gvputs(job,   "Transform {\n");
    if (nearTail (job, A[1], e)) {
	state->TailHt = ht;
	gvprintf(job, "  translation 0 %.3f 0\n", -y);
	gvprintf(job, "  rotation 0 0 1 %.3f\n", M_PI);
    }
    else {
	state->HeadHt = ht;
	gvprintf(job, "  translation 0 %.3f 0\n", y);
    }
    gvputs(job,   "  children [\n"
                  "    Shape {\n");
    gvprintf(job, "      geometry Cone {bottomRadius %.3f height %.3f }\n",
	rad, ht);
    gvputs(job,   "      appearance Appearance {\n"
                  "        material Material {\n"
                  "          ambientIntensity 0.33\n");
    gvprintf(job, "          diffuseColor %.3f %.3f %.3f\n",
	    obj->pencolor.u.rgba[0] / 255.,
	    obj->pencolor.u.rgba[1] / 255.,
	    obj->pencolor.u.rgba[2] / 255.);
    gvputs(job,   "        }\n"
                  "      }\n"
                  "    }\n"
                  "  ]\n"
                  "}\n");
}

static void vrml_polygon(GVJ_t *job, pointf *A, size_t np, int filled) {
    obj_state_t *obj = job->obj;
    node_t *n;
    edge_t *e;
    double z = obj->z;
    pointf p, mp;
    gdPoint *points;
    int pen;
    gdImagePtr brush = NULL;
    double theta;
    state_t *state = job->context;

    switch (obj->type) {
    case ROOTGRAPH_OBJTYPE:
	gvprintf(job, " Background { skyColor %.3f %.3f %.3f }\n",
	    obj->fillcolor.u.rgba[0] / 255.,
	    obj->fillcolor.u.rgba[1] / 255.,
	    obj->fillcolor.u.rgba[2] / 255.);
	state->Saw_skycolor = true;
	break;
    case CLUSTER_OBJTYPE:
	break;
    case NODE_OBJTYPE:
	n = obj->u.n;
	pen = set_penstyle(job, state->im, brush);
	points = gv_calloc(np, sizeof(gdPoint));
	for (size_t i = 0; i < np; i++) {
	    mp = vrml_node_point(job, n, A[i]);
	    points[i].x = ROUND(mp.x);
	    points[i].y = ROUND(mp.y);
	}
	assert(np <= INT_MAX);
	if (filled)
	    gdImageFilledPolygon(state->im, points, (int)np, color_index(state->im, obj->fillcolor));
	gdImagePolygon(state->im, points, (int)np, pen);
	free(points);
	if (brush)
	    gdImageDestroy(brush);

	gvputs(job,   "Shape {\n"
	              "  appearance Appearance {\n"
	              "    material Material {\n"
	              "      ambientIntensity 0.33\n"
	              "        diffuseColor 1 1 1\n"
	              "    }\n");
	gvprintf(job, "    texture ImageTexture { url \"node%d.png\" }\n", AGSEQ(n));
	gvputs(job,   "  }\n"
	              "  geometry Extrusion {\n"
	              "    crossSection [");
	for (size_t i = 0; i < np; i++) {
	    p.x = A[i].x - ND_coord(n).x;
	    p.y = A[i].y - ND_coord(n).y;
	    gvprintf(job, " %.3f %.3f,", p.x, p.y);
	}
	p.x = A[0].x - ND_coord(n).x;
	p.y = A[0].y - ND_coord(n).y;
	gvprintf(job, " %.3f %.3f ]\n", p.x, p.y);
	gvprintf(job, "    spine [ %.5g %.5g %.5g, %.5g %.5g %.5g ]\n",
		ND_coord(n).x, ND_coord(n).y, z - .01,
		ND_coord(n).x, ND_coord(n).y, z + .01);
	gvputs(job,   "  }\n"
	              "}\n");
	break;
    case EDGE_OBJTYPE:
	e = obj->u.e;
	if (np != 3) {
	    static atomic_flag flag;
	    if (!atomic_flag_test_and_set(&flag)) {
		agwarningf(
		  "vrml_polygon: non-triangle arrowheads not supported - ignoring\n");
	    }
	}
	if (state->IsSegment) {
	    doArrowhead (job, A);
	    return;
	}
	p.x = p.y = 0.0;
	for (size_t i = 0; i < np; i++) {
	    p.x += A[i].x;
	    p.y += A[i].y;
	}
	p.x /= (int)np;
	p.y /= (int)np;

	/* it is bad to know that A[1] is the aiming point, but we do */
	theta =
	    atan2((A[0].y + A[2].y) / 2.0 - A[1].y,
		  (A[0].x + A[2].x) / 2.0 - A[1].x) + M_PI / 2.0;

	z = GETZ(job,obj,p,e);

	/* FIXME: arrow vector ought to follow z coord of bezier */
	gvputs(job,   "Transform {\n");
	gvprintf(job, "  translation %.3f %.3f %.3f\n", p.x, p.y, z);
	gvputs(job,   "  children [\n"
	              "    Transform {\n");
	gvprintf(job, "      rotation 0 0 1 %.3f\n", theta);
	gvputs(job,   "      children [\n"
	              "        Shape {\n");
	gvprintf(job, "          geometry Cone {bottomRadius %.3f height %.3f }\n",
		obj->penwidth * 2.5, obj->penwidth * 10.0);
	gvprintf(job, "          appearance USE E%d\n", AGSEQ(e));
	gvputs(job,   "        }\n"
	              "      ]\n"
	              "    }\n"
	              "  ]\n"
	              "}\n");
	break;
    }
}

/* doSphere:
 * Output sphere in VRML for point nodes.
 */
static void doSphere(GVJ_t *job, pointf p, double z, double rx) {
    obj_state_t *obj = job->obj;

    gvputs(job,   "Transform {\n");
    gvprintf(job, "  translation %.3f %.3f %.3f\n", p.x, p.y, z);
    gvprintf(job, "  scale %.3f %.3f %.3f\n", rx, rx, rx);
    gvputs(job,   "  children [\n"
                  "    Transform {\n"
                  "      children [\n"
                  "        Shape {\n"
                  "          geometry Sphere { radius 1.0 }\n"
                  "          appearance Appearance {\n"
                  "            material Material {\n"
                  "              ambientIntensity 0.33\n");
    gvprintf(job, "              diffuseColor %.3f %.3f %.3f\n", 
	    obj->pencolor.u.rgba[0] / 255.,
	    obj->pencolor.u.rgba[1] / 255.,
	    obj->pencolor.u.rgba[2] / 255.);
    gvputs(job,   "            }\n"
                  "          }\n"
                  "        }\n"
                  "      ]\n"
                  "    }\n"
                  "  ]\n"
                  "}\n");
}

static void vrml_ellipse(GVJ_t * job, pointf * A, int filled)
{
    obj_state_t *obj = job->obj;
    node_t *n;
    edge_t *e;
    double z = obj->z;
    double rx, ry;
    int dx, dy;
    pointf npf, nqf;
    point np;
    int pen;
    gdImagePtr brush = NULL;
    state_t *state = job->context;

    rx = A[1].x - A[0].x;
    ry = A[1].y - A[0].y;

    switch (obj->type) {
    case ROOTGRAPH_OBJTYPE:
    case CLUSTER_OBJTYPE:
	break;
    case NODE_OBJTYPE:
	n = obj->u.n;
	if (shapeOf(n) == SH_POINT) {
	    doSphere(job, A[0], z, rx);
	    return;
	}
	pen = set_penstyle(job, state->im, brush);

	npf = vrml_node_point(job, n, A[0]);
	nqf = vrml_node_point(job, n, A[1]);

	dx = ROUND(2 * (nqf.x - npf.x));
	dy = ROUND(2 * (nqf.y - npf.y));

	PF2P(npf, np);

	if (filled)
	    gdImageFilledEllipse(state->im, np.x, np.y, dx, dy, color_index(state->im, obj->fillcolor));
	gdImageArc(state->im, np.x, np.y, dx, dy, 0, 360, pen);

	if (brush)
	    gdImageDestroy(brush);

	gvputs(job,   "Transform {\n");
	gvprintf(job, "  translation %.3f %.3f %.3f\n", A[0].x, A[0].y, z);
	gvprintf(job, "  scale %.3f %.3f 1\n", rx, ry);
	gvputs(job,   "  children [\n"
	              "    Transform {\n"
	              "      rotation 1 0 0   1.57\n"
	              "      children [\n"
	              "        Shape {\n"
	              "          geometry Cylinder { side FALSE }\n"
	              "          appearance Appearance {\n"
	              "            material Material {\n"
	              "              ambientIntensity 0.33\n"
	              "              diffuseColor 1 1 1\n"
	              "            }\n");
	gvprintf(job, "            texture ImageTexture { url \"node%d.png\" }\n", AGSEQ(n));
	gvputs(job,   "          }\n"
	              "        }\n"
	              "      ]\n"
	              "    }\n"
	              "  ]\n"
	              "}\n");
	break;
    case EDGE_OBJTYPE:
	e = obj->u.e;
	z = GETZ(job,obj,A[0],e);

	gvputs(job,   "Transform {\n");
	gvprintf(job, "  translation %.3f %.3f %.3f\n", A[0].x, A[0].y, z);
	gvputs(job,   "  children [\n"
	              "    Shape {\n");
	gvprintf(job, "      geometry Sphere {radius %.3f }\n", (double) rx);
	gvprintf(job, "      appearance USE E%d\n", AGSEQ(e));
	gvputs(job,   "    }\n"
	              "  ]\n"
	              "}\n");
    }
}

static gvrender_engine_t vrml_engine = {
    vrml_begin_job,
    vrml_end_job,
    0,                          /* vrml_begin_graph */
    0,                          /* vrml_end_graph */
    0,                          /* vrml_begin_layer */
    0,                          /* vrml_end_layer */
    vrml_begin_page,
    vrml_end_page,
    0,                          /* vrml_begin_cluster */
    0,                          /* vrml_end_cluster */
    0,                          /* vrml_begin_nodes */
    0,                          /* vrml_end_nodes */
    0,                          /* vrml_begin_edges */
    0,                          /* vrml_end_edges */
    vrml_begin_node,
    vrml_end_node,
    vrml_begin_edge,
    vrml_end_edge,
    0,                          /* vrml_begin_anchor */
    0,                          /* vrml_end_anchor */
    0,                          /* vrml_begin_label */
    0,                          /* vrml_end_label */
    vrml_textspan,
    0,				/* vrml_resolve_color */
    vrml_ellipse,
    vrml_polygon,
    vrml_bezier,
    0,				/* vrml_polyline  - FIXME */
    0,                          /* vrml_comment */
    0,                          /* vrml_library_shape */
};

static gvrender_features_t render_features_vrml = {
    GVRENDER_DOES_Z, 		/* flags */
    0.,                         /* default pad - graph units */
    NULL,                       /* knowncolors */
    0,                          /* sizeof knowncolors */
    RGBA_BYTE,                  /* color_type */
};

static gvdevice_features_t device_features_vrml = {
    GVDEVICE_BINARY_FORMAT
      | GVDEVICE_NO_WRITER,	/* flags */
    {0.,0.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.},                  /* default dpi */
};
#endif				/* HAVE_GD_PNG */

gvplugin_installed_t gvrender_vrml_types[] = {
#ifdef HAVE_GD_PNG
    {FORMAT_VRML, "vrml", 1, &vrml_engine, &render_features_vrml},
#endif
    {0, NULL, 0, NULL, NULL}
};

gvplugin_installed_t gvdevice_vrml_types[] = {
#ifdef HAVE_GD_PNG
    {FORMAT_VRML, "vrml:vrml", 1, NULL, &device_features_vrml},
#endif
    {0, NULL, 0, NULL, NULL}
};
