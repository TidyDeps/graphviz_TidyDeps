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

#include <glcomp/glcompdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*events*/
    extern void glCompMouseInit(glCompMouse * m);
void glCompMouseDown(glCompObj *obj, float x, float y, glMouseButtonType t);
void glCompMouseOver(glCompObj *obj, float x, float y);
void glCompMouseUp(glCompObj *obj, float x, float y, glMouseButtonType t);

#ifdef __cplusplus
}
#endif
