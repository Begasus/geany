/*
*   Copyright (c) 1996-2003, Darren Hiebert
*
*   Author: Darren Hiebert <dhiebert@users.sourceforge.net>
*           http://ctags.sourceforge.net
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*   It is provided on an as-is basis and no responsibility is accepted for its
*   failure to perform as expected.
*
*   This is a reimplementation of the ctags (1) program. It is an attempt to
*   provide a fully featured ctags program which is free of the limitations
*   which most (all?) others are subject to.
*
*   This module contains the start-up code and routines to determine the list
*   of files to parsed for tags.
*/

/*
*   INCLUDE FILES
*/
#include "general.h"  /* must always come first */

#include <glib.h>

/*  To provide timings features if available.
 */
#ifdef HAVE_CLOCK
# ifdef HAVE_TIME_H
#  include <time.h>
# endif
#else
# ifdef HAVE_TIMES
#  ifdef HAVE_SYS_TIMES_H
#   include <sys/times.h>
#  endif
# endif
#endif

/*  To provide directory searching for recursion feature.
 */

#ifdef HAVE_DIRENT_H
# ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>  /* required by dirent.h */
# endif
# include <dirent.h>  /* to declare opendir() */
# undef boolean
#endif
#ifdef HAVE_DIRECT_H
# include <direct.h>  /* to _getcwd() */
#endif
#ifdef HAVE_DIR_H
# include <dir.h>  /* to declare findfirst() and findnext */
#endif
#ifdef HAVE_IO_H
# include <io.h>  /* to declare _findfirst() */
#endif

#include <string.h>

#include "debug.h"
#include "entry.h"
#include "keyword.h"
#include "main.h"
#include "options.h"
#include "read.h"
#include "routines.h"

/*
*   MACROS
*/
#define plural(value)           (((unsigned long)(value) == 1L) ? "" : "s")

/*
*   DATA DEFINITIONS
*/
static struct { long files, lines, bytes; } Totals = { 0, 0, 0 };

/*
*   FUNCTION DEFINITIONS
*/

extern void addTotals (
		const unsigned int files, const long unsigned int lines,
		const long unsigned int bytes)
{
	Totals.files += files;
	Totals.lines += lines;
	Totals.bytes += bytes;
}

extern boolean isDestinationStdout (void)
{
	boolean toStdout = FALSE;

	if (Option.xref  ||  Option.filter  ||
		(Option.tagFileName != NULL  &&  (strcmp (Option.tagFileName, "-") == 0
						  || strcmp (Option.tagFileName, "/dev/stdout") == 0
		)))
		toStdout = TRUE;
	return toStdout;
}


# if defined (WIN32)

static boolean createTagsForMatchingEntries (char *const pattern)
{
	boolean resize = FALSE;
	const size_t dirLength = baseFilename (pattern) - (char *) pattern;
	vString *const filePath = vStringNew ();
#if defined (HAVE_FINDFIRST)
	struct ffblk fileInfo;
	int result = findfirst (pattern, &fileInfo, FA_DIREC);
	while (result == 0)
	{
		const char *const entryName = fileInfo.ff_name;

		/*  We must not recurse into the directories "." or "..".
		 */
		if (strcmp (entryName, ".") != 0  &&  strcmp (entryName, "..") != 0)
		{
			vStringNCopyS (filePath, pattern, dirLength);
			vStringCatS (filePath, entryName);
			resize |= createTagsForEntry (vStringValue (filePath));
		}
		result = findnext (&fileInfo);
	}
#elif defined (HAVE__FINDFIRST)
	struct _finddata_t fileInfo;
	long hFile = _findfirst (pattern, &fileInfo);

	if (hFile != -1L)
	{
		do
		{
			const char *const entryName = fileInfo.name;

			/*  We must not recurse into the directories "." or "..".
			 */
			if (strcmp (entryName, ".") != 0  &&  strcmp (entryName, "..") != 0)
			{
				vStringNCopyS (filePath, pattern, dirLength);
				vStringCatS (filePath, entryName);
				resize |= createTagsForEntry (vStringValue (filePath));
			}
		} while (_findnext (hFile, &fileInfo) == 0);
		_findclose (hFile);
	}
#endif

	vStringDelete (filePath);
	return resize;
}

#endif


static boolean recurseIntoDirectory (const char *const dirName)
{
	boolean resize = FALSE;
	if (isRecursiveLink (dirName))
		verbose ("ignoring \"%s\" (recursive link)\n", dirName);
	else if (! Option.recurse)
		verbose ("ignoring \"%s\" (directory)\n", dirName);
	else
	{
#if defined (HAVE_OPENDIR)
		DIR *const dir = opendir (dirName);
		if (dir == NULL)
			error (WARNING | PERROR, "cannot recurse into directory \"%s\"",
				   dirName);
		else
		{
			struct dirent *entry;
			verbose ("RECURSING into directory \"%s\"\n", dirName);
			while ((entry = readdir (dir)) != NULL)
			{
				if (strcmp (entry->d_name, ".") != 0  &&
					strcmp (entry->d_name, "..") != 0)
				{
					vString *filePath;
					if (strcmp (dirName, ".") == 0)
						filePath = vStringNewInit (entry->d_name);
					else
						filePath = combinePathAndFile (dirName, entry->d_name);
					resize |= createTagsForEntry (vStringValue (filePath));
					vStringDelete (filePath);
				}
			}
			closedir (dir);
		}
#elif defined (WIN32)
		vString *const pattern = vStringNew ();
		verbose ("RECURSING into directory \"%s\"\n", dirName);
		vStringCopyS (pattern, dirName);
		vStringPut (pattern, OUTPUT_PATH_SEPARATOR);
		vStringCatS (pattern, "*.*");
		resize = createTagsForMatchingEntries (vStringValue (pattern));
		vStringDelete (pattern);
#endif  /* HAVE_OPENDIR */
	}
	return resize;
}

#if defined (HAVE_CLOCK)
# define CLOCK_AVAILABLE
# ifndef CLOCKS_PER_SEC
#  define CLOCKS_PER_SEC		1000000
# endif
#elif defined (HAVE_TIMES)
# define CLOCK_AVAILABLE
# define CLOCKS_PER_SEC	60
static clock_t clock (void)
{
	struct tms buf;

	times (&buf);
	return (buf.tms_utime + buf.tms_stime);
}
#else
# define clock()  (clock_t)0
#endif

static void printTotals (const clock_t *const timeStamps)
{
	const unsigned long totalTags = TagFile.numTags.added +
									TagFile.numTags.prev;

	fprintf (errout, "%ld file%s, %ld line%s (%ld kB) scanned",
			Totals.files, plural (Totals.files),
			Totals.lines, plural (Totals.lines),
			Totals.bytes/1024L);
#ifdef CLOCK_AVAILABLE
	{
		const double interval = ((double) (timeStamps [1] - timeStamps [0])) /
								CLOCKS_PER_SEC;

		fprintf (errout, " in %.01f seconds", interval);
		if (interval != (double) 0.0)
			fprintf (errout, " (%lu kB/s)",
					(unsigned long) (Totals.bytes / interval) / 1024L);
	}
#endif
	fputc ('\n', errout);

	fprintf (errout, "%lu tag%s added to tag file",
			TagFile.numTags.added, plural (TagFile.numTags.added));
	if (Option.append)
		fprintf (errout, " (now %lu tags)", totalTags);
	fputc ('\n', errout);

	if (totalTags > 0  &&  Option.sorted)
	{
		fprintf (errout, "%lu tag%s sorted", totalTags, plural (totalTags));
#ifdef CLOCK_AVAILABLE
		fprintf (errout, " in %.02f seconds",
				((double) (timeStamps [2] - timeStamps [1])) / CLOCKS_PER_SEC);
#endif
		fputc ('\n', errout);
	}

#ifdef TM_DEBUG
	fprintf (errout, "longest tag line = %lu\n",
			(unsigned long) TagFile.max.line);
#endif
}

/* wrap g_warning so we don't include glib.h for all parsers, to keep compat with CTags */
void utils_warn(const char *msg)
{
	g_warning("%s", msg);
}

/* vi:set tabstop=4 shiftwidth=4: */
