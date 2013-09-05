// S52utils.h: utility
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013 Sylvain Duclos sduclos@users.sourceforge.net

    OpENCview is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpENCview is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with OpENCview.  If not, see <http://www.gnu.org/licenses/>.
*/



#ifndef _S52UTILS_H_
#define _S52UTILS_H_

#include "S52.h"          // S52_error_cb

#include <glib.h>         // g_print()

#ifdef S52_USE_GLIB2
#include <glib/gstdio.h>  // FILE
#else
#include <stdio.h>        // FILE
#endif

#ifdef SOLARIS
    // well should be cc
#define PRINTF printf(__FILE__":%i: : ", __LINE__),printf

#else

#ifdef S52_DEBUG
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void _printf(const char *file, int line, const char *function, const char *frmt, ...);
#define PRINTF(...) _printf(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)


#else

#define PRINTF(...)
//NOOP
//static inline void NOOP(char *str, ...) {}

#endif
#endif

#ifdef S52_USE_GLIB2
#define SPRINTF g_sprintf
#else
#define SPRINTF sprintf
#endif

//                                 g_assert(0);
// GCC only (used in Linux kernel)
//#define likely(x)       __builtin_expect((x),1)
//#define unlikely(x)     __builtin_expect((x),0)
// they don't apply __builtin_expect to (x), but to !!(x).
// C considers any non-zero value as true in a conditional,
// but __builtin_expect specifies an exact value of 1; !! negates the value twice, forcing any non-zero value to become 1.
// The option -fguess-branch-probability offers the sa thing
// more at: http://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html#C-Extensions
#define return_if_null(ptr) if (NULL==ptr) {                             \
                                PRINTF("WARNING: '%s' is NULL\n", #ptr); \
                                return FALSE;                            \
                            }


#define cchar const char


// debug: valid label in .cfg file
#define CONF_CATALOG  "CATALOG"
#define CONF_PLIB     "PLIB"
#define CONF_CHART    "CHART"
#define CONF_WORLD    "WORLD"

#define MAXL 1024    // MAX lenght of buffer _including_ '\0'
typedef char    valueBuf[MAXL];

int      S52_getConfig(const char *label, valueBuf *vbuf);

double   S52_atof   (const char *str);
int      S52_atoi   (const char *str);
size_t   S52_strlen (const char *str);
char*    S52_strstr (const char *haystack, const char *needle);
gint     S52_strncmp(const char *s1, const char *s2, gsize n);
FILE *   S52_fopen  (const char *filename, const char *mode);
int      S52_fclose (FILE *fd);
gboolean S52_string_equal(const GString *v, const GString *v2);

void     S52_tree_replace(GTree *tree, gpointer key, gpointer value);

int      S52_initLog(S52_error_cb err_cb);
int      S52_doneLog();








/*
 * Helper macros to use CONFIG_ options in C/CPP expressions. Note that
 * these only work with boolean and tristate options.
 */

/*
 * Getting something that works in C and CPP for an arg that may or may
 * not be defined is tricky.  Here, if we have "#define CONFIG_BOOGER 1"
 * we match on the placeholder define, insert the "0," for arg1 and generate
 * the triplet (0, 1, 0).  Then the last step cherry picks the 2nd arg (a one).
 * When CONFIG_BOOGER is not defined, we generate a (... 1, 0) pair, and when
 * the last step cherry picks the 2nd arg, we get a zero.
 */

#define __ARG_PLACEHOLDER_1 0,
#define config_enabled(cfg) _config_enabled(cfg)
#define _config_enabled(value) __config_enabled(__ARG_PLACEHOLDER_##value)
#define __config_enabled(arg1_or_junk) ___config_enabled(arg1_or_junk 1, 0)
#define ___config_enabled(__ignored, val, ...) val


#endif // _S52UTILS_H_
