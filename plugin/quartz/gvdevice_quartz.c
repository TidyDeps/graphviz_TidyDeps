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

#include <gvc/gvplugin_device.h>

#include "gvplugin_quartz.h"
#include <stdbool.h>
#include <TargetConditionals.h>
#include <util/gv_math.h>

#if !TARGET_OS_IPHONE && defined(HAVE_PANGOCAIRO)

static const void *memory_data_consumer_get_byte_pointer(void *info)
{
	return info;
}

CGDataProviderDirectCallbacks memory_data_provider_callbacks = {
	0,
	memory_data_consumer_get_byte_pointer,
	NULL,
	NULL,
	NULL
};

static void quartz_format(GVJ_t *job)
{
	/* image destination -> data consumer -> job's gvdevice */
	/* data provider <- job's imagedata */
	CGDataConsumerRef data_consumer = CGDataConsumerCreate(job, &device_data_consumer_callbacks);
	CGImageDestinationRef image_destination =
	  CGImageDestinationCreateWithDataConsumer(data_consumer,
	    format_to_uti((format_type)job->device.id), 1, NULL);
	CGDataProviderRef data_provider = CGDataProviderCreateDirect(job->imagedata, BYTES_PER_PIXEL * job->width * job->height, &memory_data_provider_callbacks);
	
	const void *keys[] = {kCGImagePropertyDPIWidth, kCGImagePropertyDPIHeight};
	const void *values[] = {
		CFNumberCreate(NULL, kCFNumberDoubleType, &job->dpi.x),
		CFNumberCreate(NULL, kCFNumberDoubleType, &job->dpi.y)
	};
	CFDictionaryRef dpi = CFDictionaryCreate(NULL, keys, values,
	                                         sizeof(keys) / sizeof(keys[0]),
	                                         &kCFTypeDictionaryKeyCallBacks,
	                                         &kCFTypeDictionaryValueCallBacks);

	/* add the bitmap image to the destination and save it */
	CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGImageRef image = CGImageCreate (
		job->width,							/* width in pixels */
		job->height,						/* height in pixels */
		BITS_PER_COMPONENT,					/* bits per component */
		BYTES_PER_PIXEL * 8,				/* bits per pixel */
		BYTES_PER_PIXEL * job->width,		/* bytes per row: exactly width # of pixels */
		color_space,						/* color space: sRGB */
		kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little,	/* bitmap info: corresponds to CAIRO_FORMAT_ARGB32 */
		data_provider,						/* data provider: from imagedata */
		NULL,								/* decode: don't remap colors */
		false, // don't interpolate
		kCGRenderingIntentDefault			/* rendering intent (what to do with out-of-gamut colors): default */
	);
	CGImageDestinationAddImage(image_destination, image, dpi);
	CGImageDestinationFinalize(image_destination);
	
	/* clean up */
	CGImageRelease(image);
	CGColorSpaceRelease(color_space);
	CGDataProviderRelease(data_provider);
	if (image_destination)
		CFRelease(image_destination);
	if (dpi != NULL)
		CFRelease(dpi);
	CGDataConsumerRelease(data_consumer);
}

static gvdevice_engine_t quartz_engine = {
    NULL,		/* quartz_initialize */
    quartz_format,
    NULL,		/* quartz_finalize */
};

static gvdevice_features_t device_features_quartz = {
	GVDEVICE_BINARY_FORMAT        
          | GVDEVICE_DOES_TRUECOLOR,/* flags */
	{0.,0.},                    /* default margin - points */
	{0.,0.},                    /* default page width, height - points */
	{96.,96.},                  /* dpi */
};

gvplugin_installed_t gvdevice_quartz_types_for_cairo[] = {
	{FORMAT_BMP, "bmp:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_GIF, "gif:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_EXR, "exr:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_ICNS, "icns:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_ICO, "ico:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_JPEG, "jpe:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_JPEG, "jpeg:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_JPEG, "jpg:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_JPEG2000, "jp2:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_PICT, "pct:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_PICT, "pict:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_PNG, "png:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_PSD, "psd:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_SGI, "sgi:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_TIFF, "tif:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_TIFF, "tiff:cairo", 7, &quartz_engine, &device_features_quartz},
	{FORMAT_TGA, "tga:cairo", 7, &quartz_engine, &device_features_quartz},
	{0, NULL, 0, NULL, NULL}
};

#endif
