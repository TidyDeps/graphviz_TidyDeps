/// @file
/// @ingroup common_utils
/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

%require "3.0"

  /* By default, Bison emits a parser using symbols prefixed with "yy". Graphviz
   * contains multiple Bison-generated parsers, so we alter this prefix to avoid
   * symbol clashes.
   */
%define api.prefix {html}

  /* Generate a reentrant parser with no global state */
%define api.pure full
%param { htmlscan_t *scanner }


%code requires {
#include <common/htmllex.h>
#include <common/htmltable.h>
#include <common/textspan.h>
#include <gvc/gvcext.h>
#include <util/agxbuf.h>
#include <util/list.h>
#include <util/strview.h>
}

%code provides {

DEFINE_LIST(sfont, textfont_t *)

static inline void free_ti(textspan_t item) {
  free(item.str);
}

DEFINE_LIST_WITH_DTOR(textspans, textspan_t, free_ti)

static inline void free_hi(htextspan_t item) {
  for (size_t i = 0; i < item.nitems; i++) {
    free(item.items[i].str);
  }
  free(item.items);
}

DEFINE_LIST_WITH_DTOR(htextspans, htextspan_t, free_hi)

struct htmlparserstate_s {
  htmllabel_t* lbl;       /* Generated label */
  htmltbl_t*   tblstack;  /* Stack of tables maintained during parsing */
  textspans_t  fitemList;
  htextspans_t fspanList;
  agxbuf*      str;       /* Buffer for text */
  sfont_t      fontstack;
  GVC_t*       gvc;
};

typedef struct {
#ifdef HAVE_EXPAT
    struct XML_ParserStruct *parser;
#endif
    char* ptr;         // input source
    int tok;           // token type
    agxbuf* xb;        // buffer to gather T_string data
    agxbuf lb;         // buffer for translating lexical data
    int warn;          // set if warning given
    int error;         // set if error given
    char inCell;       // set if in TD to allow T_string
    char mode;         // for handling artificial <HTML>..</HTML>
    strview_t currtok; // for error reporting
    strview_t prevtok; // for error reporting
    GVC_t *gvc;        // current GraphViz context
    HTMLSTYPE *htmllval; // generated by htmlparse.y
} htmllexstate_t;


struct htmlscan_s {
  htmllexstate_t lexer;
  htmlparserstate_t parser;
};
}

%{

#include <common/render.h>
#include <common/htmltable.h>
#include <common/htmllex.h>
#include <stdbool.h>
#include <util/alloc.h>

/// Clean up cell if error in parsing.
static void cleanCell(htmlcell_t *cp);

/// Clean up table if error in parsing.
static void cleanTbl(htmltbl_t *tp) {
  rows_t *rows = &tp->u.p.rows;
  for (size_t r = 0; r < rows_size(rows); ++r) {
    row_t *rp = rows_get(rows, r);
    for (size_t c = 0; c < cells_size(&rp->rp); ++c) {
      cleanCell(cells_get(&rp->rp, c));
    }
  }
  rows_free(rows);
  free_html_data(&tp->data);
  free(tp);
}

/// Clean up cell if error in parsing.
static void
cleanCell (htmlcell_t* cp)
{
  if (cp->child.kind == HTML_TBL) cleanTbl (cp->child.u.tbl);
  else if (cp->child.kind == HTML_TEXT) free_html_text (cp->child.u.txt);
  free_html_data (&cp->data);
  free (cp);
}

/// Append a new text span to the list.
static void
appendFItemList (htmlparserstate_t *html_state, agxbuf *ag);

static void
appendFLineList (htmlparserstate_t *html_state, int v);

static htmltxt_t*
mkText(htmlparserstate_t *html_state);

static row_t *lastRow(htmlparserstate_t *html_state);

/// Add new cell row to current table.
static void addRow(htmlparserstate_t *html_state);

/// Set cell body and type and attach to row
static void setCell(htmlparserstate_t *html_state, htmlcell_t *cp, void *obj, label_type_t kind);

/// Create label, given body and type.
static htmllabel_t *mkLabel(void *obj, label_type_t kind) {
  htmllabel_t* lp = gv_alloc(sizeof(htmllabel_t));

  lp->kind = kind;
  if (kind == HTML_TEXT)
    lp->u.txt = obj;
  else
    lp->u.tbl = obj;
  return lp;
}

/* Called on error. Frees resources allocated during parsing.
 * This includes a label, plus a walk down the stack of
 * tables. Note that `cleanTbl` frees the contained cells.
 */
static void cleanup (htmlparserstate_t *html_state);

/// Return 1 if s contains a non-space character.
static bool nonSpace(const char *s) {
  char   c;

  while ((c = *s++)) {
    if (c != ' ') return true;
  }
  return false;
}

/// Fonts are allocated in the lexer.
static void
pushFont (htmlparserstate_t *html_state, textfont_t *fp);

static void
popFont (htmlparserstate_t *html_state);

%}

%union  {
  int    i;
  htmltxt_t*  txt;
  htmlcell_t*  cell;
  htmltbl_t*   tbl;
  textfont_t*  font;
  htmlimg_t*   img;
  row_t *p;
}

%token T_end_br T_end_img T_row T_end_row T_html T_end_html
%token T_end_table T_end_cell T_end_font T_string T_error
%token T_n_italic T_n_bold T_n_underline  T_n_overline T_n_sup T_n_sub T_n_s
%token T_HR T_hr T_end_hr
%token T_VR T_vr T_end_vr
%token <i> T_BR T_br
%token <img> T_IMG T_img
%token <tbl> T_table
%token <cell> T_cell
%token <font> T_font T_italic T_bold T_underline T_overline T_sup T_sub T_s

%type <txt> fonttext
%type <cell> cell cells
%type <i> br
%type <tbl> table fonttable
%type <img> image
%type <p> row rows

%start html

%%

html  : T_html fonttext T_end_html { scanner->parser.lbl = mkLabel($2,HTML_TEXT); }
      | T_html fonttable T_end_html { scanner->parser.lbl = mkLabel($2,HTML_TBL); }
      | error { cleanup(&scanner->parser); YYABORT; }
      ;

fonttext : text { $$ = mkText(&scanner->parser); }
      ;

text : text textitem
     | textitem
     ;

textitem : string { appendFItemList(&scanner->parser,scanner->parser.str);}
         | br {appendFLineList(&scanner->parser,$1);}
         | font text n_font
         | italic text n_italic
         | underline text n_underline
         | overline text n_overline
         | bold text n_bold
         | sup text n_sup
         | sub text n_sub
         | strike text n_strike
         ;

font : T_font { pushFont (&scanner->parser,$1); }
      ;

n_font : T_end_font { popFont (&scanner->parser); }
      ;

italic : T_italic {pushFont(&scanner->parser,$1);}
          ;

n_italic : T_n_italic {popFont(&scanner->parser);}
            ;

bold : T_bold {pushFont(&scanner->parser,$1);}
          ;

n_bold : T_n_bold {popFont(&scanner->parser);}
            ;

strike : T_s {pushFont(&scanner->parser,$1);}
          ;

n_strike : T_n_s {popFont(&scanner->parser);}
            ;

underline : T_underline {pushFont(&scanner->parser,$1);}
          ;

n_underline : T_n_underline {popFont(&scanner->parser);}
            ;

overline : T_overline {pushFont(&scanner->parser,$1);}
          ;

n_overline : T_n_overline {popFont(&scanner->parser);}
            ;

sup : T_sup {pushFont(&scanner->parser,$1);}
          ;

n_sup : T_n_sup {popFont(&scanner->parser);}
            ;

sub : T_sub {pushFont(&scanner->parser,$1);}
          ;

n_sub : T_n_sub {popFont(&scanner->parser);}
            ;

br     : T_br T_end_br { $$ = $1; }
       | T_BR { $$ = $1; }
       ;

string : T_string
       | string T_string
       ;

table : opt_space T_table {
          if (nonSpace(agxbuse(scanner->parser.str))) {
            htmlerror (scanner,"Syntax error: non-space string used before <TABLE>");
            cleanup(&scanner->parser); YYABORT;
          }
          $2->u.p.prev = scanner->parser.tblstack;
          $2->u.p.rows = (rows_t){0};
          scanner->parser.tblstack = $2;
          $2->font = *sfont_back(&scanner->parser.fontstack);
          $<tbl>$ = $2;
        }
        rows T_end_table opt_space {
          if (nonSpace(agxbuse(scanner->parser.str))) {
            htmlerror (scanner,"Syntax error: non-space string used after </TABLE>");
            cleanup(&scanner->parser); YYABORT;
          }
          $$ = scanner->parser.tblstack;
          scanner->parser.tblstack = scanner->parser.tblstack->u.p.prev;
        }
      ;

fonttable : table { $$ = $1; }
          | font table n_font { $$=$2; }
          | italic table n_italic { $$=$2; }
          | underline table n_underline { $$=$2; }
          | overline table n_overline { $$=$2; }
          | bold table n_bold { $$=$2; }
          ;

opt_space : string
          | /* empty*/
          ;

rows : row { $$ = $1; }
     | rows row { $$ = $2; }
     | rows HR row { $1->ruled = true; $$ = $3; }
     ;

row : T_row { addRow (&scanner->parser); } cells T_end_row { $$ = lastRow(&scanner->parser); }
      ;

cells : cell { $$ = $1; }
      | cells cell { $$ = $2; }
      | cells VR cell { $1->vruled = true; $$ = $3; }
      ;

cell : T_cell fonttable { setCell(&scanner->parser,$1,$2,HTML_TBL); } T_end_cell { $$ = $1; }
     | T_cell fonttext { setCell(&scanner->parser,$1,$2,HTML_TEXT); } T_end_cell { $$ = $1; }
     | T_cell image { setCell(&scanner->parser,$1,$2,HTML_IMAGE); } T_end_cell { $$ = $1; }
     | T_cell { setCell(&scanner->parser,$1,mkText(&scanner->parser),HTML_TEXT); } T_end_cell { $$ = $1; }
     ;

image  : T_img T_end_img { $$ = $1; }
       | T_IMG { $$ = $1; }
       ;

HR  : T_hr T_end_hr
    | T_HR
    ;

VR  : T_vr T_end_vr
    | T_VR
    ;


%%

static void
appendFItemList (htmlparserstate_t *html_state, agxbuf *ag)
{
    const textspan_t ti = {.str = agxbdisown(ag),
                           .font = *sfont_back(&html_state->fontstack)};
    textspans_append(&html_state->fitemList, ti);
}

static void
appendFLineList (htmlparserstate_t *html_state, int v)
{
    htextspan_t lp = {0};
    textspans_t *ilist = &html_state->fitemList;

    size_t cnt = textspans_size(ilist);
    lp.just = v;
    if (cnt) {
	lp.nitems = cnt;
	lp.items = gv_calloc(cnt, sizeof(textspan_t));

	for (size_t i = 0; i < textspans_size(ilist); ++i) {
	    // move this text span into the new list
	    textspan_t *ti = textspans_at(ilist, i);
	    lp.items[i] = *ti;
	    *ti = (textspan_t){0};
	}
    }
    else {
	lp.items = gv_alloc(sizeof(textspan_t));
	lp.nitems = 1;
	lp.items[0].str = gv_strdup("");
	lp.items[0].font = *sfont_back(&html_state->fontstack);
    }

    textspans_clear(ilist);

    htextspans_append(&html_state->fspanList, lp);
}

static htmltxt_t*
mkText(htmlparserstate_t *html_state)
{
    htextspans_t *ispan = &html_state->fspanList;
    htmltxt_t *hft = gv_alloc(sizeof(htmltxt_t));

    if (!textspans_is_empty(&html_state->fitemList))
	appendFLineList (html_state, UNSET_ALIGN);

    size_t cnt = htextspans_size(ispan);
    hft->nspans = cnt;

    hft->spans = gv_calloc(cnt, sizeof(htextspan_t));
    for (size_t i = 0; i < htextspans_size(ispan); ++i) {
    	// move this HTML text span into the new list
    	htextspan_t *hi = htextspans_at(ispan, i);
    	hft->spans[i] = *hi;
    	*hi = (htextspan_t){0};
    }

    htextspans_clear(ispan);

    return hft;
}

static row_t *lastRow(htmlparserstate_t *html_state) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *sp = *rows_back(&tbl->u.p.rows);
  return sp;
}

static void addRow(htmlparserstate_t *html_state) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *sp = gv_alloc(sizeof(row_t));
  if (tbl->hrule)
    sp->ruled = true;
  rows_append(&tbl->u.p.rows, sp);
}

static void setCell(htmlparserstate_t *html_state, htmlcell_t *cp, void *obj, label_type_t kind) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *rp = *rows_back(&tbl->u.p.rows);
  cells_t *row = &rp->rp;
  cells_append(row, cp);
  cp->child.kind = kind;
  if (tbl->vrule) {
    cp->vruled = true;
    cp->hruled = false;
  }

  if(kind == HTML_TEXT)
  	cp->child.u.txt = obj;
  else if (kind == HTML_IMAGE)
    cp->child.u.img = obj;
  else
    cp->child.u.tbl = obj;
}

static void cleanup (htmlparserstate_t *html_state)
{
  htmltbl_t* tp = html_state->tblstack;
  htmltbl_t* next;

  if (html_state->lbl) {
    free_html_label (html_state->lbl,1);
    html_state->lbl = NULL;
  }
  while (tp) {
    next = tp->u.p.prev;
    cleanTbl (tp);
    tp = next;
  }

  textspans_clear(&html_state->fitemList);
  htextspans_clear(&html_state->fspanList);

  sfont_free(&html_state->fontstack);
}

static void
pushFont (htmlparserstate_t *html_state, textfont_t *fp)
{
    textfont_t* curfont = *sfont_back(&html_state->fontstack);
    textfont_t  f = *fp;

    if (curfont) {
	if (!f.color && curfont->color)
	    f.color = curfont->color;
	if ((f.size < 0.0) && (curfont->size >= 0.0))
	    f.size = curfont->size;
	if (!f.name && curfont->name)
	    f.name = curfont->name;
	if (curfont->flags)
	    f.flags |= curfont->flags;
    }

    textfont_t *const ft = dtinsert(html_state->gvc->textfont_dt, &f);
    sfont_push_back(&html_state->fontstack, ft);
}

static void
popFont (htmlparserstate_t *html_state)
{
    (void)sfont_pop_back(&html_state->fontstack);
}

/* Return parsed label or NULL if failure.
 * Set warn to 0 on success; 1 for warning message; 2 if no expat; 3 for error
 * message.
 */
htmllabel_t*
parseHTML (char* txt, int* warn, htmlenv_t *env)
{
  agxbuf        str = {0};
  htmllabel_t*  l = NULL;
  htmlscan_t    scanner = {0};

  sfont_push_back(&scanner.parser.fontstack, NULL);
  scanner.parser.gvc = GD_gvc(env->g);
  scanner.parser.str = &str;

  if (initHTMLlexer (&scanner, txt, &str, env)) {/* failed: no libexpat - give up */
    *warn = 2;
  }
  else {
    htmlparse(&scanner);
    *warn = clearHTMLlexer (&scanner);
    l = scanner.parser.lbl;
  }

  textspans_free(&scanner.parser.fitemList);
  htextspans_free(&scanner.parser.fspanList);

  sfont_free(&scanner.parser.fontstack);

  agxbfree (&str);

  return l;
}
