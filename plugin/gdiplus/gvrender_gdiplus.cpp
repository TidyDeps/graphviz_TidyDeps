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
#include <cassert>
#include <climits>
#include <stdlib.h>
#include <string.h>
#include <util/gv_math.h>

#include <gvc/gvplugin_device.h>
#include <gvc/gvplugin_render.h>
#include <gvc/gvio.h>
#include "gvplugin_gdiplus.h"

#include <memory>
#include <vector>

using namespace std;
using namespace Gdiplus;

/* Graphics for internal use, so that we can record image etc. for subsequent retrieval off the job struct */
struct ImageGraphics: public Graphics
{
	Image *image;
	IStream *stream;

	ImageGraphics(Image *newImage, IStream *newStream):
		Graphics(newImage), image(newImage), stream(newStream)
	{
	}
};

static void gdiplusgen_begin_job(GVJ_t *job)
{
	UseGdiplus();
	if (!job->external_context)
		job->context = nullptr;
	else if (job->device.id == FORMAT_METAFILE)
	{
		/* save the passed-in context in the window field, so we can create a Metafile in the context field later on */
		job->window = job->context;
		auto m = reinterpret_cast<Metafile**>(job->window);
		*m = nullptr;
		job->context = nullptr;
}
}

static void gdiplusgen_end_job(GVJ_t *job)
{
		auto context = reinterpret_cast<Graphics*>(job->context);

	if (!job->external_context) {
		/* flush and delete the graphics */
		ImageGraphics *imageGraphics = static_cast<ImageGraphics *>(context);
		Image *image = imageGraphics->image;
		IStream *stream = imageGraphics->stream;
		delete imageGraphics;

		switch (job->device.id) {
			case FORMAT_EMF:
			case FORMAT_EMFPLUS:
			case FORMAT_METAFILE:
				break;
			default:
				SaveBitmapToStream(*static_cast<Bitmap *>(image), stream, job->device.id);
				break;
		}

		delete image;	/* NOTE: in the case of EMF, this actually flushes out the image to the underlying stream */

		/* blast the streamed buffer back to the gvdevice */
		/* NOTE: this is somewhat inefficient since we should be streaming directly to gvdevice rather than buffering first */
		/* ... however, GDI+ requires any such direct IStream to implement Seek Read, Write, Stat methods and gvdevice really only offers a write-once model */
		HGLOBAL buffer = nullptr;
		GetHGlobalFromStream(stream, &buffer);
		stream->Release();
		gvwrite(job, (char*)GlobalLock(buffer), GlobalSize(buffer));
		GlobalFree(buffer);
	}
	else if (job->device.id == FORMAT_METAFILE)
		delete context;
}

static void gdiplusgen_begin_page(GVJ_t *job)
{
	if (!job->context)
	{
		if (!job->external_context && job->device.id != FORMAT_METAFILE) {
		/* allocate memory and attach stream to it */
		HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, 0);
		IStream *stream = nullptr;
		CreateStreamOnHGlobal(buffer, FALSE, &stream);	/* FALSE means don't deallocate buffer when releasing stream */

		Image *image;
		switch (job->device.id) {

		case FORMAT_EMF:
		case FORMAT_EMFPLUS:
			/* EMF image */
			image = new Metafile (stream,
				DeviceContext().hdc,
				RectF(0.0f, 0.0f, job->width, job->height),
				MetafileFrameUnitPixel,
					job->device.id == FORMAT_EMFPLUS ? EmfTypeEmfPlusOnly : EmfTypeEmfPlusDual);
				/* output in EMF for wider compatibility; output in EMF+ for antialiasing etc. */
			break;

		default:
			/* bitmap image */
			image = new Bitmap (job->width, job->height, PixelFormat32bppARGB);
			break;
		}

			job->context = new ImageGraphics(image, stream);
	}
		else if (job->device.id == FORMAT_METAFILE)
		{
			/* create EMF image in the job window which was set during begin job */
			Metafile* metafile = new Metafile(DeviceContext().hdc,
				RectF(0.0f, 0.0f, job->width, job->height),
				MetafileFrameUnitPixel,
				EmfTypeEmfPlusOnly);
			auto m = reinterpret_cast<Metafile**>(job->window);
			*m = metafile;
			job->context = new Graphics(metafile);
		}
	}

	/* start graphics state */
	Graphics *context = (Graphics *)job->context;
	context->SetSmoothingMode(SmoothingModeHighQuality);
	context->SetTextRenderingHint(TextRenderingHintAntiAlias);

	/* set up the context transformation */
	context->ResetTransform();

	context->ScaleTransform(d2f(job->scale.x), d2f(job->scale.y));
	context->RotateTransform(-job->rotation);
	context->TranslateTransform(d2f(job->translation.x), d2f(-job->translation.y));
}



static void gdiplusgen_textspan(GVJ_t *job, pointf p, textspan_t *span)
{
	auto context = reinterpret_cast<Graphics*>(job->context);

		/* adjust text position */
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

	Layout* layout;
	if (span->free_layout == &gdiplus_free_layout)
		layout = reinterpret_cast<Layout*>(span->layout);
	else
		layout = new Layout(span->font->name, span->font->size, span->str);

	/* draw the text */
	SolidBrush brush(Color(job->obj->pencolor.u.rgba [3], job->obj->pencolor.u.rgba [0], job->obj->pencolor.u.rgba [1], job->obj->pencolor.u.rgba [2]));
	context->DrawString(&layout->text[0], layout->text.size(), layout->font.get(),
	                    PointF(d2f(p.x), d2f(-p.y)), GetGenericTypographic(),
	                    &brush);

	if (span->free_layout != &gdiplus_free_layout)
		delete layout;

	}


static vector<PointF> points(pointf *A, int n)
{
	/* convert Graphviz pointf (struct of double) to GDI+ PointF (struct of float) */
	vector<PointF> newPoints;
	for (int i = 0; i < n; ++i)
		newPoints.push_back(PointF(d2f(A[i].x), d2f(-A[i].y)));
	return newPoints;
}

static void gdiplusgen_path(GVJ_t *job, const GraphicsPath &pathname,
                            int filled) {
	auto context = reinterpret_cast<Graphics *>(job->context);

	/* fill the given path with job fill color */
	if (filled) {
		SolidBrush fill_brush(Color(job->obj->fillcolor.u.rgba [3], job->obj->fillcolor.u.rgba [0], job->obj->fillcolor.u.rgba [1], job->obj->fillcolor.u.rgba [2]));
		context->FillPath(&fill_brush, &pathname);
	}

	/* draw the given path from job pen color and pen width */
	Pen draw_pen(Color(job->obj->pencolor.u.rgba [3], job->obj->pencolor.u.rgba [0], job->obj->pencolor.u.rgba [1], job->obj->pencolor.u.rgba [2]),
		d2f(job->obj->penwidth));

	/*
	 * Set line type
	 * See http://msdn.microsoft.com/en-us/library/ms535050%28v=vs.85%29.aspx
	 */
	switch (job->obj->pen) {
	case PEN_NONE:
		return;
	case PEN_DASHED:
		draw_pen.SetDashStyle(DashStyleDash);
		break;
	case PEN_DOTTED:
		draw_pen.SetDashStyle(DashStyleDot);
		break;
	case PEN_SOLID:
		break;
	}

	context->DrawPath(&draw_pen, &pathname);
}

static void gdiplusgen_ellipse(GVJ_t *job, pointf *A, int filled)
{
	/* convert ellipse into path */
	GraphicsPath pathname;
	double dx = A[1].x - A[0].x;
	double dy = A[1].y - A[0].y;
	pathname.AddEllipse(RectF(d2f(A[0].x - dx), d2f(-A[0].y - dy), d2f(dx * 2),
	                          d2f(dy * 2)));

	/* draw the path */
	gdiplusgen_path(job, pathname, filled);
}

static void gdiplusgen_polygon(GVJ_t *job, pointf *A, size_t n, int filled) {
	/* convert polygon into path */
	GraphicsPath pathname;
	assert(n <= INT_MAX);
	pathname.AddPolygon(&points(A, n).front(), (int)n);

	/* draw the path */
	gdiplusgen_path(job, pathname, filled);
}

static void gdiplusgen_bezier(GVJ_t *job, pointf *A, size_t n, int filled) {
	/* convert the beziers into path */
	GraphicsPath pathname;
	assert(n <= INT_MAX);
	pathname.AddBeziers(&points(A, n).front(), (int)n);

	/* draw the path */
	gdiplusgen_path(job, pathname, filled);
}

static void gdiplusgen_polyline(GVJ_t *job, pointf *A, size_t n) {
	/* convert the lines into path */
	GraphicsPath pathname;
	assert(n <= INT_MAX);
	pathname.AddLines(&points(A,n).front(), (int)n);

	/* draw the path */
	gdiplusgen_path(job, pathname, 0);
}

static gvrender_engine_t gdiplusgen_engine = {
    gdiplusgen_begin_job,
    gdiplusgen_end_job,
    0,							/* gdiplusgen_begin_graph */
    0,							/* gdiplusgen_end_graph */
    0,							/* gdiplusgen_begin_layer */
    0,							/* gdiplusgen_end_layer */
    gdiplusgen_begin_page,
    0,							/* gdiplusgen_end_page */
    0,							/* gdiplusgen_begin_cluster */
    0,							/* gdiplusgen_end_cluster */
    0,							/* gdiplusgen_begin_nodes */
    0,							/* gdiplusgen_end_nodes */
    0,							/* gdiplusgen_begin_edges */
    0,							/* gdiplusgen_end_edges */
    0,							/* gdiplusgen_begin_node */
    0,							/* gdiplusgen_end_node */
    0,							/* gdiplusgen_begin_edge */
    0,							/* gdiplusgen_end_edge */
    0,							/* gdiplusgen_begin_anchor */
    0,							/* gdiplusgen_end_anchor */
    0,							/* gdiplusgen_begin_label */
    0,							/* gdiplusgen_end_label */
    gdiplusgen_textspan,
    0,
    gdiplusgen_ellipse,
    gdiplusgen_polygon,
    gdiplusgen_bezier,
    gdiplusgen_polyline,
    0,							/* gdiplusgen_comment */
    0,							/* gdiplusgen_library_shape */
};

static gvrender_features_t render_features_gdiplus = {
	GVRENDER_Y_GOES_DOWN | GVRENDER_DOES_TRANSFORM, /* flags */
    4.,							/* default pad - graph units */
    nullptr,						/* knowncolors */
    0,							/* sizeof knowncolors */
    RGBA_BYTE				/* color_type */
};

static gvdevice_features_t device_features_gdiplus_emf = {
    GVDEVICE_BINARY_FORMAT
      | GVDEVICE_DOES_TRUECOLOR
	  | GVRENDER_NO_WHITE_BG,/* flags */
    {0.,0.},                    /* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {72.,72.}                  /* dpi */
};

static gvdevice_features_t device_features_gdiplus = {
    GVDEVICE_BINARY_FORMAT
      | GVDEVICE_DOES_TRUECOLOR,/* flags */
    {0.,0.},                    /* default margin - points */
    {0.,0.},                    /* default page width, height - points */
    {96.,96.}                  /* dpi */
};

gvplugin_installed_t gvrender_gdiplus_types[] = {
    {0, "gdiplus", 1, &gdiplusgen_engine, &render_features_gdiplus},
    {0, nullptr, 0, nullptr, nullptr}
};

gvplugin_installed_t gvdevice_gdiplus_types[] = {
	{FORMAT_METAFILE, "metafile:gdiplus", 8, nullptr, &device_features_gdiplus_emf},
	{FORMAT_BMP, "bmp:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_EMF, "emf:gdiplus", 8, nullptr, &device_features_gdiplus_emf},
	{FORMAT_EMFPLUS, "emfplus:gdiplus", 8, nullptr, &device_features_gdiplus_emf},
	{FORMAT_GIF, "gif:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_JPEG, "jpe:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_JPEG, "jpeg:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_JPEG, "jpg:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_PNG, "png:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_TIFF, "tif:gdiplus", 8, nullptr, &device_features_gdiplus},
	{FORMAT_TIFF, "tiff:gdiplus", 8, nullptr, &device_features_gdiplus},
	{0, nullptr, 0, nullptr, nullptr}
};
