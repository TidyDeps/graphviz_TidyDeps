/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include <guiddef.h>
#include <gvc/gvplugin.h>

#include "gvplugin_gdiplus.h"

extern gvplugin_installed_t gvrender_gdiplus_types[];
extern gvplugin_installed_t gvtextlayout_gdiplus_types[];
extern gvplugin_installed_t gvloadimage_gdiplus_types[];
extern gvplugin_installed_t gvdevice_gdiplus_types[];
extern gvplugin_installed_t gvdevice_gdiplus_types_for_cairo[];

using namespace std;
using namespace Gdiplus;

/* class id corresponding to each format_type */
static GUID format_id [] = {
	GUID_NULL,
	GUID_NULL,
	ImageFormatBMP,
	ImageFormatEMF,
	ImageFormatEMF,
	ImageFormatGIF,
	ImageFormatJPEG,
	ImageFormatPNG,
	ImageFormatTIFF
};

static ULONG_PTR _gdiplusToken = 0;

static void UnuseGdiplus()
{
	GdiplusShutdown(_gdiplusToken);
}

void UseGdiplus()
{
	/* only startup once, and ensure we get shutdown */
	if (!_gdiplusToken)
	{
		GdiplusStartupInput input;
		GdiplusStartup(&_gdiplusToken, &input, nullptr);
		atexit(&UnuseGdiplus);
	}
}

const Gdiplus::StringFormat* GetGenericTypographic()
{
	const Gdiplus::StringFormat* format = StringFormat::GenericTypographic();
	return format;
}

void SaveBitmapToStream(Bitmap &bitmap, IStream *stream, int format)
{
	/* search the encoders for one that matches our device id, then save the bitmap there */
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
	UINT encoderNum;
	UINT encoderSize;
	GetImageEncodersSize(&encoderNum, &encoderSize);
	vector<char> codec_buffer(encoderSize);
	ImageCodecInfo *codecs = (ImageCodecInfo *)&codec_buffer.front();
	GetImageEncoders(encoderNum, encoderSize, codecs);
	for (UINT i = 0; i < encoderNum; ++i)
		if (IsEqualGUID(format_id[format], codecs[i].FormatID)) {
			bitmap.Save(stream, &codecs[i].Clsid, nullptr);
			break;
		}
}

static gvplugin_api_t apis[] = {
    {API_render, gvrender_gdiplus_types},
	{API_textlayout, gvtextlayout_gdiplus_types},
	{API_loadimage, gvloadimage_gdiplus_types},
    {API_device, gvdevice_gdiplus_types},
	{API_device, gvdevice_gdiplus_types_for_cairo},
    {(api_t)0, 0},
};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef GVDLL
#define GVPLUGIN_GDIPLUS_API __declspec(dllexport)
#else
#define GVPLUGIN_GDIPLUS_API
#endif

GVPLUGIN_GDIPLUS_API gvplugin_library_t gvplugin_gdiplus_LTX_library = {
  const_cast<char*>("gdiplus"), apis
};

#ifdef __cplusplus
}
#endif
