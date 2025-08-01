/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include <cstddef>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <LASi.h>

#include "config.h"

#include <gvc/gvplugin_render.h>
#include <gvc/gvplugin_device.h>
#include <gvc/gvio.h>
#include <gvc/gvcint.h>
#include <common/const.h>
#include <common/utils.h>
#include <util/agxbuf.h>
#include <util/prisize_t.h>
#include <util/unreachable.h>
#include "../core/ps.h"

using namespace LASi;
using namespace std;

// J$: added `pdfmark' URL embedding.  PostScript rendered from
//     dot files with URL attributes will get active PDF links
//     from Adobe's Distiller.
#define PDFMAX  14400 ///< Maximum size of PDF page

enum { FORMAT_PS, FORMAT_PS2, FORMAT_EPS };

struct Context {
  using write_fn = size_t(*)(GVJ_t*, const char*, size_t);

  Context(write_fn fn): save_write_fn(fn) { }

  PostscriptDocument doc;
  write_fn save_write_fn;
};

static size_t lasi_head_writer(GVJ_t * job, const char *s, size_t len)
{
    Context *ctxt = reinterpret_cast<Context*>(job->context);
    ctxt->doc.osHeader() << s;
    return len;
}

static size_t lasi_body_writer(GVJ_t * job, const char *s, size_t len)
{
    Context *ctxt = reinterpret_cast<Context*>(job->context);
    ctxt->doc.osBody() << s;
    return len;
}

static size_t lasi_footer_writer(GVJ_t * job, const char *s, size_t len)
{
    Context *ctxt = reinterpret_cast<Context*>(job->context);
    ctxt->doc.osFooter() << s;
    return len;
}

static void lasi_begin_job(GVJ_t * job)
{
    job->context = new Context(job->gvc->write_fn);
    job->gvc->write_fn = lasi_head_writer;

    gvprintf(job, "%%%%Creator: %s version %s (%s)\n",
	    job->common->info[0], job->common->info[1], job->common->info[2]);
}

static void lasi_end_job(GVJ_t * job)
{
    job->gvc->write_fn = lasi_footer_writer;

    if (job->render.id != FORMAT_EPS)
	gvprintf(job, "%%%%Pages: %d\n", job->common->viewNum);
    if (job->common->show_boxes == nullptr)
        if (job->render.id != FORMAT_EPS)
	    gvprintf(job, "%%%%BoundingBox: %d %d %d %d\n",
	        job->boundingBox.LL.x, job->boundingBox.LL.y,
	        job->boundingBox.UR.x, job->boundingBox.UR.y);
    gvputs(job, "end\nrestore\n");

    {
        // create the new stream to "redirect" cout's output to
        ostringstream output;
    
        // smart class that will swap streambufs and replace them
        // when object goes out of scope.
        class StreamBuf_Swapper
        {
        public:
	    StreamBuf_Swapper(ostream & orig, ostream & replacement)
		          : buf_(orig.rdbuf()), str_(orig)
	    {
	        orig.rdbuf(replacement.rdbuf());
	    }
	    ~StreamBuf_Swapper()
	    {
	        str_.rdbuf(buf_);
	    }
        private:
	    std::streambuf * buf_;
	    std::ostream & str_;
        } swapper(cout, output);
    
        Context *ctxt = reinterpret_cast<Context*>(job->context);
        ctxt->doc.write(cout);
    
        job->gvc->write_fn = ctxt->save_write_fn;
        gvputs(job, output.str().c_str());

	delete ctxt;
    }
}

static void lasi_begin_graph(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    job->gvc->write_fn = lasi_body_writer;

    if (job->common->viewNum == 0) {
	gvprintf(job, "%%%%Title: %s\n", agnameof(obj->u.g));
    	if (job->render.id != FORMAT_EPS)
	    gvputs(job, "%%Pages: (atend)\n");
	else
	    gvputs(job, "%%Pages: 1\n");
        if (job->common->show_boxes == nullptr) {
    	    if (job->render.id != FORMAT_EPS)
		gvputs(job, "%%BoundingBox: (atend)\n");
	    else
	        gvprintf(job, "%%%%BoundingBox: %d %d %d %d\n",
	            job->pageBoundingBox.LL.x, job->pageBoundingBox.LL.y,
	            job->pageBoundingBox.UR.x, job->pageBoundingBox.UR.y);
	}
	gvputs(job, "%%EndComments\nsave\n");
        // include shape library
        cat_libfile(job, job->common->lib, ps_txt);
	// include epsf
        epsf_define(job);
        if (job->common->show_boxes) {
            const char* args[2];
            args[0] = job->common->show_boxes[0];
            args[1] = nullptr;
            cat_libfile(job, nullptr, args);
        }
    }
    // Set base URL for relative links (for Distiller ≥ 3.0)
    if (obj->url)
	gvprintf(job, "[ {Catalog} << /URI << /Base %s >> >>\n"
		"/PUT pdfmark\n", ps_string(obj->url, CHAR_UTF8));
}

static void lasi_begin_layer(GVJ_t * job, char*, int layerNum, int numLayers)
{
    gvprintf(job, "%d %d setlayer\n", layerNum, numLayers);
}

static void lasi_begin_page(GVJ_t * job)
{
    box pbr = job->pageBoundingBox;

    gvprintf(job, "%%%%Page: %d %d\n",
	    job->common->viewNum + 1, job->common->viewNum + 1);
    if (job->common->show_boxes == nullptr)
	gvprintf(job, "%%%%PageBoundingBox: %d %d %d %d\n",
	    pbr.LL.x, pbr.LL.y, pbr.UR.x, pbr.UR.y);
    gvprintf(job, "%%%%PageOrientation: %s\n",
	    (job->rotation ? "Landscape" : "Portrait"));
    if (job->render.id == FORMAT_PS2)
	gvprintf(job, "<< /PageSize [%d %d] >> setpagedevice\n",
	    pbr.UR.x, pbr.UR.y);
    gvprintf(job, "%d %d %d beginpage\n",
	job->pagesArrayElem.x, job->pagesArrayElem.y, job->numPages);
    if (job->common->show_boxes == nullptr)
	gvprintf(job, "gsave\n%d %d %d %d boxprim clip newpath\n",
	    pbr.LL.x, pbr.LL.y, pbr.UR.x-pbr.LL.x, pbr.UR.y-pbr.LL.y);
    gvprintf(job, "%g %g set_scale %d rotate %g %g translate\n",
	    job->scale.x, job->scale.y,
	    job->rotation,
	    job->translation.x, job->translation.y);

    // Define the size of the PS canvas
    if (job->render.id == FORMAT_PS2) {
	if (pbr.UR.x >= PDFMAX || pbr.UR.y >= PDFMAX)
	    job->common->errorfn("canvas size (%d,%d) exceeds PDF limit (%d)\n"
		  "\t(suggest setting a bounding box size, see dot(1))\n",
		  pbr.UR.x, pbr.UR.y, PDFMAX);
	gvprintf(job, "[ /CropBox [%d %d %d %d] /PAGES pdfmark\n",
		pbr.LL.x, pbr.LL.y, pbr.UR.x, pbr.UR.y);
    }
}

static void lasi_end_page(GVJ_t * job)
{
    if (job->common->show_boxes) {
	gvputs(job, "0 0 0 edgecolor\n");
	cat_libfile(job, nullptr, job->common->show_boxes + 1);
    }
    // the showpage is really a no-op, but at least one PS processor
    // out there needs to see this literal token.  endpage does the real work.
    gvputs(job, "endpage\nshowpage\ngrestore\n");
    gvputs(job, "%%PageTrailer\n");
    gvprintf(job, "%%%%EndPage: %d\n", job->common->viewNum);
}

static void lasi_begin_cluster(GVJ_t * job)
{
    obj_state_t *obj = job->obj;

    gvprintf(job, "%% %s\n", agnameof(obj->u.sg));
    gvputs(job, "gsave\n");
}

static void lasi_end_cluster(GVJ_t * job)
{
    gvputs(job, "grestore\n");
}

static void lasi_begin_node(GVJ_t * job)
{
    gvputs(job, "gsave\n");
}

static void lasi_end_node(GVJ_t * job)
{
    gvputs(job, "grestore\n");
}

static void
lasi_begin_edge(GVJ_t * job)
{
    gvputs(job, "gsave\n");
}

static void lasi_end_edge(GVJ_t * job)
{
    gvputs(job, "grestore\n");
}

static void lasi_begin_anchor(GVJ_t *job, char *url, char*, char*, char*)
{
    obj_state_t *obj = job->obj;

    if (url && obj->url_map_p) {
	gvputs(job, "[ /Rect [ ");
	gvprintpointflist(job, obj->url_map_p, 2);
	gvputs(job, " ]\n");
	gvprintf(job, "  /Border [ 0 0 0 ]\n"
		"  /Action << /Subtype /URI /URI %s >>\n"
		"  /Subtype /Link\n"
		"/ANN pdfmark\n",
		ps_string(url, CHAR_UTF8));
    }
}

static void ps_set_pen_style(GVJ_t *job)
{
    double penwidth = job->obj->penwidth;
    char *p, *line, **s = job->obj->rawstyle;

    gvprintdouble(job, penwidth);
    gvputs(job," setlinewidth\n");

    while (s && (p = line = *s++)) {
	if (line == std::string{"setlinewidth"})
	    continue;
	while (*p)
	    p++;
	p++;
	while (*p) {
	    gvprintf(job,"%s ", p);
	    while (*p)
		p++;
	    p++;
	}
	if (line == std::string{"invis"})
	    job->obj->penwidth = 0;
	gvprintf(job, "%s\n", line);
    }
}

static void ps_set_color(GVJ_t *job, gvcolor_t *color)
{
    const char *objtype;

    if (color) {
	switch (job->obj->type) {
	    case ROOTGRAPH_OBJTYPE:
	    case CLUSTER_OBJTYPE:
		objtype = "graph";
		break;
	    case NODE_OBJTYPE:
		objtype = "node";
		break;
	    case EDGE_OBJTYPE:
		objtype = "edge";
		break;
	    default:
		objtype = "sethsb";
		break;
	}
	gvprintf(job, "%.3g %.3g %.3g %scolor\n",
	    color->u.HSVA[0], color->u.HSVA[1], color->u.HSVA[2], objtype);
    }
}

static void lasi_textspan(GVJ_t * job, pointf p, textspan_t * span)
{
    const char *font;
    const PangoFontDescription *pango_font;
    FontStretch stretch; 
    FontStyle style;
    FontVariant variant;
    FontWeight weight; 
    PostscriptAlias *pA;

    if (job->obj->pencolor.u.HSVA[3] < .5)
	return; // skip transparent text

    if (span->layout) {
	pango_font = pango_layout_get_font_description((PangoLayout*)(span->layout));
	font = pango_font_description_get_family(pango_font);
	switch (pango_font_description_get_stretch(pango_font)) {
	    case PANGO_STRETCH_ULTRA_CONDENSED: stretch = ULTRACONDENSED; break;
	    case PANGO_STRETCH_EXTRA_CONDENSED: stretch = EXTRACONDENSED; break;
	    case PANGO_STRETCH_CONDENSED: stretch = CONDENSED; break;
	    case PANGO_STRETCH_SEMI_CONDENSED: stretch = SEMICONDENSED; break;
	    case PANGO_STRETCH_NORMAL: stretch = NORMAL_STRETCH; break;
	    case PANGO_STRETCH_SEMI_EXPANDED: stretch = SEMIEXPANDED; break;
	    case PANGO_STRETCH_EXPANDED: stretch = EXPANDED; break;
	    case PANGO_STRETCH_EXTRA_EXPANDED: stretch = EXTRAEXPANDED; break;
	    case PANGO_STRETCH_ULTRA_EXPANDED: stretch = ULTRAEXPANDED; break;
	    default:
	        UNREACHABLE();
	}
	switch (pango_font_description_get_style(pango_font)) {
	    case PANGO_STYLE_NORMAL: style = NORMAL_STYLE; break;
	    case PANGO_STYLE_OBLIQUE: style = OBLIQUE; break;
	    case PANGO_STYLE_ITALIC: style = ITALIC; break;
	    default:
	        UNREACHABLE();
	}
	switch (pango_font_description_get_variant(pango_font)) {
	    case PANGO_VARIANT_NORMAL: variant = NORMAL_VARIANT; break;
	    case PANGO_VARIANT_SMALL_CAPS: variant = SMALLCAPS; break;
#if PANGO_VERSION_CHECK(1, 50, 0)
	    case PANGO_VARIANT_ALL_SMALL_CAPS: variant = SMALLCAPS; break;
	    case PANGO_VARIANT_PETITE_CAPS: variant = SMALLCAPS; break;
	    case PANGO_VARIANT_ALL_PETITE_CAPS: variant = SMALLCAPS; break;
	    case PANGO_VARIANT_UNICASE: variant = SMALLCAPS; break;
	    case PANGO_VARIANT_TITLE_CAPS: variant = SMALLCAPS; break;
#endif
	    default:
	        UNREACHABLE();
	}
	switch (pango_font_description_get_weight(pango_font)) {
#if PANGO_VERSION_CHECK(1, 24, 0)
	    case PANGO_WEIGHT_THIN: weight = ULTRALIGHT; break; // no exact match in LASi
#endif
	    case PANGO_WEIGHT_ULTRALIGHT: weight = ULTRALIGHT; break;
	    case PANGO_WEIGHT_LIGHT: weight = LIGHT; break;
#if PANGO_VERSION_CHECK(1, 36, 7)
	    case PANGO_WEIGHT_SEMILIGHT: weight = LIGHT; break; // no exact match in LASi
#endif
#if PANGO_VERSION_CHECK(1, 24, 0)
	    case PANGO_WEIGHT_BOOK: weight = NORMAL_WEIGHT; break; // no exact match in LASi
#endif
	    case PANGO_WEIGHT_NORMAL: weight = NORMAL_WEIGHT; break;
#if PANGO_VERSION_CHECK(1, 24, 0)
	    case PANGO_WEIGHT_MEDIUM: weight = NORMAL_WEIGHT; break; // no exact match in LASi
#endif
	    case PANGO_WEIGHT_SEMIBOLD: weight = BOLD; break; /* no exact match in lasi */
	    case PANGO_WEIGHT_BOLD: weight = BOLD; break;
	    case PANGO_WEIGHT_ULTRABOLD: weight = ULTRABOLD; break;
	    case PANGO_WEIGHT_HEAVY: weight = HEAVY; break;
#if PANGO_VERSION_CHECK(1, 24, 0)
	    case PANGO_WEIGHT_ULTRAHEAVY: weight = HEAVY; break; // no exact match in LASi
#endif
	    default:
	        UNREACHABLE();
	}
    }
    else {
	pA = span->font->postscript_alias;
	font = pA->svg_font_family;
	stretch = NORMAL_STRETCH;
	if (pA->svg_font_style && pA->svg_font_style == std::string{"italic"})
	    style = ITALIC;
	else
	    style = NORMAL_STYLE;
	variant = NORMAL_VARIANT;
	if (pA->svg_font_weight && pA->svg_font_weight == std::string{"bold"})
	    weight = BOLD;
	else
	    weight = NORMAL_WEIGHT;
    }

    ps_set_color(job, &(job->obj->pencolor));
    Context *ctxt = reinterpret_cast<Context*>(job->context);
    ctxt->doc.osBody() << setFont(font, style, weight, variant, stretch) << setFontSize(span->font->size) << "\n";
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
    p.y += span->yoffset_centerline;
    gvprintpointf(job, p);
    gvputs(job, " moveto ");
    ctxt->doc.osBody() << show(span->str) << "\n";

}

static void lasi_ellipse(GVJ_t * job, pointf * A, int filled)
{
    // A[] contains 2 points: the center and corner.
    pointf AA[2];

    AA[0] = A[0];
    AA[1].x = A[1].x - A[0].x;
    AA[1].y = A[1].y - A[0].y;

    if (filled && job->obj->fillcolor.u.HSVA[3] > .5) {
	ps_set_color(job, &(job->obj->fillcolor));
	gvprintpointflist(job, AA, 2);
	gvputs(job, " ellipse_path fill\n");
    }
    if (job->obj->pencolor.u.HSVA[3] > .5) {
	ps_set_pen_style(job);
	ps_set_color(job, &(job->obj->pencolor));
	gvprintpointflist(job, AA, 2);
	gvputs(job, " ellipse_path stroke\n");
    }
}

static void lasi_bezier(GVJ_t *job, pointf *A, size_t n, int filled) {
    if (filled && job->obj->fillcolor.u.HSVA[3] > .5) {
	ps_set_color(job, &(job->obj->fillcolor));
	gvputs(job, "newpath ");
	gvprintpointf(job, A[0]);
	gvputs(job, " moveto\n");
	for (size_t j = 1; j < n; j += 3) {
	    gvprintpointflist(job, &A[j], 3);
	    gvputs(job, " curveto\n");
	}
	gvputs(job, "closepath fill\n");
    }
    if (job->obj->pencolor.u.HSVA[3] > .5) {
	ps_set_pen_style(job);
	ps_set_color(job, &(job->obj->pencolor));
	gvputs(job, "newpath ");
	gvprintpointf(job, A[0]);
	gvputs(job, " moveto\n");
	for (size_t j = 1; j < n; j += 3) {
	    gvprintpointflist(job, &A[j], 3);
	    gvputs(job, " curveto\n");
	}
	gvputs(job, "stroke\n");
    }
}

static void lasi_polygon(GVJ_t *job, pointf *A, size_t n, int filled) {
    if (filled && job->obj->fillcolor.u.HSVA[3] > .5) {
	ps_set_color(job, &(job->obj->fillcolor));
	gvputs(job, "newpath ");
	gvprintpointf(job, A[0]);
	gvputs(job, " moveto\n");
	for (size_t j = 1; j < n; j++) {
	    gvprintpointf(job, A[j]);
	    gvputs(job, " lineto\n");
        }
	gvputs(job, "closepath fill\n");
    }
    if (job->obj->pencolor.u.HSVA[3] > .5) {
	ps_set_pen_style(job);
	ps_set_color(job, &(job->obj->pencolor));
	gvputs(job, "newpath ");
	gvprintpointf(job, A[0]);
	gvputs(job, " moveto\n");
        for (size_t j = 1; j < n; j++) {
	    gvprintpointf(job, A[j]);
	    gvputs(job, " lineto\n");
	}
	gvputs(job, "closepath stroke\n");
    }
}

static void lasi_polyline(GVJ_t *job, pointf *A, size_t n) {
    if (job->obj->pencolor.u.HSVA[3] > .5) {
	ps_set_pen_style(job);
	ps_set_color(job, &(job->obj->pencolor));
	gvputs(job, "newpath ");
	gvprintpointf(job, A[0]);
	gvputs(job, " moveto\n");
        for (size_t j = 1; j < n; j++) {
	    gvprintpointf(job, A[j]);
	    gvputs(job, " lineto\n");
	}
	gvputs(job, "stroke\n");
    }
}

static void lasi_comment(GVJ_t * job, char *str)
{
    gvputs(job, "% ");
    gvputs(job, str);
    gvputs(job, "\n");
}

static void lasi_library_shape(GVJ_t *job, char *name, pointf *A, size_t n,
                               int filled) {
    if (filled && job->obj->fillcolor.u.HSVA[3] > .5) {
	ps_set_color(job, &(job->obj->fillcolor));
	gvputs(job, "[ ");
	gvprintpointflist(job, A, n);
	gvputs(job, " ");
	gvprintpointf(job, A[0]);
	gvprintf(job, " ]  %" PRISIZE_T " true %s\n", n, name);
    }
    if (job->obj->pencolor.u.HSVA[3] > .5) {
        ps_set_pen_style(job);
        ps_set_color(job, &(job->obj->pencolor));
	gvputs(job, "[ ");
	gvprintpointflist(job, A, n);
	gvputs(job, " ");
	gvprintpointf(job, A[0]);
	gvprintf(job, " ]  %" PRISIZE_T " false %s\n", n, name);
    }
}

static gvrender_engine_t lasi_engine = {
    lasi_begin_job,
    lasi_end_job,
    lasi_begin_graph,
    0,				// lasi_end_graph
    lasi_begin_layer,
    0,				// lasi_end_layer
    lasi_begin_page,
    lasi_end_page,
    lasi_begin_cluster,
    lasi_end_cluster,
    0,				// lasi_begin_nodes
    0,				// lasi_end_nodes
    0,				// lasi_begin_edges
    0,				// lasi_end_edges
    lasi_begin_node,
    lasi_end_node,
    lasi_begin_edge,
    lasi_end_edge,
    lasi_begin_anchor,
    0,				// lasi_end_anchor
    0,				// lasi_begin_label
    0,				// lasi_end_label
    lasi_textspan,
    0,				// lasi_resolve_color
    lasi_ellipse,
    lasi_polygon,
    lasi_bezier,
    lasi_polyline,
    lasi_comment,
    lasi_library_shape,
};

static gvrender_features_t render_features_lasi = {
    GVRENDER_DOES_TRANSFORM
	| GVRENDER_DOES_MAPS
	| GVRENDER_NO_WHITE_BG
	| GVRENDER_DOES_MAP_RECTANGLE,
    4.,                         // default pad - graph units
    nullptr,			// knowncolors
    0,				// sizeof knowncolors
    HSVA_DOUBLE,		// color_type
};

static gvdevice_features_t device_features_ps = {
    GVDEVICE_DOES_PAGES
	| GVDEVICE_DOES_LAYERS,	// flags
    {36.,36.},			// default margin - points
    {612.,792.},                // default page width, height - points
    {72.,72.},			// default dpi
};

static gvdevice_features_t device_features_eps = {
    0,				// flags
    {36.,36.},			// default margin - points
    {612.,792.},                // default page width, height - points
    {72.,72.},			// default dpi
};

gvplugin_installed_t gvrender_lasi_types[] = {
    {FORMAT_PS, "lasi", -5, &lasi_engine, &render_features_lasi},
    {0, nullptr, 0, nullptr, nullptr}
};

gvplugin_installed_t gvdevice_lasi_types[] = {
    {FORMAT_PS, "ps:lasi", -5, nullptr, &device_features_ps},
    {FORMAT_PS2, "ps2:lasi", -5, nullptr, &device_features_ps},
    {FORMAT_EPS, "eps:lasi", -5, nullptr, &device_features_eps},
    {0, nullptr, 0, nullptr, nullptr}
};
