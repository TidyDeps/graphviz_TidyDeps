/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "smyrnadefs.h"
#include "gvprpipe.h"
#include <common/const.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include "draw.h"
#include "gui/gui.h"
#include "gui/topviewsettings.h"
#include <util/agxbuf.h>
#include <viewport.h>

#include <gvpr/gvpr.h>

static ssize_t outfn(void *sp, const char *buf, size_t nbyte, void *dp)
{
    (void)sp;
    (void)dp;

    append_textview((GtkTextView *)
		    glade_xml_get_widget(xml, "gvprtextoutput"), buf,
		    nbyte);
    append_textview((GtkTextView *)
		    glade_xml_get_widget(xml, "mainconsole"), buf, nbyte);
    return (ssize_t)nbyte;
}

int run_gvpr(Agraph_t * srcGraph, size_t argc, char *argv[]) {
    int rv = 1;
    gvpropts opts = {0};
    Agraph_t *gs[2];
    static int count;
    agxbuf buf = {0};

    gs[0] = srcGraph;
    gs[1] = 0;
    opts.ingraphs = gs;
    opts.out = outfn;
    opts.err = outfn;
    opts.flags = GV_USE_OUTGRAPH;

    assert(argc <= INT_MAX);
    rv = gvpr((int)argc, argv, &opts);

    if (rv) {			/* error */
	fprintf(stderr, "Error in gvpr\n");
    } else if (opts.n_outgraphs) 
    {
	refreshViewport();
	agxbprint(&buf, "<%d>", ++count);
	if (opts.outgraphs[0] != view->g[view->activeGraph])
	    add_graph_to_viewport(opts.outgraphs[0], agxbuse(&buf));
	if (opts.n_outgraphs > 1)
	    fprintf(stderr, "Warning: multiple output graphs-discarded\n");
	for (size_t i = 1; i < opts.n_outgraphs; i++) {
	    agclose(opts.outgraphs[i]);
	}
    } else 
    { 
	updateRecord (srcGraph);
        update_graph_from_settings(srcGraph);
    }
    agxbfree(&buf);
    return rv;
}
