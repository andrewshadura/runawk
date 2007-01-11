/*
 * Copyright (c) 2006 Aleksey Cheusov
 *
 * This material is provided "as is", with absolutely no warranty
 * expressed or implied. Any use is at your own risk.
 *
 * Permission to use or copy this software for any purpose is hereby
 * granted without fee, provided the above notices are retained on all
 * copies.  Permission to modify the code and to distribute modified
 * code is granted, provided the above notices are retained, and a
 * notice that the code was modified is included with the above
 * copyright notice.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef BUFSIZ
#define BUFSIZ 4096
#endif

#ifndef HAVE_WGETLN
#if !defined(__NetBSD__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__DragonFlyBSD__) && !defined(__INTERIX)
#include "fgetln.c"
#endif
#endif

static void usage (void)
{
	puts ("\
runawk provides include files for programs written in awk\n\
usage: runawk [OPTIONS] program_file [arguments...]\n\
 \"program_file\" (prepanded with -f) and \"arguments\"\n\
 are passed to awk interpreter\n\
OPTIONS:\n\
  -h    display this screen\n\
  -V    display version information\n\
  -d    list new argv array, do not run interpreter\n\
        this option is for debugging only\n\
");
}

static void version (void)
{
	puts ("\
runawk 0.6 written by Aleksey Cheusov\n\
");
}

static const char **includes = NULL;
static int includes_count    = 0;

static char *awkpath      = NULL;
static size_t awkpath_len = 0;

static char cwd [PATH_MAX];

static const char *interp = AWK_PROG;

static const char *search_file (const char *dir, const char *name)
{
	/* search in AWKPATH env. */
	const char *curr_dir = NULL;
	char buf [PATH_MAX];
	size_t i;

	/* dir argument */
	snprintf (buf, sizeof (buf), "%s/%s", dir, name);
	if (!access (buf, R_OK)){
		return strdup (buf);
	}

	/* AWKPATH env. */
	for (i = 0; i < awkpath_len; ++i){
		if (awkpath [i] && (i == 0 || awkpath [i-1] == 0)){
			curr_dir = awkpath + i;
			snprintf (buf, sizeof (buf), "%s/%s", curr_dir, name);
			if (!access (buf, R_OK)){
				return strdup (buf);
			}
		}
	}

	return NULL;
}

static void invalid_use_directive (int num, const char *line, const char *fn)
{
	fprintf (stderr,
			 "error: invalid directive at line #%d,\n line=`%s`\n file=`%s`\n",
			 num, line, fn);
}

static void push_uniq (const char *dir, const char *name);

static const char *extract_qstring (char *line, const char *fn, char *s)
{
	char *p = NULL;
	char *n = NULL;

	p = strchr (s, '"');
	if (p)
		n = strchr (p + 1, '"');

	if (!p || !n){
		invalid_use_directive (0, line, fn);
		exit (36);
	}

	*n = 0;
	return strdup (p+1);
}

static void scan_for_use (const char *name)
{
	char dir [PATH_MAX];
	char *line = NULL;
	size_t len = 0;
	FILE *fd = NULL;

	len = strlen (name);
	strncpy (dir, name, sizeof (dir));
	while (len--){
		if (dir [len] == '/'){
			dir [len] = 0;
			break;
		}
	}

	fd = fopen (name, "r");
	if (!fd){
		fprintf (stderr, "fopen(%s) failed: %s\n", name, strerror (errno));
		exit (35);
	}

	while (line = fgetln (fd, &len), line != NULL){
		if (line [len-1] == '\n')
			line [len-1] = 0;

		if (!strncmp (line, "#use ", 5)){
			push_uniq (dir, extract_qstring (line, name, line + 5));
		}
		if (!strncmp (line, "#interp ", 5)){
			interp = extract_qstring (line, name, line + 8);
		}
	}
	if (ferror (fd)){
		perror ("fgeln(3) failed");
		exit (36);
	}
}

static void ll_push (const char *item, const char ***array, size_t *array_size)
{
	*array = (const char **) realloc (
		*array, (*array_size + 1) * sizeof (char *));

	if (!*array){
		perror ("realloc(3) failed");
		exit (31);
	}

	(*array) [*array_size] = item;
	++*array_size;
}

static void push (const char *dir, const char *name)
{
	const char *new_name = NULL;

	if (name [0] != '/'){
		/* name -> path */
		new_name = search_file (dir, name);
		if (!new_name){
			fprintf (stderr, "Cannot find file `%s`, check AWKPATH environment variable\n", name);
			exit (34);
		}
		name = new_name;
	}

	/* add to queue */
	ll_push (name, &includes, &includes_count);

	/* recursive snanning for #use directive */
	scan_for_use (name);
}

static void push_uniq (const char *dir, const char *name)
{
	int i;
	const char *p;
	const char *inc;

	for (i=0; i < includes_count; ++i){
		inc = includes [i];
		p = strstr (inc, name);

		if (p && (p == inc || (p [-1] == '/' && p [strlen (p)] == 0))){
			return;
		}
	}
	push (dir, name);
}

int main (int argc, char **argv)
{
	int i;
	const char ** new_argv = NULL;
	int new_argc = 0;
	int debug = 0;

	--argc, ++argv;

	if (argc == 0){
		usage ();
		return 30;
	}

	/* AWKPATH env. */
	awkpath = getenv ("AWKPATH");
	if (awkpath){
		awkpath = strdup (awkpath);
		if (!awkpath){
			perror ("strdup failed");
			exit (33);
		}

		awkpath_len = strlen (awkpath);
		for (i=0; i < awkpath_len; ++i){
			if (awkpath [i] == ':'){
				awkpath [i] = 0;
			}
		}
	}

	/* cwd */
	if (!getcwd (cwd, sizeof (cwd))){
		perror ("getcwd (3) failed");
		exit (32);
	}

	/* opts, no getopt(3) here */
	if (argc && !strcmp (argv [0], "-h")){
		usage ();
		return 0;
	}
	if (argc && !strcmp (argv [0], "-V")){
		version ();
		return 0;
	}
	if (argc && !strcmp (argv [0], "-d")){
		debug = 1;
		--argc;
		++argv;
	}

	/* program_file */
	if (argc < 1){
		usage ();
		exit (30);
	}

	--argc;
	push (cwd, *argv++);

	/* exec */
	ll_push (interp, &new_argv, &new_argc);
	for (i=0; i < includes_count; ++i){
		ll_push ("-f",         &new_argv, &new_argc);
		ll_push (includes [i], &new_argv, &new_argc);
	}

	ll_push ("--", &new_argv, &new_argc);

	for (i=0; i < argc; ++i){
		ll_push (argv [i], &new_argv, &new_argc);
	}
	ll_push (NULL, &new_argv, &new_argc);

	if (debug){
		for (i=0; i < new_argc - 1; ++i){
			printf ("new_argv [%d] = %s\n", i, new_argv [i]);
		}

		return 0;
	}else{
		return execv (interp, (char *const *) new_argv);
	}
}
