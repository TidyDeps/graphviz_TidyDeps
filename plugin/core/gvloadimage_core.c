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
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include <gvc/gvplugin_loadimage.h>
#include <common/utils.h>
#include <gvc/gvio.h>
#include "core_loadimage_xdot.h"
#include <util/agxbuf.h>

extern shape_desc *find_user_shape(char *name);

enum {
    FORMAT_PNG_XDOT, FORMAT_GIF_XDOT, FORMAT_JPEG_XDOT, FORMAT_SVG_XDOT, FORMAT_PS_XDOT,
    FORMAT_PNG_DOT,  FORMAT_GIF_DOT,  FORMAT_JPEG_DOT,  FORMAT_SVG_DOT,  FORMAT_PS_DOT,
    FORMAT_PNG_MAP,  FORMAT_GIF_MAP,  FORMAT_JPEG_MAP,  FORMAT_SVG_MAP,  FORMAT_PS_MAP,
    FORMAT_PNG_SVG,  FORMAT_GIF_SVG,  FORMAT_JPEG_SVG,  FORMAT_SVG_SVG,
    FORMAT_PNG_FIG,  FORMAT_GIF_FIG,  FORMAT_JPEG_FIG,
    FORMAT_PNG_VRML, FORMAT_GIF_VRML, FORMAT_JPEG_VRML,
    FORMAT_PS_PS, FORMAT_PSLIB_PS, 
    FORMAT_GIF_TK,
};

static void core_loadimage_svg(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)filled;

    double width = (b.UR.x-b.LL.x);
    double height = (b.UR.y-b.LL.y);
    double originx = (b.UR.x+b.LL.x - width)/2;
    double originy = (b.UR.y+b.LL.y + height)/2;
    assert(job);
    assert(us);
    assert(us->name);

    gvputs(job, "<image xlink:href=\"");
    gvputs(job, us->name);
    if (job->rotation) {

// FIXME - this is messed up >>>
        gvprintf (job, "\" width=\"%gpx\" height=\"%gpx\" preserveAspectRatio=\"xMidYMid meet\" x=\"%g\" y=\"%g\"",
            height, width, originx, -originy);
        gvprintf (job, " transform=\"rotate(%d %g %g)\"",
            job->rotation, originx, -originy);
// <<<
    }
    else {
        gvprintf (job, "\" width=\"%gpx\" height=\"%gpx\" preserveAspectRatio=\"xMinYMin meet\" x=\"%g\" y=\"%g\"",
            width, height, originx, -originy);
    }
    gvputs(job, "/>\n");
}

static void core_loadimage_fig(GVJ_t * job, usershape_t *us, boxf bf, bool filled)
{
    (void)filled;

    int object_code = 2;        /* always 2 for polyline */
    int sub_type = 5;           /* always 5 for image */
    int line_style = 0;		/* solid, dotted, dashed */
    int thickness = 0;
    int pen_color = 0;
    int fill_color = -1;
    int depth = 1;
    int pen_style = -1;         /* not used */
    int area_fill = 0;
    double style_val = 0.0;
    int join_style = 0;
    int cap_style = 0;
    int radius = 0;
    int forward_arrow = 0;
    int backward_arrow = 0;
    int npoints = 5;
    int flipped = 0;

    assert(job);
    assert(us);
    assert(us->name);

    gvprintf(job, "%d %d %d %d %d %d %d %d %d %.1f %d %d %d %d %d %d\n %d %s\n",
            object_code, sub_type, line_style, thickness, pen_color,
            fill_color, depth, pen_style, area_fill, style_val, join_style,
            cap_style, radius, forward_arrow, backward_arrow, npoints,
            flipped, us->name);
    gvprintf(job," %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
	    bf.LL.x, bf.LL.y,
	    bf.LL.x, bf.UR.y,
	    bf.UR.x, bf.UR.y,
	    bf.UR.x, bf.LL.y,
	    bf.LL.x, bf.LL.y);
}

static void core_loadimage_vrml(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)b;
    (void)filled;

    assert(job);
    assert(job->obj);
    assert(us);
    assert(us->name);

    assert(job->obj->u.n);

    gvprintf(job, "Shape {\n");
    gvprintf(job, "  appearance Appearance {\n");
    gvprintf(job, "    material Material {\n");
    gvprintf(job, "      ambientIntensity 0.33\n");
    gvprintf(job, "        diffuseColor 1 1 1\n");
    gvprintf(job, "    }\n");
    gvprintf(job, "    texture ImageTexture { url \"%s\" }\n", us->name);
    gvprintf(job, "  }\n");
    gvprintf(job, "}\n");
}

static void ps_freeimage(usershape_t *us)
{
#ifdef HAVE_SYS_MMAN_H
    munmap(us->data, us->datasize);
#else
    free(us->data);
#endif
}

/* usershape described by a postscript file */
static void core_loadimage_ps(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)filled;

    assert(job);
    assert(us);
    assert(us->name);

    if (us->data) {
        if (us->datafree != ps_freeimage) {
            us->datafree(us);        /* free incompatible cache data */
            us->data = NULL;
            us->datafree = NULL;
            us->datasize = 0;
        }
    }

    if (!us->data) { /* read file into cache */
        int fd;
	struct stat statbuf;

	if (!gvusershape_file_access(us))
	    return;
	fd = fileno(us->f);
        switch (us->type) {
            case FT_PS:
            case FT_EPS:
		fstat(fd, &statbuf);
		us->datasize = (size_t)statbuf.st_size;
#ifdef HAVE_SYS_MMAN_H
		us->data = mmap(0, us->datasize, PROT_READ, MAP_PRIVATE, fd, 0);
		if (us->data == MAP_FAILED)
			us->data = NULL;
#else
		us->data = malloc(statbuf.st_size);
		if (us->data != NULL)
			read(fd, us->data, statbuf.st_size);
#endif
		us->must_inline = true;
                break;
            default:
                break;
        }
        if (us->data)
            us->datafree = ps_freeimage;
	gvusershape_file_release(us);
    }

    if (us->data) {
        gvprintf(job, "gsave %g %g translate newpath\n",
		b.LL.x - (double)(us->x), b.LL.y - (double)(us->y));
        if (us->must_inline)
            epsf_emit_body(job, us);
        else
            gvprintf(job, "user_shape_%d\n", us->macro_id);
        gvprintf(job, "grestore\n");
    }
}

/* usershape described by a member of a postscript library */
static void core_loadimage_pslib(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    int i;
    pointf AF[4];
    shape_desc *shape;

    assert(job);
    assert(us);
    assert(us->name);

    if ((shape = us->data)) {
	AF[0] = b.LL;
	AF[2] = b.UR;
	AF[1].x = AF[0].x;
	AF[1].y = AF[2].y;
	AF[3].x = AF[2].x;
	AF[3].y = AF[0].y;
        if (filled) {
            gvprintf(job, "[ ");
            for (i = 0; i < 4; i++)
                gvprintf(job, "%g %g ", AF[i].x, AF[i].y);
            gvprintf(job, "%g %g ", AF[0].x, AF[0].y);
            gvprintf(job, "]  %d true %s\n", 4, us->name);
        }
        gvprintf(job, "[ ");
        for (i = 0; i < 4; i++)
            gvprintf(job, "%g %g ", AF[i].x, AF[i].y);
        gvprintf(job, "%g %g ", AF[0].x, AF[0].y);
        gvprintf(job, "]  %d false %s\n", 4, us->name);
    }
}

static void core_loadimage_tk(GVJ_t * job, usershape_t *us, boxf b, bool filled)
{
    (void)filled;

    gvprintf (job, "image create photo \"photo_%s\" -file \"%s\"\n",
	us->name, us->name);
    gvprintf (job, "$c create image %.2f %.2f -image \"photo_%s\"\n",
	(b.UR.x + b.LL.x) / 2, (b.UR.y + b.LL.y) / 2, us->name);
}

static void core_loadimage_null(GVJ_t *gvc, usershape_t *us, boxf b, bool filled)
{
    /* null function - basically suppress the missing loader message */
    (void)gvc;
    (void)us;
    (void)b;
    (void)filled;
}

static gvloadimage_engine_t engine_svg = {
    core_loadimage_svg
};

static gvloadimage_engine_t engine_fig = {
    core_loadimage_fig
};

static gvloadimage_engine_t engine_vrml = {
    core_loadimage_vrml
};

static gvloadimage_engine_t engine_ps = {
    core_loadimage_ps
};

static gvloadimage_engine_t engine_pslib = {
    core_loadimage_pslib
};

static gvloadimage_engine_t engine_null = {
    core_loadimage_null
};

static gvloadimage_engine_t engine_xdot = {
    core_loadimage_xdot
};

static gvloadimage_engine_t engine_tk = {
    core_loadimage_tk
};

gvplugin_installed_t gvloadimage_core_types[] = {
    {FORMAT_PNG_SVG, "png:svg", 1, &engine_svg, NULL},
    {FORMAT_GIF_SVG, "gif:svg", 1, &engine_svg, NULL},
    {FORMAT_JPEG_SVG, "jpeg:svg", 1, &engine_svg, NULL},
    {FORMAT_JPEG_SVG, "jpe:svg", 1, &engine_svg, NULL},
    {FORMAT_JPEG_SVG, "jpg:svg", 1, &engine_svg, NULL},

    {FORMAT_PNG_FIG, "png:fig", 1, &engine_fig, NULL},
    {FORMAT_GIF_FIG, "gif:fig", 1, &engine_fig, NULL},
    {FORMAT_JPEG_FIG, "jpeg:fig", 1, &engine_fig, NULL},
    {FORMAT_JPEG_FIG, "jpe:fig", 1, &engine_fig, NULL},
    {FORMAT_JPEG_FIG, "jpg:fig", 1, &engine_fig, NULL},

    {FORMAT_PNG_VRML, "png:vrml", 1, &engine_vrml, NULL},
    {FORMAT_GIF_VRML, "gif:vrml", 1, &engine_vrml, NULL},
    {FORMAT_JPEG_VRML, "jpeg:vrml", 1, &engine_vrml, NULL},
    {FORMAT_JPEG_VRML, "jpe:vrml", 1, &engine_vrml, NULL},
    {FORMAT_JPEG_VRML, "jpg:vrml", 1, &engine_vrml, NULL},

    {FORMAT_PS_PS, "eps:ps", 1, &engine_ps, NULL},
    {FORMAT_PS_PS, "ps:ps", 1, &engine_ps, NULL},
    {FORMAT_PSLIB_PS, "(lib):ps", 1, &engine_pslib, NULL},  /* for pslib */

    {FORMAT_PNG_MAP, "png:map", 1, &engine_null, NULL},
    {FORMAT_GIF_MAP, "gif:map", 1, &engine_null, NULL},
    {FORMAT_JPEG_MAP, "jpeg:map", 1, &engine_null, NULL},
    {FORMAT_JPEG_MAP, "jpe:map", 1, &engine_null, NULL},
    {FORMAT_JPEG_MAP, "jpg:map", 1, &engine_null, NULL},
    {FORMAT_PS_MAP, "ps:map", 1, &engine_null, NULL},
    {FORMAT_PS_MAP, "eps:map", 1, &engine_null, NULL},
    {FORMAT_SVG_MAP, "svg:map", 1, &engine_null, NULL},

    {FORMAT_PNG_DOT, "png:dot", 1, &engine_null, NULL},
    {FORMAT_GIF_DOT, "gif:dot", 1, &engine_null, NULL},
    {FORMAT_JPEG_DOT, "jpeg:dot", 1, &engine_null, NULL},
    {FORMAT_JPEG_DOT, "jpe:dot", 1, &engine_null, NULL},
    {FORMAT_JPEG_DOT, "jpg:dot", 1, &engine_null, NULL},
    {FORMAT_PS_DOT, "ps:dot", 1, &engine_null, NULL},
    {FORMAT_PS_DOT, "eps:dot", 1, &engine_null, NULL},
    {FORMAT_SVG_DOT, "svg:dot", 1, &engine_null, NULL},

    {FORMAT_PNG_XDOT, "png:xdot", 1, &engine_xdot, NULL},
    {FORMAT_GIF_XDOT, "gif:xdot", 1, &engine_xdot, NULL},
    {FORMAT_JPEG_XDOT, "jpeg:xdot", 1, &engine_xdot, NULL},
    {FORMAT_JPEG_XDOT, "jpe:xdot", 1, &engine_xdot, NULL},
    {FORMAT_JPEG_XDOT, "jpg:xdot", 1, &engine_xdot, NULL},
    {FORMAT_PS_XDOT, "ps:xdot", 1, &engine_xdot, NULL},
    {FORMAT_PS_XDOT, "eps:xdot", 1, &engine_xdot, NULL},
    {FORMAT_SVG_XDOT, "svg:xdot", 1, &engine_xdot, NULL},

    {FORMAT_SVG_SVG, "svg:svg", 1, &engine_svg, NULL},

    {FORMAT_GIF_TK, "gif:tk", 1, &engine_tk, NULL},

    {0, NULL, 0, NULL, NULL}
};
