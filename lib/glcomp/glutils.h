/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#ifdef _WIN32
#include "windows.h"
#endif
#include <glcomp/opengl.h>
#include <glcomp/glcompdefs.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void GetOGLPosRef(int x, int y, float *X, float *Y);
    float GetOGLDistance(float l);
void to3D(int x, int y, float *X, float *Y, float *Z);
    double point_to_lineseg_dist(glCompPoint p, glCompPoint a, glCompPoint b);
    extern void glCompCalcWidget(glCompCommon * parent,
				 glCompCommon * child, glCompCommon * ref);
    extern void glCompDrawRectangle(glCompRect * r);
void glCompDrawRectPrism(glCompPoint *p, float w, float h, float b, float d,
                         glCompColor *c, bool bumped);
void glCompSetColor(glCompColor c);

float distBetweenPts(glCompPoint A, glCompPoint B, float R);
    extern int is_point_in_rectangle(float X, float Y, float RX, float RY, float RW,float RH);

#ifdef __cplusplus
}
#endif
