/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/
#include "topfisheyeview.h"
#include <math.h>
#include "viewport.h"
#include "viewportcamera.h"
#include "draw.h"
#include "smyrna_utils.h"

#include <assert.h>
#include "hier.h"
#include "topfisheyeview.h"
#include <string.h>
#include <common/color.h>
#include <common/colorprocs.h>
#include <util/alloc.h>

static int get_temp_coords(topview *t, int level, int v, float *coord_x,
                           float *coord_y);

static int color_interpolation(glCompColor srcColor, glCompColor tarColor,
				glCompColor * color, int levelcount,
				int level)
{
    if (levelcount <= 0)
	return -1;


    color->R =
	((float) level * tarColor.R - (float) level * srcColor.R +
	 (float) levelcount * srcColor.R) / (float) levelcount;
    color->G =
	((float) level * tarColor.G - (float) level * srcColor.G +
	 (float) levelcount * srcColor.G) / (float) levelcount;
    color->B =
	((float) level * tarColor.B - (float) level * srcColor.B +
	 (float) levelcount * srcColor.B) / (float) levelcount;
    return 0;
}

static v_data *makeGraph(Agraph_t* gg, int *nedges)
{
    int ne = agnedges(gg);
    int nv = agnnodes(gg);
    v_data *graph = gv_calloc(nv, sizeof(v_data));
    int *edges = gv_calloc(2 * ne + nv, sizeof(int));	/* reserve space for self loops */
    float *ewgts = gv_calloc(2 * ne + nv, sizeof(float));
    Agraph_t *g = NULL;
    ne = 0;
    int i = 0;
    for (Agnode_t *np = agfstnode(gg); np; np = agnxtnode(gg, np)) {
	graph[i].edges = edges++;	/* reserve space for the self loop */
	graph[i].ewgts = ewgts++;
	int i_nedges = 1; // one for the self

	if (!g)
	    g = agraphof(np);
	for (Agedge_t *ep = agfstedge(g, np); ep; ep = agnxtedge(g, ep, np)) {
	    Agnode_t *tp = agtail(ep);
	    Agnode_t *hp = aghead(ep);
	    assert(hp != tp);
	    /* FIX: handle multiedges */
	    Agnode_t *const vp = tp == np ? hp : tp;
	    ne++;
	    i_nedges++;
	    *edges++ = ND_TVref(vp);
	    *ewgts++ = 1;

	}

	graph[i].nedges = i_nedges;
	graph[i].edges[0] = i;
	graph[i].ewgts[0] = 1 - i_nedges;
	i++;
    }
    ne /= 2;			/* each edge counted twice */
    *nedges = ne;
    return graph;
}

static void refresh_old_values(topview * t)
{
    int level, v;
    Hierarchy *hp = t->fisheyeParams.h;
    for (level = 0; level < hp->nlevels; level++) {
	for (v = 0; v < hp->nvtxs[level]; v++) {
	    ex_vtx_data *gg = hp->geom_graphs[level];
	    gg[v].old_physical_x_coord = gg[v].physical_x_coord;
	    gg[v].old_physical_y_coord = gg[v].physical_y_coord;
	    gg[v].old_active_level = gg[v].active_level;
	}
    }

}

/* To use:
 * double* x_coords; // initial x coordinates
 * double* y_coords; // initial y coordinates
 * focus_t* fs;
 * int ne;
 * v_data* graph = makeGraph (topview*, &ne);
 * hierarchy = makeHier(topview->NodeCount, ne, graph, x_coords, y_coords);
 * freeGraph (graph);
 * fs = initFocus (topview->Nodecount); // create focus set
 */
void prepare_topological_fisheye(Agraph_t* g,topview * t)
{
    double *x_coords = gv_calloc(t->Nodecount, sizeof(double));	// initial x coordinates
    double *y_coords = gv_calloc(t->Nodecount, sizeof(double));	// initial y coordinates
    focus_t *fs;
    int ne;
    int i;
    int closest_fine_node;
    int cur_level = 0;
    Hierarchy *hp;
    gvcolor_t cl;
    Agnode_t *np;

    v_data *graph = makeGraph(g, &ne);

    i=0;
    for (np = agfstnode(g); np; np = agnxtnode(g, np))
    {
	x_coords[i]=ND_A(np).x;
	y_coords[i]=ND_A(np).y;
	i++;
    }
    hp = t->fisheyeParams.h =
	makeHier(agnnodes(g), ne, graph, x_coords, y_coords,
		 t->fisheyeParams.dist2_limit);
    freeGraph(graph);
    free(x_coords);
    free(y_coords);

    fs = t->fisheyeParams.fs = initFocus(agnnodes(g));	// create focus set

    closest_fine_node = 0;	/* first node */
    fs->num_foci = 1;
    fs->foci_nodes[0] = closest_fine_node;
    fs->x_foci[0] = hp->geom_graphs[cur_level][closest_fine_node].x_coord;
    fs->y_foci[0] = hp->geom_graphs[cur_level][closest_fine_node].y_coord;

    view->Topview->fisheyeParams.repos.width =
	(int) (view->bdxRight - view->bdxLeft);
    view->Topview->fisheyeParams.repos.height =
	(int) (view->bdyTop - view->bdyBottom);

    //topological fisheye

    colorxlate(get_attribute_value
	       ("topologicalfisheyefinestcolor", view,
		view->g[view->activeGraph]), &cl, RGBA_DOUBLE);
    view->Topview->fisheyeParams.srcColor.R = (float) cl.u.RGBA[0];
    view->Topview->fisheyeParams.srcColor.G = (float) cl.u.RGBA[1];
    view->Topview->fisheyeParams.srcColor.B = (float) cl.u.RGBA[2];
    colorxlate(get_attribute_value
	       ("topologicalfisheyecoarsestcolor", view,
		view->g[view->activeGraph]), &cl, RGBA_DOUBLE);
    view->Topview->fisheyeParams.tarColor.R = (float) cl.u.RGBA[0];
    view->Topview->fisheyeParams.tarColor.G = (float) cl.u.RGBA[1];
    view->Topview->fisheyeParams.tarColor.B = (float) cl.u.RGBA[2];


    sscanf(agget
	   (view->g[view->activeGraph],
	    "topologicalfisheyedistortionfactor"), "%lf",
	   &view->Topview->fisheyeParams.repos.distortion);
    sscanf(agget
	   (view->g[view->activeGraph], "topologicalfisheyefinenodes"),
	   "%d", &view->Topview->fisheyeParams.level.num_fine_nodes);
    sscanf(agget
	   (view->g[view->activeGraph],
	    "topologicalfisheyecoarseningfactor"), "%lf",
	   &view->Topview->fisheyeParams.level.coarsening_rate);
    int dist2_limit = 0;
    sscanf(agget
	   (view->g[view->activeGraph], "topologicalfisheyedist2limit"),
	   "%d", &dist2_limit);
    view->Topview->fisheyeParams.dist2_limit = dist2_limit != 0;
    sscanf(agget(view->g[view->activeGraph], "topologicalfisheyeanimate"),
	   "%d", &view->Topview->fisheyeParams.animate);

    set_active_levels(hp, fs->foci_nodes, fs->num_foci, &t->fisheyeParams.level);
    positionAllItems(hp, fs, &t->fisheyeParams.repos);
    refresh_old_values(t);
}

static void drawtopfishnodes(topview * t)
{
    glCompColor color;
    int level, v;
    Hierarchy *hp = t->fisheyeParams.h;
    static int max_visible_level = 0;
    const glCompColor srcColor = view->Topview->fisheyeParams.srcColor;
    const glCompColor tarColor = view->Topview->fisheyeParams.tarColor;

    //drawing nodes
    glPointSize(7);
    level = 0;
    glBegin(GL_POINTS);
    for (level = 0; level < hp->nlevels; level++) {
	for (v = 0; v < hp->nvtxs[level]; v++) {
	    float x0, y0;
	    if (get_temp_coords(t, level, v, &x0, &y0)) {

		if (!(-x0 / view->zoom > view->clipX1 && -x0 / view->zoom < view->clipX2 &&
		      -y0 / view->zoom > view->clipY1 && -y0 / view->zoom < view->clipY2))
		    continue;

		if (max_visible_level < level)
		    max_visible_level = level;
		if (color_interpolation(srcColor, tarColor, &color, max_visible_level, level)
		    != 0) {
		    continue;
		}
		glColor4f(color.R, color.G, color.B, view->defaultnodealpha);

		glVertex3f(x0, y0, 0.0f);
	    }
	}
    }
    glEnd();

}

static void drawtopfishedges(topview * t)
{
    glCompColor color;

    int level, v, i, n;
    Hierarchy *hp = t->fisheyeParams.h;
    static int max_visible_level = 0;
    const glCompColor srcColor = view->Topview->fisheyeParams.srcColor;
    const glCompColor tarColor = view->Topview->fisheyeParams.tarColor;

    //and edges
    glBegin(GL_LINES);
    for (level = 0; level < hp->nlevels; level++) {
	for (v = 0; v < hp->nvtxs[level]; v++) {
	    v_data *g = hp->graphs[level];
	    float x0, y0;
	    if (get_temp_coords(t, level, v, &x0, &y0)) {
		for (i = 1; i < g[v].nedges; i++) {
		    float x, y;
		    n = g[v].edges[i];


		    if (max_visible_level < level)
			max_visible_level = level;
		    if (color_interpolation(srcColor, tarColor, &color, max_visible_level,
		        level) != 0) {
			continue;
		    }
		    glColor4f(color.R, color.G, color.B, view->defaultnodealpha);

		    if (get_temp_coords(t, level, n, &x, &y)) {
			glVertex3f(x0, y0, 0.0f);
			glVertex3f(x, y, 0.0f);
		    } else
		    {
			int levell, nodee;
			find_active_ancestor_info(hp, level, n, &levell,
						  &nodee);
			if (get_temp_coords(t, levell, nodee, &x, &y)) {

			    if (!(-x0 / view->zoom > view->clipX1
				    && -x0 / view->zoom < view->clipX2
				    && -y0 / view->zoom > view->clipY1
				    && -y0 / view->zoom < view->clipY2)
				&& !(-x / view->zoom > view->clipX1
				       && -x / view->zoom < view->clipX2
				       && -y / view->zoom > view->clipY1
				       && -y / view->zoom < view->clipY2))

				continue;

			    glVertex3f(x0, y0, 0.0f);
			    glVertex3f(x, y, 0.0f);
			}
		    }
		}
	    }
	}
    }
    glEnd();

}

static void get_active_frame(void) {
    gdouble seconds = g_timer_elapsed(view->timer, NULL);
    const int fr = (int)(seconds * 1000.0);
    if (fr < view->total_frames) {

	if (fr == view->active_frame)
	    return;
	view->active_frame = fr;
	return;
    }
    g_timer_stop(view->timer);
    view->Topview->fisheyeParams.animate = 0;
}

void drawtopologicalfisheye(topview * t)
{
    get_active_frame();
    drawtopfishnodes(t);
    drawtopfishedges(t);
}

static void get_interpolated_coords(float x0, float y0, float x1, float y1,
                                    int fr, int total_fr, float *x, float *y) {
  *x = x0 + (x1 - x0) / (float)total_fr * (float)(fr + 1);
  *y = y0 + (y1 - y0) / (float)total_fr * (float)(fr + 1);
}

static int get_temp_coords(topview *t, int level, int v, float *coord_x,
                           float *coord_y) {
    Hierarchy *hp = t->fisheyeParams.h;
    ex_vtx_data *gg = hp->geom_graphs[level];

    if (!t->fisheyeParams.animate) {
	if (gg[v].active_level != level)
	    return 0;

	*coord_x = gg[v].physical_x_coord;
	*coord_y = gg[v].physical_y_coord;
    } else {


	int OAL, AL;

	float x0 = 0;
	float y0 = 0;
	float x1 = 0;
	float y1 = 0;
	AL = gg[v].active_level;
	OAL = gg[v].old_active_level;

	if (OAL < level || AL < level)	//no draw
	    return 0;
	if (OAL >= level || AL >= level)	//draw the node
	{
	    if (OAL == level && AL == level)	//draw as is from old coords to new)
	    {
		x0 = gg[v].old_physical_x_coord;
		y0 = gg[v].old_physical_y_coord;
		x1 = gg[v].physical_x_coord;
		y1 = gg[v].physical_y_coord;
	    }
	    if (OAL > level && AL == level)	//draw as  from ancs  to new)
	    {
		find_old_physical_coords(t->fisheyeParams.h, level, v, &x0, &y0);
		x1 = gg[v].physical_x_coord;
		y1 = gg[v].physical_y_coord;
	    }
	    if (OAL == level && AL > level)	//draw as  from ancs  to new)
	    {
		find_physical_coords(t->fisheyeParams.h, level, v, &x1, &y1);
		x0 = gg[v].old_physical_x_coord;
		y0 = gg[v].old_physical_y_coord;
	    }
	    get_interpolated_coords(x0, y0, x1, y1, view->active_frame,
				    view->total_frames, coord_x, coord_y);
	    if (x0 == 0 || x1 == 0)
		return 0;

	}
    }
    return 1;
}

void changetopfishfocus(topview * t, float *x, float *y, int num_foci)
{

    gvcolor_t cl;
    focus_t *fs = t->fisheyeParams.fs;
    int i;
    int closest_fine_node;
    int cur_level = 0;

    Hierarchy *hp = t->fisheyeParams.h;
    refresh_old_values(t);
    fs->num_foci = num_foci;
    for (i = 0; i < num_foci; i++) {
	find_closest_active_node(hp, x[i], y[i], &closest_fine_node);
	fs->foci_nodes[i] = closest_fine_node;
	fs->x_foci[i] =
	    hp->geom_graphs[cur_level][closest_fine_node].x_coord;
	fs->y_foci[i] =
	    hp->geom_graphs[cur_level][closest_fine_node].y_coord;
    }


    view->Topview->fisheyeParams.repos.width =
	(int) (view->bdxRight - view->bdxLeft);
    view->Topview->fisheyeParams.repos.height =
	(int) (view->bdyTop - view->bdyBottom);

    colorxlate(get_attribute_value
	       ("topologicalfisheyefinestcolor", view,
		view->g[view->activeGraph]), &cl, RGBA_DOUBLE);
    view->Topview->fisheyeParams.srcColor.R = (float) cl.u.RGBA[0];
    view->Topview->fisheyeParams.srcColor.G = (float) cl.u.RGBA[1];
    view->Topview->fisheyeParams.srcColor.B = (float) cl.u.RGBA[2];
    colorxlate(get_attribute_value
	       ("topologicalfisheyecoarsestcolor", view,
		view->g[view->activeGraph]), &cl, RGBA_DOUBLE);
    view->Topview->fisheyeParams.tarColor.R = (float) cl.u.RGBA[0];
    view->Topview->fisheyeParams.tarColor.G = (float) cl.u.RGBA[1];
    view->Topview->fisheyeParams.tarColor.B = (float) cl.u.RGBA[2];



    sscanf(agget
	   (view->g[view->activeGraph],
	    "topologicalfisheyedistortionfactor"), "%lf",
	   &view->Topview->fisheyeParams.repos.distortion);
    sscanf(agget
	   (view->g[view->activeGraph], "topologicalfisheyefinenodes"),
	   "%d", &view->Topview->fisheyeParams.level.num_fine_nodes);
    sscanf(agget
	   (view->g[view->activeGraph],
	    "topologicalfisheyecoarseningfactor"), "%lf",
	   &view->Topview->fisheyeParams.level.coarsening_rate);
    int dist2_limit = 0;
    sscanf(agget
	   (view->g[view->activeGraph], "topologicalfisheyedist2limit"),
	   "%d", &dist2_limit);
    view->Topview->fisheyeParams.dist2_limit = dist2_limit != 0;
    sscanf(agget(view->g[view->activeGraph], "topologicalfisheyeanimate"),
	   "%d", &view->Topview->fisheyeParams.animate);

    set_active_levels(hp, fs->foci_nodes, fs->num_foci, &t->fisheyeParams.level);

    positionAllItems(hp, fs, &t->fisheyeParams.repos);

    view->Topview->fisheyeParams.animate = 1;

    if (t->fisheyeParams.animate) {
	view->active_frame = 0;
	g_timer_start(view->timer);
    }

}
