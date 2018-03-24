// S52utils.c: utility
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2018 Sylvain Duclos sduclos@users.sourceforge.net

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
#define CFG_NAME   "/sdcard/s52droid/s52.cfg"
#else
#define CFG_NAME   "s52.cfg"
#endif

// user provided lob msg callback
static S52_log_cb _log_cb  = NULL;

//#if defined(S52_DEBUG) || defined(S52_USE_LOGFILE)
//static GTimeVal   _now;

#ifdef S52_USE_LOGFILE
static gint       _logFile = 0;
typedef void (*GPrintFunc)(const gchar *string);
static GPrintFunc _oldPrintHandler = NULL;
#endif  // S52_USE_LOGFILE

// trap signal (CTRL-C/SIGINT abort rendering)
// abort long running process in _draw(), _drawLast() and _suppLineOverlap()
// must be compiled with -std=gnu99 or -std=c99 -D_POSIX_C_SOURCE=199309L
//#include <unistd.h>      // getuid()
//#include <sys/types.h>
//#include <signal.h>      // siginfo_t
//#include <locale.h>      // setlocal()
static volatile gint G_GNUC_MAY_ALIAS _atomicAbort;

#ifdef _MINGW
// not available on win32
#else
#include <glib-unix.h>
/* GLib signal wrapper for:
    SIGHUP
    SIGINT
    SIGTERM
    SIGUSR1
    SIGUSR2
    SIGWINCH 2.54
*/

// errno == EINTR - Interrupted system call when SIGHUP
// $ kill -l  show all 64 interrupt
// 1) SIGHUP   2) SIGINT   3) SIGQUIT   4) SIGILL    5) SIGTRAP
// 6) SIGABRT  7) SIGBUS   8) SIGFPE    9) SIGKILL  10) SIGUSR1
//11) SIGSEGV 12) SIGUSR2 13) SIGPIPE  14) SIGALRM  15) SIGTERM
#endif  // _MINGW

// not available on win32
#ifdef S52_USE_BACKTRACE
#if !defined(S52_USE_ANDROID) || !defined(_MINGW)
#include <execinfo.h>  // backtrace(), backtrace_symbols()
#endif
#endif


// internal libS52.so version + build def's
static const char _version[] = S52_VERSION
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
#ifdef  S52_USE_LOGFILE
      ",S52_USE_LOGFILE"
#endif
#ifdef  S52_USE_DBUS
      ",S52_USE_DBUS"
#endif
#ifdef  S52_USE_SOCK
      ",S52_USE_SOCK"
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
#ifdef  S52_USE_GLSC2
      ",S52_USE_GLSC2"
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
#ifdef  S52_USE_TXT_SHADOW
      ",S52_USE_TXT_SHADOW"
#endif
#ifdef  S52_USE_RADAR
      ",S52_USE_RADAR"
#endif
#ifdef  S52_USE_RASTER
      ",S52_USE_RASTER"
#endif
#ifdef  S52_USE_C_AGGR_C_ASSO
      ",S52_USE_C_AGGR_C_ASSO"
#endif
#ifdef  S52_USE_LCMS2
      ",S52_USE_LCMS2"
#endif
#ifdef  S52_USE_CA_ENC
      ",S52_USE_CA_ENC"
#endif
"\n";

CCHAR   *S52_utils_version(void)
{
    return _version;
}

int      S52_utils_getConfig(CCHAR *label, char *vbuf)
// return TRUE and string value in vbuf for label, FALSE if fail
{
   FILE *fp;
   //int  ret;
   int  nline = 1;
   //char lbuf[PATH_MAX];
   //char frmt[PATH_MAX];
   char lbuf[MAXL];
   char frmt[MAXL];
   char str [MAXL];
   char *pstr = NULL;

   fp = g_fopen(CFG_NAME, "r");
   if (NULL == fp) {
       PRINTF("WARNING: .cfg not found: %s\n", CFG_NAME);
       return FALSE;
   }

   // prevent buffer overflow
   SNPRINTF(frmt, MAXL, "%s%i%s", " %s %", MAXL-1, "[^\n]s");
   //printf("frmt:%s\n", frmt);

   pstr = fgets(str, MAXL, fp);
   while (NULL != pstr) {
       // debug
       //printf("%i - label:%s value:%s\n", nline, lbuf, vbuf);

       if ('#' != str[0]) {
           //ret = sscanf(str, frmt, lbuf, vbuf);
           sscanf(str, frmt, lbuf, vbuf);
           if (0 == g_strcmp0(lbuf, label)) {
               PRINTF("--->>> label:%s value:%s \n", lbuf, vbuf);
               fclose(fp);
               return TRUE;
           }
       }

       ++nline;
       pstr = fgets(str, MAXL, fp);
   }

   fclose(fp);

   vbuf[0] = '\0';

   return FALSE;
}

#if defined(S52_DEBUG) || defined(S52_USE_LOGFILE)
#include <mcheck.h>  // mtrace(), muntrace()


//#if 0

// FIXME: add S52_USE_SHM as a S52 API
// FIXME: write strait to shm bypassing printf malloc so that mem stracking glib new
// dump debug printf via tail -f /dev/shm/libS52.<isodate>.log

#include <sys/mman.h>
#include <sys/stat.h> // For mode constants
#include <fcntl.h>    // For O_* constants
//int shm_open(const char *name, int oflag, mode_t mode);
//int shm_unlink(const char *name);

//Link with -lrt.

//#if 0
//#if 1
//#include <mcheck.h>  // mtrace(), muntrace()

#define _LIBC
#include <malloc.h>
#include <unistd.h>  // write()

static void *_malloc_hook_cb(size_t size, const void *caller);
static void  _free_hook_cb(void *ptr, const void *caller);

typedef void *(*fn_malloc_hook_t)(size_t, const void *);
typedef void  (*fn_free_hook_t)  (void *, const void *);

static fn_malloc_hook_t _malloc_hook_orig = NULL;
static fn_free_hook_t   _free_hook_orig   = NULL;

static size_t _mem_alloc = 0;
static int    _shm_fd    = 0;
static CCHAR *_shm_name  = "libS52.mallocsz.txt";

#define HEXSZ 21

static char      *_addr2hex(size_t ptr)
{
    static const char ascii[16]  = "0123456789ABCDEF";
    static char       buf[HEXSZ] = "  0x1234567812345678\n";  // 2 * 8 + '\0' + 0x
    static char       c          = 0x0F;

    size_t ptrtmp = (size_t)GPOINTER_TO_INT(ptr);
    for (int i=0; i<16; ++i) {
        buf[HEXSZ - i - 2] = ascii[c & ptrtmp];
        ptrtmp = ptrtmp >> 4;
    }

    return buf;
}

static void      *_malloc_hook_cb(size_t size, const void *caller)
{
    //malloc_stats();
    (void)caller;

    // Restore all old hooks
    __malloc_hook = _malloc_hook_orig;
    __free_hook   = _free_hook_orig;

    // Call recursively
    size_t ptr = (size_t)malloc(size);
    _mem_alloc += size;

    char *buf = _addr2hex((size_t)ptr);
    buf[0] = '+';
    buf[HEXSZ-1] = ' ';  // overwrite '\n'
    write(_shm_fd, buf, HEXSZ);

    buf = _addr2hex(size);
    buf[0]       = ' ';
    buf[HEXSZ-1] = '\n';  // overwrite '\n'
    write(_shm_fd, buf, HEXSZ);


    void *buffer[128];
    int nptrs = backtrace(buffer, 128);
    backtrace_symbols_fd(buffer, nptrs, _shm_fd);

    /*
    // Save underlying hooks
    _malloc_hook_orig = __malloc_hook;
    _free_hook_orig   = __free_hook;

    // printf might call malloc, so protect it too
    // + 0x0000000000E7ADC0   0x0000000000000010
    // malloc (16) returns 0xe7adc0
    //printf("malloc (%u) returns %p\n", (unsigned int) size, ptr);
    //*/

    // Restore our own hooks
    __malloc_hook = _malloc_hook_cb;
    __free_hook   = _free_hook_cb;

    return (void*)ptr;
}

static void       _free_hook_cb(void *ptr, const void *caller)
{
    (void)caller;

    //Restore all old hooks
    __malloc_hook = _malloc_hook_orig;
    __free_hook   = _free_hook_orig;

    char *buf = _addr2hex((size_t)ptr);
    buf[0] = '-';
    write(_shm_fd, buf, HEXSZ);


    // Call recursively
    free(ptr);

    /*
    // Save underlying hooks
    _malloc_hook_orig = __malloc_hook;
    _free_hook_orig   = __free_hook;

    // printf might call free, so protect it too.
    printf("freed pointer %p\n", ptr);
    */

    // Restore our own hooks
    __malloc_hook = _malloc_hook_cb;
    __free_hook   = _free_hook_cb;
}

static void       _init_hook(void)
{
    if (NULL==_malloc_hook_orig && NULL==_free_hook_orig) {
        _malloc_hook_orig = __malloc_hook;
        _free_hook_orig   = __free_hook;

        __malloc_hook     = _malloc_hook_cb;
        __free_hook       = _free_hook_cb;
    } else {
        // logic bug - unballanced init/done
        g_assert(0);
    }
}

static void       _done_hook(void)
{
    // __malloc_hook & __free_hook are originally NULL!
    //if (NULL!=_malloc_hook_orig && NULL!=_free_hook_orig) {
        __malloc_hook     = _malloc_hook_orig;
        __free_hook       = _free_hook_orig;
        _malloc_hook_orig = NULL;
        _free_hook_orig   = NULL;
    //} else {
        // logic bug - unballanced init/done
    //    g_assert(0);
    //}

}
//#endif  // 0

void     S52_utils_printf(const char *file, int line, const char *function, const char *frmt, ...)
// FIXME: filter msg type: NOTE:, DEBUG:, FIXME:, WARNING:, ERROR:
{
    // FIXME: use g_vsnprintf - will return n, number of bytes if buf large enough
    int  MAX = 1024;
    char buf[MAX];
    char headerfrmt[] = "%s:%i in %s(): ";
    // get str size
    int  size = snprintf(buf, MAX, headerfrmt, file, line, function);

    // fill the rest
    if (size < MAX) {
        // get no of remaining char
        va_list argptr;
        va_start(argptr, frmt);
        int n = vsnprintf(&buf[size], (MAX-size), frmt, argptr);
        // FIXME: n bytes if buf large enough
        //int n = g_vsnprintf(&buf[size], (MAX-size), frmt, argptr);
        va_end(argptr);

        // alloca sz
        char bufFinal[size + n + 1];
        memcpy(bufFinal, buf, size);
        va_start(argptr, frmt);
        //int nn = vsnprintf(&buf[size], (MAX-size), frmt, argptr);
        int nn = vsnprintf(&bufFinal[size], (n+1), frmt, argptr);
        va_end(argptr);

        //write(1, bufFinal, n+nn+1);
        printf("%s", bufFinal);

#if !defined(S52_USE_LOGFILE)
        // if user set a callback .. call it,
        // unless logging to file witch will call the cb
        if (NULL != _log_cb) {
            _log_cb(bufFinal);
        }
#endif

        if (nn > (n+1)) {
        //if (n > (MAX-size)) {
            //g_print("WARNING: _printf(): string buffer FULL, str len:%i, buf len:%i\n", n, (MAX-size));
            g_message("WARNING: _printf(): string buffer FULL, str len:%i, buf len:%i\n", nn, (n+1));
            g_assert(0);
        }
    } else {
        // FIXME: use printf() or g_message / g_error !
        g_message("WARNING: _printf(): buf FULL, str size:%i, buf len:%i\n", size, MAX);
        g_assert(0);
    }

    return;
}

#ifdef S52_USE_LOGFILE
static void   _S52_printf(CCHAR *string)
{
    char str[MAXL];
    //static
    GTimeVal now;
    g_get_current_time(&now);

    snprintf(str, MAXL-1, "%s %s", g_time_val_to_iso8601(&now), string);

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
#endif  // S52_USE_LOGFILE

int      S52_utils_initLog(S52_log_cb log_cb)
// set print handler
// set tmp log file
{
    _shm_fd = shm_open(_shm_name, O_RDWR | O_CREAT | O_TRUNC, 0644 );

    // debug - setup trace log
    //g_setenv("MALLOC_TRACE", "mem.log", TRUE);

    if (NULL != log_cb) {
        log_cb("S52_utils_initLog(): init logging\n");
        _log_cb = log_cb;
    }

#ifdef S52_USE_LOGFILE
    GError *error = NULL;
    _logFile = g_file_open_tmp("XXXXXX", NULL, &error);
    if (-1 == _logFile) {
        PRINTF("WARNING: g_file_open_tmp(): failed\n");
    } else {
        PRINTF("DEBUG: logfile tmp dir:%s\n", g_get_tmp_dir());
    }
    if (NULL != error) {
        g_printf("WARNING: g_file_open_tmp() failed (%s)\n", error->message);
        g_error_free(error);
    }

    _oldPrintHandler = g_set_print_handler(_S52_printf);

#else
    PRINTF("DEBUG: no LOGFILE, compiler flags 'S52_USE_LOGFILE' not set\n");
#endif  // S52_USE_LOGFILE

    return TRUE;
}

int      S52_utils_doneLog()
{
    //shm_unlink(_shm_name);

    // mtrace
    //g_unsetenv("MALLOC_TRACE");

    _log_cb = NULL;

#ifdef S52_USE_LOGFILE
    g_set_print_handler(_oldPrintHandler);
    _oldPrintHandler = NULL;

    if (0 != _logFile)
        close(_logFile);
#endif  // S52_USE_LOGFILE

    return TRUE;
}
#endif  // S52_DEBUG

int      S52_atoi(CCHAR *str)
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
    return (int)g_ascii_strtod(str, NULL);
    //return atoi(str);
}

double   S52_atof(CCHAR *str)
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
    //return atof(str);
    return g_ascii_strtod(str, NULL);
}

char    *S52_utils_new0(size_t sz, int n)
// debug - tally heap mem
//
// idea: - draw pixels to represent each mem bloc (sz*n)
//       - then hightlight pixels when blocs (S52/S57 obj) are accessed (read/write)
{
    char *ptr = g_malloc0(sz*n);
    //char *ptr = g_new0(sz*n);

    PRINTF("DEBUG: >>>>>>>>>>>>>>>>>>>>>>>>>> ptr:%p sz:%i\n", ptr, sz*n);

    return ptr;
}

//#include <stdio.h>
//#include "valgrind.h"
//#include "memcheck.h"
/*
__attribute__((noinline))
int _app(void);
//int I_WRAP_SONAME_FNNAME_ZU(NONE, _app)(void)
//int I_WRAP_SONAME_FNNAME_ZU(libS52Zdso, _app)(void)
{
   int    result;
   OrigFn fn;
   VALGRIND_GET_ORIG_FN(fn);
   printf("XXXX _app() wrapper start\n");
   CALL_FN_W_v(result, fn);
   //CALL_FN_v_v(result, fn);
   printf("XXXX _app() wrapper end: result %d\n", result);
   return result;
}
*/


//#include "unwind-minimal.h"
#ifdef S52_USE_BACKTRACE
#ifdef S52_USE_ANDROID

#define ANDROID
#define UNW_LOCAL_ONLY
#include <unwind.h>          // _Unwind_*()
#include <libunwind-ptrace.h>
//#include <libunwind-arm.h>
//#include <libunwind.h>
//#include <libunwind-common.h>


#define MAX_FRAME 128
struct callStackSaver {
   unsigned short crntFrame;
   unsigned int   ptrArr[MAX_FRAME];
   unsigned int   libAdjustment;
};

void              _dump_crash_report(unsigne pid)
// shortened code from android's debuggerd
// to get a backtrace on ARM
{
    unw_addr_space_t as;
    struct UPT_info *ui;
    unw_cursor_t     cursor;

    as = unw_create_addr_space(&_UPT_accessors, 0);
    ui = _UPT_create(pid);

    int ret = unw_init_remote(&cursor, as, ui);
    if (ret < 0) {
        PRINTF("WARNING: unw_init_remote() failed [pid %i]\n", pid);
        _UPT_destroy(ui);
        return;
    }

    PRINTF("DEBUG: backtrace of the remote process (pid %d) using libunwind-ptrace:\n", pid);

    do {
        unw_word_t ip, sp, offp;
        char buf[512];

        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        unw_get_proc_name(&cursor, buf, sizeof (buf), &offp);

        PRINTF("DEBUG:   ip: %10p, sp: %10p   %s\n", (void*) ip, (void*) sp, buf);

    } while ((ret = unw_step (&cursor)) > 0);

    _UPT_destroy (ui);
}

static int        _Unwind_Reason_Code _trace_func(struct _Unwind_Context *ctx, void *user_data)
{
    //unsigned int rawAddr = __gnu_Unwind_Find_exidx(ctx); //  _Unwind_GetIP(ctx);
    unsigned int rawAddr = _Unwind_GetIP(ctx);
    //unsigned int rawAddr = (unsigned int) __builtin_frame_address(0);
    //unsigned int rawAddr = __builtin_return_address(0);

    struct callStackSaver* state = (struct callStackSaver*) user_data;

    if (state->crntFrame < MAX_FRAME) {
        state->ptrArr[state->crntFrame] = rawAddr - state->libAdjustment;
        ++state->crntFrame;
    }

    return _URC_CONTINUE_UNWIND;
    //return _URC_OK;
}

#if 0
//*
static guint      _GetLibraryAddress(const char* libraryName)
{
    FILE* file = fopen("/proc/self/maps", "rt");
    if (file==NULL) {
        return 0;
    }

    unsigned int addr = 0;
    //const char* libraryName = "libMyLibraryName.so";
    int len_libname = strlen(libraryName);

    char buff[256];
    while( fgets(buff, sizeof(buff), file) != NULL ) {
        int len = strlen(buff);
        if ((len>0) && (buff[len-1]=='\n')) {
            buff[--len] = '\0';
        }
        if (len <= len_libname || memcmp(buff + len - len_libname, libraryName, len_libname)) {
            continue;
        }

        unsigned int start, end, offset;
        char flags[4];
        if ( sscanf( buff, "%zx-%zx %c%c%c%c %zx", &start, &end, &flags[0], &flags[1], &flags[2], &flags[3], &offset ) != 7 ) {
            continue;
        }

        if ( flags[0]=='r' && flags[1]=='-' && flags[2]=='x' ) {
            addr = start - offset;
            break;
        }
    } // while

    fclose(file);

    return addr;
}

static int        _get_backtrace (void** buffer, int n)
{
    unw_cursor_t  cursor;
    unw_context_t uc;
    unw_word_t    ip;
    unw_word_t    sp;

    //unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);

    int i = 0;
    while (unw_step(&cursor) > 0 && i < n) {
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        buffer[i] = (void*)ip;
        i++;
        //printf ("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
    }

    return i;
}
//*/
#endif  // 0

static int        _unwind(void)
{
// ============ test using Unwind ====================================
/*
_URC_OK                       = 0,  // operation completed successfully
_URC_FOREIGN_EXCEPTION_CAUGHT = 1,
_URC_END_OF_STACK             = 5,
_URC_HANDLER_FOUND            = 6,
_URC_INSTALL_CONTEXT          = 7,
_URC_CONTINUE_UNWIND          = 8,
_URC_FAILURE                  = 9   // unspecified failure of some kind
*/
        /*
        struct callStackSaver state;
        //state.libAdjustment = _GetLibraryAddress("libs52droid.so");
        state.libAdjustment = 0;
        state.crntFrame     = 0;
        state.ptrArr[0]     = 0;


        _Unwind_Reason_Code code = _Unwind_Backtrace(_trace_func, (void*)&state);
        PRINTF("After _Unwind_Backtrace() .. code=%i, nFrame=%i, frame=%x\n", code, state.crntFrame, state.ptrArr[0]);
        //*/

       // int nptrs = _get_backtrace((void**)&buffer, 128);

}
#endif // S52_USE_ANDROID

int      S52_utils_backtrace(void)
{
    void  *buffer[128];
    char **strings;
    int    nptrs = 0;

    nptrs = backtrace(buffer, 128);

    PRINTF("DEBUG: ==== backtrace() returned %d addresses ====\n", nptrs);

    strings = backtrace_symbols(buffer, nptrs);
    if (NULL == strings) {
        PRINTF("WARNING: backtrace_symbols() .. no symbols");
        return FALSE;
    }

    for (int i=0; i<nptrs; ++i) {
        PRINTF("DEBUG: ==== %s\n", strings[i]);  // clang - null dereference - return false above
    }

    free(strings);

    return TRUE;
}
#endif  // S52_USE_BACKTRACE

//gboolean(*GSourceFunc) (gpointer user_data);
static gboolean   _trapSIG(gpointer user_data)
// Note: glib UNIX signal handling
{
    int intNo = GPOINTER_TO_INT(user_data);

    /*
    SIGHUP
    SIGINT
    SIGTERM
    SIGUSR1
    SIGUSR2
    SIGWINCH 2.54
    */

    switch(intNo) {
        case SIGHUP:
            PRINTF("NOTE: GLib Signal SIGHUP(%i) cought .. controling terminal closing\n", SIGHUP);
            break;
        case SIGINT:
            g_atomic_int_set(&_atomicAbort, TRUE);
            PRINTF("NOTE: GLib Signal SIGINT(%i) cought .. setting up atomic to abort\n", SIGINT);
            break;

        case SIGTERM:
            // needed by glib when quiting main loop with printf malloc not reentrant
            PRINTF("NOTE: GLib Signal SIGTERM(%i) cought .. quit main loop\n", SIGTERM);
            break;
        case SIGUSR1:
            PRINTF("NOTE: GLib Signal SIGUSR1(%i) cought .. user signal 1\n", SIGUSR1);
            break;
        case SIGUSR2:
            PRINTF("NOTE: GLib Signal SIGUSR2(%i) cought .. user signal 2\n", SIGUSR2);
            break;

        case SIGWINCH: // glib 2.54
            PRINTF("NOTE: GLib Signal SIGWINCH(%i) cought .. window full-screen\n", SIGWINCH);
            break;

        default:
            // shouldn't reach this !?
            PRINTF("WARNING: Signal not hangled (%i)\n", intNo);
            g_assert_not_reached();  // turn off via G_DISABLE_ASSERT
    }

    return FALSE;  // stop propagate ?
}

int      S52_utils_initSIG(void)
// init signal handler
{

#ifdef _MINGW
    signal(SIGINT,  _trapSIG);  //  2 - Interrupt (ANSI).
    signal(SIGSEGV, _trapSIG);  // 11 - Segmentation violation (ANSI).

#else  // _MINGW

    //struct sigaction sa;

    //memset(&sa, 0, sizeof(sa));
    //sa.sa_sigaction = _trapSIG;
    //sigemptyset(&sa.sa_mask);
    //sa.sa_flags = SA_SIGINFO;  // -std=c99 -D_POSIX_C_SOURCE=199309L

    guint eventID = 0;

    // 1 - SIGHUP: (POSIX) controlling terminal is closed.
    eventID = g_unix_signal_add(SIGHUP, _trapSIG, GINT_TO_POINTER(SIGHUP));
    if (0 >= eventID) {
        PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGHUP);
    }

    //  2 - SIGINT: Interrupt (ANSI) - Ctrl-C
    // abort long running process in _draw(), _drawLast() and _suppLineOverlap()
    g_atomic_int_set(&_atomicAbort, FALSE);

    //sigaction(SIGINT,  &sa, &_old_signal_handler_SIGINT);
    eventID = g_unix_signal_add(SIGINT, _trapSIG, GINT_TO_POINTER(SIGINT));
    if (0 >= eventID) {
        PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGINT);
    }

    //  3 - Quit (POSIX)
    //sigaction(SIGQUIT, &sa, &_old_signal_handler_SIGQUIT);
    //  5 - Trap (ANSI)
    //sigaction(SIGTRAP, &sa, &_old_signal_handler_SIGTRAP);
    //  6 - Abort (ANSI)
    //sigaction(SIGABRT, &sa, &_old_signal_handler_SIGABRT);
    //  9 - Kill, unblock-able (POSIX)
    //sigaction(SIGKILL, &sa, &_old_signal_handler_SIGKILL);
    // 11 - Segmentation violation (ANSI).
    //sigaction(SIGSEGV, &sa, &_old_signal_handler_SIGSEGV);   // loop in android

    // 15 - SIGTERM: Termination (ANSI)
    //sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);
    eventID = g_unix_signal_add(SIGTERM, _trapSIG, GINT_TO_POINTER(SIGTERM));
    if (0 >= eventID) {
        PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGTERM);
    }

    // 10 - SIGUSR1
    //sigaction(SIGUSR1, &sa, &_old_signal_handler_SIGUSR1);
    eventID = g_unix_signal_add(SIGUSR1, _trapSIG, GINT_TO_POINTER(SIGUSR1));
    if (0 >= eventID) {
        PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGUSR1);
    }

    // 12 - SIGUSR2
    //sigaction(SIGUSR2, &sa, &_old_signal_handler_SIGUSR2);
    eventID = g_unix_signal_add(SIGUSR2, _trapSIG, GINT_TO_POINTER(SIGUSR2));
    if (0 >= eventID) {
        PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGUSR2);
    }

    // 28 - SIGWINCH: window full-screen - glib 2.54
    //eventID = g_unix_signal_add(SIGWINCH, _trapSIG, GINT_TO_POINTER(SIGWINCH));
    //if (0 >= eventID) {
    //    PRINTF("WARNING: GLib Signal add SIG(%i) failed\n", SIGWINCH);
    //}


#endif  // _MINGW

    return TRUE;
}

void     S52_utils_setAtomicInt(int newVal)
{
    g_atomic_int_set(&_atomicAbort, newVal);

    return;
}

int      S52_utils_getAtomicInt(void)
{
    g_atomic_int_get(&_atomicAbort);

    return _atomicAbort;
}

int      S52_utils_mtrace(void)
{
    //mtrace();

    _init_hook();

    return TRUE;
}

int      S52_utils_muntrace(void)
{
    //muntrace();

    _done_hook();

    return TRUE;
}

void     S52_utils_gdbBreakPoint(void)
{
    raise(SIGINT);
}

///////////////////////////// J U N K ///////////////////////////////////
#if 0
/*
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
*/
#endif  // 0

#if 0

#ifdef _MINGW
static void       _trapSIG(int sig)
{
    //void  *buffer[100];
    //char **strings;

    // Ctrl-C
    if (SIGINT == sig) {
        PRINTF("NOTE: Signal SIGINT(%i) cought .. setting up atomic to abort draw()\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);
        return;
    }

    if (SIGSEGV == sig) {
        PRINTF("NOTE: Segmentation violation cought (%i) ..\n", sig);
    } else {
        PRINTF("NOTE: other signal(%i) trapped\n", sig);
    }

    // shouldn't reach this !?
    g_assert_not_reached();  // turn off via G_DISABLE_ASSERT

    exit(sig);
}

#else  // _MINGW

static void       _trapSIG(int sig, siginfo_t *info, void *secret)
{
    // 2 - Interrupt (ANSI), Ctrl-C
    if (SIGINT == sig) {
        PRINTF("NOTE: Signal SIGINT(%i) cought .. setting up atomic to abort\n", sig);
        g_atomic_int_set(&_atomicAbort, TRUE);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGINT.sa_sigaction)
            _old_signal_handler_SIGINT.sa_sigaction(sig, info, secret);

        return;
    }

    //  3  - Quit (POSIX), Ctrl + D
    if (SIGQUIT == sig) {
        PRINTF("NOTE: Signal SIGQUIT(%i) cought .. Quit\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGQUIT.sa_sigaction)
            _old_signal_handler_SIGQUIT.sa_sigaction(sig, info, secret);

        return;
    }

    //  5  - Trap (ANSI)
    if (SIGTRAP == sig) {
        PRINTF("NOTE: Signal SIGTRAP(%i) cought .. debugger\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTRAP.sa_sigaction)
            _old_signal_handler_SIGTRAP.sa_sigaction(sig, info, secret);

        return;
    }

    //  6  - Abort (ANSI)
    if (SIGABRT == sig) {
        PRINTF("NOTE: Signal SIGABRT(%i) cought .. Abort\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGABRT.sa_sigaction)
            _old_signal_handler_SIGABRT.sa_sigaction(sig, info, secret);

        return;
    }

    //  9  - Kill, unblock-able (POSIX)
    if (SIGKILL == sig) {
        PRINTF("NOTE: Signal SIGKILL(%i) cought .. Kill\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGKILL.sa_sigaction)
            _old_signal_handler_SIGKILL.sa_sigaction(sig, info, secret);

        return;
    }

    // 11 - Segmentation violation
    if (SIGSEGV == sig) {
        PRINTF("NOTE: Segmentation violation cought (%i) ..\n", sig);

#ifdef S52_USE_BACKTRACE
#ifdef S52_USE_ANDROID
        _unwind();

        // break loop - android debuggerd rethrow SIGSEGV
        exit(0);

#endif  // S52_USE_ANDROID


        S52_utils_backtrace();

#endif  //S52_USE_BACKTRACE

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGSEGV.sa_sigaction)
            _old_signal_handler_SIGSEGV.sa_sigaction(sig, info, secret);

        return;
    }

    // 15 - Termination (ANSI)
    if (SIGTERM == sig) {
        PRINTF("NOTE: Signal SIGTERM(%i) cought .. Termination\n", sig);

        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGTERM.sa_sigaction)
            _old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);

        return;
    }

    // 10
    if (SIGUSR1 == sig) {
        PRINTF("NOTE: Signal 'User-defined 1' cought - SIGUSR1(%i)\n", sig);

        // debug
        S52_utils_backtrace();

        return;
    }
    // 12
    if (SIGUSR2 == sig) {
        PRINTF("NOTE: Signal 'User-defined 2' cought - SIGUSR2(%i)\n", sig);
        return;
    }



//#ifdef S52_USE_ANDROID
        // break loop - android debuggerd rethrow SIGSEGV
        //exit(0);
//#endif


    // shouldn't reach this !?
    PRINTF("WARNING: Signal not hangled (%i)\n", sig);
    g_assert_not_reached();  // turn off via G_DISABLE_ASSERT

/*
#ifdef S52_USE_BREAKPAD
    // experimental
    MinidumpFileWriter writer;
    writer.Open("/tmp/minidump.dmp");
    TypedMDRVA<MDRawHeader> header(&writer_);
    header.Allocate();
    header->get()->signature = MD_HEADER_SIGNATURE;
    writer.Close();
#endif
*/

}
#endif  // _MINGW
#endif  // 0

