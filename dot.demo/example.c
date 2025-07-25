/// @file
/// @brief small example, demonstrates usage of @ref agopen, @ref agnode,
/// @ref agedge, @ref agsafeset and @ref agwrite

#include <graphviz/gvc.h>
#include <stdio.h>
#include <stdlib.h>

#define NO_LAYOUT_OR_RENDERING

int main(void) {
#ifndef NO_LAYOUT_OR_RENDERING
  // set up a graphviz context - but only once even for multiple graphs
  GVC_t *gvc = gvContext();
#endif

  // Create a simple digraph
  Agraph_t *g = agopen("g", Agdirected, 0);
  Agnode_t *n = agnode(g, "n", 1);
  Agnode_t *m = agnode(g, "m", 1);
  (void)agedge(g, n, m, 0, 1);

  // Set an attribute - in this case one that affects the visible rendering
  agsafeset(n, "color", "red", "");

#ifdef NO_LAYOUT_OR_RENDERING
  // Just write the graph without layout
  agwrite(g, stdout);
#else
  // Use the directed graph layout engine
  gvLayout(gvc, g, "dot");

  // Output in .dot format
  gvRender(gvc, g, "dot", stdout);

  gvFreeLayout(gvc, g);
#endif

  agclose(g);

  return EXIT_SUCCESS;
}
