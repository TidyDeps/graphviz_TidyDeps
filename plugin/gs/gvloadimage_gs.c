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
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <util/agxbuf.h>

#include <gvc/gvplugin_loadimage.h>

#include <ghostscript/iapi.h>
#include <ghostscript/ierrors.h>
#include <cairo/cairo.h>


/**
 * Ensure compatibility with Ghostscipt versions newer than 9.18
 * while maintaining compatibility with the older versions.
 **/

#ifndef e_VMerror
#define e_VMerror gs_error_VMerror
#endif

#ifndef e_unregistered
#define e_unregistered gs_error_unregistered
#endif

#ifndef e_invalidid
#define e_invalidid gs_error_invalidid
#endif

enum {
    FORMAT_PS_CAIRO, FORMAT_EPS_CAIRO,
};

typedef struct gs_s {
    cairo_t* cr;
    cairo_surface_t* surface;
    cairo_pattern_t* pattern;
} gs_t;

static void gvloadimage_gs_free(usershape_t *us)
{
    gs_t *gs = us->data;

    if (gs->pattern) cairo_pattern_destroy(gs->pattern);
    if (gs->surface) cairo_surface_destroy(gs->surface);
    free(gs);
}

static int gs_writer(void *caller_handle, const char *str, int len)
{
    GVJ_t *job = caller_handle;

    if (job->common->verbose) {
        assert(len >= 0);
        return (int)fwrite(str, 1, (size_t)len, stderr);
    }
    return len;
}

static void gs_error(GVJ_t * job, const char *name, const char *funstr, int err)
{
    const char *errsrc;

    assert (err < 0);

    if (err >= e_VMerror) 
	errsrc = "PostScript Level 1"; 
    else if (err >= e_unregistered)
	errsrc = "PostScript Level 2";
    else if (err >= e_invalidid)
	errsrc = "DPS error";
    else
	errsrc = "Ghostscript internal error";

    job->common->errorfn("%s: %s() returned: %d (%s)\n",
		name, funstr, err, errsrc);
}

static int gvloadimage_process_file(GVJ_t *job, usershape_t *us, void *instance)
{
    int rc = 0, exit_code;

    if (! gvusershape_file_access(us)) {
	job->common->errorfn("Failure to read shape file\n");
	return -1;
    }
    rc = gsapi_run_file(instance, us->name, -1, &exit_code);
    if (rc) {
	gs_error(job, us->name, "gsapi_run_file", rc);
    }
    gvusershape_file_release(us);
    return rc;
}

static int gvloadimage_process_surface(GVJ_t *job, usershape_t *us, gs_t *gs, void *instance)
{
    cairo_t *cr; /* temp cr for gs */
    int rc, rc2;
    char *gs_args[] = {
	"dot",      /* actual value of argv[0] doesn't matter */
	"-dQUIET",
	"-dNOPAUSE",
	"-sDEVICE=cairo",
	NULL,
	NULL,
	NULL,
    };
#define GS_ARGC sizeof(gs_args)/sizeof(gs_args[0])

    gs->surface = cairo_surface_create_similar( 
	cairo_get_target(gs->cr),
	CAIRO_CONTENT_COLOR_ALPHA,
	(int)(us->x + us->w),
	(int)(us->y + us->h));

    cr = cairo_create(gs->surface);  /* temp context for gs */

    agxbuf width_height = {0};
    agxbprint(&width_height, "-g%0.fx%0.f", us->x + us->w, us->y + us->h);
    gs_args[5] = agxbuse(&width_height);
    agxbuf dpi = {0};
    agxbprint(&dpi, "-r%d", us->dpi);
    gs_args[6] = agxbuse(&dpi);
    agxbuf cairo_context = {0};
    agxbprint(&cairo_context, "-sCairoContext=%p", cr);
    gs_args[4] = agxbuse(&cairo_context);

    rc = gsapi_init_with_args(instance, GS_ARGC, gs_args);
    agxbfree(&cairo_context);
    agxbfree(&dpi);
    agxbfree(&width_height);

    cairo_destroy(cr); /* finished with temp context */

    if (rc)
	gs_error(job, us->name, "gsapi_init_with_args", rc);
    else
	rc = gvloadimage_process_file(job, us, instance);

    if (rc) {
	cairo_surface_destroy(gs->surface);
	gs->surface = NULL;
    }

    rc2 = gsapi_exit(instance);
    if (rc2) {
	gs_error(job, us->name, "gsapi_exit", rc2);
	return rc2;
    }

    if (!rc) 
        gs->pattern = cairo_pattern_create_for_surface (gs->surface);

    return rc;
}

static cairo_pattern_t* gvloadimage_gs_load(GVJ_t * job, usershape_t *us)
{
    gs_t *gs = NULL;
    gsapi_revision_t gsapi_revision_info;
    void *instance;
    int rc;

    assert(job);
    assert(us);
    assert(us->name);

    if (us->data) {
        if (us->datafree == gvloadimage_gs_free
	&& ((gs_t*)(us->data))->cr == job->context)
	    gs = us->data; /* use cached data */
	else {
	    us->datafree(us);        /* free incompatible cache data */
	    us->data = NULL;
	}
    }
    if (!gs) {
	gs = malloc(sizeof(gs_t));
	if (!gs) {
	    job->common->errorfn("malloc() failure\n");
	    return NULL;
	}
	gs->cr = job->context;
	gs->surface = NULL;
	gs->pattern = NULL;

	/* cache this - even if things go bad below - avoids repeats */
	us->data = gs;
	us->datafree = gvloadimage_gs_free;

#define GSAPI_REVISION_REQUIRED 863
	rc = gsapi_revision(&gsapi_revision_info, sizeof(gsapi_revision_t));
        if (rc && rc < (int)sizeof(gsapi_revision_t)) {
    	    job->common->errorfn("gs revision - struct too short %d\n", rc);
	    return NULL;
        }
	if (gsapi_revision_info.revision < GSAPI_REVISION_REQUIRED) {
    	    job->common->errorfn("gs revision - too old %d\n",
		gsapi_revision_info.revision);
	    return NULL;
	}

	rc = gsapi_new_instance(&instance, job);
	if (rc)
	    gs_error(job, us->name, "gsapi_new_instance", rc);
	else {
	    rc = gsapi_set_stdio(instance, NULL, gs_writer, gs_writer);
	    if (rc)
	        gs_error(job, us->name, "gsapi_set_stdio", rc);
	    else
                rc = gvloadimage_process_surface(job, us, gs, instance);
	    gsapi_delete_instance(instance);
	}
    }
    return gs->pattern;
}

static void gvloadimage_gs_cairo(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)filled;

    cairo_t *cr = job->context; // target context
    cairo_pattern_t *pattern = gvloadimage_gs_load(job, us);

    if (pattern) {
        cairo_save(cr);
	cairo_translate(cr, b.LL.x - us->x, -b.UR.y);
	cairo_scale(cr, (b.UR.x - b.LL.x) / us->w, (b.UR.y - b.LL.y) / us->h);
        cairo_set_source(cr, pattern);
	cairo_paint(cr);
        cairo_restore(cr);
    }
}

static gvloadimage_engine_t engine_cairo = {
    gvloadimage_gs_cairo
};

gvplugin_installed_t gvloadimage_gs_types[] = {
    {FORMAT_PS_CAIRO,   "ps:cairo", 1, &engine_cairo, NULL},
    {FORMAT_EPS_CAIRO, "eps:cairo", 1, &engine_cairo, NULL},
    {0, NULL, 0, NULL, NULL}
};
