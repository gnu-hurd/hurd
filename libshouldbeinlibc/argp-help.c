/* Hierarchial argument parsing help output
   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Written by Miles Bader <miles@gnu.ai.mit.edu>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <malloc.h>
#include <ctype.h>

#include "argp.h"
#include "argp-fmtstream.h"
#include "argp-namefrob.h"

#define SHORT_OPT_COL 2		/* column in which short options start */
#define LONG_OPT_COL  6		/* column in which long options start */
#define DOC_OPT_COL   2		/* column in which doc options start */
#define OPT_DOC_COL  29		/* column in which option text starts */
#define HEADER_COL    1		/* column in which group headers are printed */
#define USAGE_INDENT 12		/* indentation of wrapped usage lines */
#define RMARGIN      79		/* right margin used for wrapping */

/* Returns true if OPT hasn't been marked invisible.  Visibility only affects
   whether OPT is displayed or used in sorting, not option shadowing.  */
#define ovisible(opt) (! ((opt)->flags & OPTION_HIDDEN))

/* Returns true if OPT is an alias for an earlier option.  */
#define oalias(opt) ((opt)->flags & OPTION_ALIAS)

/* Returns true if OPT is an documentation-only entry.  */
#define odoc(opt) ((opt)->flags & OPTION_DOC)

/* Returns true if OPT is the end-of-list marker for a list of options.  */
#define oend(opt) __option_is_end (opt)

/* Returns true if OPT has a short option.  */
#define oshort(opt) __option_is_short (opt)

/*
   The help format for a particular option is like:

     -xARG, -yARG, --long1=ARG, --long2=ARG        Documentation...

   Where ARG will be omitted if there's no argument, for this option, or
   will be surrounded by "[" and "]" appropiately if the argument is
   optional.  The documentation string is word-wrapped appropiately, and if
   the list of options is long enough, it will be started on a separate line.
   If there are no short options for a given option, the first long option is
   indented slighly in a way that's supposed to make most long options appear
   to be in a separate column.

   For example, the following output (from ps):

     -p PID, --pid=PID          List the process PID
	 --pgrp=PGRP            List processes in the process group PGRP
     -P, -x, --no-parent        Include processes without parents
     -Q, --all-fields           Don't elide unusable fields (normally if there's
				some reason ps can't print a field for any
				process, it's removed from the output entirely)
     -r, --reverse, --gratuitously-long-reverse-option
				Reverse the order of any sort
	 --session[=SID]        Add the processes from the session SID (which
				defaults to the sid of the current process)

    Here are some more options:
     -f ZOT, --foonly=ZOT       Glork a foonly
     -z, --zaza                 Snit a zar

     -?, --help                 Give this help list
	 --usage                Give a short usage message
     -V, --version              Print program version

   The struct argp_option array for the above could look like:

   {
     {"pid",       'p',      "PID",  0, "List the process PID"},
     {"pgrp",      OPT_PGRP, "PGRP", 0, "List processes in the process group PGRP"},
     {"no-parent", 'P',	      0,     0, "Include processes without parents"},
     {0,           'x',       0,     OPTION_ALIAS},
     {"all-fields",'Q',       0,     0, "Don't elide unusable fields (normally"
                                        " if there's some reason ps can't"
					" print a field for any process, it's"
                                        " removed from the output entirely)" },
     {"reverse",   'r',       0,     0, "Reverse the order of any sort"},
     {"gratuitously-long-reverse-option", 0, 0, OPTION_ALIAS},
     {"session",   OPT_SESS,  "SID", OPTION_ARG_OPTIONAL,
                                        "Add the processes from the session"
					" SID (which defaults to the sid of"
					" the current process)" },

     {0,0,0,0, "Here are some more options:"},
     {"foonly", 'f', "ZOT", 0, "Glork a foonly"},
     {"zaza", 'z', 0, 0, "Snit a zar"},

     {0}
   }

   Note that the last three options are automatically supplied by argp_parse,
   unless you tell it not to with ARGP_NO_HELP.

*/

/* Returns true if CH occurs between BEG and END.  */
static int
find_char (char ch, char *beg, char *end)
{
  while (beg < end)
    if (*beg == ch)
      return 1;
    else
      beg++;
  return 0;
}

struct hol_cluster;		/* fwd decl */

struct hol_entry
{
  /* First option.  */
  const struct argp_option *opt;
  /* Number of options (including aliases).  */
  unsigned num;

  /* A pointers into the HOL's short_options field, to the first short option
     letter for this entry.  The order of the characters following this point
     corresponds to the order of options pointed to by OPT, and there are at
     most NUM.  A short option recorded in a option following OPT is only
     valid if it occurs in the right place in SHORT_OPTIONS (otherwise it's
     probably been shadowed by some other entry).  */
  char *short_options;

  /* Entries are sorted by their group first, in the order:
       1, 2, ..., n, 0, -m, ..., -2, -1
     and then alphabetically within each group.  The default is 0.  */
  int group;

  /* The cluster of options this entry belongs to, or 0 if none.  */
  struct hol_cluster *cluster;
};

/* A cluster of entries to reflect the argp tree structure.  */
struct hol_cluster
{
  /* A descriptive header printed before options in this cluster.  */
  const char *header;

  /* Used to order clusters within the same group with the same parent,
     according to the order in which they occured in the parent argp's child
     list.  */
  int index;

  /* How to sort this cluster with respect to options and other clusters at the
     same depth (clusters always follow options in the same group).  */
  int group;

  /* The cluster to which this cluster belongs, or 0 if it's at the base
     level.  */
  struct hol_cluster *parent;

  /* The distance this cluster is from the root.  */
  int depth;

  /* Clusters in a given hol are kept in a linked list, to make freeing them
     possible.  */
  struct hol_cluster *next;
};

/* A list of options for help.  */
struct hol
{
  /* An array of hol_entry's.  */
  struct hol_entry *entries;
  /* The number of entries in this hol.  If this field is zero, the others
     are undefined.  */
  unsigned num_entries;

  /* A string containing all short options in this HOL.  Each entry contains
     pointers into this string, so the order can't be messed with blindly.  */
  char *short_options;

  /* Clusters of entries in this hol.  */
  struct hol_cluster *clusters;
};

/* Create a struct hol from an array of struct argp_option.  CLUSTER is the
   hol_cluster in which these entries occur, or 0, if at the root.  */
static struct hol *
make_hol (const struct argp_option *opt, struct hol_cluster *cluster)
{
  char *so;
  const struct argp_option *o;
  struct hol_entry *entry;
  unsigned num_short_options = 0;
  struct hol *hol = malloc (sizeof (struct hol));

  assert (hol);

  hol->num_entries = 0;
  hol->clusters = 0;

  if (opt)
    {
      int cur_group = 0;

      /* The first option must not be an alias.  */
      assert (! oalias (opt));

      /* Calculate the space needed.  */
      for (o = opt; ! oend (o); o++)
	{
	  if (! oalias (o))
	    hol->num_entries++;
	  if (oshort (o))
	    num_short_options++;	/* This is an upper bound.  */
	}

      hol->entries = malloc (sizeof (struct hol_entry) * hol->num_entries);
      hol->short_options = malloc (num_short_options + 1);

      assert (hol->entries && hol->short_options);

      /* Fill in the entries.  */
      so = hol->short_options;
      for (o = opt, entry = hol->entries; ! oend (o); entry++)
	{
	  entry->opt = o;
	  entry->num = 0;
	  entry->short_options = so;
	  entry->group = cur_group =
	    o->group
	    ? o->group
	    : ((!o->name && !o->key)
	       ? cur_group + 1
	       : cur_group);
	  entry->cluster = cluster;

	  do
	    {
	      entry->num++;
	      if (oshort (o) && ! find_char (o->key, hol->short_options, so))
		/* O has a valid short option which hasn't already been used.*/
		*so++ = o->key;
	      o++;
	    }
	  while (! oend (o) && oalias (o));
	}
      *so = '\0';		/* null terminated so we can find the length */
    }

  return hol;
}

/* Add a new cluster to HOL, with the given GROUP and HEADER (taken from the
   associated argp child list entry), INDEX, and PARENT, and return a pointer
   to it.  */
static struct hol_cluster *
hol_add_cluster (struct hol *hol, int group, const char *header, int index,
		 struct hol_cluster *parent)
{
  struct hol_cluster *cl = malloc (sizeof (struct hol_cluster));
  if (cl)
    {
      cl->group = group;
      cl->header = header;

      cl->index = index;
      cl->parent = parent;

      cl->next = hol->clusters;
      hol->clusters = cl;
    }
  return cl;
}

/* Free HOL and any resources it uses.  */
static void
hol_free (struct hol *hol)
{
  struct hol_cluster *cl = hol->clusters;

  while (cl)
    {
      struct hol_cluster *next = cl->next;
      free (cl);
      cl = next;
    }

  if (hol->num_entries > 0)
    {
      free (hol->entries);
      free (hol->short_options);
    }

  free (hol);
}

static inline int
hol_entry_short_iterate (const struct hol_entry *entry,
			 int (*func)(const struct argp_option *opt,
				     const struct argp_option *real,
				     void *cookie),
			 void *cookie)
{
  unsigned nopts;
  int val = 0;
  const struct argp_option *opt, *real = entry->opt;
  char *so = entry->short_options;

  for (opt = real, nopts = entry->num; nopts > 0 && !val; opt++, nopts--)
    if (oshort (opt) && *so == opt->key)
      {
	if (!oalias (opt))
	  real = opt;
	if (ovisible (opt))
	  val = (*func)(opt, real, cookie);
	so++;
      }

  return val;
}

static inline int
hol_entry_long_iterate (const struct hol_entry *entry,
			int (*func)(const struct argp_option *opt,
				    const struct argp_option *real,
				    void *cookie),
			void *cookie)
{
  unsigned nopts;
  int val = 0;
  const struct argp_option *opt, *real = entry->opt;

  for (opt = real, nopts = entry->num; nopts > 0 && !val; opt++, nopts--)
    if (opt->name)
      {
	if (!oalias (opt))
	  real = opt;
	if (ovisible (opt))
	  val = (*func)(opt, real, cookie);
      }

  return val;
}

/* Iterator that returns true for the first short option.  */
static inline int
until_short (const struct argp_option *opt, const struct argp_option *real,
	     void *cookie)
{
  return oshort (opt) ? opt->key : 0;
}

/* Returns the first valid short option in ENTRY, or 0 if there is none.  */
static char
hol_entry_first_short (const struct hol_entry *entry)
{
  return hol_entry_short_iterate (entry, until_short, 0);
}

/* Returns the first valid long option in ENTRY, or 0 if there is none.  */
static const char *
hol_entry_first_long (const struct hol_entry *entry)
{
  const struct argp_option *opt;
  unsigned num;
  for (opt = entry->opt, num = entry->num; num > 0; opt++, num--)
    if (opt->name && ovisible (opt))
      return opt->name;
  return 0;
}

/* Returns the entry in HOL with the long option name NAME, or 0 if there is
   none.  */
static struct hol_entry *
hol_find_entry (struct hol *hol, const char *name)
{
  struct hol_entry *entry = hol->entries;
  unsigned num_entries = hol->num_entries;

  while (num_entries-- > 0)
    {
      const struct argp_option *opt = entry->opt;
      unsigned num_opts = entry->num;

      while (num_opts-- > 0)
	if (opt->name && ovisible (opt) && strcmp (opt->name, name) == 0)
	  return entry;
	else
	  opt++;

      entry++;
    }

  return 0;
}

/* If an entry with the long option NAME occurs in HOL, set it's special
   sort position to GROUP.  */
static void
hol_set_group (struct hol *hol, const char *name, int group)
{
  struct hol_entry *entry = hol_find_entry (hol, name);
  if (entry)
    entry->group = group;
}

/* Order by group:  0, 1, 2, ..., n, -m, ..., -2, -1.
   EQ is what to return if GROUP1 and GROUP2 are the same.  */
static int
group_cmp (int group1, int group2, int eq)
{
  if (group1 == group2)
    return eq;
  else if ((group1 < 0 && group2 < 0) || (group1 >= 0 && group2 >= 0))
    return group1 - group2;
  else
    return group2 - group1;
}

/* Compare clusters CL1 & CL2 by the order that they should appear in
   output.  */
static int
hol_cluster_cmp (const struct hol_cluster *cl1, const struct hol_cluster *cl2)
{
  /* If one cluster is deeper than the other, use its ancestor at the same
     level, so that finding the common ancestor is straightforward.  */
  while (cl1->depth < cl2->depth)
    cl1 = cl1->parent;
  while (cl2->depth < cl1->depth)
    cl2 = cl2->parent;

  /* Now reduce both clusters to their ancestors at the point where both have
     a common parent; these can be directly compared.  */
  while (cl1->parent != cl2->parent)
    cl1 = cl1->parent, cl2 = cl2->parent;

  return group_cmp (cl1->group, cl2->group, cl2->index - cl1->index);
}

/* Return the ancestor of CL that's just below the root (i.e., has a parent
   of 0).  */
static struct hol_cluster *
hol_cluster_base (struct hol_cluster *cl)
{
  while (cl->parent)
    cl = cl->parent;
  return cl;
}

/* Return true if CL1 is a child of CL2.  */
static int
hol_cluster_is_child (const struct hol_cluster *cl1,
		      const struct hol_cluster *cl2)
{
  while (cl1 && cl1 != cl2)
    cl1 = cl1->parent;
  return cl1 == cl2;
}

/* Given the name of a OPTION_DOC option, modifies NAME to start at the tail
   that should be used for comparisons, and returns true iff it should be
   treated as a non-option.  */
static int
canon_doc_option (const char **name)
{
  int non_opt;
  /* Skip initial whitespace.  */
  while (isspace (*name))
    (*name)++;
  /* Decide whether this looks like an option (leading `-') or not.  */
  non_opt = (**name != '-');
  /* Skip until part of name used for sorting.  */
  while (**name && !isalnum (*name))
    (*name)++;
  return non_opt;
}

/* Order ENTRY1 & ENTRY2 by the order which they should appear in a help
   listing.  */
static int
hol_entry_cmp (const struct hol_entry *entry1, const struct hol_entry *entry2)
{
  /* The group numbers by which the entries should be ordered; if either is
     in a cluster, then this is just the group within the cluster.  */
  int group1 = entry1->group, group2 = entry2->group;

  if (entry1->cluster != entry2->cluster)
    /* The entries are not within the same cluster, so we can't compare them
       directly, we have to use the appropiate clustering level too.  */
    if (! entry1->cluster)
      /* ENTRY1 is at the `base level', not in a cluster, so we have to
	 compare it's group number with that of the base cluster in which
	 ENTRY2 resides.  Note that if they're in the same group, the
	 clustered option always comes laster.  */
      return group_cmp (group1, hol_cluster_base (entry2->cluster)->group, -1);
    else if (! entry2->cluster)
      /* Likewise, but ENTRY2's not in a cluster.  */
      return group_cmp (hol_cluster_base (entry1->cluster)->group, group2, 1);
    else
      /* Both entries are in clusters, we can just compare the clusters.  */
      return hol_cluster_cmp (entry1->cluster, entry2->cluster);
  else if (group1 == group2)
    /* The entries are both in the same cluster and group, so compare them
       alphabetically.  */
    {
      int short1 = hol_entry_first_short (entry1);
      int short2 = hol_entry_first_short (entry2);
      int doc1 = odoc (entry1->opt);
      int doc2 = odoc (entry2->opt);
      const char *long1 = hol_entry_first_long (entry1);
      const char *long2 = hol_entry_first_long (entry2);

      if (doc1)
	doc1 = canon_doc_option (&long1);
      if (doc2)
	doc2 = canon_doc_option (&long2);

      if (doc1 != doc2)
	/* `documentation' options always follow normal options (or
	   documentation options that *look* like normal options).  */
	return doc1 - doc2;
      else if (!short1 && !short2 && long1 && long2)
	/* Only long options.  */
	return __strcasecmp (long1, long2);
      else
	/* Compare short/short, long/short, short/long, using the first
	   character of long options.  Entries without *any* valid
	   options (such as options with OPTION_HIDDEN set) will be put
	   first, but as they're not displayed, it doesn't matter where
	   they are.  */
	{
	  char first1 = short1 ? short1 : long1 ? *long1 : 0;
	  char first2 = short2 ? short2 : long2 ? *long2 : 0;
	  int lower_cmp = tolower (first1) - tolower (first2);
	  /* Compare ignoring case, except when the options are both the
	     same letter, in which case lower-case always comes first.  */
	  return lower_cmp ? lower_cmp : first2 - first1;
	}
    }
  else
    /* Within the same cluster, but not the same group, so just compare
       groups.  */
    return group_cmp (group1, group2, 0);
}

/* Version of hol_entry_cmp with correct signature for qsort.  */
static int
hol_entry_qcmp (const void *entry1_v, const void *entry2_v)
{
  return hol_entry_cmp (entry1_v, entry2_v);
}

/* Sort HOL by group and alphabetically by option name (with short options
   taking precedence over long).  Since the sorting is for display purposes
   only, the shadowing of options isn't effected.  */
static void
hol_sort (struct hol *hol)
{
  if (hol->num_entries > 0)
    qsort (hol->entries, hol->num_entries, sizeof (struct hol_entry),
	   hol_entry_qcmp);
}

/* Append MORE to HOL, destroying MORE in the process.  Options in HOL shadow
   any in MORE with the same name.  */
static void
hol_append (struct hol *hol, struct hol *more)
{
  struct hol_cluster **cl_end = &hol->clusters;

  /* Steal MORE's cluster list, and add it to the end of HOL's.  */
  while (*cl_end)
    cl_end = &(*cl_end)->next;
  *cl_end = more->clusters;
  more->clusters = 0;

  /* Merge entries.  */
  if (more->num_entries > 0)
    if (hol->num_entries == 0)
      {
	hol->num_entries = more->num_entries;
	hol->entries = more->entries;
	hol->short_options = more->short_options;
	more->num_entries = 0;	/* Mark MORE's fields as invalid.  */
      }
    else
      /* append the entries in MORE to those in HOL, taking care to only add
	 non-shadowed SHORT_OPTIONS values.  */
      {
	unsigned left;
	char *so, *more_so;
	struct hol_entry *e;
	unsigned num_entries = hol->num_entries + more->num_entries;
	struct hol_entry *entries =
	  malloc (num_entries * sizeof (struct hol_entry));
	unsigned hol_so_len = strlen (hol->short_options);
	char *short_options =
	  malloc (hol_so_len + strlen (more->short_options) + 1);

	memcpy (entries, hol->entries,
		hol->num_entries * sizeof (struct hol_entry));
	memcpy (entries + hol->num_entries, more->entries,
		more->num_entries * sizeof (struct hol_entry));

	memcpy (short_options, hol->short_options, hol_so_len);

	/* Fix up the short options pointers from HOL.  */
	for (e = entries, left = hol->num_entries; left > 0; e++, left--)
	  e->short_options += (short_options - hol->short_options);

	/* Now add the short options from MORE, fixing up its entries too.  */
	so = short_options + hol_so_len;
	more_so = more->short_options;
	for (left = more->num_entries; left > 0; e++, left--)
	  {
	    int opts_left;
	    const struct argp_option *opt;

	    e->short_options = so;

	    for (opts_left = e->num, opt = e->opt; opts_left; opt++, opts_left--)
	      {
		int ch = *more_so;
		if (oshort (opt) && ch == opt->key)
		  /* The next short option in MORE_SO, CH, is from OPT.  */
		  {
		    if (! find_char (ch,
				     short_options, short_options + hol_so_len))
		      /* The short option CH isn't shadowed by HOL's options,
			 so add it to the sum.  */
		      *so++ = ch;
		    more_so++;
		  }
	      }
	  }

	*so = '\0';

	free (hol->entries);
	free (hol->short_options);

	hol->entries = entries;
	hol->num_entries = num_entries;
	hol->short_options = short_options;
      }

  hol_free (more);
}

/* Inserts enough spaces to make sure STREAM is at column COL.  */
static void
indent_to (argp_fmtstream_t stream, unsigned col)
{
  int needed = col - __argp_fmtstream_point (stream);
  while (needed-- > 0)
    __argp_fmtstream_putc (stream, ' ');
}

/* If the option REAL has an argument, we print it in using the printf
   format REQ_FMT or OPT_FMT depending on whether it's a required or
   optional argument.  */
static void
arg (const struct argp_option *real, const char *req_fmt, const char *opt_fmt,
     argp_fmtstream_t stream)
{
  if (real->arg)
    if (real->flags & OPTION_ARG_OPTIONAL)
      __argp_fmtstream_printf (stream, opt_fmt, real->arg);
    else
      __argp_fmtstream_printf (stream, req_fmt, real->arg);
}

/* Helper functions for hol_entry_help.  */

/* Some state used while printing a help entry (used to communicate with
   helper functions).  See the doc for hol_entry_help for more info, as most
   of the fields are copied from its arguments.  */
struct pentry_state
{
  const struct hol_entry *entry;
  argp_fmtstream_t stream;
  struct hol_entry **prev_entry;
  int *sep_groups;

  int first;			/* True if nothing's been printed so far.  */
};

/* Prints STR as a header line, with the margin lines set appropiately, and
   notes the fact that groups should be separated with a blank line.  Note
   that the previous wrap margin isn't restored, but the left margin is reset
   to 0.  */
static void
print_header (const char *str, struct pentry_state *st)
{
  if (*str)
    {
      if (st->prev_entry && *st->prev_entry)
	__argp_fmtstream_putc (st->stream, '\n'); /* Precede with a blank line.  */
      indent_to (st->stream, HEADER_COL);
      __argp_fmtstream_set_lmargin (st->stream, HEADER_COL);
      __argp_fmtstream_set_wmargin (st->stream, HEADER_COL);
      __argp_fmtstream_puts (st->stream, str);
      __argp_fmtstream_set_lmargin (st->stream, 0);
    }

  if (st->sep_groups)
    *st->sep_groups = 1;	/* Separate subsequent groups. */
}

/* Inserts a comma if this isn't the first item on the line, and then makes
   sure we're at least to column COL.  Also clears FIRST.  */
static void
comma (unsigned col, struct pentry_state *st)
{
  if (st->first)
    {
      const struct hol_entry *pe = st->prev_entry ? *st->prev_entry : 0;
      const struct hol_cluster *cl = st->entry->cluster;

      if (st->sep_groups && *st->sep_groups
	  && pe && st->entry->group != pe->group)
	__argp_fmtstream_putc (st->stream, '\n');

      if (pe && cl && pe->cluster != cl && cl->header && *cl->header
	  && !hol_cluster_is_child (pe->cluster, cl))
	/* If we're changing clusters, then this must be the start of the
	   ENTRY's cluster unless that is an ancestor of the previous one
	   (in which case we had just popped into a sub-cluster for a bit).
	   If so, then print the cluster's header line.  */
	{
	  int old_wm = __argp_fmtstream_wmargin (st->stream);
	  print_header (cl->header, st);
	  __argp_fmtstream_putc (st->stream, '\n');
	  __argp_fmtstream_set_wmargin (st->stream, old_wm);
	}

      st->first = 0;
    }
  else
    __argp_fmtstream_puts (st->stream, ", ");

  indent_to (st->stream, col);
}

/* Print help for ENTRY to STREAM.  *PREV_ENTRY should contain the last entry
   printed before this, or null if it's the first, and if ENTRY is in a
   different group, and *SEP_GROUPS is true, then a blank line will be
   printed before any output.  *SEP_GROUPS is also set to true if a
   user-specified group header is printed.  */
static void
hol_entry_help (struct hol_entry *entry, argp_fmtstream_t stream,
		struct hol_entry **prev_entry, int *sep_groups)
{
  unsigned num;
  const struct argp_option *real = entry->opt, *opt;
  char *so = entry->short_options;
  int old_lm = __argp_fmtstream_set_lmargin (stream, 0);
  int old_wm = __argp_fmtstream_wmargin (stream);
  struct pentry_state pest = { entry, stream, prev_entry, sep_groups, 1 };

  /* First emit short options.  */
  __argp_fmtstream_set_wmargin (stream, SHORT_OPT_COL); /* For truly bizarre cases. */
  for (opt = real, num = entry->num; num > 0; opt++, num--)
    if (oshort (opt) && opt->key == *so)
      /* OPT has a valid (non shadowed) short option.  */
      {
	if (ovisible (opt))
	  {
	    comma (SHORT_OPT_COL, &pest);
	    __argp_fmtstream_putc (stream, '-');
	    __argp_fmtstream_putc (stream, *so);
	    arg (real, " %s", "[%s]", stream);
	  }
	so++;
      }

  /* Now, long options.  */
  if (odoc (real))
    /* Really a `documentation' option.  */
    {
      __argp_fmtstream_set_wmargin (stream, DOC_OPT_COL);
      for (opt = real, num = entry->num; num > 0; opt++, num--)
	if (opt->name && ovisible (opt))
	  {
	    comma (DOC_OPT_COL, &pest);
	    __argp_fmtstream_puts (stream, opt->name);
	  }
    }
  else
    /* A realy long option.  */
    {
      __argp_fmtstream_set_wmargin (stream, LONG_OPT_COL);
      for (opt = real, num = entry->num; num > 0; opt++, num--)
	if (opt->name && ovisible (opt))
	  {
	    comma (LONG_OPT_COL, &pest);
	    __argp_fmtstream_printf (stream, "--%s", opt->name);
	    arg (real, "=%s", "[=%s]", stream);
	  }
    }

  __argp_fmtstream_set_lmargin (stream, 0);
  if (pest.first)
    /* Didn't print any switches, what's up?  */
    if (!oshort (real) && !real->name && real->doc)
      /* This is a group header, print it nicely.  */
      print_header (real->doc, &pest);
    else
      /* Just a totally shadowed option or null header; print nothing.  */
      goto cleanup;		/* Just return, after cleaning up.  */
  else if (real->doc)
    /* Now the option documentation.  */
    {
      unsigned col = __argp_fmtstream_point (stream);
      const char *doc = real->doc;

      __argp_fmtstream_set_lmargin (stream, OPT_DOC_COL);
      __argp_fmtstream_set_wmargin (stream, OPT_DOC_COL);

      if (col > OPT_DOC_COL + 3)
	__argp_fmtstream_putc (stream, '\n');
      else if (col >= OPT_DOC_COL)
	__argp_fmtstream_puts (stream, "   ");
      else
	indent_to (stream, OPT_DOC_COL);

      __argp_fmtstream_puts (stream, gettext (doc));

      /* Reset the left margin.  */
      __argp_fmtstream_set_lmargin (stream, 0);
    }

  __argp_fmtstream_putc (stream, '\n');

  if (prev_entry)
    *prev_entry = entry;

cleanup:
  __argp_fmtstream_set_lmargin (stream, old_lm);
  __argp_fmtstream_set_wmargin (stream, old_wm);
}

/* Output a long help message about the options in HOL to STREAM.  */
static void
hol_help (struct hol *hol, argp_fmtstream_t stream)
{
  unsigned num;
  struct hol_entry *entry;
  struct hol_entry *last_entry = 0;
  int sep_groups = 0;		/* True if we should separate different
				   sections with blank lines.   */
  for (entry = hol->entries, num = hol->num_entries; num > 0; entry++, num--)
    hol_entry_help (entry, stream, &last_entry, &sep_groups);
}

/* Helper functions for hol_usage.  */

/* If OPT is a short option without an arg, append its key to the string
   pointer pointer to by COOKIE, and advance the pointer.  */
static int
add_argless_short_opt (const struct argp_option *opt,
		       const struct argp_option *real,
		       void *cookie)
{
  char **snao_end = cookie;
  if (! (opt->arg || real->arg))
    *(*snao_end)++ = opt->key;
  return 0;
}

/* If OPT is a short option with an arg, output a usage entry for it to the
   stream pointed at by COOKIE.  */
static int
usage_argful_short_opt (const struct argp_option *opt,
			const struct argp_option *real,
			void *cookie)
{
  argp_fmtstream_t stream = cookie;
  const char *arg = opt->arg;

  if (! arg)
    arg = real->arg;

  if (arg)
    if ((opt->flags | real->flags) & OPTION_ARG_OPTIONAL)
      __argp_fmtstream_printf (stream, " [-%c[%s]]", opt->key, arg);
    else
      {
	/* Manually do line wrapping so that it (probably) won't
	   get wrapped at the embedded space.  */
	if (__argp_fmtstream_point (stream) + 6 + strlen (arg)
	    >= __argp_fmtstream_rmargin (stream))
	  __argp_fmtstream_putc (stream, '\n');
	else
	  __argp_fmtstream_putc (stream, ' ');
	__argp_fmtstream_printf (stream, "[-%c %s]", opt->key, arg);
      }

  return 0;
}

/* Output a usage entry for the long option opt to the stream pointed at by
   COOKIE.  */
static int
usage_long_opt (const struct argp_option *opt,
		const struct argp_option *real,
		void *cookie)
{
  argp_fmtstream_t stream = cookie;
  const char *arg = opt->arg;

  if (! arg)
    arg = real->arg;

  if (arg)
    if ((opt->flags | real->flags) & OPTION_ARG_OPTIONAL)
      __argp_fmtstream_printf (stream, " [--%s[=%s]]", opt->name, arg);
    else
      __argp_fmtstream_printf (stream, " [--%s=%s]", opt->name, arg);
  else
    __argp_fmtstream_printf (stream, " [--%s]", opt->name);

  return 0;
}

/* Print a short usage description for the arguments in HOL to STREAM.  */
static void
hol_usage (struct hol *hol, argp_fmtstream_t stream)
{
  if (hol->num_entries > 0)
    {
      unsigned nentries;
      struct hol_entry *entry;
      char *short_no_arg_opts = alloca (strlen (hol->short_options) + 1);
      char *snao_end = short_no_arg_opts;

      /* First we put a list of short options without arguments.  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	hol_entry_short_iterate (entry, add_argless_short_opt, &snao_end);
      if (snao_end > short_no_arg_opts)
	{
	  *snao_end++ = 0;
	  __argp_fmtstream_printf (stream, " [-%s]", short_no_arg_opts);
	}

      /* Now a list of short options *with* arguments.  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	hol_entry_short_iterate (entry, usage_argful_short_opt, stream);

      /* Finally, a list of long options (whew!).  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	hol_entry_long_iterate (entry, usage_long_opt, stream);
    }
}

/* Make a HOL containing all levels of options in ARGP.  CLUSTER is the
   cluster in which ARGP's entries should be clustered, or 0.  */
static struct hol *
argp_hol (const struct argp *argp, struct hol_cluster *cluster)
{
  const struct argp_child *child = argp->children;
  struct hol *hol = make_hol (argp->options, cluster);
  if (child)
    while (child->argp)
      {
	struct hol_cluster *child_cluster =
	  ((child->group || child->header)
	   /* Put CHILD->argp within its own cluster.  */
	   ? hol_add_cluster (hol, child->group, child->header,
			      child - argp->children, cluster)
	   /* Just merge it into the parent's cluster.  */
	   : cluster);
	hol_append (hol, argp_hol (child->argp, child_cluster)) ;
	child++;
      }
  return hol;
}

/* Calculate how many different levels with alternative args strings exist in
   ARGP.  */
static size_t
argp_args_levels (const struct argp *argp)
{
  size_t levels = 0;
  const struct argp_child *child = argp->children;

  if (argp->args_doc && strchr (argp->args_doc, '\n'))
    levels++;

  if (child)
    while (child->argp)
      levels += argp_args_levels ((child++)->argp);

  return levels;
}

/* Print all the non-option args documented in ARGP to STREAM.  Any output is
   preceded by a space.  LEVELS is a pointer to a byte vector the length
   returned by argp_args_levels; it should be initialized to zero, and
   updated by this routine for the next call if ADVANCE is true.  True is
   returned as long as there are more patterns to output.  */
static int
argp_args_usage (const struct argp *argp, char **levels, int advance,
		 argp_fmtstream_t stream)
{
  char *our_level = *levels;
  int multiple = 0;
  const struct argp_child *child = argp->children;
  const char *doc = argp->args_doc, *nl = 0;

  if (doc)
    {
      nl = strchr (doc, '\n');
      if (nl)
	/* This is a `multi-level' args doc; advance to the correct position
	   as determined by our state in LEVELS, and update LEVELS.  */
	{
	  int i;
	  multiple = 1;
	  for (i = 0; i < *our_level; i++)
	    doc = nl + 1, nl = strchr (doc, '\n');
	  (*levels)++;
	}
      if (! nl)
	nl = doc + strlen (doc);

      /* Manually do line wrapping so that it (probably) won't get wrapped at
	 any embedded spaces.  */
      if (__argp_fmtstream_point (stream) + 1 + nl - doc
	  >= __argp_fmtstream_rmargin (stream))
	__argp_fmtstream_putc (stream, '\n');
      else
	__argp_fmtstream_putc (stream, ' ');

      __argp_fmtstream_write (stream, doc, nl - doc);
    }

  if (child)
    while (child->argp)
      advance = !argp_args_usage ((child++)->argp, levels, advance, stream);

  if (advance && multiple)
    /* Need to increment our level.  */
    if (*nl)
      /* There's more we can do here.  */
      {
	(*our_level)++;
	advance = 0;		/* Our parent shouldn't advance also. */
      }
    else if (*our_level > 0)
      /* We had multiple levels, but used them up; reset to zero.  */
      *our_level = 0;

  return !advance;
}

/* Print the documentation for ARGP to STREAM; if POST is false, then
   everything preceeding a `\v' character in the documentation strings (or
   the whole string, for those with none) is printed, otherwise, everything
   following the `\v' character (nothing for strings without).  Each separate
   bit of documentation is separated a blank line, and if PRE_BLANK is true,
   then the first is as well.  If FIRST_ONLY is true, only the first
   occurance is output.  Returns true if anything was output.  */
static int
argp_doc (const struct argp *argp, int post, int pre_blank, int first_only,
	  argp_fmtstream_t stream)
{
  const struct argp_child *child = argp->children;
  const char *doc = argp->doc;
  int anything = 0;

  if (doc)
    {
      char *vt = strchr (doc, '\v');

      if (pre_blank && (vt || !post))
	__argp_fmtstream_putc (stream, '\n');

      if (vt)
	if (post)
	  __argp_fmtstream_puts (stream, vt + 1);
	else
	  __argp_fmtstream_write (stream, doc, vt - doc);
      else
	if (! post)
	  __argp_fmtstream_puts (stream, doc);
      if (__argp_fmtstream_point (stream) > __argp_fmtstream_lmargin (stream))
	__argp_fmtstream_putc (stream, '\n');

      anything = 1;
    }
  if (child)
    while (child->argp && !(first_only && anything))
      anything |=
	argp_doc ((child++)->argp, post, anything || pre_blank, first_only,
		  stream);

  return anything;
}

/* Output a usage message for ARGP to STREAM.  FLAGS are from the set
   ARGP_HELP_*.  NAME is what to use wherever a `program name' is needed. */
void __argp_help (const struct argp *argp, FILE *stream,
		  unsigned flags, char *name)
{
  int anything = 0;		/* Whether we've output anything.  */
  struct hol *hol = 0;
  argp_fmtstream_t fs;

  if (! stream)
    return;

  fs = __argp_make_fmtstream (stream, 0, RMARGIN, 0);
  if (! fs)
    return;

  if (flags & (ARGP_HELP_USAGE | ARGP_HELP_SHORT_USAGE | ARGP_HELP_LONG))
    {
      hol = argp_hol (argp, 0);

      /* If present, these options always come last.  */
      hol_set_group (hol, "help", -1);
      hol_set_group (hol, "version", -1);

      hol_sort (hol);
    }

  if (flags & (ARGP_HELP_USAGE | ARGP_HELP_SHORT_USAGE))
    /* Print a short `Usage:' message.  */
    {
      int first_pattern = 1, more_patterns;
      size_t num_pattern_levels = argp_args_levels (argp);
      char *pattern_levels = alloca (num_pattern_levels);

      memset (pattern_levels, 0, num_pattern_levels);

      do
	{
	  int old_lm;
	  int old_wm = __argp_fmtstream_set_wmargin (fs, USAGE_INDENT);
	  char *levels = pattern_levels;

	  __argp_fmtstream_printf (fs, "%s %s",
				   gettext (first_pattern
					    ? "Usage:" : "  or: "), name);

	  /* We set the lmargin as well as the wmargin, because hol_usage
	     manually wraps options with newline to avoid annoying breaks.  */
	  old_lm = __argp_fmtstream_set_lmargin (fs, USAGE_INDENT);

	  if (flags & ARGP_HELP_SHORT_USAGE)
	    /* Just show where the options go.  */
	    {
	      if (hol->num_entries > 0)
		__argp_fmtstream_puts (fs, gettext (" [OPTION...]"));
	    }
	  else
	    /* Actually print the options.  */
	    {
	      hol_usage (hol, fs);
	      flags |= ARGP_HELP_SHORT_USAGE; /* But only do so once.  */
	    }

	  more_patterns = argp_args_usage (argp, &levels, 1, fs);

	  __argp_fmtstream_set_wmargin (fs, old_wm);
	  __argp_fmtstream_set_lmargin (fs, old_lm);

	  __argp_fmtstream_putc (fs, '\n');
	  anything = 1;

	  first_pattern = 0;
	}
      while (more_patterns);
    }

  if (flags & ARGP_HELP_PRE_DOC)
    anything |= argp_doc (argp, 0, 0, 1, fs);

  if (flags & ARGP_HELP_SEE)
    {
      __argp_fmtstream_printf (fs, gettext ("\
Try `%s --help' or `%s --usage' for more information.\n"),
					    name, name);
      anything = 1;
    }

  if (flags & ARGP_HELP_LONG)
    /* Print a long, detailed help message.  */
    {
      /* Print info about all the options.  */
      if (hol->num_entries > 0)
	{
	  if (anything)
	    __argp_fmtstream_putc (fs, '\n');
	  hol_help (hol, fs);
	  anything = 1;
	}
    }

  if (flags & ARGP_HELP_POST_DOC)
    /* Print any documentation strings at the end.  */
    anything |= argp_doc (argp, 1, anything, 0, fs);

  if ((flags & ARGP_HELP_BUG_ADDR) && argp_program_bug_address)
    {
      if (anything)
	__argp_fmtstream_putc (fs, '\n');
      __argp_fmtstream_printf (fs, gettext ("Report bugs to %s.\n"),
			       argp_program_bug_address);
      anything = 1;
    }

  if (hol)
    hol_free (hol);

  __argp_fmtstream_free (fs);
}
#ifdef weak_alias
weak_alias (__argp_help, argp_help)
#endif

/* Output, if appropriate, a usage message for STATE to STREAM.  FLAGS are
   from the set ARGP_HELP_*.  */
void
__argp_state_help (struct argp_state *state, FILE *stream, unsigned flags)
{
  if ((!state || ! (state->flags & ARGP_NO_ERRS)) && stream)
    {
      if (state && (state->flags & ARGP_LONG_ONLY))
	flags |= ARGP_HELP_LONG_ONLY;

      __argp_help (state ? state->argp : 0, stream, flags,
		   state ? state->name : program_invocation_name);

      if (!state || ! (state->flags & ARGP_NO_EXIT))
	{
	  if (flags & ARGP_HELP_EXIT_ERR)
	    exit (1);
	  if (flags & ARGP_HELP_EXIT_OK)
	    exit (0);
	}
  }
}
#ifdef weak_alias
weak_alias (__argp_state_help, argp_state_help)
#endif

/* If appropriate, print the printf string FMT and following args, preceded
   by the program name and `:', to stderr, and followed by a `Try ... --help'
   message, then exit (1).  */
void
__argp_error (struct argp_state *state, const char *fmt, ...)
{
  if (!state || !(state->flags & ARGP_NO_ERRS))
    {
      FILE *stream = state ? state->err_stream : stderr;

      if (stream)
	{
	  va_list ap;

	  fputs (state ? state->name : program_invocation_name, stream);
	  putc (':', stream);
	  putc (' ', stream);

	  va_start (ap, fmt);
	  vfprintf (stream, fmt, ap);
	  va_end (ap);

	  putc ('\n', stream);

	  __argp_state_help (state, stream, ARGP_HELP_STD_ERR);
	}
    }
}
#ifdef weak_alias
weak_alias (__argp_error, argp_error)
#endif

/* Similar to the standard gnu error-reporting function error(), but will
   respect the ARGP_NO_EXIT and ARGP_NO_ERRS flags in STATE, and will print
   to STATE->err_stream.  This is useful for argument parsing code that is
   shared between program startup (when exiting is desired) and runtime
   option parsing (when typically an error code is returned instead).  The
   difference between this function and argp_error is that the latter is for
   *parsing errors*, and the former is for other problems that occur during
   parsing but don't reflect a (syntactic) problem with the input.  */
void
__argp_failure (struct argp_state *state, int status, int errnum,
	      const char *fmt, ...)
{
  if (!state || !(state->flags & ARGP_NO_ERRS))
    {
      FILE *stream = state ? state->err_stream : stderr;

      if (stream)
	{
	  fputs (state ? state->name : program_invocation_name, stream);

	  if (fmt)
	    {
	      va_list ap;

	      putc (':', stream);
	      putc (' ', stream);

	      va_start (ap, fmt);
	      vfprintf (stream, fmt, ap);
	      va_end (ap);
	    }

	  if (errnum)
	    {
	      putc (':', stream);
	      putc (' ', stream);
	      fputs (strerror (errnum), stream);
	    }

	  putc ('\n', stream);

	  if (status)
	    exit (status);
	}
    }
}
#ifdef weak_alias
weak_alias (__argp_failure, argp_failure)
#endif
