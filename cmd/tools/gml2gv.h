/**
 * @file
 * @brief <a href=https://en.wikipedia.org/wiki/Graph_Modelling_Language>GML</a>-DOT converter
 */

#include <stdio.h>
#include <cgraph/cgraph.h>
#include <cgraph/list.h>

typedef struct {
    unsigned short kind;
    unsigned short sort;
    char* name;
    union {
	char* value;
	void *lp; ///< actually an `attrs_t *`
    }u;
} gmlattr;

void free_attr(gmlattr *p);

DEFINE_LIST_WITH_DTOR(attrs, gmlattr *, free_attr)

typedef struct {
    char* id;
    attrs_t attrlist;  
} gmlnode;

void free_node(gmlnode *p);

DEFINE_LIST_WITH_DTOR(nodes, gmlnode *, free_node)

typedef struct {
    Dtlink_t link;
    char* source;
    char* target;
    attrs_t attrlist;  
} gmledge;

typedef struct gmlgraph {
    Dtlink_t link;
    struct gmlgraph* parent;
    int directed;
    attrs_t attrlist;  
    nodes_t nodelist;
    Dt_t* edgelist;  
    Dt_t* graphlist;  
} gmlgraph;

extern int gmllex(void);
extern void gmllexeof(void);
extern void gmlerror(const char *);
extern int gmlerrors(void);
extern void initgmlscan (FILE*);
extern Agraph_t* gml_to_gv (char*, FILE*, int, int*);
