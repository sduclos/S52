// S52utils.c: utility
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2012  Sylvain Duclos sduclos@users.sourceforgue.net

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



#include "S52utils.h"

#include <glib.h>

#ifdef S52_USE_GLIB2
#include <glib/gprintf.h> // g_strrstr()
#include <glib/gstdio.h>  // FILE
#else
#include <stdio.h>        // FILE, fopen(), ...
#include <stdlib.h>       // atof(), atoi()
#include <string.h>       // strstr(), strlen()
#endif

#include <string.h>       // strlen()
#include <stdio.h>        // asctime
#include <time.h>         // time
#include <unistd.h>       // write

// configuration file
#ifdef S52_USE_ANDROID
#define CONF_NAME   "/sdcard/s52android/s52.cfg"
#else
#define CONF_NAME   "s52.cfg"
#endif

#define NaN         (1.0/0.0)

//static FILE      *_log = NULL;
static gint _log = 0;

typedef void (*GPrintFunc)(const gchar *string);
static GPrintFunc   _oldPrintHandler = NULL;
static GTimeVal     _now;
static S52_error_cb _err_cb = NULL;

void g_get_current_time(GTimeVal *result);

void _printf(const char *file, int line, const char *function, const char *frmt, ...)
{
	char buf[256];
    snprintf(buf, 256, "%s:%i in %s(): ", file, line, function);

	int size = (int) strlen(buf);
	if (size < 256) {
		va_list argptr;
		va_start(argptr, frmt);
		vsnprintf(&buf[size], 256 - size, frmt, argptr);
		va_end(argptr);
	}

    g_print("%s", buf);
}

int      S52_getConfig(const char *label, valueBuf *vbuf)
// return TRUE and string value in vbuf for label, FLASE if fail
{
   FILE *fp;
   int  ret;
   //char lbuf[PATH_MAX];
   //char frmt[PATH_MAX];
   char lbuf[MAXL];
   char frmt[MAXL];

   fp = fopen(CONF_NAME, "r");
   if (NULL == fp) {
       PRINTF("WARNING: conf not found: %s\n", CONF_NAME);
       return FALSE;
   }


   // prevent buffer overflow
   //sprintf(frmt, "%s%i%s", " %s %", MAXL-1, "[^\n]s");
   SPRINTF(frmt, "%s%i%s", " %s %", MAXL-1, "[^\n]s");
   //printf("frmt:%s\n", frmt);

   ret = fscanf(fp, frmt, lbuf, vbuf);
   while (ret > 0) {
       //if (('#'!=lbuf[0]) && (0 == strncmp(lbuf, label, strlen(label)))) {
       if (('#'!=lbuf[0]) && (0 == S52_strncmp(lbuf, label, S52_strlen(label)))) {
               //sscanf(c, "%255[^\n]", *vbuf);
               PRINTF("label:%s value:%s \n", lbuf, *vbuf);
               fclose(fp);
               return TRUE;
       }

       ret = fscanf(fp, frmt, lbuf, vbuf);
       //ret = fscanf(fp, "%s%255[^\n]\n", lbuf, tmp);
       //printf("label:%s \n", lbuf);
       //printf("value:%s \n", tmp);
       //printf("ret:%i\n", ret);
   }
   fclose(fp);

   *vbuf[0] = '\0';
   return FALSE;
}

int      S52_atoi(const char *str)
// relacement of stdlib.h atoi
{
    //return atoi(str);

    // the to (int) might not be such a great idea!
    return (int)S52_atof(str);
}

double   S52_atof(const char *str)
// relacement of stdlib.h atof
{
    if (NULL == str)
        return (1.0/0.0); //nan

    if (0==S52_strlen(str)) {
        //PRINTF("WARNING: zero length string (inf)\n");
        //g_assert(0);
        return (1.0/0.0); //nan
    }


#ifdef S52_USE_GLIB2
    return g_strtod(str, NULL);
#else
    return atof(str);
#endif

}

//int    S52_strlen(const char *str)
size_t   S52_strlen(const char *str)
{
    return strlen(str);

//#ifdef S52_USE_GLIB2
//    return g_utf8_strlen(str, -1);
//#else
//    return strlen(str);
//#endif

    /*
    int len = 0;

    register const char *s;

    // BUG: int roll over
    for (s = str; *s; s++, len++)
        ;

    return len;
    */
}

//gchar* S52_strstr(const gchar *haystack, const gchar *needle)
char*    S52_strstr(const char *haystack, const char *needle)
{
#ifdef S52_USE_GLIB2
    return g_strrstr(haystack, needle);
#else
    return (char *)strstr(haystack, needle);
#endif
}

gint     S52_strncmp(const gchar *s1, const gchar *s2, gsize n)
{
#ifdef S52_USE_GLIB2
    return g_ascii_strncasecmp(s1, s2, n);
    //return g_strncasecmp(s1, s2, n);
    //return strncmp(s1, s2, n);
#else
    return strncmp(s1, s2, n);
#endif
}

FILE *   S52_fopen (const gchar *filename, const gchar *mode)
{
#ifdef S52_USE_GLIB2
    return g_fopen(filename, mode);
#else
    return fopen(filename, mode);
#endif
}

int      S52_fclose (FILE *fd)
{
//#ifdef S52_USE_GLIB2
//    return g_fclose(fd);
//#else
    return fclose(fd);
//#endif
}

gboolean S52_string_equal(const GString *v, const GString *v2)
{
#ifdef S52_USE_GLIB2
    // on android glid2 can't handle NULL string
    if (NULL==v || NULL==v2) {
        PRINTF("S52_string_equal():WARNING: string NULL\n");
        return FALSE;
    }
    return g_string_equal(v, v2);
#else
    if (v->len == v2->len) {
        if (0 == memcmp(v->str, v2->str, v->len))
            return TRUE;
    }
    return FALSE;
#endif

}

void     S52_tree_replace(GTree *tree, gpointer key, gpointer value)
{
#ifdef S52_USE_GLIB2
    g_tree_replace(tree, key, value);
#else
    //g_assert_not_reached();
    //g_tree_remove(tree, key);
    // WARNING: can't load mutiple PLib
    g_tree_insert(tree, key, value);
#endif
}

void     S52_printf(const gchar *string)
{
    char str[MAXL];
    g_get_current_time(&_now);

    snprintf(str, MAXL-1, "%s %s\n", g_time_val_to_iso8601(&_now), string);
    write(_log, str, strlen(str));

    if (NULL != _err_cb)
        _err_cb(str);
}

int      S52_initLog(S52_error_cb err_cb)
{
    _err_cb = err_cb;

#ifdef S52_USE_LOG
    printf("starting log (%s)\n", g_get_tmp_dir());

    _log = g_file_open_tmp("XXXXXX", NULL, NULL);
    if (-1 == _log)
        g_assert(0);

    _oldPrintHandler = g_set_print_handler(S52_printf);
    //S52_LOG("log started");
#endif


    return TRUE;
}

int      S52_doneLog()
{
    //S52_LOG("log finish");

    g_set_print_handler(_oldPrintHandler);
    if (0 != _log)
        close(_log);

    _err_cb = NULL;

    return TRUE;
}



//////////////////////
//
// from GIMP xyz2rgb.c
//
static double m2[3][3] =
{
  {  3.240479, -1.537150, -0.498535 },
  { -0.969256,  1.875992,  0.041556 },
  {  0.055648, -0.204043,  1.057311 }
};

int      S52_xyL2rgb(double *xr, double *yg, double *Lb)
{
  double R, G, B;

  double Y = *Lb;
  double X = (*xr / *yg) * Y;
  double Z = ((1 - *xr - *yg) / *yg) * Y;

  X = X / 100.0;
  Y = Y / 100.0;
  Z = Z / 100.0;


  R = m2[0][0] * X + m2[0][1] * Y + m2[0][2] * Z;
  G = m2[1][0] * X + m2[1][1] * Y + m2[1][2] * Z;
  B = m2[2][0] * X + m2[2][1] * Y + m2[2][2] * Z;

  //printf ("RGB = (%f %f %f) \n", R, G, B);

  R = (R<0.0)? 0.0 : (R>1.0)? 1.0 : R;
  G = (G<0.0)? 0.0 : (G>1.0)? 1.0 : G;
  B = (B<0.0)? 0.0 : (B>1.0)? 1.0 : B;

  *xr = R*255.0;
  *yg = G*255.0;;
  *Lb = B*255.0;;

  return 1;
}

