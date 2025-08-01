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

#include <common/types.h>
#include <gvc/gvplugin.h>
#include "gvplugin_quartz.h"
#include <gvc/gvio.h>
#include <TargetConditionals.h>

extern gvplugin_installed_t gvrender_quartz_types;
extern gvplugin_installed_t gvtextlayout_quartz_types;
extern gvplugin_installed_t gvloadimage_quartz_types;
extern gvplugin_installed_t gvdevice_quartz_types;

#if !TARGET_OS_IPHONE
extern gvplugin_installed_t gvdevice_quartz_types_for_cairo;
#endif
/* data consumer backed by the gvdevice */

static size_t device_data_consumer_put_bytes (void *info, const void *buffer, size_t count)
{
	return gvwrite(info, buffer, count);
}

CGDataConsumerCallbacks device_data_consumer_callbacks = {
	device_data_consumer_put_bytes,
	NULL
};

CFStringRef format_to_uti(format_type format)
{
	switch (format) {
		case FORMAT_BMP:
			return CFSTR("com.microsoft.bmp");
		case FORMAT_EXR:
			return CFSTR("com.ilm.openexr-image");
		case FORMAT_GIF:
			return CFSTR("com.compuserve.gif");
		case FORMAT_ICNS:
			return CFSTR("com.apple.icns");
		case FORMAT_ICO:
			return CFSTR("com.microsoft.ico");
		case FORMAT_JPEG:
			return CFSTR("public.jpeg");
		case FORMAT_JPEG2000:
			return CFSTR("public.jpeg-2000");
		case FORMAT_PICT:
			return CFSTR("com.apple.pict");
		case FORMAT_PNG:
			return CFSTR("public.png");
		case FORMAT_PSD:
			return CFSTR("com.adobe.photoshop-image");
		case FORMAT_SGI:
			return CFSTR("com.sgi.sgi-image");
		case FORMAT_TIFF:
			return CFSTR("public.tiff");
		case FORMAT_TGA:
			return CFSTR("com.truevision.tga-image");
		default:
			return NULL;
	}
}

static gvplugin_api_t apis[] = {
    {API_render, &gvrender_quartz_types},
	{API_textlayout, &gvtextlayout_quartz_types},
	{API_loadimage, &gvloadimage_quartz_types},
	{API_device, &gvdevice_quartz_types},
#if !TARGET_OS_IPHONE && defined(HAVE_PANGOCAIRO)
    {API_device, &gvdevice_quartz_types_for_cairo},
#endif
    {(api_t)0, 0},
};

gvplugin_library_t gvplugin_quartz_LTX_library = { "quartz", apis };
