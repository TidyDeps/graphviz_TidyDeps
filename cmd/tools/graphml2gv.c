/**
 * @file
 * @brief <a href=https://en.wikipedia.org/wiki/GraphML>GRAPHML</a>-DOT converter
 */

/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


#include    "convert.h"
#include    <assert.h>
#include    <getopt.h>
#include    <limits.h>
#include    <stdbool.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    "openFile.h"
#include    <util/agxbuf.h>
#include    <util/alloc.h>
#include    <util/exit.h>
#include    <util/gv_ctype.h>
#include    <util/list.h>
#include    <util/unreachable.h>
#ifdef HAVE_EXPAT
#include    <expat.h>

#ifndef XML_STATUS_ERROR
#define XML_STATUS_ERROR 0
#endif

#define NAMEBUF		100

#define GRAPHML_ID      "_graphml_id"

#define	TAG_NONE	-1
#define	TAG_GRAPH	0
#define TAG_NODE	1
#define TAG_EDGE	2

static FILE *outFile;
static char *CmdName;
static char **Files;
static int Verbose;
static char* gname = "";

DEFINE_LIST_WITH_DTOR(strs, char *, free)

static void pushString(strs_t *stk, const char *s) {

  // duplicate the string we will push
  char *copy = gv_strdup(s);

  // push this onto the stack
  strs_push_back(stk, copy);
}

static void popString(strs_t *stk) {

  if (strs_is_empty(stk)) {
    fprintf(stderr, "PANIC: graphml2gv: empty element stack\n");
    graphviz_exit(EXIT_FAILURE);
  }

  char *const popped = strs_pop_back(stk);
  free(popped);
}

static char *topString(strs_t *stk) {

  if (strs_is_empty(stk)) {
    fprintf(stderr, "PANIC: graphml2gv: empty element stack\n");
    graphviz_exit(EXIT_FAILURE);
  }

  return *strs_back(stk);
}

static void freeString(strs_t *stk) {
  strs_free(stk);
}

typedef struct {
    char* gname;
    strs_t elements;
    int closedElementType;
    bool edgeinverted;
} userdata_t;

static Agraph_t *root;		/* root graph */
static Agraph_t *G;		/* Current graph */
static Agedge_t *E; // current edge

DEFINE_LIST(graph_stack, Agraph_t *)
static graph_stack_t Gstack;

static userdata_t genUserdata(char *dfltname) {
  userdata_t user = {0};
  user.elements = (strs_t){0};
  user.closedElementType = TAG_NONE;
  user.edgeinverted = false;
  user.gname = dfltname;
  return user;
}

static void freeUserdata(userdata_t ud) {
  freeString(&ud.elements);
}

static int isAnonGraph(const char *name) {
    if (*name++ != '%')
	return 0;
    while (gv_isdigit(*name))
	name++;			/* skip over digits */
    return (*name == '\0');
}

static void push_subg(Agraph_t * g)
{
  // save the root if this is the first graph
  if (graph_stack_is_empty(&Gstack)) {
    root = g;
  }

  // insert the new graph
  graph_stack_push_back(&Gstack, g);

  // update the top graph
  G = g;
}

static Agraph_t *pop_subg(void)
{
  if (graph_stack_is_empty(&Gstack)) {
    fprintf(stderr, "graphml2gv: Gstack underflow in graph parser\n");
    graphviz_exit(EXIT_FAILURE);
  }

  // pop the top graph
  Agraph_t *g = graph_stack_pop_back(&Gstack);

  // update the top graph
  if (!graph_stack_is_empty(&Gstack)) {
    G = *graph_stack_back(&Gstack);
  }

  return g;
}

static Agnode_t *bind_node(const char *name)
{
  return agnode(G, (char *)name, 1);
}

static Agedge_t *bind_edge(const char *tail, const char *head)
{
    Agnode_t *tailNode, *headNode;
    char *key = 0;

    tailNode = agnode(G, (char *) tail, 1);
    headNode = agnode(G, (char *) head, 1);
    E = agedge(G, tailNode, headNode, key, 1);
    return E;
}

static int get_xml_attr(char *attrname, const char **atts)
{
    int count = 0;
    while (atts[count] != NULL) {
	if (strcmp(attrname, atts[count]) == 0) {
	    return count + 1;
	}
	count += 2;
    }
    return -1;
}

static char *defval = "";

static void setEdgeAttr(Agedge_t *ep, char *name, const char *value,
                        userdata_t *ud) {
    if (strcmp(name, "headport") == 0) {
	char *const attrname = ud->edgeinverted ? "tailport" : "headport";
	Agsym_t *ap = agattr_text(root, AGEDGE, attrname, 0);
	if (!ap)
	    ap = agattr_text(root, AGEDGE, attrname, defval);
	agxset(ep, ap, value);
    } else if (strcmp(name, "tailport") == 0) {
	char *const attrname = ud->edgeinverted ? "headport" : "tailport";
	Agsym_t *ap = agattr_text(root, AGEDGE, attrname, 0);
	if (!ap)
	    ap = agattr_text(root, AGEDGE, attrname, defval);
	agxset(ep, ap, value);
    } else {
	Agsym_t *ap = agattr_text(root, AGEDGE, name, 0);
	if (!ap)
	    ap = agattr_text(root, AGEDGE, name, defval);
	agxset(ep, ap, value);
    }
}

/*------------- expat handlers ----------------------------------*/

static void
startElementHandler(void *userData, const char *name, const char **atts)
{
    int pos;
    userdata_t *ud = userData;
    Agraph_t *g = NULL;

    if (strcmp(name, "graphml") == 0) {
	/* do nothing */
    } else if (strcmp(name, "graph") == 0) {
	const char *edgeMode = "";
	const char *id;
	Agdesc_t dir;
	char buf[NAMEBUF];	/* holds % + number */

	if (ud->closedElementType == TAG_GRAPH) {
	    fprintf(stderr,
		    "Warning: Node contains more than one graph.\n");
	}
	pos = get_xml_attr("id", atts);
	if (pos > 0) {
	    id = atts[pos];
	}
	else
	    id = ud->gname;
	pos = get_xml_attr("edgedefault", atts);
	if (pos > 0) {
	    edgeMode = atts[pos];
	}

	if (graph_stack_is_empty(&Gstack)) {
	    if (strcmp(edgeMode, "directed") == 0) {
		dir = Agdirected;
	    } else if (strcmp(edgeMode, "undirected") == 0) {
		dir = Agundirected;
	    } else {
		if (Verbose) {
		    fprintf(stderr,
			"Warning: graph has no edgedefault attribute - assume directed\n");
		}
		dir = Agdirected;
	    }
	    g = agopen((char *) id, dir, &AgDefaultDisc);
	    push_subg(g);
	} else {
	    Agraph_t *subg;
	    if (isAnonGraph(id)) {
		static int anon_id = 1;
		snprintf(buf, sizeof(buf), "%%%d", anon_id++);
		id = buf;
	    }
	    subg = agsubg(G, (char *) id, 1);
	    push_subg(subg);
	}

	pushString(&ud->elements, id);
    } else if (strcmp(name, "node") == 0) {
	pos = get_xml_attr("id", atts);
	if (pos > 0) {
	    const char *attrname;
	    attrname = atts[pos];
            if (G == 0)
                fprintf(stderr,"node %s outside graph, ignored\n",attrname);
	    else
                bind_node(attrname);

	    pushString(&ud->elements, attrname);
	}

    } else if (strcmp(name, "edge") == 0) {
	const char *head = "", *tail = "";
	char *tname;
	Agnode_t *t;

	pos = get_xml_attr("source", atts);
	if (pos > 0)
	    tail = atts[pos];
	pos = get_xml_attr("target", atts);
	if (pos > 0)
	    head = atts[pos];

        if (G == 0)
            fprintf(stderr,"edge source %s target %s outside graph, ignored\n",tail,head);
        else {
            bind_edge(tail, head);

            t = AGTAIL(E);
	    tname = agnameof(t);

	    if (strcmp(tname, tail) == 0) {
	        ud->edgeinverted = false;
	    } else if (strcmp(tname, head) == 0) {
	        ud->edgeinverted = true;
	    }

	    pos = get_xml_attr("id", atts);
	    if (pos > 0) {
	        setEdgeAttr(E, GRAPHML_ID, atts[pos], ud);
	    }
        }
    } else {
	/* must be some extension */
	fprintf(stderr,
		"Unknown node %s - ignoring.\n",
		name);
    }
}

static void endElementHandler(void *userData, const char *name)
{
    userdata_t *ud = userData;

    if (strcmp(name, "graph") == 0) {
	pop_subg();
	popString(&ud->elements);
	ud->closedElementType = TAG_GRAPH;
    } else if (strcmp(name, "node") == 0) {
	char *ele_name = topString(&ud->elements);
	if (ud->closedElementType == TAG_GRAPH) {
	    Agnode_t *node = agnode(root, ele_name, 0);
	    if (node) agdelete(root, node);
	}
	popString(&ud->elements);
	ud->closedElementType = TAG_NODE;
    } else if (strcmp(name, "edge") == 0) {
	E = 0;
	ud->closedElementType = TAG_EDGE;
	ud->edgeinverted = false;
    }
}

static Agraph_t *graphml_to_gv(char *graphname, FILE *graphmlFile, int *rv) {
    char buf[BUFSIZ];
    int done;
    userdata_t udata = genUserdata(graphname);
    XML_Parser parser = XML_ParserCreate(NULL);

    *rv = 0;
    XML_SetUserData(parser, &udata);
    XML_SetElementHandler(parser, startElementHandler, endElementHandler);

    root = 0;
    do {
	size_t len = fread(buf, 1, sizeof(buf), graphmlFile);
	if (len == 0)
	    break;
	done = len < sizeof(buf);
	assert(len <= INT_MAX);
	if (XML_Parse(parser, buf, (int)len, done) == XML_STATUS_ERROR) {
	    fprintf(stderr,
		    "%s at line %lu\n",
		    XML_ErrorString(XML_GetErrorCode(parser)),
		    XML_GetCurrentLineNumber(parser));
	    *rv = 1;
	    break;
	}
    } while (!done);
    XML_ParserFree(parser);
    freeUserdata(udata);

    return root;
}

static FILE *getFile(void)
{
    FILE *rv = NULL;
    static FILE *savef = NULL;
    static int cnt = 0;

    if (Files == NULL) {
	if (cnt++ == 0) {
	    rv = stdin;
	}
    } else {
	if (savef)
	    fclose(savef);
	while (Files[cnt]) {
	    if ((rv = fopen(Files[cnt++], "r")) != 0)
		break;
	    else
		fprintf(stderr, "Can't open %s\n", Files[cnt - 1]);
	}
    }
    savef = rv;
    return rv;
}

static const char *use = "Usage: %s [-gd?] [-o<file>] [<graphs>]\n\
 -g<name>  : use <name> as template for graph names\n\
 -o<file>  : output to <file> (stdout)\n\
 -v        : verbose mode\n\
 -?        : usage\n";

static void usage(int v)
{
    fprintf(stderr, use, CmdName);
    graphviz_exit(v);
}

static char *cmdName(char *path)
{
    char *sp;

    sp = strrchr(path, '/');
    if (sp)
	sp++;
    else
	sp = path;
    return sp;
}

static void initargs(int argc, char **argv)
{
    int c;

    CmdName = cmdName(argv[0]);
    opterr = 0;
    while ((c = getopt(argc, argv, ":vg:o:")) != -1) {
	switch (c) {
	case 'g':
	    gname = optarg;
	    break;
	case 'v':
	    Verbose = 1;
	    break;
	case 'o':
	    if (outFile != NULL)
		fclose(outFile);
	    outFile = openFile(CmdName, optarg, "w");
	    break;
	case ':':
	    fprintf(stderr, "%s: option -%c missing argument\n", CmdName, optopt);
	    usage(1);
	    break;
	case '?':
	    if (optopt == '?')
		usage(0);
	    else {
		fprintf(stderr, "%s: option -%c unrecognized\n", CmdName,
			optopt);
		usage(1);
	    }
	    break;
	default:
	    UNREACHABLE();
	}
    }

    argv += optind;
    argc -= optind;

    if (argc)
	Files = argv;
    if (!outFile)
	outFile = stdout;
}

static char *nameOf(agxbuf *buf, char *name, int cnt) {
    if (*name == '\0')
	return name;
    if (cnt) {
	agxbprint(buf, "%s%d", name, cnt);
	return agxbuse(buf);
    }
    else
	return name;
}

#endif

int main(int argc, char **argv)
{
    Agraph_t *graph;
    Agraph_t *prev = 0;
    FILE *inFile;
    int rv = 0, gcnt = 0;

#ifdef HAVE_EXPAT
    agxbuf buf = {0};
    initargs(argc, argv);
    while ((inFile = getFile())) {
	while ((graph = graphml_to_gv(nameOf(&buf, gname, gcnt), inFile, &rv))) {
	    gcnt++;
	    if (prev)
		agclose(prev);
	    prev = graph;
	    if (Verbose) 
		fprintf (stderr, "%s: %d nodes %d edges\n",
		    agnameof(graph), agnnodes(graph), agnedges(graph));
	    agwrite(graph, outFile);
	    fflush(outFile);
	}
    }

    graph_stack_free(&Gstack);

    agxbfree(&buf);
    graphviz_exit(rv);
#else
    fputs("graphml2gv: not configured for conversion from GXL to GV\n", stderr);
    graphviz_exit(1);
#endif
}
