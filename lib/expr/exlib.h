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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Glenn Fowler
 * AT&T Research
 *
 * expression library private definitions
 */

#include <ast/ast.h>
#include <sfio/sfio.h>
#include <stdio.h>
#include <util/agxbuf.h>

typedef struct Exinput_s		/* input stack			*/
{
	struct Exinput_s*next;		/* next in stack		*/
	int		close;		/* close fp on pop		*/
	char*		file;		/* previous file		*/
	FILE*		fp;		/* expression file pointer	*/
	int		line;		/* previous line		*/
	int		nesting;	/* expression nesting level	*/
	int		peek;		/* 1 char peek			*/
	int		unit;		/* first frame in parse unit	*/
	char*		pushback;	/* pushback buffer		*/
	char*		pp;		/* pushback pointer		*/
} Exinput_t;

typedef struct Print_s			/* compiled printf arg node	*/
{
	struct Print_s*	next;		/* next arg			*/
	char*		format;		/* printf format for arg	*/
	struct Exnode_s*param[3];	/* 0:width 1:precision 2:base	*/
	struct Exnode_s*arg;		/* arg to format		*/
} Print_t;

#define _EX_DATA_PRIVATE_ \
	Exnode_t*	next;		/* free list link		*/ \
	Extype_t	value;		/* dynamic variable value	*/ \
	struct								   \
	{								   \
	Exid_t*		procedure;	/* called procedure		*/ \
	Exnode_t*	args;		/* actual argument list		*/ \
	}		call;		/* procedure call		*/ \
	struct								   \
	{								   \
	Exnode_t*	array;		/* array name			*/ \
	Exid_t*		index;		/* array index			*/ \
	Exnode_t*	statement;	/* statement to apply		*/ \
	}		generate;	/* associative array generator	*/ \
	struct								   \
	{								   \
	Exid_t*		array;		/* array 			*/ \
	Exnode_t*	string;		/* string 			*/ \
	Exnode_t*	seps;		/* optional separators		*/ \
	}		split;		/* string split			*/ \
	struct								   \
	{								   \
	Exnode_t*	descriptor;	/* Expr_t.file index		*/ \
	Print_t*	args;		/* compiler printf args		*/ \
	}		print;		/* printf			*/ \
	struct								   \
	{								   \
	Exnode_t*	base; 		/* base string */ \
	Exnode_t*	pat; 		/* pattern or start index	*/ \
	Exnode_t*	repl; 		/* optional replacement or end index	*/ \
	}		string;		/* string builtins			*/ \
	struct								   \
	{								   \
	Exnode_t*	args;		/* formal arg list		*/ \
	Exnode_t*	body;		/* body				*/ \
	Dt_t*		frame;		/* local symbol frame		*/ \
	int		arity;		/* # formal args		*/ \
	}		procedure;	/* procedure args and body	*/ \
	struct								   \
	{								   \
	Exnode_t*	descriptor;	/* Expr_t.file index		*/ \
	Exnode_t*	format;		/* format arg			*/ \
	Exnode_t*	args;		/* actual args			*/ \
	}		scan;		/* printf			*/

#define _EX_NODE_PRIVATE_ \
	int	subop;		/* operator qualifier		*/

#define _EX_PROG_PRIVATE_ \
	Vmalloc_t*	ve;		/* eval tmp region		*/ \
	Dt_t*		frame;		/* frame symbol table		*/ \
	Exdisc_t*	disc;		/* user discipline		*/ \
	Exinput_t*	input;		/* input stack			*/ \
	Expr_t*		program;	/* previous program on stack	*/ \
	agxbuf		tmp;		/* tmp string buffer		*/ \
	Extype_t	loopret;	/* return value			*/ \
	Exid_t		main;		/* main procedure		*/ \
	char		line[512];	/* last few input tokens	*/ \
	char*		linep;		/* line[] pointer		*/ \
	int		eof;		/* lex hit eof			*/ \
	int		errors;		/* fatal error count		*/ \
	int		linewrap;	/* linep wrapped around line[]	*/ \
	long long loopcount; /* break|continue|return count	*/ \
	long loopop; /* break|continue|return op	*/ \
	int		nesting;	/* exstatement() nesting	*/

#include <expr/expr.h>
#include <ctype.h>
#include <ast/error.h>

#define id_string	(&exbuiltin[0])

#define exunlex(p,c)	((p)->linep--,(p)->input->peek=(c))
#define putcontext(p,c)	(((p)->linep>=&(p)->line[sizeof((p)->line)]?(p)->linep=(p)->line,(p)->linewrap=1:0),*(p)->linep++=(c))
#define setcontext(p)	((p)->linep=(p)->line,(p)->linewrap=0)

typedef struct Switch_s			/* switch parse state		*/
{
	struct Switch_s*prev;		/* previous switch state	*/
	Exnode_t*	firstcase;	/* first case block		*/
	Exnode_t*	lastcase;	/* last case block		*/
	Exnode_t*	defcase;	/* default case block		*/
	Extype_t**	base;		/* label base pointer		*/
	size_t		cur;		/* current label offset         */
	size_t		cap;		/* total labels allocated       */
	int		def;		/* default label hit		*/
	long type; ///< switch test type
} Switch_t;

typedef struct // associative array bucket
{
	Dtlink_t	link;		/* table link			*/
	Extype_t	key;		/* key				*/
	Extype_t	value;		/* value			*/
	char		name[1];	/* index name			*/
} Exassoc_t;

typedef struct // ex global state
{
	Exid_t*		id;		/* current declaration id	*/
	long declare; ///< current declaration type
	int		nolabel;	/* <id>':' not a label		*/
	Exinput_t	null;		/* null input			*/
	Expr_t*		program;	/* current program		*/
	Exnode_t*	procedure;	/* current procedure		*/
	Exref_t*	refs;		/* . reference list		*/
	int		assigned;	/* declaration assignment	*/
	Switch_t*	swstate;	/* switch parse state		*/
	char		nullstring[1];	/* ""				*/
} Exstate_t;

extern Exid_t		exbuiltin[];
extern const char*	exversion;
extern Exstate_t	expr;

extern int		ex_parse(void);	/* yacc should do this		*/

#ifdef __cplusplus
}
#endif
