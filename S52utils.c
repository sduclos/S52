// S52utils.c: utility
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2016 Sylvain Duclos sduclos@users.sourceforge.net

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

#include <glib.h>          // g_get_current_time()
#include <glib/gprintf.h>  // g_strrstr()
#include <glib/gstdio.h>   // FILE

#include <stdlib.h>        // atoi(), atof()
#include <string.h>        // strlen()
#include <unistd.h>        // write()

// debug - configuration file
#ifdef S52_USE_ANDROID
#define CONF_NAME   "/sdcard/s52droid/s52.cfg"
#else
#define CONF_NAME   "s52.cfg"
#endif

#ifdef S52_USE_LOGFILE
static gint       _logFile = 0;
static GTimeVal   _now;
typedef void (*GPrintFunc)(const gchar *string);
static GPrintFunc _oldPrintHandler = NULL;
#endif

static S52_log_cb _log_cb          = NULL;

// internal libS52.so version
static const char _version[] = "libS52-2016FEB19-1.168"
#ifdef  _MINGW
      ",_MINGW"
#endif
#ifdef  S52_USE_GV
      ",S52_USE_GV"
#endif
#ifdef  GV_USE_DOUBLE_PRECISION_COORD
      ",GV_USE_DOUBLE_PRECISION_COORD"
#endif
#ifdef  S52_USE_OGR_FILECOLLECTOR
      ",S52_USE_OGR_FILECOLLECTOR"
#endif
#ifdef  S52_USE_PROJ
      ",S52_USE_PROJ"
#endif
#ifdef  S52_USE_SUPP_LINE_OVERLAP
      ",S52_USE_SUPP_LINE_OVERLAP"
#endif
#ifdef  S52_DEBUG
      ",S52_DEBUG"
#endif
#ifdef  S52_USE_LOG
      ",S52_USE_LOG"
#endif
#ifdef  S52_USE_LOGFILE
      ",S52_USE_LOGFILE"
#endif
#ifdef  S52_USE_DBUS
      ",S52_USE_DBUS"
#endif
#ifdef  S52_USE_SOCK
      ",S52_USE_SOCK"
#endif
#ifdef  S52_USE_GOBJECT
      ",S52_USE_GOBJECT"
#endif
#ifdef  S52_USE_BACKTRACE
      ",S52_USE_BACKTRACE"
#endif
#ifdef  S52_USE_EGL
      ",S52_USE_EGL"
#endif 
#ifdef  S52_USE_GL1
      ",S52_USE_GL1"
#endif
#ifdef  S52_USE_OPENGL_VBO
      ",S52_USE_OPENGL_VBO"
#endif
#ifdef  S52_USE_GLSC1
      ",S52_USE_GLSC1"
#endif
#ifdef  S52_USE_GL2
      ",S52_USE_GL2"
#endif
#ifdef  S52_USE_GLES2
      ",S52_USE_GLES2"
#endif
#ifdef  S52_USE_ANDROID
      ",S52_USE_ANDROID"
#endif
#ifdef  S52_USE_TEGRA2
      ",S52_USE_TEGRA2"
#endif
#ifdef  S52_USE_ADRENO
      ",S52_USE_ADRENO"
#endif
#ifdef  S52_USE_COGL
      ",S52_USE_COGL"
#endif
#ifdef  S52_USE_FREETYPE_GL
      ",S52_USE_FREETYPE_GL"
#endif
#ifdef  S52_USE_SYM_AISSEL01
      ",S52_USE_SYM_AISSEL01"
#endif
#ifdef  S52_USE_WORLD
      ",S52_USE_WORLD"
#endif
#ifdef  S52_USE_SYM_VESSEL_DNGHL
      ",S52_USE_SYM_VESSEL_DNGHL"
#endif
#ifdef  S52_USE_TXT_SHADOW
      ",S52_USE_TXT_SHADOW"
#endif
#ifdef  S52_USE_RADAR
      ",S52_USE_RADAR"
#endif
#ifdef  S52_USE_MESA3D
      ",S52_USE_MESA3D"
#endif
#ifdef  S52_USE_C_AGGR_C_ASSO
      ",S52_USE_C_AGGR_C_ASSO"
#endif
"\n";

cchar   *S52_utils_version(void)
{
    return _version;
}

int      S52_utils_getConfig(cchar *label, valueBuf *vbuf)
// return TRUE and string value in vbuf for label, FALSE if fail
{
   FILE *fp;
   int  ret;
   //char lbuf[PATH_MAX];
   //char frmt[PATH_MAX];
   char lbuf[MAXL];
   char frmt[MAXL];

   //fp = fopen(CONF_NAME, "r");
   fp = g_fopen(CONF_NAME, "r");
   if (NULL == fp) {
       PRINTF("WARNING: conf not found: %s\n", CONF_NAME);
       return FALSE;
   }


   // prevent buffer overflow
   //sprintf(frmt, "%s%i%s", " %s %", MAXL-1, "[^\n]s");
   SNPRINTF(frmt, MAXL, "%s%i%s", " %s %", MAXL-1, "[^\n]s");
   //printf("frmt:%s\n", frmt);

   ret = fscanf(fp, frmt, lbuf, vbuf);
   while (ret > 0) {
       if (('#'!=lbuf[0]) && (0==strncmp(lbuf, label, strlen(label)))) {
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

void _printf(const char *file, int line, const char *function, const char *frmt, ...)
{
    //S52GL.c:6020 in _contextValid(): Renderer:   Gallium 0.4 on AMD CEDAR
    //S52GL.c:6021 in _contextValid(): Version:    3.0 Mesa 10.1.3
    //S52GL.c:6022 in _contextValid(): Shader:     1.30
    // this driver need a buffer of 5K to fit all extesion string

    //const int MAX = 1024 + 1024 + 1024 + 1024;
    //const
    int MAX = 1024 + 1024 + 1024 + 1024 + 1024;
    char buf[MAX];
    snprintf(buf, MAX, "%s:%i in %s(): ", file, line, function);

	int size = (int) strlen(buf);
    if (size < MAX) {
		va_list argptr;
		va_start(argptr, frmt);
		int n = vsnprintf(&buf[size], (MAX-size), frmt, argptr);
        va_end(argptr);

        if (n > (MAX-size)) {
            g_print("WARNING: _printf(): string buffer FULL, str len:%i, buf len:%i\n", n, (MAX-size));
            g_assert(0);
        }
	}

    g_print("%s", buf);
}

#ifdef S52_USE_LOGFILE
static void     _S52_printf(cchar *string)
{
    char str[MAXL];
    g_get_current_time(&_now);

    snprintf(str, MAXL-1, "%s %s", g_time_val_to_iso8601(&_now), string);

    // if user set a callback .. call it
    if (NULL != _log_cb) {
        _log_cb(str);
    }

    // log to file
    if (NULL != _logFile) {
        write(_logFile, str, strlen(str));
    }

    // STDOUT
    g_printf("%s", str);
}
#endif

int      S52_utils_initLog(S52_log_cb log_cb)
// set print handler
// set tmp log file
{
    _log_cb = log_cb;

#ifdef S52_USE_LOGFILE
    GError *error = NULL;
    _logFile = g_file_open_tmp("XXXXXX", NULL, &error);
    if (-1 == _logFile) {
        PRINTF("WARNING: g_file_open_tmp(): failed\n");
    } else {
        PRINTF("DEBUG: tmp dir:%s\n", g_get_tmp_dir());
    }
    if (NULL != error) {
        g_printf("WARNING: g_file_open_tmp() failed (%s)\n", error->message);
        g_error_free(error);
    }

    _oldPrintHandler = g_set_print_handler(_S52_printf);

#else
    PRINTF("DEBUG: no LOGFILE, compiler flags 'S52_USE_LOGFILE' not set\n");
#endif

    return TRUE;
}

int      S52_utils_doneLog()
{
    _log_cb = NULL;

#ifdef S52_USE_LOGFILE
    g_set_print_handler(_oldPrintHandler);
    _oldPrintHandler = NULL;

    if (0 != _logFile)
        close(_logFile);
#endif

    return TRUE;
}

int      S52_atoi(cchar *str)
// safe atoi()
// use for parsing the PLib and S57 attribute
{
    if (NULL == str) {
        PRINTF("WARNING: NULL string\n");
        g_assert(0);
        return 0;
    }

    if (0 == strlen(str)) {
        PRINTF("WARNING: zero length string\n");
        g_assert(0);
        return 0;
    }

    // the to (int) might not be such a great idea!  (no rounding)
    //return (int)S52_atof(str);
    //return (int)g_strtod(str, NULL);
    return atoi(str);
}

double   S52_atof(cchar *str)
// safe atof()
{
    if (NULL == str) {
        PRINTF("WARNING: NULL string\n");
        g_assert(0);
        return 0;
    }

    if (0 == strlen(str)) {
        PRINTF("WARNING: zero length string\n");
        g_assert(0);
        return 0;
    }

    //return g_strtod(str, NULL);
    return atof(str);
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
