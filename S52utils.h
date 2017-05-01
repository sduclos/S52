// S52utils.h: utility
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2017 Sylvain Duclos sduclos@users.sourceforge.net

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

#include "S52.h"          // S52_log_cb

#ifdef SOLARIS
// well should be cc
#define PRINTF printf(__FILE__":%i: : ", __LINE__),printf

#else  // SOLARIS

#if defined(S52_DEBUG) || defined(S52_USE_LOGFILE)
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// FIXME: filter msg type: NOTE:, DEBUG:, FIXME:, WARNING:, ERROR:
void _printf(const char *file, int line, const char *function, const char *frmt, ...);
#define PRINTF(...) _printf(__FILE__, __LINE__, __func__, __VA_ARGS__)
#else    // S52_DEBUG  S52_USE_LOGFILE
#define PRINTF(...)
#endif  // S52_DEBUG  S52_USE_LOGFILE
#endif  // SOLARIS

#define SNPRINTF(b,n,f, ...) if (n <= g_snprintf(b,n,f,__VA_ARGS__)) {PRINTF("WARNING: str overflow\n");g_assert(0);}

#define return_if_null(ptr)                  \
if (NULL==ptr) {                             \
    PRINTF("WARNING: '%s' is NULL\n", #ptr); \
    g_assert(0);                             \
    return FALSE;                            \
}


#define CCHAR const char


// debug: valid label in .cfg file
#define CFG_CATALOG  "CATALOG"
#define CFG_PLIB     "PLIB"
#define CFG_CHART    "CHART"
#define CFG_WORLD    "WORLD"
#define CFG_TTF      "TTF"

#define MAXL 1024    // MAX lenght of buffer _including_ '\0'
typedef char valueBuf[MAXL];

int      S52_utils_getConfig(CCHAR *label, char *vbuf);

CCHAR   *S52_utils_version(void);
int      S52_utils_initLog(S52_log_cb log_cb);
int      S52_utils_doneLog(void);

int      S52_atoi(CCHAR *str);
double   S52_atof(CCHAR *str);

// debug
char    *S52_utils_new0(size_t sz, int n);
#define _g_new0(s,n)  (s*)S52_utils_new0(sizeof(s), n)



///////////////////////////////////////////////////////////////////////////
//
// Other trick that could be usefull
//

// quiet compiler warning on unused param
#define UNUSED(expr) do { (void)(expr); } while (0)



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
