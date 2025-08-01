/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/*
 *  Compile-time and run-time interface between gvpr and libexpr
 */

#include "config.h"
#include <assert.h>
#include <ast/error.h>
#include <cgraph/cgraph.h>
#include <gvpr/actions.h>
#include <gvpr/compile.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util/agxbuf.h>
#include <util/alloc.h>
#include <util/prisize_t.h>
#include <util/startswith.h>
#include <util/unreachable.h>

static int isedge(Agobj_t *obj) {
  return AGTYPE(obj) == AGOUTEDGE || AGTYPE(obj) == AGINEDGE;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#include <gvpr/gdefs.h>

#include <ctype.h>
#include <gvpr/trie.c>

static void *int2ptr(long long i) { return (void *)(intptr_t)i; }

static long long ptr2int(const void *p) { return (long long)(intptr_t)p; }

/* Return name of object.
 * Assumes obj !=  NULL
 */
static char *nameOf(Expr_t *ex, Agobj_t *obj, agxbuf *tmps) {
  char *s;
  char *key;
  Agedge_t *e;

  switch (AGTYPE(obj)) {
  case AGNODE:
  case AGRAPH:
    s = agnameof(obj);
    break;
  default: /* edge */
    e = (Agedge_t *)obj;
    key = agnameof(AGMKOUT(e));
    agxbput(tmps, agnameof(AGTAIL(e)));
    if (agisdirected(agraphof(e)))
      agxbput(tmps, "->");
    else
      agxbput(tmps, "--");
    agxbput(tmps, agnameof(AGHEAD(e)));
    if (key && *key) {
      agxbputc(tmps, '[');
      agxbput(tmps, key);
      agxbputc(tmps, ']');
    }
    s = exstring(ex, agxbuse(tmps));
    break;
  }
  return s;
}

/* If string as form "x,y,u,v" where is all are numeric,
 * return "x,y" or "u,v", depending on getll, else return ""
 */
static char *bbOf(Expr_t *pgm, char *pt, bool getll) {
  double x, y, u, v;

  if (sscanf(pt, "%lf,%lf,%lf,%lf", &x, &y, &u, &v) == 4) {
    char *p = strchr(pt, ',');
    p = strchr(p + 1, ',');
    if (getll) {
      size_t len = (size_t)(p - pt);
      char *const s = exstralloc(pgm, len + 1);
      strncpy(s, pt, len);
      s[len] = '\0';
      return s;
    }
    return exstring(pgm, p + 1);
  }
  return "";
}

/* If string as form "x,y" where is x and y are numeric,
 * return "x" or "y", depending on getx, else return ""
 */
static char *xyOf(Expr_t *pgm, char *pt, bool getx) {
  double x, y;

  if (sscanf(pt, "%lf,%lf", &x, &y) == 2) {
    char *const p = strchr(pt, ',');
    if (getx) {
      size_t len = (size_t)(p - pt);
      char *const v = exstralloc(pgm, len + 1);
      strncpy(v, pt, len);
      v[len] = '\0';
      return v;
    }
    return exstring(pgm, p + 1);
  }
  return "";
}

/* Get pos data from node; store x or y into v if successful and return  0;
 * else return -1
 */
static int posOf(Agnode_t *np, int idx, double *v) {
  static Agraph_t *root;
  static Agsym_t *pos;
  Agraph_t *nroot = agroot(np);
  char *ps;
  double p[2];

  if (root != nroot) {
    root = nroot;
    pos = agattr_text(root, AGNODE, "pos", 0);
  }
  if (!pos)
    return -1;
  ps = agxget(np, pos);
  if (sscanf(ps, "%lf,%lf", &p[0], &p[1]) == 2) {
    *v = p[idx];
    return 0;
  } else
    return -1;
}

/* Convert string argument to graph to type of graph desired.
 *   u => undirected
 *   d => directed
 *   s => strict
 *   n => non-strict
 * Case-insensitive
 * By default, the graph is directed, non-strict.
 */
static Agdesc_t xargs(char *args) {
  Agdesc_t desc = Agdirected;
  char c;

  while ((c = *args++)) {
    switch (c) {
    case 'u':
    case 'U':
      desc.directed = false;
      break;
    case 'd':
    case 'D':
      desc.directed = true;
      break;
    case 's':
    case 'S':
      desc.strict = true;
      break;
    case 'n':
    case 'N':
      desc.directed = false;
      break;
    default:
      error(ERROR_WARNING, "unknown graph descriptor '%c' : ignored", c);
      break;
    }
  }
  return desc;
}

/* Recreate string representation of expression involving
 * a reference and a symbol.
 */
static char *deparse(Expr_t *ex, Exnode_t *n, agxbuf *xb) {
  exdump(ex, n, xb);
  return agxbuse(xb);
}

/* Evaluate reference to derive desired graph object.
 * A reference is either DI* or II*
 * The parameter objp is the current object.
 * Assume ref is type-correct.
 */
static Agobj_t *deref(Expr_t *pgm, Exnode_t *x, Exref_t *ref, Agobj_t *objp,
                      Gpr_t *state) {
  void *ptr;

  if (ref == 0)
    return objp;
  else if (ref->symbol->lex == DYNAMIC) {
    ptr = int2ptr(
        x->data.variable.dyna->data.variable.dyna->data.constant.value.integer);
    if (!ptr) {
      agxbuf xb = {0};
      exerror("null reference %s in expression %s.%s", ref->symbol->name,
              ref->symbol->name, deparse(pgm, x, &xb));
      agxbfree(&xb);
      return ptr;
    } else
      return deref(pgm, x, ref->next, (Agobj_t *)ptr, state);
  } else
    switch (ref->symbol->index) { /* sym->lex == ID */
    case V_outgraph:
      return deref(pgm, x, ref->next, (Agobj_t *)state->outgraph, state);
    case V_this:
      return deref(pgm, x, ref->next, state->curobj, state);
    case V_thisg:
      return deref(pgm, x, ref->next, (Agobj_t *)state->curgraph, state);
    case V_nextg:
      return deref(pgm, x, ref->next, (Agobj_t *)state->nextgraph, state);
    case V_targt:
      return deref(pgm, x, ref->next, (Agobj_t *)state->target, state);
    case V_travedge:
      return deref(pgm, x, ref->next, (Agobj_t *)state->tvedge, state);
    case V_travroot:
      return deref(pgm, x, ref->next, (Agobj_t *)state->tvroot, state);
    case V_travnext:
      return deref(pgm, x, ref->next, (Agobj_t *)state->tvnext, state);
    case M_head:
      if (!objp && !(objp = state->curobj)) {
        exerror("Current object $ not defined");
        return 0;
      }
      if (isedge(objp))
        return deref(pgm, x, ref->next, (Agobj_t *)AGHEAD((Agedge_t *)objp),
                     state);
      else
        exerror("head of non-edge");
      break;
    case M_tail:
      if (!objp && !(objp = state->curobj)) {
        exerror("Current object $ not defined");
        return 0;
      }
      if (isedge(objp))
        return deref(pgm, x, ref->next, (Agobj_t *)AGTAIL((Agedge_t *)objp),
                     state);
      else
        exerror("tail of non-edge %p", objp);
      break;
    default:
      exerror("%s : illegal reference", ref->symbol->name);
      break;
    }
  return 0;
}

/* Check that attribute is not a read-only, pseudo-attribute.
 * fatal if not OK.
 */
static void assignable(Agobj_t *objp, unsigned char *name) {
  unsigned int ch;
  int rv;
  unsigned char *p = name;

  TFA_Init();
  while (TFA_State >= 0 && (ch = *p)) {
    TFA_Advance(ch > 127 ? 127 : (char)ch);
    p++;
  }
  rv = TFA_Definition();
  if (rv < 0)
    return;

  switch (AGTYPE(objp)) {
  case AGRAPH:
    if (rv & Y(G))
      exerror("Cannot assign to pseudo-graph attribute %s", name);
    break;
  case AGNODE:
    if (rv & Y(V))
      exerror("Cannot assign to pseudo-node attribute %s", name);
    break;
  default: /* edge */
    if (rv & Y(E))
      exerror("Cannot assign to pseudo-edge attribute %s", name);
    break;
  }
}

/* Set object's attribute name to val.
 * Initialize attribute if necessary.
 */
static int setattr(Agobj_t *objp, char *name, char *val) {
  Agsym_t *gsym = agattrsym(objp, name);
  if (!gsym) {
    gsym = agattr_text(agroot(agraphof(objp)), AGTYPE(objp), name, "");
  }
  return agxset(objp, gsym, val);
}

static char *kindToStr(int kind) {
  char *s;

  switch (kind) {
  case AGRAPH:
    s = "graph";
    break;
  case AGNODE:
    s = "node";
    break;
  default:
    s = "edge";
    break;
  }
  return s;
}

// return string rep of object’s kind
static char *kindOf(Agobj_t *objp) { return kindToStr(agobjkind(objp)); }

/* Apply symbol to get field value of objp
 * Assume objp != NULL
 */
static int lookup(Expr_t *pgm, Agobj_t *objp, Exid_t *sym, Extype_t *v) {
  if (sym->lex == ID) {
    switch (sym->index) {
    case M_head:
      if (isedge(objp))
        v->integer = ptr2int(AGHEAD((Agedge_t *)objp));
      else {
        error(ERROR_WARNING, "head of non-edge");
        return -1;
      }
      break;
    case M_tail:
      if (isedge(objp))
        v->integer = ptr2int(AGTAIL((Agedge_t *)objp));
      else {
        error(ERROR_WARNING, "tail of non-edge");
        return -1;
      }
      break;
    case M_name: {
      agxbuf tmp = {0};
      v->string = nameOf(pgm, objp, &tmp);
      agxbfree(&tmp);
      break;
    }
    case M_indegree:
      if (AGTYPE(objp) == AGNODE)
        v->integer = agdegree(agroot(objp), (Agnode_t *)objp, 1, 0);
      else {
        exerror("indegree of non-node");
        return -1;
      }
      break;
    case M_outdegree:
      if (AGTYPE(objp) == AGNODE)
        v->integer = agdegree(agroot(objp), (Agnode_t *)objp, 0, 1);
      else {
        exerror("outdegree of non-node");
        return -1;
      }
      break;
    case M_degree:
      if (AGTYPE(objp) == AGNODE)
        v->integer = agdegree(agroot(objp), (Agnode_t *)objp, 1, 1);
      else {
        exerror("degree of non-node");
        return -1;
      }
      break;
    case M_X:
      if (AGTYPE(objp) == AGNODE) {
        if (posOf((Agnode_t *)objp, 0, &v->floating))
          exerror("no x coordinate for node \"%s\"", agnameof(objp));
      } else {
        exerror("x coordinate of non-node");
        return -1;
      }
      break;
    case M_Y:
      if (AGTYPE(objp) == AGNODE) {
        if (posOf((Agnode_t *)objp, 1, &v->floating))
          exerror("no y coordinate for node \"%s\"", agnameof(objp));
      } else {
        exerror("x coordinate of non-node");
        return -1;
      }
      break;
    case M_parent:
      if (AGTYPE(objp) == AGRAPH)
        v->integer = ptr2int(agparent((Agraph_t *)objp));
      else {
        exerror("parent of non-graph");
        return -1;
      }
      break;
    case M_root:
      v->integer = ptr2int(agroot(agraphof(objp)));
      break;
    case M_n_edges:
      if (AGTYPE(objp) == AGRAPH)
        v->integer = agnedges((Agraph_t *)objp);
      else {
        exerror("n_edges of non-graph");
        return -1;
      }
      break;
    case M_n_nodes:
      if (AGTYPE(objp) == AGRAPH)
        v->integer = agnnodes((Agraph_t *)objp);
      else {
        exerror("n_nodes of non-graph");
        return -1;
      }
      break;
    case M_directed:
      if (AGTYPE(objp) == AGRAPH)
        v->integer = agisdirected((Agraph_t *)objp);
      else {
        exerror("directed of non-graph");
        return -1;
      }
      break;
    case M_strict:
      if (AGTYPE(objp) == AGRAPH)
        v->integer = agisstrict((Agraph_t *)objp);
      else {
        exerror("strict of non-graph");
        return -1;
      }
      break;
    default:
      error(ERROR_WARNING, "%s : illegal reference", sym->name);
      return -1;
      break;
    }
  } else {
    Agsym_t *gsym = agattrsym(objp, sym->name);
    if (!gsym) {
      gsym = agattr_text(agroot(agraphof(objp)), AGTYPE(objp), sym->name, "");
      agxbuf tmp = {0};
      error(ERROR_WARNING,
            "Using value of uninitialized %s attribute \"%s\" of \"%s\"",
            kindOf(objp), sym->name, nameOf(pgm, objp, &tmp));
      agxbfree(&tmp);
    }
    v->string = agxget(objp, gsym);
  }

  return 0;
}

// return value associated with $n
static char *getArg(int n, Gpr_t *state) {
  if (n >= state->argc) {
    exerror("program references ARGV[%d] - undefined", n);
    return 0;
  }
  return state->argv[n];
}

static int setDfltAttr(Agraph_t *gp, char *k, char *name, char *value) {
  int kind;

  switch (*k) {
  case 'G':
    kind = AGRAPH;
    break;
  case 'E':
    kind = AGEDGE;
    break;
  case 'N':
    kind = AGNODE;
    break;
  default:
    error(ERROR_WARNING, "Unknown kind \"%s\" passed to setDflt()", k);
    return 1;
  }

  // make the implicit default on the root graph explicit in order to avoid the
  // next `agattr_text` thinking its assignment should be hoisted to the root
  {
    Agraph_t *const root = agroot(gp);
    if (agattr_text(root, kind, name, NULL) == NULL) {
      agattr_text(root, kind, name, "");
    }
  }

  agattr_text(gp, kind, name, value);
  return 0;
}

// map string to object kind
static int toKind(char *k, char *fn) {
  switch (*k) {
  case 'G':
    return AGRAPH;
  case 'E':
    return AGEDGE;
  case 'N':
    return AGNODE;
  default:
    exerror("Unknown kind \"%s\" passed to %s()", k, fn);
    break;
  }
  return 0;
}

static char *nxtAttr(Agraph_t *gp, char *k, char *name) {
  char *fn = name ? "nxtAttr" : "fstAttr";
  int kind = toKind(k, fn);
  Agsym_t *sym;

  if (name) {
    sym = agattr_text(gp, kind, name, 0);
    if (!sym) {
      exerror("Third argument \"%s\" in nxtAttr() must be the name of an "
              "existing attribute",
              name);
      return "";
    }

  } else
    sym = NULL;

  sym = agnxtattr(gp, kind, sym);
  if (sym)
    return sym->name;
  else
    return "";
}

static char *getDfltAttr(Agraph_t *gp, char *k, char *name) {
  int kind = toKind(k, "getDflt");
  Agsym_t *sym = agattr_text(gp, kind, name, 0);
  if (!sym) {
    sym = agattr_text(gp, kind, name, "");
    error(ERROR_WARNING, "Uninitialized %s attribute \"%s\" in %s",
          kindToStr(kind), name, "getDflt");
  }
  return sym->defval;
}

// return value associated with gpr identifier
static Extype_t getval(Expr_t *pgm, Exnode_t *node, Exid_t *sym, Exref_t *ref,
                       void *env, int elt, Exdisc_t *disc) {
  Extype_t v;
  Gpr_t *state;
  Extype_t *args;
  Agobj_t *objp;
  Agobj_t *objp1;
  char *key;
  Agraph_t *gp;
  Agnode_t *np;
  Agnode_t *hp;
  Agedge_t *ep;
  gvprbinding *bp;

  assert(sym->lex != CONSTANT);
  if (elt == EX_CALL) {
    args = env;
    state = disc->user;
    switch (sym->index) {
    case F_graph:
      gp = openG(args[0].string, xargs(args[1].string));
      v.integer = ptr2int(gp);
      break;
    case F_subg:
      gp = int2ptr(args[0].integer);
      if (gp) {
        gp = openSubg(gp, args[1].string);
        v.integer = ptr2int(gp);
      } else {
        error(ERROR_WARNING, "NULL graph passed to subg()");
        v.integer = 0;
      }
      break;
    case F_issubg:
      gp = int2ptr(args[0].integer);
      if (gp) {
        v.integer = ptr2int(agsubg(gp, args[1].string, 0));
      } else {
        error(ERROR_WARNING, "NULL graph passed to isSubg()");
        v.integer = 0;
      }
      break;
    case F_fstsubg:
      gp = int2ptr(args[0].integer);
      if (gp) {
        gp = agfstsubg(gp);
        v.integer = ptr2int(gp);
      } else {
        error(ERROR_WARNING, "NULL graph passed to fstsubg()");
        v.integer = 0;
      }
      break;
    case F_nxtsubg:
      gp = int2ptr(args[0].integer);
      if (gp) {
        gp = agnxtsubg(gp);
        v.integer = ptr2int(gp);
      } else {
        error(ERROR_WARNING, "NULL graph passed to nxtsubg()");
        v.integer = 0;
      }
      break;
    case F_node:
      gp = int2ptr(args[0].integer);
      if (gp) {
        np = openNode(gp, args[1].string);
        v.integer = ptr2int(np);
      } else {
        error(ERROR_WARNING, "NULL graph passed to node()");
        v.integer = 0;
      }
      break;
    case F_addnode:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to addNode()");
        v.integer = 0;
      } else if (!np) {
        error(ERROR_WARNING, "NULL node passed to addNode()");
        v.integer = 0;
      } else
        v.integer = ptr2int(addNode(gp, np, 1));
      break;
    case F_fstnode:
      gp = int2ptr(args[0].integer);
      if (gp) {
        np = agfstnode(gp);
        v.integer = ptr2int(np);
      } else {
        error(ERROR_WARNING, "NULL graph passed to fstnode()");
        v.integer = 0;
      }
      break;
    case F_nxtnode:
      np = int2ptr(args[0].integer);
      if (np) {
        np = agnxtnode(agroot(np), np);
        v.integer = ptr2int(np);
      } else {
        error(ERROR_WARNING, "NULL node passed to nxtnode()");
        v.integer = 0;
      }
      break;
    case F_nxtnodesg:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        np = agnxtnode(gp, np);
        v.integer = ptr2int(np);
      } else {
        error(ERROR_WARNING, "NULL node passed to nxtnode_sg()");
        v.integer = 0;
      }
      break;
    case F_isnode:
      gp = int2ptr(args[0].integer);
      if (gp) {
        v.integer = ptr2int(agnode(gp, args[1].string, 0));
      } else {
        error(ERROR_WARNING, "NULL graph passed to isNode()");
        v.integer = 0;
      }
      break;
    case F_issubnode:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        v.integer = ptr2int(addNode(gp, np, 0));
      } else {
        error(ERROR_WARNING, "NULL node passed to isSubnode()");
        v.integer = 0;
      }
      break;
    case F_indegree:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        v.integer = agdegree(gp, np, 1, 0);
      } else {
        error(ERROR_WARNING, "NULL node passed to indegreeOf()");
        v.integer = 0;
      }
      break;
    case F_outdegree:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        v.integer = agdegree(gp, np, 0, 1);
      } else {
        error(ERROR_WARNING, "NULL node passed to outdegreeOf()");
        v.integer = 0;
      }
      break;
    case F_degree:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        v.integer = agdegree(gp, np, 1, 1);
      } else {
        error(ERROR_WARNING, "NULL node passed to degreeOf()");
        v.integer = 0;
      }
      break;
    case F_isin:
      gp = int2ptr(args[0].integer);
      objp = int2ptr(args[1].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to isIn()");
        v.integer = 0;
      } else if (!objp) {
        error(ERROR_WARNING, "NULL object passed to isIn()");
        v.integer = 0;
      } else
        v.integer = agcontains(gp, objp);
      break;
    case F_compof:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to compOf()");
        v.integer = 0;
      } else if (!np) {
        error(ERROR_WARNING, "NULL node passed to compOf()");
        v.integer = 0;
      } else
        v.integer = ptr2int(compOf(gp, np));
      break;
    case F_kindof:
      objp = int2ptr(args[0].integer);
      if (!objp) {
        exerror("NULL object passed to kindOf()");
        v.string = 0;
      } else
        switch (AGTYPE(objp)) {
        case AGRAPH:
          v.string = "G";
          break;
        case AGNODE:
          v.string = "N";
          break;
        case AGINEDGE:
        case AGOUTEDGE:
          v.string = "E";
          break;
        default:
          UNREACHABLE();
        }
      break;
    case F_edge:
      key = args[2].string;
      if (*key == '\0')
        key = 0;
      np = int2ptr(args[0].integer);
      hp = int2ptr(args[1].integer);
      if (!np) {
        error(ERROR_WARNING, "NULL tail node passed to edge()");
        v.integer = 0;
      } else if (!hp) {
        error(ERROR_WARNING, "NULL head node passed to edge()");
        v.integer = 0;
      } else {
        ep = openEdge(0, np, hp, key);
        v.integer = ptr2int(ep);
      }
      break;
    case F_edgesg:
      key = args[3].string;
      if (*key == '\0')
        key = 0;
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      hp = int2ptr(args[2].integer);
      if (!np) {
        error(ERROR_WARNING, "NULL tail node passed to edge_sg()");
        v.integer = 0;
      } else if (!hp) {
        error(ERROR_WARNING, "NULL head node passed to edge_sg()");
        v.integer = 0;
      } else {
        ep = openEdge(gp, np, hp, key);
        v.integer = ptr2int(ep);
      }
      break;
    case F_addedge:
      gp = int2ptr(args[0].integer);
      ep = int2ptr(args[1].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to addEdge()");
        v.integer = 0;
      } else if (!ep) {
        error(ERROR_WARNING, "NULL edge passed to addEdge()");
        v.integer = 0;
      } else
        v.integer = ptr2int(addEdge(gp, ep, 1));
      break;
    case F_opp:
      ep = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!ep) {
        error(ERROR_WARNING, "NULL edge passed to opp()");
        v.integer = 0;
      } else if (!np) {
        error(ERROR_WARNING, "NULL node passed to opp()");
        v.integer = 0;
      } else {
        if (aghead(ep) == np)
          np = agtail(ep);
        else
          np = aghead(ep);
        v.integer = ptr2int(np);
      }
      break;
    case F_isedge:
      key = args[2].string;
      if (*key == '\0')
        key = 0;
      np = int2ptr(args[0].integer);
      hp = int2ptr(args[1].integer);
      if (!np) {
        error(ERROR_WARNING, "NULL tail node passed to isEdge()");
        v.integer = 0;
      } else if (!hp) {
        error(ERROR_WARNING, "NULL head node passed to isEdge()");
        v.integer = 0;
      } else
        v.integer = ptr2int(isEdge(agroot(np), np, hp, key));
      break;
    case F_isedgesg:
      key = args[3].string;
      if (*key == '\0')
        key = 0;
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      hp = int2ptr(args[2].integer);
      if (!gp)
        gp = agroot(np);
      if (!np) {
        error(ERROR_WARNING, "NULL tail node passed to isEdge_sg()");
        v.integer = 0;
      } else if (!hp) {
        error(ERROR_WARNING, "NULL head node passed to isEdge_sg()");
        v.integer = 0;
      } else
        v.integer = ptr2int(isEdge(gp, np, hp, key));
      break;
    case F_issubedge:
      gp = int2ptr(args[0].integer);
      ep = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(ep);
      if (ep) {
        v.integer = ptr2int(addEdge(gp, ep, 0));
      } else {
        error(ERROR_WARNING, "NULL edge passed to isSubedge()");
        v.integer = 0;
      }
      break;
    case F_fstout:
      np = int2ptr(args[0].integer);
      if (np) {
        ep = agfstout(agroot(np), np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstout()");
        v.integer = 0;
      }
      break;
    case F_fstoutsg:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        ep = agfstout(gp, np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstout_sg()");
        v.integer = 0;
      }
      break;
    case F_nxtout:
      ep = int2ptr(args[0].integer);
      if (ep) {
        ep = agnxtout(agroot(ep), ep);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL edge passed to nxtout()");
        v.integer = 0;
      }
      break;
    case F_nxtoutsg:
      gp = int2ptr(args[0].integer);
      ep = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(ep);
      if (ep) {
        ep = agnxtout(gp, ep);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL edge passed to nxtout_sg()");
        v.integer = 0;
      }
      break;
    case F_fstin:
      np = int2ptr(args[0].integer);
      if (np) {
        ep = agfstin(agroot(np), np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstin()");
        v.integer = 0;
      }
      break;
    case F_fstinsg:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        ep = agfstin(gp, np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstin_sg()");
        v.integer = 0;
      }
      break;
    case F_nxtin:
      ep = int2ptr(args[0].integer);
      if (ep) {
        ep = agnxtin(agroot(ep), ep);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL edge passed to nxtin()");
        v.integer = 0;
      }
      break;
    case F_nxtinsg:
      gp = int2ptr(args[0].integer);
      ep = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(ep);
      if (ep) {
        ep = agnxtin(gp, ep);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL edge passed to nxtin_sg()");
        v.integer = 0;
      }
      break;
    case F_fstedge:
      np = int2ptr(args[0].integer);
      if (np) {
        ep = agfstedge(agroot(np), np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstedge()");
        v.integer = 0;
      }
      break;
    case F_fstedgesg:
      gp = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!gp)
        gp = agroot(np);
      if (np) {
        ep = agfstedge(gp, np);
        v.integer = ptr2int(ep);
      } else {
        error(ERROR_WARNING, "NULL node passed to fstedge_sg()");
        v.integer = 0;
      }
      break;
    case F_nxtedge:
      ep = int2ptr(args[0].integer);
      np = int2ptr(args[1].integer);
      if (!ep) {
        error(ERROR_WARNING, "NULL edge passed to nxtedge()");
        v.integer = 0;
      } else if (!np) {
        error(ERROR_WARNING, "NULL node passed to nxtedge()");
        v.integer = 0;
      } else {
        ep = agnxtedge(agroot(np), ep, np);
        v.integer = ptr2int(ep);
      }
      break;
    case F_nxtedgesg:
      gp = int2ptr(args[0].integer);
      ep = int2ptr(args[1].integer);
      np = int2ptr(args[2].integer);
      if (!gp)
        gp = agroot(np);
      if (!ep) {
        error(ERROR_WARNING, "NULL edge passed to nxtedge_sg()");
        v.integer = 0;
      } else if (!np) {
        error(ERROR_WARNING, "NULL node passed to nxtedge_sg()");
        v.integer = 0;
      } else {
        ep = agnxtedge(gp, ep, np);
        v.integer = ptr2int(ep);
      }
      break;
    case F_copy:
      gp = int2ptr(args[0].integer);
      objp = int2ptr(args[1].integer);
      if (!objp) {
        error(ERROR_WARNING, "NULL object passed to clone()");
        v.integer = 0;
      } else
        v.integer = ptr2int(copy(gp, objp));
      break;
    case F_clone:
      gp = int2ptr(args[0].integer);
      objp = int2ptr(args[1].integer);
      if (!objp) {
        error(ERROR_WARNING, "NULL object passed to clone()");
        v.integer = 0;
      } else
        v.integer = ptr2int(cloneO(gp, objp));
      break;
    case F_cloneG:
      gp = int2ptr(args[0].integer);
      if (gp) {
        gp = cloneG(gp, args[1].string);
        v.integer = ptr2int(gp);
      } else {
        error(ERROR_WARNING, "NULL graph passed to cloneG()");
        v.integer = 0;
      }
      break;
    case F_copya:
      objp = int2ptr(args[0].integer);
      objp1 = int2ptr(args[1].integer);
      if (!(objp && objp1)) {
        error(ERROR_WARNING, "NULL object passed to copyA()");
        v.integer = 0;
      } else
        v.integer = copyAttr(objp, objp1);
      break;
    case F_rename:
      objp = int2ptr(args[0].integer);
      if (!objp) {
        error(ERROR_WARNING, "NULL object passed to rename()");
        v.integer = -1;
      } else
        v.integer = agrelabel_node((Agnode_t *)objp, args[1].string);
      break;
    case F_induce:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to induce()");
        v.integer = 1;
      } else {
        (void)graphviz_node_induce(gp, NULL);
        v.integer = 0;
      }
      break;
    case F_write:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to write()");
        v.integer = 1;
      } else
        v.integer = sfioWrite(gp, state->outFile);
      break;
    case F_writeg:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to writeG()");
        v.integer = 1;
      } else
        v.integer = writeFile(gp, args[1].string);
      break;
    case F_readg:
      gp = readFile(args[0].string);
      v.integer = ptr2int(gp);
      break;
    case F_fwriteg:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to fwriteG()");
        v.integer = 1;
      } else
        v.integer = fwriteFile(pgm, gp, args[1].integer);
      break;
    case F_freadg:
      gp = freadFile(pgm, args[0].integer);
      v.integer = ptr2int(gp);
      break;
    case F_openf:
      v.integer = openFile(pgm, args[0].string, args[1].string);
      break;
    case F_closef:
      v.integer = closeFile(pgm, args[0].integer);
      break;
    case F_readl:
      v.string = readLine(pgm, args[0].integer);
      break;
    case F_isdirect:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to isDirect()");
        v.integer = 0;
      } else {
        v.integer = agisdirected(gp);
      }
      break;
    case F_isstrict:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to isStrict()");
        v.integer = 0;
      } else {
        v.integer = agisstrict(gp);
      }
      break;
    case F_delete:
      gp = int2ptr(args[0].integer);
      objp = int2ptr(args[1].integer);
      if (!objp) {
        error(ERROR_WARNING, "NULL object passed to delete()");
        v.integer = 1;
      } else if (objp == (Agobj_t *)state->curgraph) {
        error(ERROR_WARNING, "cannot delete current graph $G");
        v.integer = 1;
      } else if (objp == (Agobj_t *)state->target) {
        error(ERROR_WARNING, "cannot delete target graph $T");
        v.integer = 1;
      } else if (objp == state->curobj) {
        if (!(v.integer = deleteObj(gp, objp)))
          state->curobj = NULL;
      } else
        v.integer = deleteObj(gp, objp);
      break;
    case F_lock:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to lock()");
        v.integer = -1;
      } else {
        const int op = args[1].integer > 0 ? 1 : args[1].integer < 0 ? -1 : 0;
        v.integer = lockGraph(gp, op);
      }
      break;
    case F_nnodes:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to nNodes()");
        v.integer = 0;
      } else {
        v.integer = agnnodes(gp);
      }
      break;
    case F_nedges:
      gp = int2ptr(args[0].integer);
      if (!gp) {
        error(ERROR_WARNING, "NULL graph passed to nEdges()");
        v.integer = 0;
      } else {
        v.integer = agnedges(gp);
      }
      break;
    case F_atoi:
      v.integer = atoi(args[0].string);
      break;
    case F_atof:
      v.floating = atof(args[0].string);
      break;
    case F_sqrt:
      v.floating = sqrt(args[0].floating);
      break;
    case F_cos:
      v.floating = cos(args[0].floating);
      break;
    case F_sin:
      v.floating = sin(args[0].floating);
      break;
    case F_atan2:
      v.floating = atan2(args[0].floating, args[1].floating);
      break;
    case F_exp:
      v.floating = exp(args[0].floating);
      break;
    case F_pow:
      v.floating = pow(args[0].floating, args[1].floating);
      break;
    case F_log:
      v.floating = log(args[0].floating);
      break;
    case F_min:
      v.floating = MIN(args[0].floating, args[1].floating);
      break;
    case F_max:
      v.floating = MAX(args[0].floating, args[1].floating);
      break;
    case F_sys:
      v.integer = system(args[0].string);
      break;
    case F_hasattr:
    case F_get: {
      objp = int2ptr(args[0].integer);
      char *name = args[1].string;
      if (!objp) {
        exerror("NULL object passed to aget()/hasAttr()");
        v.integer = 0;
      } else if (!name) {
        exerror("NULL name passed to aget()/hasAttr()");
        v.integer = 0;
      } else {
        Agsym_t *gsym = agattrsym(objp, name);
        if (sym->index == F_hasattr)
          v.integer = (gsym != NULL);
        else {
          if (!gsym) {
            gsym = agattr_text(agroot(agraphof(objp)), AGTYPE(objp), name, "");
            agxbuf tmp = {0};
            error(ERROR_WARNING,
                  "Using value of %s uninitialized attribute \"%s\" of \"%s\" "
                  "in aget()",
                  kindOf(objp), name, nameOf(pgm, objp, &tmp));
            agxbfree(&tmp);
          }
          v.string = agxget(objp, gsym);
        }
      }
      break;
    }
    case F_set:
      objp = int2ptr(args[0].integer);
      if (!objp) {
        error(ERROR_WARNING, "NULL object passed to aset()");
        v.integer = 1;
      } else {
        char *name = args[1].string;
        char *value = args[2].string;
        if (!name) {
          error(ERROR_WARNING, "NULL name passed to aset()");
          v.integer = 1;
        } else if (!value) {
          error(ERROR_WARNING, "NULL value passed to aset()");
          v.integer = 1;
        } else {
          v.integer = setattr(objp, name, value);
        }
      }
      break;
    case F_dset:
      gp = int2ptr(args[0].integer);
      if (gp) {
        char *kind = args[1].string;
        char *name = args[2].string;
        char *value = args[3].string;
        if (!name) {
          error(ERROR_WARNING, "NULL name passed to setDflt()");
          v.integer = 1;
        } else if (!value) {
          error(ERROR_WARNING, "NULL value passed to setDflt()");
          v.integer = 1;
        } else if (!kind) {
          error(ERROR_WARNING, "NULL kind passed to setDflt()");
          v.integer = 1;
        } else {
          v.integer = setDfltAttr(gp, kind, name, value);
        }
      } else {
        error(ERROR_WARNING, "NULL graph passed to node()");
        v.integer = 0;
      }
      break;
    case F_fstattr:
      gp = int2ptr(args[0].integer);
      if (gp) {
        char *kind = args[1].string;
        if (!kind) {
          error(ERROR_ERROR, "NULL kind passed to fstAttr()");
          v.string = 0;
        } else {
          v.string = nxtAttr(gp, kind, NULL);
        }
      } else {
        exerror("NULL graph passed to fstAttr()");
        v.string = 0;
      }
      break;
    case F_nxtattr:
    case F_isattr:
    case F_dget:
      gp = int2ptr(args[0].integer);
      if (gp) {
        char *kind = args[1].string;
        char *name = args[2].string;
        if (!name) {
          exerror("NULL name passed to %s", sym->name);
          v.string = 0;
        } else if (!kind) {
          exerror("NULL kind passed to %s", sym->name);
          v.string = 0;
        } else if (sym->index == F_isattr) {
          v.integer = agattr_text(gp, toKind(kind, sym->name), name, 0) != NULL;
        } else if (sym->index == F_nxtattr) {
          v.string = nxtAttr(gp, kind, name);
        } else {
          v.string = getDfltAttr(gp, kind, name);
        }
      } else {
        exerror("NULL graph passed to %s", sym->name);
        v.string = 0;
      }
      break;
    case F_canon:
      v.string = canon(pgm, args[0].string);
      break;
    case F_ishtml:
      v.integer = aghtmlstr(args[0].string);
      break;
    case F_html:
      gp = int2ptr(args[0].integer);
      if (gp) {
        v.string = toHtml(gp, args[1].string);
      } else {
        error(ERROR_WARNING, "NULL graph passed to html()");
        v.string = 0;
      }
      break;
    case F_tolower:
      v.string = toLower(pgm, args[0].string);
      break;
    case F_colorx:
      v.string = colorx(pgm, args[0].string, args[1].string);
      break;
    case F_strcmp:
      if (args[0].string) {
        if (args[1].string)
          v.integer = strcmp(args[0].string, args[1].string);
        else
          v.integer = -1;
      } else if (args[1].string)
        v.integer = 1;
      else
        v.integer = 0;
      break;
    case F_toupper:
      v.string = toUpper(pgm, args[0].string);
      break;
    case F_xof:
      v.string = xyOf(pgm, args[0].string, true);
      break;
    case F_yof:
      v.string = xyOf(pgm, args[0].string, false);
      break;
    case F_llof:
      v.string = bbOf(pgm, args[0].string, true);
      break;
    case F_urof:
      v.string = bbOf(pgm, args[0].string, false);
      break;
    case F_length:
      v.integer = strlen(args[0].string);
      break;
    case F_index:
      v.integer = indexOf(args[0].string, args[1].string);
      break;
    case F_rindex:
      v.integer = rindexOf(args[0].string, args[1].string);
      break;
    case F_match: {
      const size_t m = match(args[0].string, args[1].string);
      if (m == SIZE_MAX) {
        v.integer = -1;
      } else {
        v.integer = (long long)m;
      }
      break;
    }
    case F_call:
      if ((bp = findBinding(state, args[0].string)))
        v.integer = (bp->fn)(args[1].string);
      else
        v.integer = -1;
      break;
    default:
      v.integer = -1;
      exerror("unknown function call: %s", sym->name);
    }
    return v;
  } else if (elt == EX_ARRAY) {
    args = env;
    state = disc->user;
    switch (sym->index) {
    case A_ARGV:
      v.string = getArg(args[0].integer, state);
      break;
    default:
      exerror("unknown array name: %s", sym->name);
      v.string = 0;
    }
    return v;
  }

  state = env;
  if (ref) {
    objp = deref(pgm, node, ref, 0, state);
    if (!objp) {
      agxbuf xb = {0};
      exerror("null reference in expression %s", deparse(pgm, node, &xb));
      agxbfree(&xb);
    }
  } else if (sym->lex == ID && sym->index <= LAST_V) {
    switch (sym->index) {
    case V_this:
      v.integer = ptr2int(state->curobj);
      break;
    case V_thisg:
      v.integer = ptr2int(state->curgraph);
      break;
    case V_nextg:
      v.integer = ptr2int(state->nextgraph);
      break;
    case V_targt:
      v.integer = ptr2int(state->target);
      break;
    case V_outgraph:
      v.integer = ptr2int(state->outgraph);
      break;
    case V_tgtname:
      v.string = state->tgtname;
      break;
    case V_infname:
      v.string = state->infname;
      break;
    case V_ARGC:
      v.integer = state->argc;
      break;
    case V_travtype:
      v.integer = state->tvt;
      break;
    case V_travroot:
      v.integer = ptr2int(state->tvroot);
      break;
    case V_travnext:
      v.integer = ptr2int(state->tvnext);
      break;
    case V_travedge:
      v.integer = ptr2int(state->tvedge);
      break;
    }
    return v;
  } else {
    objp = state->curobj;
    if (!objp) {
      agxbuf xb = {0};
      exerror("current object $ not defined as reference for %s",
              deparse(pgm, node, &xb));
      agxbfree(&xb);
    }
  }

  if (objp) {
    if (lookup(pgm, objp, sym, &v)) {
      agxbuf xb = {0};
      exerror("in expression %s", deparse(pgm, node, &xb));
      agxbfree(&xb);
      v.integer = 0;
    }
  } else
    v.integer = 0;

  return v;
}

#define MINTYPE (LAST_M + 1) /* First type occurs after last M_ */

static char *typeName(long op) { return typenames[op - MINTYPE]; }

/* Set sym to value v.
 * Return -1 if not allowed.
 * Assume already type correct.
 */
static int setval(Expr_t *pgm, Exnode_t *x, Exid_t *sym, Exref_t *ref,
                  void *env, Extype_t v) {
  Gpr_t *state;
  Agobj_t *objp;
  Agnode_t *np;
  int rv = 0;

  state = env;
  if (ref) {
    objp = deref(pgm, x, ref, 0, state);
    if (!objp) {
      agxbuf xb = {0};
      exerror("in expression %s.%s", ref->symbol->name, deparse(pgm, x, &xb));
      agxbfree(&xb);
      return -1;
    }
  } else if (MINNAME <= sym->index && sym->index <= MAXNAME) {
    switch (sym->index) {
    case V_outgraph:
      state->outgraph = int2ptr(v.integer);
      break;
    case V_travtype: {
      long long iv = v.integer;
      if (validTVT(v.integer))
        state->tvt = (trav_type)iv;
      else
        error(1, "unexpected value %lld assigned to %s : ignored", iv,
              typeName(T_tvtyp));
      break;
    }
    case V_travroot:
      np = int2ptr(v.integer);
      if (!np || agroot(np) == state->curgraph)
        state->tvroot = np;
      else {
        error(1, "cannot set $tvroot, node %s not in $G : ignored",
              agnameof(np));
      }
      break;
    case V_travnext:
      np = int2ptr(v.integer);
      if (!np || agroot(np) == state->curgraph) {
        state->tvnext = np;
        state->flags |= GV_NEXT_SET;
      } else {
        error(1, "cannot set $tvnext, node %s not in $G : ignored",
              agnameof(np));
      }
      break;
    case V_tgtname:
      free(state->tgtname);
      state->tgtname = strdup(v.string);
      state->name_used = 0;
      break;
    default:
      rv = -1;
      break;
    }
    return rv;
  } else {
    objp = state->curobj;
    if (!objp) {
      agxbuf xb = {0};
      exerror("current object $ undefined in expression %s",
              deparse(pgm, x, &xb));
      agxbfree(&xb);
      return -1;
    }
  }

  assignable(objp, (unsigned char *)sym->name);
  return setattr(objp, sym->name, v.string);
}

/// gvpr’s custom `#` implementation
///
/// The main purpose of this is to extend `#` to work on the command line
/// options array, `ARGV`.
///
/// @param rhs The right-hand side operand to `#`.
/// @param disc The libexpr discipline.
/// @return The “length” as defined by us.
static Extype_t length(Exid_t *rhs, Exdisc_t *disc) {
  Extype_t v = {0};
  switch (rhs->index) {
  case A_ARGV: {
    Gpr_t *const state = disc->user;
    v.integer = state->argc;
    break;
  }
  default:
    exerror("unknown array name: %s", rhs->name);
    break;
  }
  return v;
}

/// gvpr’s custom `in` implementation
///
/// The main purpose of this is to extend `in` to work on the command line
/// options array, `ARGV`.
///
/// @param lhs The left-hand side operand to `in`.
/// @param rhs The right-hand side operand to `in`.
/// @param disc The libexpr discipline.
/// @return Non-zero if `lhs` was found to be within `rhs`.
static int in(Extype_t lhs, Exid_t *rhs, Exdisc_t *disc) {
  switch (rhs->index) {
  case A_ARGV: {
    Gpr_t *const state = disc->user;
    return lhs.integer >= 0 && lhs.integer < state->argc;
  }
  default:
    exerror("unknown array name: %s", rhs->name);
    break;
  }
  return 0;
}

static int codePhase;

#define haveGraph (1 <= codePhase && codePhase <= 4)
#define haveTarget (2 <= codePhase && codePhase <= 4)
#define inWalk (2 <= codePhase && codePhase <= 3)

/* typeChk:
 * Type check input type against implied type of symbol sym.
 * If okay, return result type; else return 0.
 * For functions, input type set must intersect with function domain.
 * This means type errors may occur, but these will be caught at runtime.
 * For non-functions, input type must be 0.
 */
static tctype typeChk(tctype intype, Exid_t *sym) {
  tctype dom = 0, rng = 0;

  switch (sym->lex) {
  case DYNAMIC:
    dom = 0;
    switch (sym->type) {
    case T_obj:
      rng = YALL;
      break;
    case T_node:
      rng = Y(V);
      break;
    case T_graph:
      rng = Y(G);
      break;
    case T_edge:
      rng = Y(E);
      break;
    case INTEGER:
      rng = Y(I);
      break;
    case FLOATING:
      rng = Y(F);
      break;
    case STRING:
      rng = Y(S);
      break;
    default:
      exerror("unknown dynamic type %" PRIdMAX " of symbol %s",
              (intmax_t)sym->type, sym->name);
      break;
    }
    break;
  case ID:
    if (sym->index <= MAXNAME) {
      switch (sym->index) {
      case V_travroot:
      case V_this:
      case V_thisg:
      case V_nextg:
        if (!haveGraph)
          exerror("keyword %s cannot be used in BEGIN/END statements",
                  sym->name);
        break;
      case V_targt:
        if (!haveTarget)
          exerror("keyword %s cannot be used in BEGIN/BEG_G/END statements",
                  sym->name);
        break;
      }
      dom = tchk[sym->index][0];
      rng = tchk[sym->index][1];
    } else {
      dom = YALL;
      rng = Y(S);
    }
    break;
  case NAME:
    if (!intype && !haveGraph)
      exerror("undeclared, unmodified names like \"%s\" cannot be\nused in "
              "BEGIN and END statements",
              sym->name);
    dom = YALL;
    rng = Y(S);
    break;
  default:
    exerror("unexpected symbol in typeChk: name %s, lex %" PRIdMAX, sym->name,
            (intmax_t)sym->lex);
    break;
  }

  if (dom) {
    if (!intype)
      intype = YALL; /* type of $ */
    if (!(dom & intype))
      rng = 0;
  } else if (intype)
    rng = 0;
  return rng;
}

// type check variable expression
static tctype typeChkExp(Exref_t *ref, Exid_t *sym) {
  tctype ty;

  if (ref) {
    ty = typeChk(0, ref->symbol);
    for (ref = ref->next; ty && ref; ref = ref->next)
      ty = typeChk(ty, ref->symbol);
    if (!ty)
      return 0;
  } else
    ty = 0;
  return typeChk(ty, sym);
}

/* Called during compilation for uses of references:   abc.x
 * Also for abc.f(..),  type abc.v, "abc".x and CONSTANTS.
 * The grammar has been  altered to disallow the first 3.
 * Type check expressions; return value unused.
 */
static Extype_t refval(Expr_t *pgm, Exnode_t *node, Exid_t *sym, Exref_t *ref) {

  Extype_t v;
  if (sym->lex == CONSTANT) {
    switch (sym->index) {
    case C_flat:
      v.integer = TV_flat;
      break;
    case C_ne:
      v.integer = TV_ne;
      break;
    case C_en:
      v.integer = TV_en;
      break;
    case C_bfs:
      v.integer = TV_bfs;
      break;
    case C_dfs:
      v.integer = TV_dfs;
      break;
    case C_fwd:
      v.integer = TV_fwd;
      break;
    case C_rev:
      v.integer = TV_rev;
      break;
    case C_postdfs:
      v.integer = TV_postdfs;
      break;
    case C_postfwd:
      v.integer = TV_postfwd;
      break;
    case C_postrev:
      v.integer = TV_postrev;
      break;
    case C_prepostdfs:
      v.integer = TV_prepostdfs;
      break;
    case C_prepostfwd:
      v.integer = TV_prepostfwd;
      break;
    case C_prepostrev:
      v.integer = TV_prepostrev;
      break;
    case C_null:
      v.integer = 0;
      break;
    default:
      v = exzero(node->type);
      break;
    }
  } else {
    if (!typeChkExp(ref, sym)) {
      agxbuf xb = {0};
      exerror("type error using %s", deparse(pgm, node, &xb));
      agxbfree(&xb);
    }
    v = exzero(node->type);
  }
  return v;
}

/* Evaluate (l ex->op r) producing a value of type ex->type,
 * stored in l.
 * May be unary, with r = NULL
 * Return -1 if operation cannot be done, 0 otherwise.
 * If arg is != 0, operation unnecessary; just report possibility.
 */
static int binary(Exnode_t *l, Exnode_t *ex, Exnode_t *r, int arg) {
  Agobj_t *lobjp;
  Agobj_t *robjp;
  int ret = -1;

  if (BUILTIN(l->type))
    return -1;
  if (r && BUILTIN(r->type))
    return -1;
  if (!INTEGRAL(ex->type))
    return -1;

  if (l->type == T_tvtyp) {

    if (!r)
      return -1; /* Assume libexpr handled unary */
    if (r->type != T_tvtyp)
      return -1;

    long long li = l->data.constant.value.integer;
    long long ri = r->data.constant.value.integer;
    switch (ex->op) {
    case EQ:
      if (arg)
        return 0;
      l->data.constant.value.integer = li == ri;
      ret = 0;
      break;
    case NE:
      if (arg)
        return 0;
      l->data.constant.value.integer = li != ri;
      ret = 0;
      break;
    case '<':
      if (arg)
        return 0;
      l->data.constant.value.integer = li < ri;
      ret = 0;
      break;
    case LE:
      if (arg)
        return 0;
      l->data.constant.value.integer = li <= ri;
      ret = 0;
      break;
    case GE:
      if (arg)
        return 0;
      l->data.constant.value.integer = li >= ri;
      ret = 0;
      break;
    case '>':
      if (arg)
        return 0;
      l->data.constant.value.integer = li > ri;
      ret = 0;
      break;
    }
  }

  /* l is a graph object; make sure r is also */
  if (r && r->type == T_tvtyp)
    return -1;

  lobjp = int2ptr(l->data.constant.value.integer);
  if (r)
    robjp = int2ptr(r->data.constant.value.integer);
  else
    robjp = 0;
  switch (ex->op) {
  case EQ:
    if (arg)
      return 0;
    l->data.constant.value.integer = !compare(lobjp, robjp);
    ret = 0;
    break;
  case NE:
    if (arg)
      return 0;
    l->data.constant.value.integer = compare(lobjp, robjp);
    ret = 0;
    break;
  case '<':
    if (arg)
      return 0;
    l->data.constant.value.integer = compare(lobjp, robjp) < 0;
    ret = 0;
    break;
  case LE:
    if (arg)
      return 0;
    l->data.constant.value.integer = compare(lobjp, robjp) <= 0;
    ret = 0;
    break;
  case GE:
    if (arg)
      return 0;
    l->data.constant.value.integer = compare(lobjp, robjp) >= 0;
    ret = 0;
    break;
  case '>':
    if (arg)
      return 0;
    l->data.constant.value.integer = compare(lobjp, robjp) > 0;
    ret = 0;
    break;
  }

  return ret;
}

static int strToTvtype(char *s) {
  int rt = 0;
  char *sfx;

  if (startswith(s, "TV_")) {
    sfx = s + 3;
    if (!strcmp(sfx, "flat")) {
      rt = TV_flat;
    } else if (!strcmp(sfx, "ne")) {
      rt = TV_ne;
    } else if (!strcmp(sfx, "en")) {
      rt = TV_en;
    } else if (!strcmp(sfx, "bfs")) {
      rt = TV_bfs;
    } else if (!strcmp(sfx, "dfs")) {
      rt = TV_dfs;
    } else if (!strcmp(sfx, "fwd")) {
      rt = TV_fwd;
    } else if (!strcmp(sfx, "rev")) {
      rt = TV_rev;
    } else if (!strcmp(sfx, "postdfs")) {
      rt = TV_postdfs;
    } else if (!strcmp(sfx, "postfwd")) {
      rt = TV_postfwd;
    } else if (!strcmp(sfx, "postrev")) {
      rt = TV_postrev;
    } else if (!strcmp(sfx, "prepostdfs")) {
      rt = TV_prepostdfs;
    } else if (!strcmp(sfx, "prepostfwd")) {
      rt = TV_prepostfwd;
    } else if (!strcmp(sfx, "prepostrev")) {
      rt = TV_prepostrev;
    } else
      exerror("illegal string \"%s\" for type tvtype_t", s);
  } else
    exerror("illegal string \"%s\" for type tvtype_t", s);
  return rt;
}

static char *tvtypeToStr(long long v) {
  char *s = 0;

  switch (v) {
  case TV_flat:
    s = "TV_flat";
    break;
  case TV_ne:
    s = "TV_ne";
    break;
  case TV_en:
    s = "TV_en";
    break;
  case TV_bfs:
    s = "TV_bfs";
    break;
  case TV_dfs:
    s = "TV_dfs";
    break;
  case TV_fwd:
    s = "TV_fwd";
    break;
  case TV_rev:
    s = "TV_rev";
    break;
  case TV_postdfs:
    s = "TV_postdfs";
    break;
  case TV_postfwd:
    s = "TV_postfwd";
    break;
  case TV_postrev:
    s = "TV_postrev";
    break;
  case TV_prepostdfs:
    s = "TV_prepostdfs";
    break;
  case TV_prepostfwd:
    s = "TV_prepostfwd";
    break;
  case TV_prepostrev:
    s = "TV_prepostrev";
    break;
  default:
    exerror("Unexpected value %lld for type tvtype_t", v);
    break;
  }
  return s;
}

/* Convert value x to type string.
 * Assume x does not have a built-in type
 * Return -1 if conversion cannot be done, 0 otherwise.
 * If arg is != 0, conversion unnecessary; just report possibility.
 */
static int stringOf(Expr_t *prog, Exnode_t *x, int arg) {
  Agobj_t *objp;
  int rv = 0;

  if (arg)
    return 0;

  if (x->type == T_tvtyp) {
    if (!(x->data.constant.value.string =
              tvtypeToStr(x->data.constant.value.integer)))
      rv = -1;
  } else {
    objp = int2ptr(x->data.constant.value.integer);
    if (!objp) {
      exerror("cannot generate name for NULL %s", typeName(x->type));
      rv = -1;
    } else {
      agxbuf tmp = {0};
      x->data.constant.value.string = nameOf(prog, objp, &tmp);
      agxbfree(&tmp);
    }
  }
  x->type = STRING;
  return rv;
}

/* Convert value x of type x->type to type type.
 * Return -1 if conversion cannot be done, 0 otherwise.
 * If arg is != 0, conversion unnecessary; just report possibility.
 * In particular, assume x != 0 if arg == 0.
 */
static int convert(Exnode_t *x, long type, int arg) {
  Agobj_t *objp;
  int ret = -1;

  /* If both types are built-in, let libexpr handle */
  if (BUILTIN(type) && BUILTIN(x->type))
    return -1;
  if (type == T_obj && x->type <= T_obj)
    ret = 0; /* trivial cast from specific graph object to T_obj */
  else if (type <= T_obj && x->type == INTEGER) {
    if (x->data.constant.value.integer == 0)
      ret = 0; /* allow NULL pointer */
  } else if (type == INTEGER) {
    ret = 0;
  } else if (x->type == T_obj) {
    /* check dynamic type */
    if (arg) {
      if (type != FLOATING && type <= T_obj)
        ret = 0;
    } else {
      objp = int2ptr(x->data.constant.value.integer);
      switch (type) {
      case T_graph:
        if (!objp || AGTYPE(objp) == AGRAPH)
          ret = 0;
        break;
      case T_node:
        if (!objp || AGTYPE(objp) == AGNODE)
          ret = 0;
        break;
      case T_edge:
        if (!objp || isedge(objp))
          ret = 0;
        break;
      }
    }
  } else if (type == STRING) {
    if (x->type == T_tvtyp) {
      ret = 0;
      if (!arg) {
        x->data.constant.value.string =
            tvtypeToStr(x->data.constant.value.integer);
      }
    }
  } else if (type == T_tvtyp && x->type == INTEGER) {
    if (arg)
      ret = 0;
    else if (validTVT(x->data.constant.value.integer))
      ret = 0;
    else
      exerror("Integer value %lld not legal for type tvtype_t",
              x->data.constant.value.integer);
  }
  /* in case libexpr hands us the trivial case */
  else if (x->type == type) {
    ret = 0;
  } else if (x->type == STRING) {
    char *s;
    if (type == T_tvtyp) {
      if (arg)
        ret = 0;
      else {
        ret = 0;
        s = x->data.constant.value.string;
        x->data.constant.value.integer = strToTvtype(s);
      }
    }
  }
  if (!arg && ret == 0)
    x->type = type;
  return ret;
}

/* Calculate unique key for object.
 * We use this to unify local copies of nodes and edges.
 */
static Extype_t keyval(Extype_t v, long type) {
  if (type <= T_obj) {
    v.integer = AGID(int2ptr(v.integer));
  }
  return v;
}

// convert type indices to symbolic name
static int a2t[] = {0,      FLOATING, INTEGER, STRING,
                    T_node, T_edge,   T_graph, T_obj};

// create and initialize expr discipline
static Exdisc_t *initDisc(Gpr_t *state) {
  Exdisc_t *dp = calloc(1, sizeof(Exdisc_t));
  if (!dp) {
    error(ERROR_ERROR, "could not create libexp discipline: out of memory");
    return 0;
  }

  dp->version = EX_VERSION;
  dp->flags = EX_CHARSTRING | EX_UNDECLARED;
  dp->symbols = symbols;
  dp->convertf = convert;
  dp->stringof = stringOf;
  dp->binaryf = binary;
  dp->typename = typeName;
  if (state->errf)
    dp->errorf = state->errf;
  else
    dp->errorf = (Exerror_f)errorf;
  dp->keyf = keyval;
  dp->getf = getval;
  dp->reff = refval;
  dp->setf = setval;
  dp->lengthf = length;
  dp->inf = in;
  dp->exitf = state->exitf;
  dp->types = a2t;
  dp->user = state;

  state->dp = dp; /* dp is freed when state is freed */

  return dp;
}

/* Compile given string, then extract and return
 * typed expression.
 */
static Exnode_t *compile(Expr_t *prog, char *src, char *input, int line,
                         const char *lbl, const char *sfx, int kind) {
  Exnode_t *e = 0;
  int rv;

  /* create input stream */
  FILE *sf = tmpfile();
  assert(sf != NULL);
  if (input) {
    fputs(input, sf);
  }
  if (sfx) {
    fputs(sfx, sf);
  }
  rewind(sf);

  /*  prefixing label if necessary */
  agxbuf label = {0};
  if (lbl) {
    agxbprint(&label, "%s:\n", lbl);
    line--;
  }

  if (!src)
    src = "<command line>";
  rv = excomp(prog, src, line, sf, lbl ? agxbdisown(&label) : NULL);
  fclose(sf);

  if (rv >= 0 && getErrorErrors() == 0)
    e = exexpr(prog, lbl, NULL, kind);

  return e;
}

// check if guard is an assignment and warn
static void checkGuard(Exnode_t *gp, char *src, int line) {
  gp = exnoncast(gp);
  if (gp && exisAssign(gp)) {
    if (src) {
      setErrorFileLine(src, line);
    }
    error(ERROR_WARNING, "assignment used as bool in guard");
  }
}

static case_stmt *mkStmts(Expr_t *prog, char *src, case_infos_t cases,
                          const char *lbl) {
  agxbuf tmp = {0};

  case_stmt *cs = gv_calloc(case_infos_size(&cases), sizeof(case_stmt));

  for (size_t i = 0; i < case_infos_size(&cases); i++) {
    case_info *sp = case_infos_at(&cases, i);
    if (sp->guard) {
      agxbprint(&tmp, "%s_g%" PRISIZE_T, lbl, i);
      cs[i].guard =
          compile(prog, src, sp->guard, sp->gstart, agxbuse(&tmp), 0, INTEGER);
      if (getErrorErrors())
        break;
      checkGuard(cs[i].guard, src, sp->gstart);
    }
    if (sp->action) {
      agxbprint(&tmp, "%s_a%" PRISIZE_T, lbl, i);
      cs[i].action =
          compile(prog, src, sp->action, sp->astart, agxbuse(&tmp), 0, INTEGER);
      if (getErrorErrors())
        break;
      /* If no error but no compiled action, the input action must
       * have been essentially an empty block, which should be
       * considered different from a missing block. So, compile a
       * trivial block.
       */
      if (!cs[i].action) {
        agxbprint(&tmp, "%s__a%" PRISIZE_T, lbl, i);
        cs[i].action =
            compile(prog, src, "1", sp->astart, agxbuse(&tmp), 0, INTEGER);
      }
    }
  }
  agxbfree(&tmp);
  return cs;
}

/// @return True if the block uses the input graph
static bool mkBlock(comp_block *bp, Expr_t *prog, char *src, parse_block *inp,
                    size_t i) {
  bool has_begin_g = false; // does this block use a `BEG_G` statement?

  codePhase = 1;
  if (inp->begg_stmt) {
    static const char PREFIX[] = "_begin_g_";
    agxbuf label = {0};
    agxbprint(&label, "%s%" PRISIZE_T, PREFIX, i);
    symbols[0].type = T_graph;
    tchk[V_this][1] = Y(G);
    bp->begg_stmt = compile(prog, src, inp->begg_stmt, inp->l_beging,
                            agxbuse(&label), 0, VOIDTYPE);
    agxbfree(&label);
    if (getErrorErrors())
      goto finishBlk;
    has_begin_g = true;
  }

  codePhase = 2;
  if (!case_infos_is_empty(&inp->node_stmts)) {
    static const char PREFIX[] = "_nd";
    agxbuf label = {0};
    symbols[0].type = T_node;
    tchk[V_this][1] = Y(V);
    bp->n_nstmts = case_infos_size(&inp->node_stmts);
    agxbprint(&label, "%s%" PRISIZE_T, PREFIX, i);
    bp->node_stmts = mkStmts(prog, src, inp->node_stmts, agxbuse(&label));
    agxbfree(&label);
    if (getErrorErrors())
      goto finishBlk;
    bp->does_walk_graph = true;
  }

  codePhase = 3;
  if (!case_infos_is_empty(&inp->edge_stmts)) {
    static const char PREFIX[] = "_eg";
    agxbuf label = {0};
    symbols[0].type = T_edge;
    tchk[V_this][1] = Y(E);
    bp->n_estmts = case_infos_size(&inp->edge_stmts);
    agxbprint(&label, "%s%" PRISIZE_T, PREFIX, i);
    bp->edge_stmts = mkStmts(prog, src, inp->edge_stmts, agxbuse(&label));
    agxbfree(&label);
    if (getErrorErrors())
      goto finishBlk;
    bp->does_walk_graph = true;
  }

finishBlk:
  if (getErrorErrors()) {
    free(bp->node_stmts);
    free(bp->edge_stmts);
    bp->node_stmts = 0;
    bp->edge_stmts = 0;
  }

  return has_begin_g || bp->does_walk_graph;
}

// convert command line flags to actions in END_G
static const char *doFlags(compflags_t flags) {
  if (flags.srcout) {
    if (flags.induce) {
      return "\n$O = $G;\ninduce($O);\n";
    }
    return "\n$O = $G;\n";
  }
  if (flags.induce) {
    return "\ninduce($O);\n";
  }
  return "\n";
}

// convert gpr sections in libexpr program
comp_prog *compileProg(parse_prog *inp, Gpr_t *state, compflags_t flags) {
  const char *endg_sfx = NULL;
  bool uses_graph = false;

  /* Make sure we have enough bits for types */
  assert(CHAR_BIT * sizeof(tctype) >= (1 << TBITS));

  comp_prog *p = calloc(1, sizeof(comp_prog));
  if (!p) {
    error(ERROR_ERROR, "could not create compiled program: out of memory");
    goto finish;
  }

  if (flags.srcout || flags.induce || flags.clone) {
    endg_sfx = doFlags(flags);
  }

  if (!initDisc(state))
    goto finish;

  exinit();
  if (!(p->prog = exopen(state->dp)))
    goto finish;

  codePhase = 0;
  if (inp->begin_stmt) {
    p->begin_stmt = compile(p->prog, inp->source, inp->begin_stmt, inp->l_begin,
                            0, 0, VOIDTYPE);
    if (getErrorErrors())
      goto finish;
  }

  if (!parse_blocks_is_empty(&inp->blocks)) {
    comp_block *bp;

    p->blocks = bp =
        gv_calloc(parse_blocks_size(&inp->blocks), sizeof(comp_block));

    for (size_t i = 0; i < parse_blocks_size(&inp->blocks); bp++, i++) {
      parse_block *ibp = parse_blocks_at(&inp->blocks, i);
      uses_graph |= mkBlock(bp, p->prog, inp->source, ibp, i);
      if (getErrorErrors())
        goto finish;
      p->n_blocks++;
    }
  }
  p->uses_graph = uses_graph;

  codePhase = 4;
  if (inp->endg_stmt || endg_sfx) {
    symbols[0].type = T_graph;
    tchk[V_this][1] = Y(G);
    p->endg_stmt = compile(p->prog, inp->source, inp->endg_stmt, inp->l_endg,
                           "_end_g", endg_sfx, VOIDTYPE);
    if (getErrorErrors())
      goto finish;
  }

  codePhase = 5;
  if (inp->end_stmt) {
    symbols[0].type = T_obj;
    p->end_stmt = compile(p->prog, inp->source, inp->end_stmt, inp->l_end,
                          "_end_", 0, VOIDTYPE);
    if (getErrorErrors())
      goto finish;
  }
  setErrorLine(0); /* execution errors have no line numbers */

  if (p->end_stmt)
    p->uses_graph = true;

finish:
  if (getErrorErrors()) {
    freeCompileProg(p);
    p = 0;
  }

  return p;
}

void freeCompileProg(comp_prog *p) {
  comp_block *bp;

  if (!p)
    return;

  exclose(p->prog);
  for (size_t i = 0; i < p->n_blocks; i++) {
    bp = p->blocks + i;
    free(bp->node_stmts);
    free(bp->edge_stmts);
  }
  free(p->blocks);
  free(p);
}

/* Read graph from file and initialize
 * dynamic data.
 */
Agraph_t *readG(FILE *fp) {
  Agraph_t *g = agread(fp, NULL);
  if (g) {
    aginit(g, AGRAPH, UDATA, sizeof(gdata), false);
    aginit(g, AGNODE, UDATA, sizeof(ndata), false);
    aginit(g, AGEDGE, UDATA, sizeof(edata), false);
  }
  return g;
}

// open graph and initialize dynamic data
Agraph_t *openG(char *name, Agdesc_t desc) {
  Agraph_t *g = agopen(name, desc, NULL);
  if (g)
    agbindrec(g, UDATA, sizeof(gdata), false);
  return g;
}

// open subgraph and initialize dynamic data
Agraph_t *openSubg(Agraph_t *g, char *name) {
  Agraph_t *sg;

  sg = agsubg(g, name, 1);
  if (sg && !aggetrec(sg, UDATA, 0))
    agbindrec(sg, UDATA, sizeof(gdata), false);
  return sg;
}

// create node and initialize dynamic data
Agnode_t *openNode(Agraph_t *g, char *name) {
  Agnode_t *np;

  np = agnode(g, name, 1);
  if (np && !aggetrec(np, UDATA, 0))
    agbindrec(np, UDATA, sizeof(ndata), false);
  return np;
}

// create edge and initialize dynamic data
Agedge_t *openEdge(Agraph_t *g, Agnode_t *t, Agnode_t *h, char *key) {
  Agedge_t *ep;
  Agraph_t *root;

  root = sameG(t, h, "openEdge", "tail and head nodes");
  if (!root)
    return 0;
  if (g) {
    if (!sameG(g, root, "openEdge", "subgraph and nodes"))
      return 0;
  } else
    g = root;

  ep = agedge(g, t, h, key, 1);
  if (ep && !aggetrec(ep, UDATA, 0))
    agbindrec(ep, UDATA, sizeof(edata), false);
  return ep;
}
