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
#include <stdlib.h>
#include <sys/stat.h>

#include <gvc/gvplugin_loadimage.h>

#ifdef HAVE_PANGOCAIRO
#ifdef HAVE_POPPLER
#include <poppler.h>
#include <cairo.h>

enum {
    FORMAT_PDF_CAIRO,
};

static void gvloadimage_poppler_free(usershape_t *us)
{
    g_object_unref(us->data);
}

static PopplerDocument* gvloadimage_poppler_load(GVJ_t * job, usershape_t *us)
{
    PopplerDocument *document = NULL;
    int num_pages;

    assert(job);
    assert(us);
    assert(us->name);

    if (us->data) {
        if (us->datafree == gvloadimage_poppler_free)
             document = us->data; /* use cached data */
        else {
             us->datafree(us);        /* free incompatible cache data */
             us->data = NULL;
             us->datafree = NULL;
        }

    }

    if (!document) { /* read file into cache */
	if (!gvusershape_file_access(us))
	    return NULL;
        switch (us->type) {
            case FT_PDF: {

		char *absolute;
		if (g_path_is_absolute(us->name)) {
		    absolute = g_strdup (us->name);
		} else {
		    char *dir = g_get_current_dir();
		    absolute = g_build_filename(dir, us->name, NULL);
		    free (dir);
		}

		GError *error = NULL;
		char *uri = g_filename_to_uri(absolute, NULL, &error);

		g_free(absolute);
		if (uri == NULL) {
		    printf("%s\n", error->message);
		    g_error_free(error);
		    return NULL;
		}

		document = poppler_document_new_from_file (uri, NULL, &error);
		g_free(uri);
		if (document == NULL) {
		    printf("%s\n", error->message);
		    g_error_free(error);
		    return NULL;
		}

		// check page 1 exists

                num_pages = poppler_document_get_n_pages (document);
                if (num_pages < 1) {
                    printf("poppler fail: num_pages %d,  must be at least 1", num_pages);
                    return NULL;
                }
                break;
	    }

	    default:
		break;
        }

        if (document) {
            us->data = document;
            us->datafree = gvloadimage_poppler_free;
        }

	gvusershape_file_release(us);
    }

    return document;
}

static void gvloadimage_poppler_cairo(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)filled;

    PopplerDocument* document = gvloadimage_poppler_load(job, us);
    PopplerPage* page;

    cairo_t *cr = job->context; /* target context */
    cairo_surface_t *surface;	 /* source surface */

    if (document) {
    
	// already done this once, so no err checking
        page = poppler_document_get_page (document, 0);

        cairo_save(cr);

 	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)us->w, (int)us->h);
	cairo_surface_reference(surface);

        cairo_set_source_surface(cr, surface, 0, 0);
	cairo_translate(cr, b.LL.x, -b.UR.y);
	cairo_scale(cr, (b.UR.x - b.LL.x)/(us->w), (b.UR.y - b.LL.y)/(us->h)); 
        poppler_page_render (page, cr);
        cairo_paint (cr);

        cairo_restore(cr);
    }
}

static gvloadimage_engine_t engine_cairo = {
    gvloadimage_poppler_cairo
};
#endif
#endif

gvplugin_installed_t gvloadimage_poppler_types[] = {
#ifdef HAVE_PANGOCAIRO
#ifdef HAVE_POPPLER
    {FORMAT_PDF_CAIRO, "pdf:cairo", 1, &engine_cairo, NULL},
#endif
#endif
    {0, NULL, 0, NULL, NULL}
};
