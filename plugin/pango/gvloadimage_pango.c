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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gvc/gvplugin_loadimage.h>
#include <gvc/gvio.h>

#include <cairo.h>

enum {
    FORMAT_PNG_CAIRO, FORMAT_PNG_PS,
};

static cairo_status_t
reader (void *closure, unsigned char *data, unsigned int length)
{
    assert(closure);
    if (length == fread(data, 1, length, (FILE *)closure)
     || feof((FILE *)closure))
        return CAIRO_STATUS_SUCCESS;
    return CAIRO_STATUS_READ_ERROR;
}

static void cairo_freeimage(usershape_t *us)
{
    cairo_surface_destroy(us->data);
}

static cairo_surface_t *cairo_loadimage(usershape_t *us) {
    cairo_surface_t *surface = NULL; /* source surface */

    assert(us);
    assert(us->name);
    assert(us->name[0]);

    if (us->data) {
        if (us->datafree == cairo_freeimage)
             surface = us->data; /* use cached data */
        else {
             us->datafree(us);        /* free incompatible cache data */
             us->datafree = NULL;
             us->data = NULL;
        }
    }
    if (!surface) { /* read file into cache */
	if (!gvusershape_file_access(us))
	    return NULL;
        assert(us->f);
        switch (us->type) {
#ifdef CAIRO_HAS_PNG_FUNCTIONS
            case FT_PNG:
                surface = cairo_image_surface_create_from_png_stream(reader, us->f);
                cairo_surface_reference(surface);
                break;
#endif
            default:
                surface = NULL;
        }
        if (surface) {
            us->data = surface;
            us->datafree = cairo_freeimage;
        }
	gvusershape_file_release(us);
    }
    return surface;
}

static void pango_loadimage_cairo(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    cairo_t *cr = job->context; /* target context */
    cairo_surface_t *surface;	 /* source surface */

    assert(job);
    assert(us);
    assert(us->name);
    assert(us->name[0]);

    // suppress unused parameter warning
    (void)filled;

    surface = cairo_loadimage(us);
    if (surface) {
        cairo_save(cr);
	cairo_translate(cr, b.LL.x, -b.UR.y);
	cairo_scale(cr, (b.UR.x - b.LL.x)/(us->w), (b.UR.y - b.LL.y)/(us->h));
        cairo_set_source_surface (cr, surface, 0, 0);
        cairo_paint (cr);
        cairo_restore(cr);
    }
}

static void pango_loadimage_ps(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    // suppress unused parameter warning
    (void)filled;

    cairo_surface_t *const surface = cairo_loadimage(us); // source surface
    if (surface) {
       	const cairo_format_t format = cairo_image_surface_get_format(surface);
        if ((format != CAIRO_FORMAT_ARGB32) && (format != CAIRO_FORMAT_RGB24))
	    return;

	const int X = cairo_image_surface_get_width(surface);
	const int Y = cairo_image_surface_get_height(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	const unsigned char *data = cairo_image_surface_get_data(surface);

        gvputs(job, "save\n");

	/* define image data as string array (one per raster line) */
	/* see parallel code in gd_loadimage_ps().  FIXME: refactor... */
        gvputs(job, "/myctr 0 def\n");
        gvputs(job, "/myarray [\n");
        for (int y = 0; y < Y; y++) {
	    gvputs(job, "<");
	    const unsigned char *ix = data + y * stride;
            for (int x = 0; x < X; x++) {
		uint32_t rgba;
		memcpy(&rgba, ix, sizeof(rgba));
		ix += sizeof(rgba);
		const unsigned blue = rgba & 0xff;
		const unsigned green = (rgba >> 8) & 0xff;
		const unsigned red = (rgba >> 16) & 0xff;
		const unsigned alpha = (rgba >> 24) & 0xff;
		if (alpha < 0x7f)
		    gvputs(job, "ffffff");
		else
                    gvprintf(job, "%02x%02x%02x", red, green, blue);
            }
	    gvputs(job, ">\n");
        }
	gvputs(job, "] def\n");
        gvputs(job,"/myproc { myarray myctr get /myctr myctr 1 add def } def\n");

        /* this sets the position of the image */
        gvprintf(job, "%g %g translate\n",
		(b.LL.x + (b.UR.x - b.LL.x) * (1. - (job->dpi.x) / 96.) / 2.),
		(b.LL.y + (b.UR.y - b.LL.y) * (1. - (job->dpi.y) / 96.) / 2.));

        /* this sets the rendered size to fit the box */
        gvprintf(job,"%g %g scale\n",
		((b.UR.x - b.LL.x) * 72. / 96.),
		((b.UR.y - b.LL.y) * 72. / 96.));

        /* xsize ysize bits-per-sample [matrix] */
        gvprintf(job, "%d %d 8 [%d 0 0 %d 0 %d]\n", X, Y, X, -Y, Y);

        gvputs(job, "{myproc} false 3 colorimage\n");

        gvputs(job, "restore\n");
    }
}

static gvloadimage_engine_t engine_cairo = {
    pango_loadimage_cairo
};

static gvloadimage_engine_t engine_ps = {
    pango_loadimage_ps
};

gvplugin_installed_t gvloadimage_pango_types[] = {
    {FORMAT_PNG_CAIRO, "png:cairo", 1, &engine_cairo, NULL},
    {FORMAT_PNG_PS, "png:lasi", 2, &engine_ps, NULL},
    {FORMAT_PNG_PS, "png:ps", 2, &engine_ps, NULL},
    {0, NULL, 0, NULL, NULL}
};
