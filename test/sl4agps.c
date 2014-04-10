// sl4agps.c: Experiment to call android via SL4A to read xoom's GPS/GYRO
//            and relay this info to libS52 socket.
//            Also a testbed for android GUI programming for
//            exercising some experimental S52 stuff with EGL/GLES2
//
//
// SD 2012APR04 - update

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2014 Sylvain Duclos sduclos@users.sourceforge.net

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


#include <S52.h>

#include <glib.h>
#include <glib-object.h>   // g_type_init()
#include <glib/gprintf.h>  // g_print(), g_ascii_strtod(), g_strrstr()
#include <gio/gio.h>       // socket stuff ..
#include <errno.h>         // errno

#include <unistd.h>        // PID
#include <math.h>          // INFINITY

#define RAD_TO_DEG         57.29577951308232

//#define PATH "/data/media"     // android 4.1
#define PATH "/sdcard/s52droid"  // android 4.2
#define GPS   PATH "/bin/sl4agps"
#define PID  ".pid"

static char _localhost[] = "127.0.0.1";

#define SL4A_PORT          45001
#define SL4A_HEART_BEAT_SEC   10
static int                 _sl4a_show_dialog = FALSE;
static GSocketConnection  *_sl4a_connectionA = NULL;

#define S52_PORT           2950
static GSocketConnection  *_s52_connection  = NULL;
static S52ObjectHandle     _s52_ownshp      = NULL;

static int                 _request_id      = 0;

static GStaticMutex        _mp_mutex = G_STATIC_MUTEX_INIT;

#define TB "\\t"  // Tabulation
#define NL "\\n"  // New Line

//#define STRSZ  256
#define STRSZ  512
#define BUFSZ 1024
//#define BUFSZ 4096
//#define BUFSZ 8192

static GString *_json_obj = NULL;
static GString *_params   = NULL; //[BUFSZ]; // param part that will later be in the final _json_obj


// this file is created when building sl4agps
#include "sl4agps.cfg"

// trap signal
// must be compiled with -std=gnu99
#include <sys/types.h>
#include <signal.h>
//#include <execinfo.h>
static struct sigaction _old_signal_handler_SIGINT;
static struct sigaction _old_signal_handler_SIGSEGV;
static struct sigaction _old_signal_handler_SIGTERM;
static struct sigaction _old_signal_handler_SIGUSR1;
static struct sigaction _old_signal_handler_SIGUSR2;

static GMainLoop *_main_loop = NULL;

static char    *_flattenstr(char *str, int n)
// strip '\n' - replace 0x0a by 0x20
{
    int i=0;
    for (i=0; i<n; ++i) {
        if ('\n' == str[i])
            str[i] = ' ';
    }

    return str;
}

static GSocketConnection *_init_sock(char *hostname, int port)
{
    GError            *error = NULL;
    GSocketConnection *conn  = NULL;

    GSocketClient *client = g_socket_client_new();
    if (NULL == client) {
        g_print("sl4agps:_init_sock(): client NULL  ..\n");
        return NULL;
    }

    GSocketConnectable *connectable = g_network_address_new(hostname, port);
    if (NULL == client) {
        g_print("sl4agps:_init_sock(): connectable NULL  ..\n");
        return NULL;
    }

    // FIXME: try using:
    // g_socket_connection_connect_async(GSocketConnection *, GSocketAddress *, GCancellable *, GAsyncReadyCallback, gpointer user_data)
    // -OR-
    // { g_object_ref (connection);
    //   GSocket *socket = g_socket_connection_get_socket(connection);
    //   gint fd = g_socket_get_fd(socket);
    //   GIOChannel *channel = g_io_channel_unix_new(fd);
    //   // Pass connection as user_data to the watch callback
    //   g_io_add_watch(channel, G_IO_IN, (GIOFunc) network_read, connection);
    //   return TRUE;
    // }
    // so that loading a big ENC doesn't block the UI.
    // NOTE: loadCell() and draw() will each block libS52 though.
    conn = g_socket_client_connect(client, connectable, NULL, &error);

    g_object_unref(client);
    g_object_unref(connectable);

    if (NULL != error) {
        // FIXME: check if 'connection' is NULL or else need to be destroyed!
        g_print("sl4agps:_init_sock():g_socket_client_connect() fail: %s [conn:%p]\n", error->message, conn);

        // path not tested
        if (NULL != conn)
            g_object_unref(conn);

        return NULL;
    }

    g_print("sl4agps:_init_sock(): New connection: host:%s, port:%i\n", hostname, port);

    return conn;
}

static char    *_send_cmd(GSocketConnection **conn, const char *command, const char *params)
// FIXME: buffer
{
    static char buffer[BUFSZ] = {'\0'};

    //g_print("sl4agps:_send_s52_cmd(): starting .. [%s]\n", params);

    //if (NULL == (void*)*conn) {
    if (NULL == *conn) {
        g_print("sl4agps:_send_cmd(): fail - no conection\n");
        return NULL;
    }

    GSocket *socket = g_socket_connection_get_socket(*conn);
    if (NULL == socket) {
        g_print("sl4agps:_send_cmd(): fail - no socket\n");
        return NULL;
    }

    // build a full JSON object
    g_string_printf(_json_obj, "{\"id\":%i,\"method\":\"%s\",\"params\":[%s]}\n", _request_id++, command, params);

    GError *error = NULL;
    gssize szsnd = g_socket_send_with_blocking(socket, _json_obj->str, _json_obj->len, FALSE, NULL, &error);
    if ((NULL!=error) || (0==szsnd) || (-1==szsnd)) {
        //  0 - connection was closed by the peer
        // -1 - on error
        if (NULL == error)
            g_print("sl4agps:_send_cmd():ERROR:g_socket_send_with_blocking(): connection close [%s]\n", _json_obj->str);
        else
            g_print("sl4agps:_send_cmd():ERROR:g_socket_send_with_blocking() fail - code:%i, msg:'%s' cmd:%s",
                    error->code, error->message, _json_obj->str);

        //*conn = (struct GSocketConnection *)NULL;
        *conn = NULL;

        return NULL;
    }
    //g_print("sl4agps:sl4agps:_send_cmd(): sended:%s", _json_obj->str);

    // wait response - blocking socket
    gssize szrcv = g_socket_receive_with_blocking(socket, buffer, BUFSZ-1, TRUE, NULL, &error);
    if ((NULL!=error) || (0==szrcv) || (-1==szrcv)) {
        //  0 - connection was closed by the peer
        // -1 - on error
        if (NULL == error)
            g_print("sl4agps:_send_cmd():ERROR:g_socket_receive_with_blocking(): connection close [%s]\n", buffer);
        else
            g_print("sl4agps:_send_cmd():ERROR:g_socket_receive_with_blocking() fail - code:%i, msg:'%s' cmd:%s",
                    error->code, error->message, buffer);

        //*conn = (struct GSocketConnection *)NULL;
        *conn = NULL;

        return NULL;
    }
    buffer[szrcv] = '\0';

    //g_print("sl4agps:_send_cmd(): n:%i received:%s", n, buffer);
    if (0 == szrcv) {
        g_print("''\n");
        return NULL;
    }

    gchar *result = g_strrstr(buffer, "result");
    if (NULL == result) {
        g_print("sl4agps:_send_cmd(): no 'result' buffer:%s", buffer);
        return NULL;
    }

    return buffer;
}

static gchar   *_s52_encodeNsend(const char *command, const char *frmt, ...)
{
    //g_print("sl4agps:_s52_encodeNsend(): starting .. [%s]\n", frmt);

    if (NULL == frmt) {
        g_print("WARNING:sl4agps:_s52_encodeNsend(): frmt is NULL\n");
        return NULL;
    }

    va_list argptr;
    va_start(argptr, frmt);
    g_string_vprintf(_params, frmt, argptr);
    va_end(argptr);

    gchar *resp = _send_cmd(&_s52_connection, command, _params->str);
    if (NULL != resp) {
        while (*resp != '[')
            resp++;
    } else {
        g_print("WARNING:sl4agps:_s52_encodeNsend():_send_cmd(): failed (NULL)\n");
    }

    return resp;
}

static int      _s52_init(void)
// check is libS52 is up
{
    _s52_connection = _init_sock(_localhost, S52_PORT);
    if (NULL == _s52_connection)
        return FALSE;

    _s52_ownshp = NULL;
    gchar *resp = _s52_encodeNsend("S52_newOWNSHP", "\"%s\"", "OWNSHP" NL "---.- deg / --.- kt");
    if (NULL != resp)
        sscanf(resp, "[ %lu", (long unsigned int *) &_s52_ownshp);

    if (NULL != _s52_ownshp)
        _s52_encodeNsend("S52_setDimension", "%lu,%lf,%lf,%lf,%lf", (long unsigned int) _s52_ownshp, 0.0, 100.0, 15.0, 0.0);

    return TRUE;
}

static int      _s52_done(void)
{
    _s52_encodeNsend("S52_delMarObj", "%lu", (long unsigned int) _s52_ownshp);

    if (NULL != _s52_connection)
        g_object_unref(_s52_connection);

    return TRUE;
}

static double   _s52_getMarinerParam(int marParam)
{
    //double pal = S52_getMarinerParam(marParam);
    //_s52_encodeNsend("S52_getMarinerParam", "%i", marParam);
    const gchar *doublestr = _s52_encodeNsend("S52_getMarinerParam", "%i", marParam);
    if (NULL == doublestr)
        return INFINITY;

    double d = g_ascii_strtod(doublestr+1, NULL);

    return d;
}

static double   _s52_setMarinerParam(int marParam, double val)
{
    const gchar *doublestr = _s52_encodeNsend("S52_setMarinerParam", "%i,%lf", marParam, val);
    if (NULL == doublestr)
        return INFINITY;

    double d = g_ascii_strtod(doublestr+1, NULL);

    return d;
}


static int      _sl4a_init_sock  (char *hostname, int port)
{
    GError *error = NULL;

    //g_print("sl4agps:_sl4a_init_sock(): starting ..\n");

    GSocketClient *client = g_socket_client_new();
    if (NULL == client) {
        g_print("sl4agps:_sl4a_init_sock(): client NULL  ..\n");
        return FALSE;
    }

    GSocketConnectable *connectable = g_network_address_new(hostname, port);
    if (NULL == client) {
        g_print("sl4agps:_sl4a_init_sock(): connectable NULL  ..\n");
        return FALSE;
    }

    _sl4a_connectionA = g_socket_client_connect(client, connectable, NULL, &error);
    //_sl4a_connectionB = g_socket_client_connect(client, connectable, NULL, &error);

    g_object_unref(client);
    g_object_unref(connectable);

    if (NULL != error) {
        g_print("sl4agps:_sl4a_init_sock():g_socket_client_connect(): fail [%s]\n", error->message);
        return FALSE;
    }

    g_print("sl4agps:_sl4a_init_sock(): New connection: host:%s, port:%i\n", hostname, port);

    return TRUE;
}

static int      _sl4a_init       (void)
{
    // FIXME: try sending something (init()) first to see if SL4A is allready up from
    // previous run and if so skip init().
    // BUG: starting SL4A twice slow down SL4A
    // first check if SL4A is allready up
    if (NULL == (_sl4a_connectionA = _init_sock(_localhost, SL4A_PORT))) {

        //    "-a   com.googlecode.android_scripting.action.LAUNCH_BACKGROUND_SCRIPT         "
        //    "--activity-previous-is-top";
        // launch public server
        //    "--ez com.googlecode.android_scripting.extra.USE_PUBLIC_IP true"
        const gchar cmdSL4A[] =
            "sh   /system/bin/am start                                                     "
            "-a   com.googlecode.android_scripting.action.LAUNCH_SERVER                    "
            "-n   com.googlecode.android_scripting/.activity.ScriptingLayerServiceLauncher "
            "--ei com.googlecode.android_scripting.extra.USE_SERVICE_PORT 45001            "
            "--ez com.googlecode.android_scripting.extra.USE_PUBLIC_IP true                "
            "--activity-previous-is-top";

        // Android quirk: g_spawn() return the return value of the cmd (here 0)
        // check this: return FALSE to meen SUCCESS!!
        int ret = g_spawn_command_line_async(cmdSL4A, NULL);
        if (FALSE == ret) {
            g_print("sl4agps:_sl4a_init(): fail to start sl4a server script .. exit\n");
            return FALSE;
        } else {
            g_print("sl4agps:_sl4a_init(): started sl4a server script ..\n");
        }

        while (NULL == (_sl4a_connectionA = _init_sock(_localhost, SL4A_PORT))) {
            g_print("sl4agps:DEBUG: _sl4a_init():_sl4a_init_sock() failed .. wait 1 sec for the server to come on-line\n");
            g_usleep(1000 * 1000); // 1.0 sec
        }

        // this needs to be called only once, after the SL4A server starts:
        // (maybe usefull on remote app, unneeded at the moment since sl4a work just fine without)
        //sl4a_rpc_method(_sl4a_socket_fd, (char*)"_authenticate", (char)'v', (void*)"s", "");
        //_send_cmd(_sl4a_connectionA, (char*)"_authenticate", "''");
    } else {
        g_print("sl4agps:_sl4a_init(): SL4A allready on-line\n");
    }


    // Gyro v=ii
    // - 1 = All Sensors, 2 = Accelerometer, 3 = Magnetometer and 4 = Light.
    // - interval uSec
    _send_cmd(&_sl4a_connectionA, "startSensingTimed", "1,1000000");

    // GPS  v=ii
    // Integer minDistance[optional, default 60000]: minimum time between updates in uSec,
    // Integer minUpdateDistance[optional, default 30]: minimum distance between updates in meters)
    //_send_cmd(&_sl4a_connectionA, "startLocating", "");    // 0.5 sec
    _send_cmd(&_sl4a_connectionA, "startLocating", "30,1000000");    // 30m, 1.0sec

    return TRUE;
}

static int      _sl4a_done       (void)
{
    if (NULL != _sl4a_connectionA) {
        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
        g_object_unref(_sl4a_connectionA);
        g_print("sl4agps:_sl4a_done(): connection to SL4A closed\n");
        _sl4a_connectionA = NULL;
    }

    return TRUE;
}

static int      _sl4a_diagShow   (char *items)
{
    // dialog allready up
    if (TRUE == _sl4a_show_dialog)
        return TRUE;

    //_sl4a_send_rpc("dialogSetItems", str);
    _send_cmd(&_sl4a_connectionA, "dialogCreateAlert", "'Select:'");
    //_send_cmd(_sl4a_connectionA, "dialogSetItems", palListstr);
    _send_cmd(&_sl4a_connectionA, "dialogSetSingleChoiceItems", items);
    _send_cmd(&_sl4a_connectionA, "dialogSetPositiveButtonText", "'Apply'");
    //_send_cmd(_sl4a_connectionA, "dialogSetNegativeButtonText", "'testNegative'");
    _send_cmd(&_sl4a_connectionA, "dialogShow", "");

    _sl4a_show_dialog = TRUE;

    return TRUE;
}

static int      _sl4a_diagGetResp(void)
{
    int itemIdx = -1;

    //*
    char *retstr = _send_cmd(&_sl4a_connectionA, "eventPoll", "1");
    if (NULL != retstr) {
        const gchar *resultstr = g_strrstr(retstr, "result");
        if (NULL == resultstr) {
            g_print("sl4agps:_sl4a_diagGetResp():g_strrstr(): ERROR\n");
            return -1;
        }

        // empty - no event
        if (']' == resultstr[9])
            return -1;
    }
    //*/

    char *respstr = _send_cmd(&_sl4a_connectionA, "dialogGetSelectedItems", "");
    if (NULL != respstr) {
        const gchar *resultstr = g_strrstr(respstr, "result");
        if (NULL == resultstr) {
            g_print("sl4agps:_sl4a_diagGetResp():g_strrstr(): ERROR\n");
            return -1;
        }

        errno = 0;
        //itemIdx  = (int) g_ascii_strtod(resultstr+9+7, NULL);
        itemIdx  = (int) g_ascii_strtod(resultstr+9, NULL);
        if (0 != errno) {
            g_print("sl4agps:_sl4a_diagGetResp():g_ascii_strtod(): ERROR\n");
            return -1;
        }
    }

    //return TRUE;
    return itemIdx;
}

static int      _sl4a_diagGetNum (double *number)
{
    //{"error":null,"id":72,"result":{"value":"32.1","which":"positive"}}
    char *respstr = _send_cmd(&_sl4a_connectionA, "dialogGetResponse", "");
    if (NULL != respstr) {
        const gchar *resultstr = g_strrstr(respstr, "\"value\":\"");
        if (NULL == resultstr) {
            g_print("sl4agps:_sl4a_diagGetNum():g_strrstr(): ERROR\n");
            return FALSE;
        }

        //g_print("sl4agps:ZZZZZZZZZZZZZZZZZZZZZZZZZZZ _sl4a_diagGetNum():%s\n", resultstr);

        errno   = 0;
        *number = g_ascii_strtod(resultstr+9, NULL);
        if (0 != errno) {
            g_print("sl4agps:_sl4a_diagGetNum():g_ascii_strtod(): ERROR\n");
            return FALSE;
        }
    }

    return TRUE;
}

static int      _sl4a_setCheckBox(int param, char *prop)
{
    double d = _s52_getMarinerParam(param);

    char str[STRSZ];
    g_snprintf(str, STRSZ, "'%s','checked','%s'", prop, (1.0==d)? "true":"false");

    // set prop
    _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);

    return TRUE;
}

static int      _sl4a_setCheckBox_key(int param, char *prop, int key)
{
    int val = (key & (int)_s52_getMarinerParam(param));

    g_print("val:%i, param:%i, key:%i\n", val, param, key);

    char str[STRSZ];
    g_snprintf(str, STRSZ, "'%s','checked','%s'", prop, (0<val) ? "true":"false");

    // set prop
    _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);

    return TRUE;
}

static int      _sl4a_setCheckBoxS52TXT(void)
{
    const gchar *ownshptxtstr = _s52_encodeNsend("S52_setTextDisp", "75");
    const gchar *vesseltxtstr = _s52_encodeNsend("S52_setTextDisp", "76");

    if (NULL==ownshptxtstr || NULL==vesseltxtstr)
        return FALSE;

    char str[STRSZ];
    if (('1' == *(ownshptxtstr+1)) || ('1' == *(vesseltxtstr+1)))
        g_snprintf(str, STRSZ, "'VESSELLABELcheckBox','checked','true'");
    else
        g_snprintf(str, STRSZ, "'VESSELLABELcheckBox','checked','false'");

    // set prop
    _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);

    return TRUE;
}


static int      _sl4a_setDegText (int param, char *prop)
{
    char str[STRSZ];
    double d = _s52_getMarinerParam(param);
    if (0.0 == d)
        g_snprintf(str, STRSZ, "'%s','Text','---.-°'", prop);
    else
        g_snprintf(str, STRSZ, "'%s','Text','%05.1f°'", prop, d);

    // set prop
    _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);

    return TRUE;
}

static int      _sl4a_setText    (int param, char *prop)
{
    char str[STRSZ];
    double d = _s52_getMarinerParam(param);
    g_snprintf(str, STRSZ, "'%s','Text','%05.3f mm'", prop, d);

    // set prop
    _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);

    return TRUE;
}

static int      _sl4a_fullShowSet(void)
{
    // FIXME: find a way to set S52 UI text color in one place

    // set S52 UI background color
    const char *rgbstr = _s52_encodeNsend("S52_getRGB", "\"UIBCK\"");
    if (NULL != rgbstr) {
        char str[STRSZ];
        unsigned int R,G,B;
        //g_print("sl4agps:_sl4a_fullShowSet():rgbstr=%s\n", rgbstr);
        sscanf(rgbstr, "[ %u , %u , %u", &R,&G,&B);

        g_snprintf(str, STRSZ, "'LeftPane','background','#88%02x%02x%02x'", R,G,B);
        //g_print("sl4agps:_sl4a_fullShowSet():str=%s, RGB=%x %x %x\n", str, R,G,B);

        _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);
    }

    //*
    // set S52 UI Text Color
    rgbstr = _s52_encodeNsend("S52_getRGB", "\"UINFF\"");
    if (NULL != rgbstr) {
        char str[STRSZ];
        unsigned int R,G,B;
        sscanf(rgbstr, "[ %u , %u , %u", &R,&G,&B);
        for (int i=0; i<NBR_TEXT; ++i) {
            //g_snprintf(str, STRSZ, "'s52_text_%i','textColor','#ff%c%c%c%c%c%c'", i, rgbstr[1],rgbstr[2], rgbstr[4],rgbstr[5], rgbstr[7],rgbstr[8]);
            g_snprintf(str, STRSZ, "'s52_text_%i','textColor','#ff%02x%02x%02x'", i, R,G,B);

            _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);
        }
    }

    // set S52 UI Border Color
    rgbstr = _s52_encodeNsend("S52_getRGB", "\"UIBDR\"");
    if (NULL != rgbstr) {
        char str[STRSZ];
        unsigned int R,G,B;
        sscanf(rgbstr, "[ %u , %u , %u", &R,&G,&B);
        for (int i=0; i<NBR_BORDER; ++i) {
            //g_snprintf(str, STRSZ, "'s52_border_%i','background','#ff%c%c%c%c%c%c'", i, rgbstr[1],rgbstr[2], rgbstr[4],rgbstr[5], rgbstr[7],rgbstr[8]);
            g_snprintf(str, STRSZ, "'s52_border_%i','background','#ff%02x%02x%02x'", i, R,G,B);

            _send_cmd(&_sl4a_connectionA, "fullSetProperty", str);
        }
    }
    //*/


    _sl4a_setCheckBox_key(S52_MAR_DISP_CATEGORY,   "DISPCATSTDcheckBox",       S52_MAR_DISP_CATEGORY_STD     );
    _sl4a_setCheckBox_key(S52_MAR_DISP_CATEGORY,   "DISPCATOTHERcheckBox",     S52_MAR_DISP_CATEGORY_OTHER   );
    _sl4a_setCheckBox_key(S52_MAR_DISP_CATEGORY,   "DISPCATSELECTcheckBox",    S52_MAR_DISP_CATEGORY_SELECT  );

    _sl4a_setCheckBox_key(S52_MAR_DISP_LAYER_LAST, "DISPCATMARSTDcheckBox",    S52_MAR_DISP_LAYER_LAST_STD   );
    _sl4a_setCheckBox_key(S52_MAR_DISP_LAYER_LAST, "DISPCATMAROTHERcheckBox",  S52_MAR_DISP_LAYER_LAST_OTHER );
    _sl4a_setCheckBox_key(S52_MAR_DISP_LAYER_LAST, "DISPCATMARSELECTcheckBox", S52_MAR_DISP_LAYER_LAST_SELECT);

    _sl4a_setCheckBox(S52_MAR_SHOW_TEXT,           "SHOWTEXTcheckBox"     );
    _sl4a_setCheckBox(S52_MAR_SCAMIN,              "SCAMINcheckBox"       );
    _sl4a_setCheckBox(S52_MAR_DISP_AFTERGLOW,      "AFTERGLOWcheckBox"    );
    _sl4a_setCheckBox(S52_MAR_DISP_CALIB,          "CALIBRATIONcheckBox"  );
    _sl4a_setCheckBox(S52_MAR_DISP_LEGEND,         "LEGENDcheckBox"       );
    _sl4a_setCheckBox(S52_MAR_DISP_WORLD,          "WORLDcheckBox"        );
    _sl4a_setCheckBox(S52_MAR_ANTIALIAS,           "ANTIALIAScheckBox"    );
    _sl4a_setCheckBox(S52_MAR_DISP_CENTROIDS,      "CENTROIDScheckBox"    );
    _sl4a_setCheckBox(S52_MAR_QUAPNT01,            "QUAPNT01checkBox"     );
    _sl4a_setCheckBox(S52_MAR_DISP_DRGARE_PATTERN, "DRGAREPATTERNcheckBox");
    _sl4a_setCheckBox(S52_MAR_DISP_NODATA_LAYER,   "NODATALAYERcheckBox"  );

    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERSYcheckBox", S52_CMD_WRD_FILTER_SY);
    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERLScheckBox", S52_CMD_WRD_FILTER_LS);
    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERLCcheckBox", S52_CMD_WRD_FILTER_LC);
    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERACcheckBox", S52_CMD_WRD_FILTER_AC);
    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERAPcheckBox", S52_CMD_WRD_FILTER_AP);
    _sl4a_setCheckBox_key(S52_CMD_WRD_FILTER,      "CMDWRDFILTERTXcheckBox", S52_CMD_WRD_FILTER_TX);

    //_sl4a_setCheckBoxS52TXT();

    _sl4a_setDegText (S52_MAR_ROT_BUOY_LIGHT,      "ROTBUOYLIGHTeditText" );

    _sl4a_setText    (S52_MAR_DOTPITCH_MM_X,       "DOTPITCHXeditText"    );
    _sl4a_setText    (S52_MAR_DOTPITCH_MM_Y,       "DOTPITCHYeditText"    );

    return TRUE;
}

static int      _sl4a_fullShow   (void)
{
    GError *error  = NULL;
    gchar  *layout = NULL;
    gsize   len    = 0;
    //gchar  *retstr = NULL;

    if (TRUE == g_file_get_contents(PATH "/UI.xml", &layout, &len, &error)) {
        _flattenstr(layout, len);
        //retstr =
        _send_cmd(&_sl4a_connectionA, "fullShow", layout);
    } else {
        // check this: can this call fail but still malloc 'layout'?
        g_print("sl4agps:_sl4a_fullShow(): g_file_get_contents() fail: %s [%i]\n", error->message, error->code);
    }

    if (NULL != layout) {
        g_free(layout);
    }

    _sl4a_fullShowSet();

    return TRUE;
}

static int      _sl4a_webViewShow(void)
{
    gchar *html = "'file:///data/media/dart/helloWeb.html',false"; // url, wait
    //gchar *html = "'file:///data/media/0/dart/s52ui/s52ui.html',false"; // url, wait
    //gchar *html = "'file:///sdcard/dart/s52ui/s52ui.html',false"; // url, wait
    //gchar *html = "'file:///data/data/com.android.chrome/s52ui/s52ui.html',false";
    //gchar *html = "'http://192.168.1.67:3030/home/sduclos/dev/prog/dart/dart-test/helloWeb/helloWeb.html',false"; // url, wait
    //gchar *html = "'http://192.168.1.67:3030/home/sduclos/dev/gis/openev-cvs/contrib/S52/test/s52ui/s52ui.html',false"; // url, wait
    //gchar *html = "'chrome://version/',false"; // url, wait
    _send_cmd(&_sl4a_connectionA, "webViewShow", html);

    return TRUE;
}

static int      _sl4a_viewHtml   (void)
{
    gchar *html = "'file:///data/media/dart/s52ui/s52ui.html'"; // url
    //gchar *html = "'/data/media/dart/s52ui/s52ui.html'"; // url
    //gchar *html = "'/sdcard/dart/s52ui/s52ui.html'"; // url
    //gchar *html = "'http://192.168.1.67:3030/home/sduclos/dev/gis/openev-cvs/contrib/S52/test/s52ui/s52ui.html'"; // url
    _send_cmd(&_sl4a_connectionA, "viewHtml", html);

    // FIXME: SL4A crash
    //_send_cmd(&_sl4a_connectionA, "view", html);

    return TRUE;
}

static int      _sl4a_getGPS     (double *lat, double *lon)
{
    *lat =  91.0; // init to out of bound
    *lon = 181.0; // init to out of bound

    char *retstr = _send_cmd(&_sl4a_connectionA, "readLocation", "");  // o=v
    if (NULL == retstr) {
        g_print("sl4agps:_sl4a_getGPS(): _send_cmd(): NULL\n");
        return FALSE;
    }

    {
        const gchar *latstr = g_strrstr(retstr, "latitude" );
        const gchar *lonstr = g_strrstr(retstr, "longitude");

        if ((NULL==latstr) || (NULL==lonstr)) {
            g_print("sl4agps:_sl4a_getGPS(): no GPS position, retstr: %s\n", retstr);

            return FALSE;
        }

        {
            errno = 0;
            gdouble latitude = g_ascii_strtod(latstr+10,   NULL);
            if (0 == errno) {
                *lat = latitude;
            }else {
                g_print("sl4agps:_sl4a_getGPS(): no GPS latitude: %s\n", latstr+10);
                return FALSE;
            }
        }

        {
            errno = 0;
            gdouble longitude = g_ascii_strtod(lonstr+11, NULL);
            if (0 == errno) {
                *lon = longitude;
            } else {
                g_print("sl4agps:_sl4a_getGPS(): no GPS longitude: %s\n", lonstr+10);
                return FALSE;
            }
        }
    }

    return TRUE;
}

static double   _sl4a_getGyro    (void)
{
    double azimuth = -1.0;
    //double pitch =  0.0;
    //double roll  =  0.0;

    char *retstr = _send_cmd(&_sl4a_connectionA, "sensorsReadOrientation", "");  // o=v
    if (NULL == retstr)
        return -1.0;


    {   // [azimuth, pitch, roll]
        const char *resultstr = g_strrstr(retstr, "result\":[");
        if (NULL == resultstr) {
            g_print("sl4agps:sl4agps:_sl4a_getGyro(): resultstr=NULL retstr=%s\n", retstr);
            return -1.0;
        }

        if (0 == g_strcmp0(resultstr, "result\":[null,null,null]}\n")) {
            g_print("sl4agps:sl4agps:_sl4a_getGyro(): resultstr=NULL retstr=%s\n", retstr);
            return -1.0;
        }

        //errno   = 0; // strtod() reset errno
        azimuth = g_ascii_strtod(resultstr+9,   NULL);
        if (0 != errno) {
            g_print("sl4agps:_sl4a_getGyro():g_ascii_strtod(): ERROR\n");
            return -1.0;
        }

        // BUG: find why android Gyro die some time and what it return in that case
        g_print("sl4agps:_sl4a_getGyro():azimuth=%f, resultstr=%s\n", azimuth, resultstr);

        azimuth *= RAD_TO_DEG;
        if (azimuth <    0.0) azimuth += 360.0;
        if (azimuth >= 360.0) azimuth -= 360.0;

    }

    return azimuth;
}

static int      _sl4a_parseEvent (void)
// FIXME: refactor check box
{
    //FIXME: find and order (need refactoring first)

    // check/poll for event from user
    char *retstr = _send_cmd(&_sl4a_connectionA, "eventWait", "1");
    if (NULL == retstr) {
        g_print("sl4agps:eventWait(): fail!\n\n");
        return TRUE;
    }

    // NOTE: there is no order in JSON, "id" can come before of after other element
    // this also meen that '}' can apper in the meedle string

    // debug
    //g_print("sl4agps:>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> eventWait(): %s\n", retstr);

    // no event - normal case
    // "result":null
    // FIXME: this work if assuming that the pattern is unique
    const gchar *resultstr = g_strrstr(retstr, "\"result\":null");
    if (NULL != resultstr) {
        g_print("sl4agps:_sl4a_parseEvent(): no event\n");
        return TRUE;
    }

    // debug
    g_print("sl4agps:>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> eventWait(): %s\n", retstr);

    // typical SL4A output
    // Android in HTML: droid.eventPost("say", "test 1 2 3", true);
    // I/stdout  ( 5093): sl4agps:>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> eventWait():
    //{"error":null,"id":1958,"result":{"data":"test 1 2 3","time":1353698208423000,"name":"say"}}



    // menu key event
    //"key":"82"
    // FIXME: this work if assuming that the pattern is unique
    const gchar *menustr = g_strrstr(retstr, "\"key\":\"82\"");
    if (NULL != menustr) {
        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
        g_print("sl4agps:menu key event\n");
        return TRUE;
    }

    // back key event - "key":"4"
    // FIXME: this work if assuming that the string pattern is unique
    const gchar *backstr = g_strrstr(retstr, "\"key\":\"4\"");
    if (NULL != backstr) {
        //_sl4a_moveToBackground();
        //_sl4a_fullShowSet();
        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
        g_print("sl4agps:back key event\n");
        return TRUE;
    }

    // --- S52_MAR_DISP_CATEGORY --------------------------------------------------
    //
    // STANDRARD check box
    //const gchar *catstdstr = g_strrstr(retstr, "\"id\":\"DISPCATSTDcheckBox\"");
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATSTDcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_STD);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // OTHER
    //const gchar *catotherstr = g_strrstr(retstr, "\"id\":\"DISPCATOTHERcheckBox\"");
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATOTHERcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_OTHER);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // SELECT
    //const gchar *catselectstr = g_strrstr(retstr, "\"id\":\"DISPCATSELECTcheckBox\"");
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATSELECTcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // Mariners Standard
    //const gchar *catselectstr = g_strrstr(retstr, "\"id\":\"DISPCATSELECTcheckBox\"");
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATMARSTDcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_STD);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // Mariners Other
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATMAROTHERcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_OTHER);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // Mariners Select
    if (NULL != g_strrstr(retstr, "\"id\":\"DISPCATMARSELECTcheckBox\"")) {
        _s52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_SELECT);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }
    // ------------------------------------------------------------------------------------


    // SHOWTEXT check box
    const gchar *showtextstr = g_strrstr(retstr, "\"id\":\"SHOWTEXTcheckBox\"");
    if (NULL != showtextstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_SHOW_TEXT, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_SHOW_TEXT, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // Color Palette Button
    // {"error":null,"id":18046,"result":{"data":{"id":"PLibColorPalBtn","type":"Button",
    // "visibility":"0","text":"1 - DAY"},"time":1338746197793000,"name":"click"}}
    const gchar *colorpalstr = g_strrstr(retstr, "PLibColorPalBtn");
    if (NULL != colorpalstr) {
        char         str[STRSZ];
        const gchar *palListstr = _s52_encodeNsend("S52_getPalettesNameList", "");

        if (NULL == palListstr)
            return FALSE;

        g_print("sl4agps:palListstr=%s\n", palListstr);

        int n = g_snprintf(str, STRSZ, "'listView1',%s", palListstr);

        {// strip '\n', '}' - replace 0x0a by 0x20
            int i = 0;
            for (i=0; i<n; ++i) {
                if ('\n' == str[i])
                    str[i] = ' ';
                if ('}' == str[i])
                    str[i] = ' ';
            }
        }

        g_print("sl4agps:PLibColorPalBtn():%s\n", str);

        _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'ListViewTitle','Text','Select Color Palettes'");
        _send_cmd(&_sl4a_connectionA, "fullSetList", str);

        return TRUE;
    }

    // ENC Button
    const gchar *encbtnstr = g_strrstr(retstr, "ENCBtn");
    if (NULL != encbtnstr) {
        char  str[STRSZ] = {'\0'};
        char  lst[STRSZ] = {'\0'};

        //g_strlcat(lst, "[", STRSZ);
        strcat(lst, "[");

        const gchar *cellNmliststr = _s52_encodeNsend("S52_getCellNameList", "");
        if (NULL == cellNmliststr)
            return FALSE;

        gchar **pstr = g_strsplit_set(cellNmliststr, "[',]}", 0);


        GDir *encdir = g_dir_open(PATH "/ENC_ROOT", 0, NULL);
        if (NULL != encdir) {
            int          adddelim = FALSE;
            const gchar *encstr   = NULL;
            while (NULL != (encstr = g_dir_read_name(encdir))) {
                if (TRUE == adddelim)
                    strcat(lst, ",");

                gchar **pstrtmp = pstr;
                while (NULL != *pstrtmp) {
                    if (('\n'!=**pstrtmp) && (NULL!=g_strrstr(*pstrtmp, encstr)))
                            break;

                    ++pstrtmp;
                }

                //g_strlcat(lst, "'", STRSZ);
                //g_strlcat(lst, encstr, STRSZ);
                //g_strlcat(lst, "',", STRSZ);
                if (NULL != *pstrtmp)
                    strcat(lst, "'*\t");
                else
                    strcat(lst, "' \t");

                strcat(lst, encstr);
                strcat(lst, "'");
                adddelim = TRUE;
            }
            g_dir_close(encdir);
        }
        g_strfreev(pstr);

        //g_strlcat(lst, "]", STRSZ);
        strcat(lst, "]");

        int n = g_snprintf(str, STRSZ, "'listView2',%s", lst);

        {// strip '\n', '}' - replace 0x0a by 0x20
            int i = 0;
            for (i=0; i<n; ++i) {
                if ('\n' == str[i])
                    str[i] = ' ';
                if ('}' == str[i])
                    str[i] = ' ';
            }
        }

        g_print("sl4agps:ENCBtn:%s\n", str);

        _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'ListViewTitle','Text','Select ENC'");
        _send_cmd(&_sl4a_connectionA, "fullSetList", str);

        return TRUE;
    }
    // {"error":null,"id":656,"result":{"data":{"id":"listView1","position":"1",
    // "selectedItemPosition":"-1","type":"ListView","visibility":"0"},
    //"time":1339607895492000,"name":"itemclick"}}
    const gchar *itemclickstr = g_strrstr(retstr, "\"name\":\"itemclick\"");
    if (NULL != itemclickstr) {
        const gchar *listView1str = g_strrstr(retstr, "\"id\":\"listView1\"");
        if (NULL != listView1str) {
            const gchar *positionstr = g_strrstr(retstr, "\"position\":\"");
            if (NULL != positionstr) {
                double d = g_ascii_strtod(positionstr+12, NULL);

                _s52_setMarinerParam(S52_MAR_COLOR_PALETTE, d);
                _s52_encodeNsend("S52_draw", "");
                //_sl4a_setColorPalName();
                //_sl4a_fullShow();
                _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
            }
            return TRUE;
        }

        const gchar *listView2str = g_strrstr(retstr, "\"id\":\"listView2\"");
        if (NULL != listView2str) {
            const gchar *positionstr = g_strrstr(retstr, "\"position\":\"");
            if (NULL != positionstr) {
                int i = (int)g_ascii_strtod(positionstr+12, NULL);

                GDir *encdir = g_dir_open(PATH "/ENC_ROOT", 0, NULL);
                if (NULL != encdir) {
                    int          count  = 0;
                    const gchar *encstr = NULL;
                    while (NULL != (encstr = g_dir_read_name(encdir))) {
                        if (i == count++) {
                            char encstrstr[80] = "'%s'";
                            sprintf(encstrstr, "'Loading/Unloading: %s'", encstr);
                            _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
                            _send_cmd(&_sl4a_connectionA, "makeToast", encstrstr);  // v=a

                            const gchar *retstr = _s52_encodeNsend("S52_loadCell", "\"%s/ENC_ROOT/%s\"", PATH, encstr);
                            //g_print("sl4agps:DEBUG:%s\n", retstr);
                            if ((NULL!=retstr) && ('0'==*(retstr+1)))
                                _s52_encodeNsend("S52_doneCell", "\"%s/ENC_ROOT/%s\"", PATH, encstr);

                            _s52_encodeNsend("S52_draw", "");

                            break;
                        }
                    }
                    g_dir_close(encdir);
                }
            }
        }
        return TRUE;
    }


    // --- PROFILING SWITCH -----------------------------------
    //
    // S52_CMD_WRD_FILTER/CMDWRDFILTERSYcheckBox
    const gchar *filersystr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERSYcheckBox\"");
    if (NULL != filersystr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // S52_CMD_WRD_FILTER/CMDWRDFILTERLScheckBox
    const gchar *filerlsstr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERLScheckBox\"");
    if (NULL != filerlsstr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // S52_CMD_WRD_FILTER/CMDWRDFILTERLCcheckBox
    const gchar *filerlcstr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERLCcheckBox\"");
    if (NULL != filerlcstr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // S52_CMD_WRD_FILTER/CMDWRDFILTERACcheckBox
    const gchar *fileracstr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERACcheckBox\"");
    if (NULL != fileracstr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // S52_CMD_WRD_FILTER/CMDWRDFILTERAPcheckBox
    const gchar *filerapstr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERAPcheckBox\"");
    if (NULL != filerapstr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // S52_CMD_WRD_FILTER/CMDWRDFILTERTXcheckBox
    const gchar *filertxstr = g_strrstr(retstr, "\"id\":\"CMDWRDFILTERTXcheckBox\"");
    if (NULL != filertxstr) {
        _s52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX);  // toggle

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }


    // SCAMIN check box
    // {"error":null,"id":351642,"result":{"data":{"id":"checkBox1",
    //"text":"","type":"CheckBox","visibility":"0","checked":"false"},
    //"time":1339070400763000,"name":"click"}}
    const gchar *scaminstr = g_strrstr(retstr, "\"id\":\"SCAMINcheckBox\"");
    if (NULL != scaminstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_SCAMIN, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_SCAMIN, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // AFTERGLOW check box
    const gchar *afglowstr = g_strrstr(retstr, "\"id\":\"AFTERGLOWcheckBox\"");
    if (NULL != afglowstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_AFTERGLOW, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // CALIB check box
    const gchar *calibstr = g_strrstr(retstr, "\"id\":\"CALIBRATIONcheckBox\"");
    if (NULL != calibstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_CALIB, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_CALIB, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // LEGEND check box
    const gchar *legendstr = g_strrstr(retstr, "\"id\":\"LEGENDcheckBox\"");
    if (NULL != legendstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_LEGEND, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_LEGEND, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // WORLD check box
    const gchar *worldstr = g_strrstr(retstr, "\"id\":\"WORLDcheckBox\"");
    if (NULL != worldstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_WORLD, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_WORLD, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // ANTIALIAS check box
    const gchar *antialiasstr = g_strrstr(retstr, "\"id\":\"ANTIALIAScheckBox\"");
    if (NULL != antialiasstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_ANTIALIAS, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_ANTIALIAS, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // CENTROIDS check box
    const gchar *centroidsstr = g_strrstr(retstr, "\"id\":\"CENTROIDScheckBox\"");
    if (NULL != centroidsstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_CENTROIDS, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_CENTROIDS, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // QUAPNT01 check box
    const gchar *quapnt01str = g_strrstr(retstr, "\"id\":\"QUAPNT01checkBox\"");
    if (NULL != quapnt01str) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_QUAPNT01, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_QUAPNT01, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // DRGARE PATTERN check box
    const gchar *drgarestr = g_strrstr(retstr, "\"id\":\"DRGAREPATTERNcheckBox\"");
    if (NULL != drgarestr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // NODATA LAYER check box
    const gchar *nodatastr = g_strrstr(retstr, "\"id\":\"NODATALAYERcheckBox\"");
    if (NULL != nodatastr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr)
            _s52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 1.0);  // on
        else
            _s52_setMarinerParam(S52_MAR_DISP_NODATA_LAYER, 0.0);  // off

        _s52_encodeNsend("S52_draw", "");

        return TRUE;
    }

    // VESSEL LABEL check Box
    const gchar *vessellabelstr = g_strrstr(retstr, "\"id\":\"VESSELLABELcheckBox\"");
    if (NULL != vessellabelstr) {
        const gchar *checkedstr = g_strrstr(retstr, "\"checked\":\"false\"");
        if (NULL == checkedstr) {
            _s52_encodeNsend("S52_setTextDisp", "75, 2, 0");
        } else {
            _s52_encodeNsend("S52_setTextDisp", "75, 2, 1");
        }

        return TRUE;
    }

    // ROTBUOYLIGHTeditText
    // {"error":null,"id":498,"result":
    // {"data":
    // {"id":"ROTBUOYLIGHTeditText","text":"---.-°","type":"EditText","visibility":"0""tag":"edit_off"}
    // "time":1339782952131000,"name":"click"}}
    const gchar *rotbuoyeditstr = g_strrstr(retstr, "\"id\":\"ROTBUOYLIGHTeditText\"");
    if (NULL != rotbuoyeditstr) {
        //g_print("sl4agps:------------------------- ROTBUOYLIGHTeditText: retstr=%s\n", retstr);

        const gchar *textstr = g_strrstr(retstr, "\"text\":\"---");
        if (NULL != textstr) {
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'ROTBUOYLIGHTeditText','Text','0.0°'");

            // debug
            //_s52_setMarinerParam(S52_MAR_ROT_BUOY_LIGHT, 178.0);
            //_sl4a_setDegText(S52_MAR_ROT_BUOY_LIGHT, "ROTBUOYLIGHTeditText");
        }

        textstr = g_strrstr(retstr, "\"tag\":\"edit_off\"");
        if (NULL != textstr) {
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'ROTBUOYLIGHTeditText','inputType','numberDecimal'");
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'ROTBUOYLIGHTeditText','tag','edit_on'");
        }

        textstr = g_strrstr(retstr, "\"text\":\"");
        if (NULL != textstr) {
            double d = g_ascii_strtod(textstr+8, NULL);
            _s52_setMarinerParam(S52_MAR_ROT_BUOY_LIGHT, d);
            _sl4a_setDegText(S52_MAR_ROT_BUOY_LIGHT, "ROTBUOYLIGHTeditText");
            _s52_encodeNsend("S52_draw", "");
        }

        return TRUE;
    }

    const gchar *dotpitchxstr = g_strrstr(retstr, "\"id\":\"DOTPITCHXeditText\"");
    if (NULL != dotpitchxstr) {
        const gchar *textstr = g_strrstr(retstr, "\"tag\":\"edit_off\"");
        if (NULL != textstr) {
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'DOTPITCHXeditText','inputType','numberDecimal'");
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'DOTPITCHXeditText','tag','edit_on'");
        }

        textstr = g_strrstr(retstr, "\"text\":\"");
        if (NULL != textstr) {
            double d = g_ascii_strtod(textstr+8, NULL);
            _s52_setMarinerParam(S52_MAR_DOTPITCH_MM_X, d);
            _sl4a_setText(S52_MAR_DOTPITCH_MM_X, "DOTPITCHXeditText");
            _s52_encodeNsend("S52_draw", "");
        }

        return TRUE;
    }

    const gchar *dotpitchystr = g_strrstr(retstr, "\"id\":\"DOTPITCHYeditText\"");
    if (NULL != dotpitchystr) {
        const gchar *textstr = g_strrstr(retstr, "\"tag\":\"edit_off\"");
        if (NULL != textstr) {
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'DOTPITCHYeditText','inputType','numberDecimal'");
            _send_cmd(&_sl4a_connectionA, "fullSetProperty", "'DOTPITCHYeditText','tag','edit_on'");
        }

        textstr = g_strrstr(retstr, "\"text\":\"");
        if (NULL != textstr) {
            double d = g_ascii_strtod(textstr+8, NULL);
            _s52_setMarinerParam(S52_MAR_DOTPITCH_MM_Y, d);
            _sl4a_setText(S52_MAR_DOTPITCH_MM_Y, "DOTPITCHYeditText");
            _s52_encodeNsend("S52_draw", "");
        }

        return TRUE;
    }



    // test 1 - listiew
    // {"error":null,"id":38,"result":{"data":{"id":null,"type":"Button",
    // "visibility":"0","text":"One"},"time":1338048791125000,"name":"click"}}
    //const gchar *b2str = g_strrstr(retstr, "\"text\":\"1 -");
    const gchar *test1str = g_strrstr(retstr, "\"id\":\"test1\"");
    if (NULL != test1str) {
        //_sl4a_fullShow();
        _send_cmd(&_sl4a_connectionA, "fullSetList", "'listView1',['A','B','C']");
        return TRUE;
    }

    // test 2 - listiew
    // {"error":null,"id":38,"result":{"data":{"id":null,"type":"Button",
    // "visibility":"0","text":"One"},"time":1338048791125000,"name":"click"}}
    const gchar *test2str = g_strrstr(retstr, "\"id\":\"test2\"");
    if (NULL != test2str) {
        //_sl4a_fullShow();
        _send_cmd(&_sl4a_connectionA, "fullSetList", "'listView2',['One','Two','Three']");
        return TRUE;
    }

    // View Google Map
    // {"error":null,"id":38,"result":{"data":{"id":null,"type":"Button",
    // "visibility":"0","text":"One"},"time":1338048791125000,"name":"click"}}
    const gchar *test3str = g_strrstr(retstr, "\"id\":\"test3\"");
    if (NULL != test3str) {
        //_sl4a_fullShow();
        //_send_cmd(_sl4a_connectionA, "viewMap", "'Rimouski'");
        // FIXME: get GPS
        _send_cmd(&_sl4a_connectionA, "viewMap", "'48.493094,-68.495293'");
        return TRUE;
    }

    return TRUE;
}

static int      _sl4a_sendS52cmd (gpointer user_data)
{
    {
        int    vecstb  = 1;     // overground
        double speed   = 60.0;  // conspic
        double azimuth = _sl4a_getGyro();
        if (-1.0 != azimuth) {
            _s52_encodeNsend("S52_setVector",      "%lu,%i,%lf,%lf",     (long unsigned int) _s52_ownshp, vecstb, azimuth, speed);
            //_s52_encodeNsend("S52_setVESSELlabel", "%lu,\"OWNSHP%s%05.1f deg / %4.1f kt\"", (long unsigned int) _s52_ownshp, TB, azimuth, speed);
            _s52_encodeNsend("S52_setVESSELlabel", "%lu,\"OWNSHP%s%05.1f deg / %4.1f kt\"", (long unsigned int) _s52_ownshp, NL, azimuth, speed);
        }

        double lat = 0.0;
        double lon = 0.0;
        if (TRUE == _sl4a_getGPS(&lat, &lon)) {
            _s52_encodeNsend("S52_pushPosition", "%lu,%lf,%lf,%lf", (long unsigned int) _s52_ownshp, lat, lon, azimuth);
            //g_print("sl4agps:lat:%f lon:%f az:%f\n", lat, lon, azimuth);
        }
    }

    // check if user touch something
    _sl4a_parseEvent();

    // must return TRUE for the timeout to call again
    return TRUE;
}

static void     _trapSIG(int sig, siginfo_t *info, void *secret)
{
    switch (sig) {

        // 2
    case SIGINT:
        g_print("sl4agps:_trapSIG(): Signal 'Interrupt' cought - SIGINT(%i)\n", sig);
        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");

        //_fullShowUp = FALSE;

        g_main_loop_quit(_main_loop);
        break;

        // 11
    case SIGSEGV:
        g_print("sl4agps:_trapSIG(): Signal 'Segmentation violation' cought - SIGSEGV(%i)\n", sig);
        unlink(GPS PID);
        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");

        // continue with normal sig handling
        _old_signal_handler_SIGSEGV.sa_sigaction(sig, info, secret);
        break;

        // 15
    case SIGTERM:
        g_print("sl4agps:_trapSIG(): Signal 'Termination (ANSI)' cought - SIGTERM(%i)\n", sig);
        unlink(GPS PID);

        _send_cmd(&_sl4a_connectionA, "fullDismiss", "");
        g_main_loop_quit(_main_loop);

        // continue with normal sig handling
        _old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);
        break;

        // 10
    case SIGUSR1: {
        //if (TRUE == _fullShowUp) {
        //    _sl4a_moveToForeground();
        //} else {
            g_print("sl4agps:_trapSIG(): Signal 'User-defined 1' cought - SIGUSR1(%i)\n", sig);


            // prevent call from _trapSIG and main_loop to _send_cmd()
            // at the same time
            g_static_mutex_lock(&_mp_mutex);

            _send_cmd(&_sl4a_connectionA, "makeToast", "'LOADING GUI ..'");  // v=a

            // the stop/start of sensor save CPU for processing fullShow()
            // with this it take 2 sec instead of 3 sec to load the XML layout
            //_send_cmd(_sl4a_connectionA, "stopSensing", "1,1000000");
            //_send_cmd(_sl4a_connectionA, "stopLocating", "");    // 0.5 sec

            _sl4a_fullShow();


            // 2012NOV: loading html5 via sl4a webKit is very fast compare to fullshow()
            //_sl4a_webViewShow();

            // 2012DEC: loading html5 via sl4a
            //_sl4a_viewHtml();

            //_send_cmd(_sl4a_connectionA, "startSensingTimed", "1,1000000");
            //_send_cmd(_sl4a_connectionA, "startLocating", "");    // 0.5 sec

            //_fullShowUp = TRUE;

            g_static_mutex_unlock(&_mp_mutex);

            // continue with normal sig handling
            if (NULL != _old_signal_handler_SIGUSR1.sa_sigaction)
                _old_signal_handler_SIGUSR1.sa_sigaction(sig, info, secret);

            break;
    }
    // 12
    case SIGUSR2:
        g_print("sl4agps:_trapSIG(): Signal 'User-defined 2' cought - SIGUSR2(%i)\n", sig);

        // re-connect to libS52
        if (NULL == _s52_connection) {
            //_s52_connection = _init_sock(_localhost, S52_PORT);

            _s52_init();
            g_print("sl4agps:_trapSIG(): re-connect to libS52\n");
        }

        /*
        const gchar cmd[] =
            "sh   /system/bin/am start       "
            "-a   android.intent.action.MAIN "
            "-n   nav.ecs.s52android/.s52ui  ";

        // Android quirk: g_spawn() return the return value of the cmd (here 0)
        // check this: return FALSE to meen SUCCESS!!
        int ret = g_spawn_command_line_async(cmd, NULL);
        if (FALSE == ret) {
            g_print("sl4agps: fail to start s52ui activity\n");
            return FALSE;
        } else {
            g_print("sl4agps: s52ui started\n");
        }
        */


        // continue with normal sig handling
        if (NULL != _old_signal_handler_SIGUSR2.sa_sigaction)
            _old_signal_handler_SIGUSR2.sa_sigaction(sig, info, secret);

        break;

    default:
        g_print("sl4agps:_trapSIG():handler not found [%i]\n", sig);
        break;
    }

    return;
}

int main(int argc, char *argv[])
{
    g_print("sl4agps:main():starting: argc=%i, argv[0]=%s\n", argc, argv[0]);

    g_thread_init(NULL);
    g_type_init();

    // init string - need room to get the resp
    _json_obj = g_string_sized_new(128);
    _params   = g_string_sized_new(128);

    if (FALSE == _sl4a_init()) {
        g_print("sl4agps:_sl4a_init() failed\n");
        return FALSE;
    }

    if (FALSE == _s52_init()) {
        g_print("sl4agps:_s52_init() failed\n");
        return FALSE;
    }

    //////////////////////////////////////////////////////////
    // init signal handler
    // CTRL-C/SIGINT/SIGTERM
    // SIGUSR1 	User-defined 1 	10
    // SIGUSR2 	User-defined 2 	12
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = _trapSIG;
        //sigemptyset(&sa.sa_mask);
        //sa.sa_flags = SA_RESTART | SA_SIGINFO;
        sa.sa_flags = SA_SIGINFO;

        //  2 - Interrupt (ANSI) - user press ESC to stop rendering
        sigaction(SIGINT,  &sa, &_old_signal_handler_SIGINT);
        sigaction(SIGUSR1, &sa, &_old_signal_handler_SIGUSR1);
        sigaction(SIGUSR2, &sa, &_old_signal_handler_SIGUSR2);
        // 11 - Segmentation violation (ANSI).
        sigaction(SIGSEGV, &sa, &_old_signal_handler_SIGSEGV);   // loop in android
        sigaction(SIGTERM, &sa, &_old_signal_handler_SIGTERM);
    }

    /*
    char *retstr = _sl4a_send_rpc("prefGetAll", "");
    if (NULL == retstr) {
        g_print("sl4agps:main():prefGetAll(): retstr=NULL\n");
    } else {
        g_print("sl4agps:main():prefGetAll(): retstr='%s'\n", retstr);
    }
    */

    {   // save pid
        // FIXME: check if an instance of sl4agps is allready running
        // an signal it somehow to reconnect to libS52
        // -OR- kill the previous instance first!
        if (TRUE == g_file_test(GPS PID, (GFileTest) (G_FILE_TEST_EXISTS))) {
            GError    *error    = NULL;
            const char done[] = "/system/bin/sh -c 'kill -SIGINT `cat " GPS PID "`'";
            if (TRUE != g_spawn_command_line_async(done, &error)) {
                g_print("sl4agps:g_spawn_command_line_async() failed [%s]\n", GPS);
                return FALSE;
            }
            unlink(GPS PID);
            g_print("sl4agps:GPS prog allready running stopped (%s)\n", GPS);
        } else
            g_print("sl4agps:GPS prog not running (%s)\n", GPS);



        pid_t pid = getpid();
        FILE *fd  = fopen(GPS PID, "w");
        if (NULL != fd) {
            fprintf(fd, "%i", pid);
            fclose(fd);
        } else {
            g_print("sl4agps:fopen(): fail .. exit");
            return TRUE;
        }

    }

    guint timeoutID = g_timeout_add(1000, _sl4a_sendS52cmd, NULL);  // 1.0 sec
    if (0 == timeoutID) {
        g_print("sl4agps:g_timeout_add(): fail .. exit");
        return TRUE;
    }


    _main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(_main_loop);

    unlink(GPS PID);

    _s52_done();

    _sl4a_done();

    g_string_free(_json_obj, TRUE);
    g_string_free(_params,   TRUE);

    g_print("sl4agps:main(): exiting ..\n");

    return TRUE;
}
