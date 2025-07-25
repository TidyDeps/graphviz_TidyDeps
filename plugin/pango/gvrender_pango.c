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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <common/const.h>
#include <gvc/gvplugin_render.h>
#include <common/utils.h>
#include <gvc/gvplugin_device.h>
#include <gvc/gvio.h>
#include <math.h>
#include <util/agxbuf.h>
#include <util/gv_math.h>

#include "gvplugin_pango.h"

#include <pango/pangocairo.h>

enum {
		FORMAT_CAIRO,
		FORMAT_PNG,
		FORMAT_PS,
		FORMAT_PDF,
		FORMAT_SVG,
		FORMAT_EPS,
};

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(A[0]))

static double dashed[] = {6.};
static int dashed_len = ARRAY_SIZE(dashed);

static double dotted[] = {2., 6.};
static int dotted_len = ARRAY_SIZE(dotted);

#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif

#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif

#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

static void cairogen_polyline(GVJ_t *job, pointf *A, size_t n);

static void cairogen_set_color(cairo_t * cr, gvcolor_t * color)
{
    cairo_set_source_rgba(cr, color->u.RGBA[0], color->u.RGBA[1],
                        color->u.RGBA[2], color->u.RGBA[3]);
}

static void cairogen_add_color_stop_rgba(cairo_pattern_t *pat, double stop , gvcolor_t * color)
{
  cairo_pattern_add_color_stop_rgba (pat, stop,color->u.RGBA[0], color->u.RGBA[1],
                        color->u.RGBA[2], color->u.RGBA[3]);
}


static cairo_status_t
writer (void *closure, const unsigned char *data, unsigned int length)
{
    if (length == gvwrite(closure, (const char*)data, length))
	return CAIRO_STATUS_SUCCESS;
    return CAIRO_STATUS_WRITE_ERROR;
}

static void cairogen_begin_job(GVJ_t * job)
{
    if (job->external_context && job->context)
        cairo_save(job->context);
}

static void cairogen_end_job(GVJ_t * job)
{
    cairo_t *cr = job->context;

    if (job->external_context)
        cairo_restore(cr);
    else {
	cairo_destroy(cr);
        job->context = NULL;
    }
}

static const double CAIRO_XMAX = 32767;
static const double CAIRO_YMAX = 32767;

static void cairogen_begin_page(GVJ_t * job)
{
    cairo_t *cr = job->context;
    cairo_surface_t *surface;
    cairo_status_t status;

    if (cr == NULL) {
        switch (job->render.id) {
        case FORMAT_PS:
        case FORMAT_EPS:
#ifdef CAIRO_HAS_PS_SURFACE
	    surface = cairo_ps_surface_create_for_stream (writer,
			job, job->width, job->height);
            if (job->render.id == FORMAT_EPS)
                cairo_ps_surface_set_eps (surface, TRUE);
#endif
	    break;
        case FORMAT_PDF:
#ifdef CAIRO_HAS_PDF_SURFACE
	    surface = cairo_pdf_surface_create_for_stream (writer,
			job, job->width, job->height);

	    {
                const char *source_date_epoch = getenv("SOURCE_DATE_EPOCH");
                if (source_date_epoch != NULL) {
                    char *end = NULL;
                    errno = 0;
                    long epoch = strtol(source_date_epoch, &end, 10);
                    // from https://reproducible-builds.org/specs/source-date-epoch/
                    //
                    //   If the value is malformed, the build process SHOULD
                    //   exit with a non-zero error code.
                    if ((epoch == LONG_MAX && errno != 0) || epoch < 0
                        || *end != '\0') {
                        fprintf(stderr,
                                "malformed value %s for $SOURCE_DATE_EPOCH\n",
                                source_date_epoch);
                        exit(EXIT_FAILURE);
                    }
                    time_t tepoch = (time_t)epoch;
                    struct tm *tm = gmtime(&tepoch);
                    if (tm == NULL) {
                        fprintf(stderr,
                                "malformed value %s for $SOURCE_DATE_EPOCH\n",
                                source_date_epoch);
                        exit(EXIT_FAILURE);
                    }
#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
                    char iso8601[sizeof("YYYY-MM-DDThh:mm:ss")] = {0};
                    (void)strftime(iso8601, sizeof(iso8601), "%Y-%m-%dT%H:%M:%SZ", tm);
                    cairo_pdf_surface_set_metadata(surface,
                                                   CAIRO_PDF_METADATA_CREATE_DATE,
                                                   iso8601);
                    cairo_pdf_surface_set_metadata(surface,
                                                   CAIRO_PDF_METADATA_MOD_DATE,
                                                   iso8601);
#endif
                }
	    }
#endif
	    break;
        case FORMAT_SVG:
#ifdef CAIRO_HAS_SVG_SURFACE
	    surface = cairo_svg_surface_create_for_stream (writer,
			job, job->width, job->height);
#endif
	    break;
        case FORMAT_CAIRO:
        case FORMAT_PNG:
        default:
	    if (job->width >= CAIRO_XMAX || job->height >= CAIRO_YMAX) {
		double scale = fmin(CAIRO_XMAX / job->width, CAIRO_YMAX / job->height);
		assert(job->width * scale <= UINT_MAX);
		job->width = (unsigned)(job->width * scale);
		assert(job->height * scale <= UINT_MAX);
		job->height = (unsigned)(job->height * scale);
		job->scale.x *= scale;
		job->scale.y *= scale;
                fprintf(stderr,
                        "%s: graph is too large for cairo-renderer bitmaps. Scaling by %g to fit\n",
                        job->common->cmdname, scale);
	    }
	    assert(job->width <= INT_MAX);
	    assert(job->height <= INT_MAX);
	    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                         (int)job->width, (int)job->height);
            if (job->common->verbose)
                fprintf(stderr,
                        "%s: allocating a %.0fK cairo image surface (%d x %d pixels)\n",
                        job->common->cmdname,
			round(job->width * job->height * BYTES_PER_PIXEL / 1024.),
			job->width, job->height);
	    break;
        }
	status = cairo_surface_status(surface);
        if (status != CAIRO_STATUS_SUCCESS)  {
		fprintf(stderr, "%s: failure to create cairo surface: %s\n",
			job->common->cmdname,
			cairo_status_to_string(status));
		cairo_surface_destroy (surface);
		return;
	}
        cr = cairo_create(surface);
        cairo_surface_destroy (surface);
        job->context = cr;
    }

    cairo_scale(cr, job->scale.x, job->scale.y);
    cairo_rotate(cr, -job->rotation * M_PI / 180.);
    cairo_translate(cr, job->translation.x, -job->translation.y);

    cairo_rectangle(cr, job->clip.LL.x, - job->clip.LL.y,
	    job->clip.UR.x - job->clip.LL.x, - (job->clip.UR.y - job->clip.LL.y));
    cairo_clip(cr);
}

static void cairogen_end_page(GVJ_t * job)
{
    cairo_t *cr = job->context;
    cairo_surface_t *surface;
    cairo_status_t status;

    switch (job->render.id) {

#ifdef CAIRO_HAS_PNG_FUNCTIONS
    case FORMAT_PNG:
        surface = cairo_get_target(cr);
	cairo_surface_write_to_png_stream(surface, writer, job);
	break;
#endif

    case FORMAT_PS:
    case FORMAT_PDF:
    case FORMAT_SVG:
	cairo_show_page(cr);
	surface = cairo_surface_reference(cairo_get_target(cr));
	cairo_surface_finish(surface);
	status = cairo_surface_status(surface);
	cairo_surface_destroy(surface);
	if (status != CAIRO_STATUS_SUCCESS)
	    fprintf(stderr, "cairo: %s\n", cairo_status_to_string(status));
	break;

    case FORMAT_CAIRO:
    default:
        surface = cairo_get_target(cr);
        if (cairo_image_surface_get_width(surface) == 0 || cairo_image_surface_get_height(surface) == 0) {
	    /* apparently cairo never allocates a surface if nothing was ever written to it */
/* but suppress this error message since a zero area surface seems to happen during normal operations, particular in -Tx11
	    fprintf(stderr, "ERROR: cairo surface has zero area, this may indicate some problem during rendering shapes.\n");
 - jce */
	}
	job->imagedata = cairo_image_surface_get_data(surface);
	break;
       	/* formatting will be done by gvdevice_format() */
    }
}

static void cairogen_begin_anchor(GVJ_t *job, char *url, char *tooltip, char *target, char *id)
{
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;
    double p0x, p0y, p1x, p1y;

    // suppress unused parameter warnings
    (void)tooltip;
    (void)target;
    (void)id;

    if (url && obj->url_map_p) {
       p0x = obj->url_map_p[0].x;
       p0y = -obj->url_map_p[0].y;
       cairo_user_to_device (cr, &p0x, &p0y);
       p1x = obj->url_map_p[1].x;
       p1y = -obj->url_map_p[1].y;
       cairo_user_to_device (cr, &p1x, &p1y);
       agxbuf buf = {0};
       agxbprint(&buf, "rect=[%f %f %f %f] uri='%s'", p0x, p0y, p1x - p0x,
                 p1y - p0y, url);
#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
       cairo_tag_begin(cr, CAIRO_TAG_LINK, agxbuse(&buf));
       cairo_tag_end (cr, CAIRO_TAG_LINK);
#endif
       agxbfree(&buf);
    }
}

static void cairogen_textspan(GVJ_t * job, pointf p, textspan_t * span)
{
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;
    pointf A[2];

    cairo_set_dash (cr, dashed, 0, 0.0);  /* clear any dashing */
    cairogen_set_color(cr, &obj->pencolor);

    switch (span->just) {
    case 'r':
	p.x -= span->size.x;
	break;
    case 'l':
	p.x -= 0.0;
	break;
    case 'n':
    default:
	p.x -= span->size.x / 2.0;
	break;
    }
    p.y += span->yoffset_centerline + span->yoffset_layout;

    cairo_move_to (cr, p.x, -p.y);
    cairo_save(cr);
    cairo_scale(cr, POINTS_PER_INCH / FONT_DPI, POINTS_PER_INCH / FONT_DPI);
    pango_cairo_show_layout(cr, (PangoLayout*)span->layout);
    cairo_restore(cr);

    if (span->font && (span->font->flags & HTML_OL)) {
	A[0].x = p.x;
	A[1].x = p.x + span->size.x;
	A[1].y = A[0].y = p.y;
	cairogen_polyline(job, A, 2);
    }
}

static void cairogen_set_penstyle(GVJ_t *job, cairo_t *cr)
{
    obj_state_t *obj = job->obj;

    if (obj->pen == PEN_DASHED) {
	cairo_set_dash (cr, dashed, dashed_len, 0.0);
    } else if (obj->pen == PEN_DOTTED) {
	cairo_set_dash (cr, dotted, dotted_len, 0.0);
    } else {
	cairo_set_dash (cr, dashed, 0, 0.0);
    }
    cairo_set_line_width (cr, obj->penwidth);
}

static void cairo_gradient_fill(cairo_t *cr, obj_state_t *obj, int filled,
                                pointf *A, size_t n) {
    cairo_pattern_t* pat;
    double angle = obj->gradient_angle * M_PI / 180;
    pointf G[2],c1;

    if (filled == GRADIENT) {
	  get_gradient_points(A, G, n, angle, 0);
	  pat = cairo_pattern_create_linear (G[0].x,G[0].y,G[1].x,G[1].y);
    }
    else {
	get_gradient_points(A, G, n, 0, 1);
	  //r1 is inner radius, r2 is outer radius
	const double r1 = G[1].x; // set a r2 ÷ 4 in get_gradient_points
	const double r2 = G[1].y;
	if (obj->gradient_angle == 0) {
	    c1.x = G[0].x;
	    c1.y = G[0].y;
	}
	else {
	    c1.x = G[0].x +  r1 * cos(angle);
	    c1.y = G[0].y -  r1 * sin(angle);
	}
	pat = cairo_pattern_create_radial(c1.x,c1.y,r1,G[0].x,G[0].y,r2);
    }
    if (obj->gradient_frac > 0) {
	cairogen_add_color_stop_rgba(pat,obj->gradient_frac - 0.001,&(obj->fillcolor));
	cairogen_add_color_stop_rgba(pat,obj->gradient_frac,&(obj->stopcolor));
    }
    else {
	cairogen_add_color_stop_rgba(pat,0,&(obj->fillcolor));
	cairogen_add_color_stop_rgba(pat,1,&(obj->stopcolor));
    }
    cairo_set_source (cr, pat);
    cairo_fill_preserve (cr);
    cairo_pattern_destroy (pat);
}

static void cairogen_ellipse(GVJ_t * job, pointf * A, int filled)
{
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;
    cairo_matrix_t matrix;
    double rx, ry;

    cairogen_set_penstyle(job, cr);

    cairo_get_matrix(cr, &matrix);

    rx = A[1].x - A[0].x;
    ry = A[1].y - A[0].y;

#define RMIN 0.01
    rx = fmax(rx, RMIN);
    ry = fmax(ry, RMIN);

    cairo_translate(cr, A[0].x, -A[0].y);
    cairo_scale(cr, rx, ry);
    cairo_move_to(cr, 1., 0.);
    cairo_arc(cr, 0., 0., 1., 0., 2 * M_PI);

    cairo_set_matrix(cr, &matrix);

    if (filled == GRADIENT || filled == RGRADIENT) {
	cairo_gradient_fill (cr, obj, filled, A, 2);
    }
    else if (filled) {
	cairogen_set_color(cr, &obj->fillcolor);
	cairo_fill_preserve(cr);
    }
    cairogen_set_color(cr, &obj->pencolor);
    cairo_stroke(cr);
}

static void cairogen_polygon(GVJ_t *job, pointf *A, size_t n, int filled) {
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;

    cairogen_set_penstyle(job, cr);

    cairo_move_to(cr, A[0].x, -A[0].y);
    for (size_t i = 1; i < n; i++)
    cairo_line_to(cr, A[i].x, -A[i].y);
    cairo_close_path(cr);
    if (filled == GRADIENT || filled == RGRADIENT) {
	cairo_gradient_fill(cr, obj, filled, A, n);
    }
    else if (filled) {
	cairogen_set_color(cr, &obj->fillcolor);
	cairo_fill_preserve(cr);
    }
    cairogen_set_color(cr, &obj->pencolor);
    cairo_stroke(cr);
}

static void cairogen_bezier(GVJ_t *job, pointf *A, size_t n, int filled) {
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;

    cairogen_set_penstyle(job, cr);

    cairo_move_to(cr, A[0].x, -A[0].y);
    for (size_t i = 1; i < n; i += 3)
	cairo_curve_to(cr, A[i].x, -A[i].y, A[i + 1].x, -A[i + 1].y,
		       A[i + 2].x, -A[i + 2].y);
    if (filled == GRADIENT || filled == RGRADIENT) {
	cairo_gradient_fill(cr, obj, filled, A, n);
    }
    else if (filled) {
	cairogen_set_color(cr, &obj->fillcolor);
	cairo_fill_preserve(cr);
    }
    cairogen_set_color(cr, &obj->pencolor);
    cairo_stroke(cr);
}

static void cairogen_polyline(GVJ_t *job, pointf *A, size_t n) {
    obj_state_t *obj = job->obj;
    cairo_t *cr = job->context;

    cairogen_set_penstyle(job, cr);

    cairo_move_to(cr, A[0].x, -A[0].y);
    for (size_t i = 1; i < n; i++)
	cairo_line_to(cr, A[i].x, -A[i].y);
    cairogen_set_color(cr, &obj->pencolor);
    cairo_stroke(cr);
}

static gvrender_engine_t cairogen_engine = {
    cairogen_begin_job,
    cairogen_end_job,
    0,				/* cairogen_begin_graph */
    0,				/* cairogen_end_graph */
    0,				/* cairogen_begin_layer */
    0,				/* cairogen_end_layer */
    cairogen_begin_page,
    cairogen_end_page,
    0,				/* cairogen_begin_cluster */
    0,				/* cairogen_end_cluster */
    0,				/* cairogen_begin_nodes */
    0,				/* cairogen_end_nodes */
    0,				/* cairogen_begin_edges */
    0,				/* cairogen_end_edges */
    0,				/* cairogen_begin_node */
    0,				/* cairogen_end_node */
    0,				/* cairogen_begin_edge */
    0,				/* cairogen_end_edge */
    cairogen_begin_anchor,	/* cairogen_begin_anchor */
    0,				/* cairogen_end_anchor */
    0,				/* cairogen_begin_label */
    0,				/* cairogen_end_label */
    cairogen_textspan,
    0,				/* cairogen_resolve_color */
    cairogen_ellipse,
    cairogen_polygon,
    cairogen_bezier,
    cairogen_polyline,
    0,				/* cairogen_comment */
    0,				/* cairogen_library_shape */
};

static gvrender_features_t render_features_cairo = {
    GVRENDER_Y_GOES_DOWN
	| GVRENDER_DOES_TRANSFORM, /* flags */
    4.,                         /* default pad - graph units */
    0,				/* knowncolors */
    0,				/* sizeof knowncolors */
    RGBA_DOUBLE,		/* color_type */
};

static gvdevice_features_t device_features_png = {
    GVDEVICE_BINARY_FORMAT
      | GVDEVICE_DOES_TRUECOLOR,/* flags */
    {0.,0.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {96.,96.},			/* typical monitor dpi */
};

static gvdevice_features_t device_features_ps = {
    GVRENDER_NO_WHITE_BG
      | GVDEVICE_DOES_TRUECOLOR,    /* flags */
    {36.,36.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.},			/* postscript 72 dpi */
};

static gvdevice_features_t device_features_eps = {
    GVRENDER_NO_WHITE_BG
      | GVDEVICE_DOES_TRUECOLOR,    /* flags */
    {36.,36.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.},			/* postscript 72 dpi */
};

static gvdevice_features_t device_features_pdf = {
    GVDEVICE_BINARY_FORMAT
      | GVRENDER_NO_WHITE_BG
      | GVRENDER_DOES_MAPS
      | GVRENDER_DOES_MAP_RECTANGLE
      | GVDEVICE_DOES_TRUECOLOR,/* flags */
    {36.,36.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.},			/* postscript 72 dpi */
};

static gvdevice_features_t device_features_svg = {
    GVRENDER_NO_WHITE_BG
      | GVDEVICE_DOES_TRUECOLOR,    /* flags */
    {0.,0.},			/* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.},			/* svg 72 dpi */
};

gvplugin_installed_t gvrender_pango_types[] = {
    {FORMAT_CAIRO, "cairo", 10, &cairogen_engine, &render_features_cairo},
    {0, NULL, 0, NULL, NULL}
};

gvplugin_installed_t gvdevice_pango_types[] = {
#ifdef CAIRO_HAS_PNG_FUNCTIONS
    {FORMAT_PNG, "png:cairo", 10, NULL, &device_features_png},
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    {FORMAT_PS, "ps:cairo", -10, NULL, &device_features_ps},
    {FORMAT_EPS, "eps:cairo", -10, NULL, &device_features_eps},
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
    {FORMAT_PDF, "pdf:cairo", 10, NULL, &device_features_pdf},
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
    {FORMAT_SVG, "svg:cairo", -10, NULL, &device_features_svg},
#endif
    {0, NULL, 0, NULL, NULL}
};
