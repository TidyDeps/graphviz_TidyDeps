/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/alloc.h>
#include <util/unreachable.h>
#include "tcldot.h"

// Agiddisc functions
static void *myiddisc_open(Agraph_t *g, Agdisc_t *disc) {
    ictx_t *const ictx = (ictx_t *)((char *)disc - offsetof(ictx_t, mydisc));
    gctx_t *const gctx = gv_alloc(sizeof(gctx_t));
    gctx->g = g;
    gctx->ictx = ictx;
    return gctx;
}
static long myiddisc_map(void *state, int objtype, char *str, uint64_t *id, int createflag) {
    (void)objtype;

    gctx_t *gctx = state;
    ictx_t *ictx = gctx->ictx;
    char *s;

    if (str) {
        if (createflag)
            s = agstrdup(gctx->g, str);
        else
            s = agstrbind(gctx->g, str);
        *id = (uint64_t)(uintptr_t)s;
    } else {
        *id = ictx->ctr;  /* counter maintained in per-interp space, so that
		ids are unique across all graphs in the interp */
        ictx->ctr += 2;
    }
    return 1;
}

static void myiddisc_free(void *state, int objtype, uint64_t id) {
    (void)objtype;

    gctx_t *gctx = state;

/* FIXME no obj* available
    ictx_t *ictx = gctx->ictx;
    char buf[32] = "";

    switch (objtype) {
        case AGRAPH: sprintf(buf,"graph%lu",id); break;
        case AGNODE: sprintf(buf,"node%lu",id); break;
        case AGINEDGE:
        case AGOUTEDGE: sprintf(buf,"edge%lu",id); break;
    }
    Tcl_DeleteCommand(ictx->interp, buf);
*/
    if (id % 2 == 0)
        agstrfree(gctx->g, (char *)(uintptr_t)id, false);
}
static char *myiddisc_print(void *state, int objtype, uint64_t id) {
    (void)state;
    (void)objtype;
    if (id % 2 == 0)
        return (char *)(uintptr_t)id;
    else
        return "";
}

static void myiddisc_idregister(void *state, int objtype, void *obj) {
    gctx_t *gctx = state;
    ictx_t *ictx = gctx->ictx;
    Tcl_Interp *interp = ictx->interp;
    Tcl_CmdProc *proc = NULL;

    switch (objtype) {
        case AGRAPH: proc=graphcmd; break;
        case AGNODE: proc=nodecmd; break;
        case AGINEDGE:
        case AGOUTEDGE: proc=edgecmd; break;
        default: UNREACHABLE();
    }
    Tcl_CreateCommand(interp, obj2cmd(obj), proc, (ClientData) gctx, NULL);
}
Agiddisc_t myiddisc = {
    myiddisc_open,
    myiddisc_map,
    myiddisc_free,
    myiddisc_print,
    free,
    myiddisc_idregister
};
