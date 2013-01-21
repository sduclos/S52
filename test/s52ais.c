// s52ais.c: GPSD client data to libS52.so
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013  Sylvain Duclos sduclos@users.sourceforgue.net

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

#ifndef S52AIS_STANDALONE
#include "s52ais.h"
#endif

#include "S52.h"            // S52_*(),

#include <gps.h>            // gps_*(), client interface to gpsd

#include <sys/select.h>     // FD*
#include <errno.h>          //

#include <string.h>         // memcpy()

#include <glib.h>           //
#include <glib/gprintf.h>   // g_sprintf()
#include <glib/gstdio.h>    // g_stat()

#ifdef WIN32
#include <winsock2.h>
#endif

#ifdef S52_USE_ANDROID
#include <android/log.h>
#define  LOG_TAG    "s52ais"

#define PATH "/data/media"
//#define PATH "/data/media/0"
#define AIS  PATH "/s52android/bin/s52ais"
#define PID  ".pid"
//#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
//#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
//#define g_print g_message
#endif

/*
//Code - Ship & Cargo Classification
static char *_shipCargoClassName[] = {
"20 - Wing in ground (WIG), all ships of this type",
"21 - Wing in ground (WIG), Hazardous category A",
"22 - Wing in ground (WIG), Hazardous category B",
"23 - Wing in ground (WIG), Hazardous category C",
"24 - Wing in ground (WIG), Hazardous category D",
"25 - Wing in ground (WIG), Reserved for future use",
"26 - Wing in ground (WIG), Reserved for future use",
"27 - Wing in ground (WIG), Reserved for future use",
"28 - Wing in ground (WIG), Reserved for future use",
"29 - Wing in ground (WIG), Reserved for future use",
"30 - Fishing",
"31 - Towing",
"32 - Towing: length exceeds 200m or breadth exceeds 25m",
"33 - Dredging or underwater ops",
"34 - Diving ops",
"35 - Military Ops",
"36 - Sailing",
"37 - Pleasure Craft",
"38 - Reserved",
"39 - Reserved",
"40 - High speed craft (HSC), all ships of this type",
"41 - High speed craft (HSC), Hazardous category A",
"42 - High speed craft (HSC), Hazardous category B",
"43 - High speed craft (HSC), Hazardous category C",
"44 - High speed craft (HSC), Hazardous category D",
"45 - High speed craft (HSC), Reserved for future use",
"46 - High speed craft (HSC), Reserved for future use",
"47 - High speed craft (HSC), Reserved for future use",
"48 - High speed craft (HSC), Reserved for future use",
"49 - High speed craft (HSC), No additional information",
"50 - Pilot Vessel",
"51 - Search and Rescue vessel",
"52 - Tug",
"53 - Port Tender",
"54 - Anti-pollution equipment",
"55 - Law Enforcement",
"56 - Spare - Local Vessel",
"57 - Spare - Local Vessel",
"58 - Medical Transport",
"59 - Ship according to RR Resolution No. 18",
"60 - Passenger, all ships of this type",
"61 - Passenger, Hazardous category A",
"62 - Passenger, Hazardous category B",
"63 - Passenger, Hazardous category C",
"64 - Passenger, Hazardous category D",
"65 - Passenger, Reserved for future use",
"66 - Passenger, Reserved for future use",
"67 - Passenger, Reserved for future use",
"68 - Passenger, Reserved for future use",
"69 - Passenger, No additional information",
"70 - Cargo, all ships of this type",
"71 - Cargo, Hazardous category A",
"72 - Cargo, Hazardous category B",
"73 - Cargo, Hazardous category C",
"74 - Cargo, Hazardous category D",
"75 - Cargo, Reserved for future use",
"76 - Cargo, Reserved for future use",
"77 - Cargo, Reserved for future use",
"78 - Cargo, Reserved for future use",
"79 - Cargo, No additional information",
"80 - Tanker, all ships of this type",
"81 - Tanker, Hazardous category A",
"82 - Tanker, Hazardous category B",
"83 - Tanker, Hazardous category C",
"84 - Tanker, Hazardous category D",
"85 - Tanker, Reserved for future use",
"86 - Tanker, Reserved for future use",
"87 - Tanker, Reserved for future use",
"88 - Tanker, Reserved for future use",
"89 - Tanker, No additional information",
"90 - Other Type, all ships of this type",
"91 - Other Type, Hazardous category A",
"92 - Other Type, Hazardous category B",
"93 - Other Type, Hazardous category C",
"94 - Other Type, Hazardous category D",
"95 - Other Type, Reserved for future use",
"96 - Other Type, Reserved for future use",
"97 - Other Type, Reserved for future use",
"98 - Other Type, Reserved for future use",
"99 - Other Type, No additional information"
};
//    unsigned int  shipCargoClassID;     // shipCargoClassID - 20 ==> _shipCargoClassName
*/

typedef struct _ais_t {
    unsigned int    mmsi;
    int             status;
    int             turn;     // Rate of turn
    char            name[AIS_SHIPNAME_MAXLEN + 1];

    GTimeVal        lastUpdate;
    double          course;
    double          speed;
    S52ObjectHandle vesselH;

#ifdef S52_USE_AFGLOW
    S52ObjectHandle afglowH;
#endif
} _ais_t;

static struct gps_data_t  _gpsdata;

static GArray            *_ais_list       = NULL;
static GStaticMutex       _ais_list_mutex = G_STATIC_MUTEX_INIT;  // protect _ais_list

#define AIS_SILENCE_MAX 600   // sec of silence from an AIS before deleting it

//#define MAX_AFGLOW_PT (15 * 60)   // 15 min trail @ 1 pos per sec - trail too long 
#define MAX_AFGLOW_PT (12 * 20)     // 12 min @ 1 pos per 5 sec
//#define MAX_AFGLOW_PT 10          // debug
//#define MAX_AFGLOW_PT 100         // debug

// debug - mmsi of AIS for testing ownship
#define OWNSHIP  0             // debug
//#define OWNSHIP 316133000    // CORIOLIS II
//#define OWNSHIP 316006302    // CNM EVOLUTION
//#define OWNSHIP 371204000    // ???
//#define OWNSHIP 316007848    // ALPHONSE DESJARDINS
//#define OWNSHIP 316007853    // LOMER GOUIN

// trap signal
#include <sys/types.h>
#include <signal.h>
static struct sigaction _old_signal_handler_SIGINT;
static struct sigaction _old_signal_handler_SIGUSR1;
static struct sigaction _old_signal_handler_SIGUSR2;
static struct sigaction _old_signal_handler_SIGSEGV;   // loop in android
static struct sigaction _old_signal_handler_SIGTERM;



#ifdef S52_USE_SOCK
//static int  _s52_initOK  = FALSE; //
static char _localhost[] = "127.0.0.1";
#define S52_PORT            2950
#include <gio/gio.h>
static GSocketConnection  *_s52_connection = NULL;
#define BUFSZ 2048
static char  _response[BUFSZ];
static char  _params  [BUFSZ];  // JSON
static int   _request_id = 0;
static GTimeVal _timeTick;
#endif

#ifdef S52_USE_DBUS
// DBUS messaging
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
DBusConnection *_dbus      = NULL;
DBusError       _dbusError;

// experiment
// NOTE: only DBus signal are send to the outside from here
//

static DBusMessage  *_newBDusSignal(const char *sigName)
// caller must free msg
{
    g_return_val_if_fail(_dbus, NULL);

    DBusMessage *message = dbus_message_new_signal(S52_DBUS_OBJ_PATH,  // (path of) signal of object
                                                   S52_DBUS_OBJ_NAME,  // interface of signal of Object
                                                   sigName);
    if (NULL == message) {
        g_warning("_newBDusSignal(): ERROR: new signal msg failed\n");
        g_assert(0);
        return FALSE;
    }

    return message;
}

static int           _signal_delMarObj     (DBusConnection *bus, S52ObjectHandle objH, char *name)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_delMarObj");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_STRING, &name,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}


static int           _signal_newVESSEL     (DBusConnection *bus, S52ObjectHandle objH, char *name)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_newVESSEL");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_STRING, &name,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

  return TRUE;
}


static int           _signal_setVESSELlabel(DBusConnection *bus, S52ObjectHandle objH, char *name)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_setVESSELlabel");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_STRING, &name,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}

static int           _signal_setVESSELstate(DBusConnection *bus, S52ObjectHandle objH,
                                              int vesselSelect, int status, int turn)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_setVESSELstate");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_INT32,  &vesselSelect,
                             DBUS_TYPE_INT32,  &status,
                             DBUS_TYPE_INT32,  &turn,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}

static int           _signal_setPosition   (DBusConnection *bus, S52ObjectHandle objH,
                                              double lat, double lon, double heading)
{
    g_return_val_if_fail(_dbus, FALSE);
    DBusMessage *message = _newBDusSignal("signal_setPosition");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_DOUBLE, &lat,
                             DBUS_TYPE_DOUBLE, &lon,
                             DBUS_TYPE_DOUBLE, &heading,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}

static int           _signal_setVector     (DBusConnection *bus, S52ObjectHandle objH,
                                              int vecstb, double course, double speed)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_setVector");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_INT32,  &vecstb,
                             DBUS_TYPE_DOUBLE, &course,
                             DBUS_TYPE_DOUBLE, &speed,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}

static int           _signal_setAISInfo    (DBusConnection *bus, S52ObjectHandle objH,
                                                   unsigned int mmsi, unsigned int imo, char *callsign,
                                                   unsigned int shiptype,
                                                   unsigned int month, unsigned int day, unsigned int hour, unsigned int minute,
                                                   unsigned int draught, char *destination)
{
    g_return_val_if_fail(_dbus, FALSE);

    DBusMessage *message = _newBDusSignal("signal_setAISStaticInfo");

    dbus_message_append_args(message,
                             DBUS_TYPE_DOUBLE, &objH,
                             DBUS_TYPE_UINT32, &mmsi,
                             DBUS_TYPE_UINT32, &imo,
                             DBUS_TYPE_STRING, &callsign,
                             DBUS_TYPE_UINT32, &shiptype,
                             DBUS_TYPE_UINT32, &month,
                             DBUS_TYPE_UINT32, &day,
                             DBUS_TYPE_UINT32, &hour,
                             DBUS_TYPE_UINT32, &minute,
                             DBUS_TYPE_UINT32, &draught,
                             DBUS_TYPE_STRING, &destination,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(bus, message, NULL);

    dbus_message_unref(message);

    return TRUE;
}

static int           _initDBus(void)
{
    // Get a connection to the session bus
    dbus_error_init(&_dbusError);
    _dbus = dbus_bus_get(DBUS_BUS_SESSION, &_dbusError);
    if (!_dbus) {
        g_warning("Failed to connect to the D-BUS daemon: %s", _dbusError.message);
        dbus_error_free(&_dbusError);

        return FALSE;
    }

    // WARNING: dbus msg move around when the main loop run
    //dbus_connection_setup_with_g_main(_dbus, NULL);

    //dbus_bus_request_name(_dbus, S52_DBUS_OBJ_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &_dbusError);

    // do not exit on disconnect
    dbus_connection_set_exit_on_disconnect(_dbus, FALSE);

    g_print("s52gpsd:_initDBus():%s:%s\n", _dbusError.name, _dbusError.message);

    return TRUE;
}
#endif // S52_USE_DBUS

#ifdef S52_USE_SOCK
static GSocketConnection *_s52_init_sock(char *hostname, int port)
{

    //g_print("_init_s52_sock(): starting ..\n");

    GSocketClient *client = g_socket_client_new();
    if (NULL == client) {
        g_print("s52ais:_s52_init_sock(): client NULL  ..\n");
        return NULL;
    }

    GSocketConnectable *connectable = g_network_address_new(hostname, port);
    if (NULL == client) {
        g_print("s52ais:_s52_init_sock(): connectable NULL  ..\n");
        return NULL;
    }

    GError            *error = NULL;
    GSocketConnection *conn  = g_socket_client_connect(client, connectable, NULL, &error);

    g_object_unref(client);
    g_object_unref(connectable);

    if (NULL != error) {
        g_print("s52ais:_s52_init_sock():ERROR: %s\n", error->message);
        return NULL;
    }

    g_print("s52ais:_s52_init_sock(): connected to hostname:%s, port:%i\n", hostname, port);

    return conn;
}

static char         *_s52_send_cmd(const char *command, const char *params)
{
    // debug
    //g_print("s52ais:_s52_send_cmd(): start:%s\n", params);

    if (NULL == _s52_connection) {
        g_print("s52ais:_s52_send_cmd(): fail - no conection\n");
        return NULL;
    }

    GSocket *socket = g_socket_connection_get_socket(_s52_connection);
    if (NULL == socket) {
        g_print("s52ais:_s52_send_cmd(): fail - no socket\n");
        return NULL;
    }

    GError *error = NULL;
    guint   n     = 0;

    // build a full JSON object
    n = g_snprintf(_response, BUFSZ, "{\"id\":%i,\"method\":\"%s\",\"params\":[%s]}\n", _request_id++, command, params);
    if (n > BUFSZ) {
        g_print("s52ais:_s52_send_cmd():g_snprintf(): no space in buffer\n");
        return NULL;
    }
    _response[n] = '\0';

    //g_print("s52ais.c:_s52_send_cmd(): sending:%s", _response);

    gssize szsnd = g_socket_send_with_blocking(socket, _response, n, FALSE, NULL, &error);
    if ((NULL!=error) || (0==szsnd) || (-1==szsnd)) {
        //  0 - connection was closed by the peer
        // -1 - on error
        if (NULL == error)
            g_print("s52ais:_s52_send_cmd():ERROR:g_socket_send_with_blocking(): connection close [%s]\n", _response);
        else
            g_print("s52ais:_s52_send_cmd():ERROR:g_socket_send_with_blocking(): %s [%i:%s]\n", error->message, error->code, _response);

        _s52_connection = NULL; // reset connection

        return NULL;
    }

    //g_print("s52ais.c:_s52_send_cmd(): sended:%s", _response);

    // wait response - blocking socket
    gssize szrcv = g_socket_receive_with_blocking(socket, _response, BUFSZ, TRUE, NULL, &error);
    if ((NULL!=error) || (0==szrcv) || (-1==szrcv)) {
        //  0 - connection was closed by the peer
        // -1 - on error
        if (NULL == error)
            g_print("s52ais:_s52_send_cmd():ERROR:g_socket_receive_with_blocking(): connection close [%s]\n", _response);
        else
            g_print("s52ais:_s52_send_cmd():ERROR:g_socket_receive_with_blocking(): %s [%i:%s]\n", error->message, error->code, _response);

        _s52_connection = NULL; // reset connection

        return NULL;
    }
    _response[szrcv] = '\0';

    //g_print("s52ais:_s52_send_cmd(): received:%s\n", _response);

    gchar *result = g_strrstr(_response, "result");
    if (NULL == result) {
        g_print("s52ais:_s52_send_cmd():no result [_response:'%s']\n", _response);
        return NULL;
    }


    return _response;
}

static char         *_encodeNsend(const char *command, const char *frmt, ...)
{
    va_list argptr;
    va_start(argptr, frmt);
    g_vsnprintf(_params, BUFSZ, frmt, argptr);
    va_end(argptr);

    gchar *resp = _s52_send_cmd(command, _params);
    if (NULL != resp) {
        while (*resp != '[')
            resp++;
    }

    return resp;
}
#endif

static _ais_t       *_getAIS    (unsigned int mmsi)
{
    // return this
    _ais_t *ais = NULL;

    // check that gps_done() haven't flush this
    if (NULL == _ais_list) {
        g_print("s52ais:_getAIS() no AIS list\n");
        return NULL;
    }

    unsigned int i = 0;
    for (i=0; i<_ais_list->len; ++i) {
        ais = &g_array_index(_ais_list, _ais_t, i);
        if (mmsi == ais->mmsi) {
            return ais;
        }
    }

    {   // NEW AIS (not found hence new)
        _ais_t newais;
        newais.mmsi     = mmsi;
        newais.status   = -1;     // 0 indicate that status form report is needed
        newais.name[AIS_SHIPNAME_MAXLEN + 1] = '\0';
        g_get_current_time(&newais.lastUpdate);
        newais.course   = -1.0;
        newais.speed    =  0.0;

        // create an active symbol, put mmsi since status is not known yet
        g_sprintf(newais.name, "%i", mmsi);

#ifdef S52_USE_SOCK
        // debug: make ferry acte as ownshp
        if (OWNSHIP == mmsi) {
            gchar *resp = _encodeNsend("S52_newOWNSHP", "\"%s\"", newais.name);
            if (NULL != resp) {
                sscanf(resp, "[ %lu", (long unsigned int *) &newais.vesselH);
            }
        } else {
            gchar *resp = _encodeNsend("S52_newVESSEL", "%i,\"%s\"", 2, newais.name);
            if (NULL != resp) {
                sscanf(resp, "[ %lu", (long unsigned int *) &newais.vesselH);
            }
            g_printf("s52ais:_getAIS(): new vesselH:%lu\n", (long unsigned int) newais.vesselH);
        }
#else
        // debug: make ferry acte as ownshp
        if (OWNSHIP == mmsi)
            newais.vesselH = S52_newOWNSHP(newais.name);
        else
            newais.vesselH = S52_newVESSEL(2, newais.name);
#endif

        // new AIS failed
        if (NULL == newais.vesselH) {
            g_print("s52ais:_getAIS(): new vesselH fail\n");
            return NULL;
        }

#ifdef S52_USE_AFGLOW
#ifdef S52_USE_SOCK
        // debug: make ferry acte as ownshp
        if (OWNSHIP == mmsi) {
            gchar *resp = _encodeNsend("S52_newMarObj", "\"%s\",%i,%i", "afgshp", S52_LINES, MAX_AFGLOW_PT);
            if (NULL != resp)
                sscanf(resp, "[ %lu", (long unsigned int *) &newais.afglowH);

        } else {
            gchar *resp = _encodeNsend("S52_newMarObj", "\"%s\",%i,%i", "afgves", S52_LINES, MAX_AFGLOW_PT);
            if (NULL != resp)
                sscanf(resp, "[ %lu", (long unsigned int *) &newais.afglowH);

            g_printf("s52ais:_getAIS(): new afglowH:%lu\n", (long unsigned int) newais.afglowH);

        }
#else
        // debug: make ferry acte as ownshp
        if (OWNSHIP == mmsi)
            newais.afglowH = S52_newMarObj("afgshp", S52_LINES, MAX_AFGLOW_PT, NULL, NULL);
        else
            newais.afglowH = S52_newMarObj("afgves", S52_LINES, MAX_AFGLOW_PT, NULL, NULL);

#endif
        if (NULL == newais.afglowH) {
            g_print("s52ais:_getAIS(): new afglowH fail\n");
            return NULL;
        }
#endif

        // save S52obj handle after registered in libS52
        g_array_append_val(_ais_list, newais);
        ais = &g_array_index(_ais_list, _ais_t, _ais_list->len - 1);


#ifdef S52_USE_DBUS
        _signal_newVESSEL(_dbus, newais.vesselH, newais.name);
#endif

    }

    // can't be NULL
    if (NULL == ais) {
        g_assert(0);
    }

    return ais;
}

static int           _setAISInfo(unsigned int mmsi, unsigned int imo, char *callsign,
                                 unsigned int shiptype,
                                 unsigned int month, unsigned int day, unsigned int hour, unsigned int minute,
                                 unsigned int draught, char *destination)
{
    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

#ifdef S52_USE_DBUS
    _signal_setAISInfo(_dbus, ais->vesselH, mmsi, imo, callsign, shiptype,
                       month, day, hour, minute, draught, destination);
#endif

    return TRUE;
}


static int           _setAISPos (unsigned int mmsi, double lat, double lon, double heading)
{
    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

    g_get_current_time(&ais->lastUpdate);


#ifdef S52_USE_SOCK
    _encodeNsend("S52_pushPosition", "%lu,%lf,%lf,%lf", (long unsigned int *)ais->vesselH, lat, lon, heading);
#else
    S52_pushPosition(ais->vesselH, lat, lon, heading);
#endif

#ifdef S52_USE_AFGLOW
#ifdef S52_USE_SOCK
    _encodeNsend("S52_pushPosition", "%lu,%lf,%lf,%lf", (long unsigned int *)ais->afglowH, lat, lon, heading);
#else
    S52_pushPosition(ais->afglowH, lat, lon, 0.0);
#endif
#endif

#ifdef S52_USE_DBUS
    _signal_setPosition   (_dbus, ais->vesselH, lat, lon, heading);
    _signal_setVESSELlabel(_dbus, ais->vesselH, ais->name);
#endif


    return TRUE;
}

static int           _setAISVec (unsigned int mmsi, double course, double speed)
{
    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

    int vecstb = 1; // overground
    // ground vector (since AIS transmit the GPS)
    ais->course = course;
    ais->speed  = speed;

#ifdef S52_USE_SOCK
    _encodeNsend("S52_setVector", "%lu,%i,%lf,%lf", ais->vesselH, vecstb, course, speed);
#else
    S52_setVector(ais->vesselH, vecstb, course, speed);
#endif

#ifdef S52_USE_DBUS
    _signal_setVector(_dbus, ais->vesselH,  1, course, speed);
#endif

    g_get_current_time(&ais->lastUpdate);

    return TRUE;
}

static int           _setAISLab (unsigned int mmsi, const char *name)
// update AIS label
{
    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

    if (NULL != name) {
        g_snprintf(ais->name, AIS_SHIPNAME_MAXLEN, "%s", name);

#ifdef S52_USE_DBUS
        _signal_setVESSELlabel(_dbus, ais->vesselH, ais->name);
#endif
    }

    g_get_current_time(&ais->lastUpdate);

    return TRUE;
}

static int           _setAISDim (unsigned int mmsi, double a, double b, double c, double d)
{
    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

#ifdef S52_USE_SOCK
    _encodeNsend("S52_setDimension", "%lu,%lf,%lf,%lf,%lf", ais->vesselH, a, b, c, d);
#else
    S52_setDimension(ais->vesselH, a, b, c, d);
#endif

    g_get_current_time(&ais->lastUpdate);

    return TRUE;
}

static int           _setAISSta (unsigned int mmsi, int status, int turn)
{
    /*   from doc in 'gpsd'
    "0 - Under way using engine",
    "1 - At anchor",
    "2 - Not under command",
    "3 - Restricted manoeuverability",
    "4 - Constrained by her draught",
    "5 - Moored",
    "6 - Aground",
    "7 - Engaged in Fishing",
    "8 - Under way sailing",
    "9 - Reserved for future amendment of Navigational Status for HSC",
    "10 - Reserved for future amendment of Navigational Status for WIG",
    "11 - Reserved for future use",
    "12 - Reserved for future use",
    "13 - Reserved for future use",
    "14 - Reserved for future use",
    "15 - Not defined (default)"
    */

    _ais_t *ais = _getAIS(mmsi);
    if (NULL == ais)
        return FALSE;

    if ((status!=ais->status) || (turn==ais->turn)) {
    // debug
    //if ((status!=ais->status) || (abs(turn) > ais->turn)) {

        if (1==status || 5==status || 6==status) {
#ifdef S52_USE_SOCK
            _encodeNsend("S52_setVESSELstate", "%lu,%i,%i,%i", ais->vesselH, 0, 2, turn );
#else
            S52_setVESSELstate(ais->vesselH, 0, 2, turn);   // AIS sleeping
#endif
        } else {
#ifdef S52_USE_SOCK
            _encodeNsend("S52_setVESSELstate", "%lu,%i,%i,%i", ais->vesselH, 0, 1, turn );

#else
            S52_setVESSELstate(ais->vesselH, 0, 1, turn);   // AIS active
#endif
        }

        ais->status = status;
        ais->turn   = turn;
    }

#ifdef S52_USE_DBUS
    // need to send the status every time because AIS-monitor.js
    // can be restarted wild libS52 is running hence status is not updated (ie no change)
    _signal_setVESSELstate(_dbus, ais->vesselH,  0, status, turn);
#endif

    return TRUE;
}

static int           _setAISDel (_ais_t *ais)
{
    if (NULL == ais) {
        printf("AIS is NULL!\n");
        g_assert(0);
    }

#ifdef S52_USE_DBUS
    _signal_delMarObj(_dbus, ais->vesselH, ais->name);
#endif

#ifdef S52_USE_SOCK
    _encodeNsend("S52_delMarObj", "%lu", ais->vesselH);
#else
    ais->vesselH = S52_delMarObj(ais->vesselH);
    if (NULL != ais->vesselH) {
        g_print("s52ais:WARNING: unkown AIS [%s]\n", ais->name);
        g_assert(0);
        ais->vesselH = NULL;
    }
#endif

#ifdef S52_USE_AFGLOW
#ifdef S52_USE_SOCK
    _encodeNsend("S52_delMarObj", "%lu", ais->afglowH);
#else
    ais->afglowH = S52_delMarObj(ais->afglowH);
    if (NULL != ais->afglowH) {
        g_print("s52ais:WARNING: unkown AIS [%s]\n", ais->name);
        g_assert(0);
        ais->afglowH = NULL;
    }
#endif
#endif

    return TRUE;
}

static int           _removeOldAIS(void)
{
    _ais_t  *ais = NULL;
    GTimeVal now;

    if (NULL == _ais_list) {
        g_print("s52ais:_getAIS() no AIS list\n");
        return FALSE;
    }

    g_get_current_time(&now);

    unsigned int i = 0;
    for (i=0; i<_ais_list->len; ++i) {
        ais = &g_array_index(_ais_list, _ais_t, i);
        // AIS report older then 10 min
        if (now.tv_sec > (ais->lastUpdate.tv_sec + AIS_SILENCE_MAX)) {
            _setAISDel(ais);

            g_array_remove_index(_ais_list, i);

            return TRUE;
        }
    }

    return FALSE;
}

static int           _dumpAIS(void)
// debug
{
    g_print("s52ais:_dumpAIS():-------------\n");

    if (NULL == _ais_list) {
        g_print("s52ais:_getAIS() no AIS list\n");
        return FALSE;
    }

    unsigned  int i   = 0;
    for (i=0; i<_ais_list->len; ++i) {
        _ais_t *ais = &g_array_index(_ais_list, _ais_t, i);
        g_print("s52ais:%i - %i, %s\n", i, ais->mmsi, ais->name);
    }
    g_print("\n");

    return TRUE;
}

static int           _updateTimeTag(void)
// then adjust time tag of AIS
{
    if (NULL == _ais_list)
        return FALSE;

    if (0 == _ais_list->len) {
        //g_print("_updateTimeTag(): no vessel - check if GPSD is on-line\nFIXME: reconnect to GPSD!!!!\n");
        //silent = TRUE;
        //g_static_mutex_unlock(&_ais_list_mutex);
        return FALSE;
    }

    {   // check global time - update time tag of all AIS each sec
        // FIXME: should be 0.5 sec
        GTimeVal now;
        g_get_current_time(&now);
        if ((now.tv_sec-_timeTick.tv_sec) < 1)
            return FALSE;

        g_get_current_time(&_timeTick);
    }

    // keep removing old AIS
    while (TRUE == _removeOldAIS())
        _dumpAIS();

    for (unsigned int i=0; i<_ais_list->len; ++i) {
        gchar         str[127+1] = {'\0'};
        GTimeVal      now;
        _ais_t *ais = &g_array_index(_ais_list, _ais_t, i);

        g_get_current_time(&now);

        if (-1.0 != ais->course) {
            g_snprintf(str, 127, "%s %lis\\n%03.f deg / %3.1f kt", ais->name, now.tv_sec - ais->lastUpdate.tv_sec, ais->course, ais->speed);
        } else {
            g_snprintf(str, 127, "%s %lis", ais->name, now.tv_sec - ais->lastUpdate.tv_sec);
        }


#ifdef S52_USE_SOCK
        _encodeNsend("S52_setVESSELlabel", "%lu,\"%s\"", ais->vesselH, str);
#else
        S52_setVESSELlabel(ais->vesselH, str);
#endif

    }

    return TRUE;
}

static void          _updateAISdata(struct gps_data_t *gpsdata)
{
    /*
    {   // debug mem leak - force new/del/_doCS/redraw cycle
        static int mmsi_n = 0;
        double dummy[3]= {0.0, 0.0, 0.0};
        //S52_objHandle dumb = S52_newMarObj("marfea", S52_POINT_T, 1, dummy, NULL);
        //S52_objHandle dumb = S52_newMarObj("marfea", S52_POINT_T, 0, NULL, NULL);
        //S52_delMarObj(dumb);
        printf("mmsi_n: %i\n", mmsi_n++);

        // force redrawn all the stuff
        //gtk_widget_queue_draw(_win);

        return;
    }
    */

    // debug
    //int s = gpsdata->set & AIS_SET;
    //g_print("set = %i, ais.type = %u, tag = %s\n", s, gpsdata->ais.type, gpsdata->tag);

    if (FALSE == _s52_connection) {
        _s52_connection = _s52_init_sock(_localhost, S52_PORT);
        if (FALSE == _s52_connection)
            return;
    }

    // Types 1,2,3 - Common navigation info
    if (1==gpsdata->ais.type || 2==gpsdata->ais.type || 3==gpsdata->ais.type) {
        double lat     = gpsdata->ais.type1.lat    / 600000.0;
        double lon     = gpsdata->ais.type1.lon    / 600000.0;
        double course  = gpsdata->ais.type1.course / 10.0;
        double speed   = gpsdata->ais.type1.speed  / 10.0;
        double heading = gpsdata->ais.type1.heading;
        //int turn    = gpsdata->ais.type1.turn;	/* signed (int): rate of turn */

        //printf("mmsi:%i, lat:%f, lon:%f\n", gpsdata->ais.mmsi, lat, lon);

        // debug
        //if (0 == firstmmsi)
        //    firstmmsi = gpsdata->ais.mmsi;
        //else
        //    if (gpsdata->ais.mmsi != firstmmsi)
        //        return;


        // heading not available
        if (511.0 == heading)
            heading = course;
        // speed not available
        if (102.3 == speed)
            speed = 0.0;


        //Turn rate is encoded as follows:
        //  0       - not turning
        //  1..126  - turning right at up to 708 degrees per minute or higher
        // -1..-126 - turning left at up to 708 degrees per minute or higher
        //  127     - turning right at more than 5deg/30s (No TI available)
        // -127     - turning left at more than 5deg/30s (No TI available)
        //  128     - (80 hex) indicates no turn information available (default)


        _setAISSta(gpsdata->ais.mmsi, gpsdata->ais.type1.status, gpsdata->ais.type1.turn);
        _setAISPos(gpsdata->ais.mmsi, lat, lon, heading);
        _setAISVec(gpsdata->ais.mmsi, course, speed);

        return;
    }

    // Type 4 - Base Station Report & Type 11 - UTC and Date Response
    if (4 == gpsdata->ais.type) {
        /*
        unsigned int year;			// UTC year
#define AIS_YEAR_NOT_AVAILABLE	0
	    unsigned int month;			// UTC month
#define AIS_MONTH_NOT_AVAILABLE	0
	    unsigned int day;			// UTC day
#define AIS_DAY_NOT_AVAILABLE	0
	    unsigned int hour;			// UTC hour
#define AIS_HOUR_NOT_AVAILABLE	24
	    unsigned int minute;		// UTC minute
#define AIS_MINUTE_NOT_AVAILABLE	60
	    unsigned int second;		// UTC second
#define AIS_SECOND_NOT_AVAILABLE	60
	    bool accuracy;		        // fix quality
	    int lon;			        // longitude
	    int lat;			        // latitude
	    unsigned int epfd;		    // type of position fix device
	    //unsigned int spare;	    // spare bits
	    bool raim;			        // RAIM flag
	    unsigned int radio;		    // radio status bits
        */
        double lat     = gpsdata->ais.type4.lat    / 600000.0;
        double lon     = gpsdata->ais.type4.lon    / 600000.0;

        // ERROR in ISO date format, can't use ':' because its a speparator in
        // attr pair list passed to label param
        char label[AIS_SHIPNAME_MAXLEN+1];
        g_snprintf(label, AIS_SHIPNAME_MAXLEN, "%4i-%02i-%02iT%02i:%02i:%02iZ",
                gpsdata->ais.type4.year, gpsdata->ais.type4.month,  gpsdata->ais.type4.day,
                gpsdata->ais.type4.hour, gpsdata->ais.type4.minute, gpsdata->ais.type4.second);

        _setAISLab(gpsdata->ais.mmsi, label);

        _setAISPos(gpsdata->ais.mmsi, lat, lon, 0.0);

        return;
    }

    //Type 5 - Ship static and voyage related data
    if (5 == gpsdata->ais.type) {

        /*
        unsigned int ais_version;  // AIS version level
        unsigned int imo;  // IMO identification
        char callsign[8];  // callsign

#define AIS_SHIPNAME_MAXLEN 20
        char shipname[AIS_SHIPNAME_MAXLEN+1];  // vessel name

        unsigned int shiptype; // ship type code

        // dimension
        unsigned int to_bow;  // dimension to bow
        unsigned int to_stern; // dimension to stern
        unsigned int to_port;  // dimension to port
        unsigned int to_starboard; // dimension to starboard

        unsigned int epfd; // type of position fix deviuce

        // ETA ?
        unsigned int month;// UTC month
        unsigned int day;  // UTC day
        unsigned int hour; // UTC hour
        unsigned int minute;  // UTC minute

        unsigned int draught;  // draft in meters
        char destination[21];  // ship destination

        unsigned int dte;  // data terminal enable
        //unsigned int spare; // spare bits
        */

        _setAISInfo(gpsdata->ais.mmsi,
                    gpsdata->ais.type5.imo,
                    gpsdata->ais.type5.callsign,
                    gpsdata->ais.type5.shiptype,
                    gpsdata->ais.type5.month,
                    gpsdata->ais.type5.day,
                    gpsdata->ais.type5.hour,
                    gpsdata->ais.type5.minute,
                    gpsdata->ais.type5.draught,
                    gpsdata->ais.type5.destination);

        _setAISLab(gpsdata->ais.mmsi, gpsdata->ais.type5.shipname);

        _setAISDim(gpsdata->ais.mmsi,
                   gpsdata->ais.type5.to_bow,  gpsdata->ais.type5.to_stern,
                   gpsdata->ais.type5.to_port, gpsdata->ais.type5.to_starboard);

        return;
    }

    // Type 8 - Broadcast Binary Message
    if (8 == gpsdata->ais.type) {
        // FIXME: add a dummy entry to signal that GPSD is on-line
        /*
        unsigned int dac;       	// Designated Area Code
	    unsigned int fid;       	// Functional ID
#define AIS_TYPE8_BINARY_MAX	952	// 952 bits
	    size_t bitcount;		    // bit count of the data
        */
        // followed by:
        //union {
        //    char bitdata[(AIS_TYPE8_BINARY_MAX + 7) / 8];  // 119.875 bytes (!), but (MAX+8)/8 == 120 bytes
        //    ...

        // AIS MSG TYPE 8: Broadcast Binary Message [mmsi:3160026, dac:316, fid:19]
        // DAC 316 - Canada
        // FID  19 - ???
        //g_print("AIS MSG TYPE 8: Broadcast Binary Message [mmsi:%i, dac:%i, fid:%i]\n",
        //        gpsdata->ais.mmsi, gpsdata->ais.type8.dac, gpsdata->ais.type8.fid);

        _setAISLab(gpsdata->ais.mmsi, "AIS MSG TYPE 8");


        return;
    }

	// Type 20 - Data Link Management Message
    if (20 == gpsdata->ais.type) {
        // FIXME: add a dummy entry to signal that GPSD is on-line
        g_print("s52ais:DEBUG - SKIP AIS MSG TYPE:20 - Data Link Management Message\n");
        return;
    }

/* debug
// gpsdata->set
#define ONLINE_SET      (1u<<1)
#define TIME_SET        (1u<<2)
#define TIMERR_SET      (1u<<3)
#define LATLON_SET      (1u<<4)
#define ALTITUDE_SET    (1u<<5)
#define SPEED_SET       (1u<<6)
#define TRACK_SET       (1u<<7)
#define CLIMB_SET       (1u<<8)
#define STATUS_SET      (1u<<9)
#define MODE_SET        (1u<<10)
#define DOP_SET         (1u<<11)
#define HERR_SET        (1u<<12)
#define VERR_SET        (1u<<13)
#define ATTITUDE_SET    (1u<<14)
#define SATELLITE_SET   (1u<<15)
#define SPEEDERR_SET    (1u<<16)
#define TRACKERR_SET    (1u<<17)
#define CLIMBERR_SET    (1u<<18)
#define DEVICE_SET      (1u<<19)
#define DEVICELIST_SET  (1u<<20)
#define DEVICEID_SET    (1u<<21)
#define RTCM2_SET       (1u<<22)
#define RTCM3_SET       (1u<<23)
#define AIS_SET         (1u<<24)
#define PACKET_SET      (1u<<25)
#define SUBFRAME_SET    (1u<<26)
#define GST_SET         (1u<<27)
#define VERSION_SET     (1u<<28)
#define POLICY_SET      (1u<<29)
#define LOGMESSAGE_SET  (1u<<30)
#define ERROR_SET       (1u<<31)
*/


    // debug
    int set = gpsdata->set & AIS_SET;
    g_print("s52ais:DEBUG - SKIP AIS MSG TYPE:%u - AIS_SET = %i [error:%s]\n", gpsdata->ais.type, set, gpsdata->error);

    return;
}

static gpointer      _gpsdClientRead(gpointer dummy)
// start gpsd client - loop forever
// if _ais_list is NULL then exit
{
    g_print("s52ais:_gpsdClientRead(): start looping ..\n");

    //if (0 != gps_open("localhost", "2947", &_gpsdata)) {   // android (gpsd 2.96)
    while (0 != gps_open("localhost", "2947", &_gpsdata)) {   // android (gpsd 2.96)
        // Note: g_print() work on android only when the main loop is running
#ifdef S52_USE_ANDROID
        //LOGI   ("_gpsdClientRead(): no gpsd running or network error, wait 1 sec: %d, %s\n", errno, gps_errstr(errno));
        g_print("s52ais:_gpsdClientRead(): no gpsd running or network error, wait 1 sec: %d, %s\n", errno, gps_errstr(errno));
#else
        g_print("s52ais:_gpsdClientRead(): no gpsd running or network error, wait 1 sec: %d, %s\n", errno, gps_errstr(errno));
#endif

        g_usleep(1000 * 1000); // 1.0 sec
    }

    if (-1 == gps_stream(&_gpsdata, WATCH_ENABLE|WATCH_NEWSTYLE, NULL)) {
        g_print("s52ais:_gpsdClientRead():gps_stream() failed .. exiting\n");
        return NULL;
    }

    // debug
    //gps_enable_debug(3, stderr);

    // heart of the client
    for (;;) {

        g_static_mutex_lock(&_ais_list_mutex);
        if (NULL == _ais_list) {
            g_print("s52ais.c:_gpsdClientRead() no AIS list .. main exited .. terminate gpsRead thread\n");

            g_static_mutex_unlock(&_ais_list_mutex);

            return NULL;
        }
        g_static_mutex_unlock(&_ais_list_mutex);

        //if (!gps_waiting(&_gpsdata, 10000000)) {   // wait 10 sec  (10*1000*1000 uSec)
        if (!gps_waiting(&_gpsdata,  500*1000)) {    // wait 0.5 sec     (500*1000 uSec)

            //g_print("s52ais.c:_gpsdClientRead():gps_waiting() timed out\n");
            g_static_mutex_lock(&_ais_list_mutex);
            _updateTimeTag();
            g_static_mutex_unlock(&_ais_list_mutex);

            // debug - try to wake up the connection to gpsd (it seem to fall asleep on android)
            //if (-1 == gps_stream(&_gpsdata, WATCH_ENABLE|WATCH_NEWSTYLE, NULL)) {
            //    g_print("_gpsdClientRead():gps_stream() fail\n");
            //}

            // read glib events
            while (g_main_context_iteration(NULL, FALSE))
                ;

        } else {
            errno = 0;
            if (-1 != gps_read(&_gpsdata)) {
                // no error

                // read glib events
                while(g_main_context_iteration(NULL, FALSE))
                    ;

                // handle AIS data
                g_static_mutex_lock(&_ais_list_mutex);
                _updateAISdata(&_gpsdata);
                g_static_mutex_unlock(&_ais_list_mutex);

            } else {
                g_print("s52ais.c:_gpsdClientRead():gps_read(): socket error 4 .. GPSD died [errno=%i]\n", errno);

                // exit thread
                gps_close(&_gpsdata);
                return dummy;
            }
        }
    }

    return dummy;
}


#ifndef S52AIS_STANDALONE
int            s52ais_updateTimeTag(void)
// then adjust time tag of AIS
// in S52GPS_STANDALONE this call is useless
{
    g_static_mutex_lock(&_ais_list_mutex);

    if (NULL == _ais_list) {
        g_print("s52ais:s52ais_updateTimeTag(): no AIS list\n");
        g_static_mutex_unlock(&_ais_list_mutex);
        return TRUE;
    }

    _isMainLoopUP = TRUE;

    // no AIS time tag to update on chart
    //static int silent = FALSE;
    //if (0==_ais_list->len && FALSE==silent) {
    if (0 == _ais_list->len) {
        g_print("s52ais:s52ais_updateTimeTag(): no vessel - check if GPSD is on-line\nFIXME: reconnect to GPSD!!!!\n");
        //silent = TRUE;
        g_static_mutex_unlock(&_ais_list_mutex);
        return TRUE;
    }

    // keep removing old AIS
    while (TRUE == _removeOldAIS())
        _dumpAIS();

    unsigned int i = 0;
    for (i=0; i<_ais_list->len; ++i) {
        gchar         str[80+1];
        GTimeVal      now;
        _ais_t *ais = &g_array_index(_ais_list, _ais_t, i);

        g_get_current_time(&now);

        //g_snprintf(str, 80, "%s %lis", ais->name, now.tv_sec - ais->lastUpdate.tv_sec);
        //g_snprintf(str, 80, "%s %lis \\n %3f deg / %3.1f kt", ais->name, now.tv_sec - ais->lastUpdate.tv_sec, ais->course, ais->speed);
        g_snprintf(str, 80, "%s %lis\\n%3f deg / %3.1f kt", ais->name, now.tv_sec - ais->lastUpdate.tv_sec, ais->course, ais->speed);
        S52_setVESSELlabel(ais->vesselH, str);
    }

    g_static_mutex_unlock(&_ais_list_mutex);

    return TRUE;
}
#endif

#ifdef S52_USE_ANDROID
static int           _startGPSD(void)
{
    /*
    <!-- Allows applications to write gpsd.sock and gpsd.pid to sdcard -->
    <uses-permission android:name = "android.permission.WRITE_EXTERNAL_STORAGE" />
    */


    GError    *error           = NULL;
    const char run_gpsd_sh[]   = "/system/bin/sh -c " PATH "/s52android/bin/run_gpsd.sh";
    const char path_gpsd_pid[] = PATH "/s52android/bin/gpsd.pid";
    GStatBuf buf;

    //g_print("_startGPSD(): check if GPSD on-line ..\n");
    if (0 != g_stat(path_gpsd_pid, &buf)) {
        if (TRUE != g_spawn_command_line_async(run_gpsd_sh, &error)) {
            g_print("s52ais:_startGPSD(): 1 - g_spawn_command_line_async() failed [%s]\n", error->message);
            return FALSE;
        }
    } else {
        // no gpsd "service" on android to clean up gpsd.pid!
        // start gpsd event if already running

        if (TRUE != g_spawn_command_line_async(run_gpsd_sh, &error)) {
            g_print("s52ais:_startGPSD(): 2 - g_spawn_command_line_async() failed [%s]\n", error->message);
            return FALSE;
        }
    }

    g_print("s52ais:_startGPSD(): GPSD .. started\n");

    // wait to come up, so that this client see the process
    // otherwise will fail in _gpsdClientRead()
    //g_usleep(1000 * 1000); // 1.0 sec

    return TRUE;
}
#endif

int            s52ais_initAIS(void)
{
    //g_print("s52ais_initAIS() .. start\n");

    // so all occurence of _ais_list are mutex'ed
    // (but this one is useless - but what if android restart main()!)
    g_static_mutex_lock(&_ais_list_mutex);
    if (NULL == _ais_list) {
        _ais_list = g_array_new(FALSE, TRUE, sizeof(_ais_t));
    } else {
        //LOGI("s52gps_initAIS(): bizzard case where we are restarting a running process!!\n");
        g_print("s52ais:s52ais_initAIS(): bizzard case where we are restarting a running process!!\n");

        g_static_mutex_unlock(&_ais_list_mutex);
        return TRUE;
    }
    g_static_mutex_unlock(&_ais_list_mutex);

#ifdef S52_USE_ANDROID
    // NOTE: on Ubuntu, GPSD is started at boot-time
    if (TRUE != _startGPSD())
        return FALSE;
#endif


#ifdef WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif


#ifdef S52_USE_DBUS
    _initDBus();
#endif

#ifdef S52_USE_SOCK
    _s52_connection = _s52_init_sock(_localhost, S52_PORT);
#endif

// no thread needed in standalone
#ifndef S52AIS_STANDALONE

#define _g_thread_create(func, data, joinable, error)           \
        (g_thread_create_full(func, data, 0, joinable, FALSE, G_THREAD_PRIORITY_NORMAL, error))
    //_gpsClientThread = _g_thread_create(_gpsdClientRead, NULL, TRUE, NULL);

    // not joinable - gps done will not wait
    _gpsClientThread = g_thread_create_full(_gpsdClientRead, NULL, 0, FALSE, TRUE, G_THREAD_PRIORITY_NORMAL, NULL);
#endif


    // setup timer
    g_get_current_time(&_timeTick);

    return TRUE;
}

static int           _flushAIS(int all)
{
    g_static_mutex_lock(&_ais_list_mutex);
    if (NULL != _ais_list) {
        unsigned int i = 0;
        for (i=0; i<_ais_list->len; ++i) {
            _ais_t *ais = &g_array_index(_ais_list, _ais_t, i);
            _setAISDel(ais);
        }

        if (TRUE == all) {
            g_array_free(_ais_list, TRUE);
            _ais_list = NULL;
        }
    }
    g_static_mutex_unlock(&_ais_list_mutex);

    return TRUE;
}

int            s52ais_doneAIS()
{
    _flushAIS(TRUE);

    g_print("s52ais:s52gpsd_doneAIS() .. \n");

#ifdef S52_USE_DBUS
    dbus_connection_unref(_dbus);
    _dbus = NULL;
#endif

#ifdef WIN32
    WSACleanup();
#endif

    return TRUE;
}

static void          _trapSIG(int sig, siginfo_t *info, void *secret)
{
    switch(sig) {

        // 2
    case SIGINT:
        g_print("s52ais:_trapSIG(): Signal 'Interrupt' cought - SIGINT(%i)\n", sig);

        // this call flush AIS, so this signal _gpsdClientRead() to exit
        s52ais_doneAIS();
        break;

        // 11
    case SIGSEGV:
        g_print("s52ais:_trapSIG(): Signal 'Segmentation violation' cought - SIGSEGV(%i)\n", sig);
#ifdef S52_USE_ANDROID
        unlink(AIS PID);
#endif
        // continue with normal sig handling
        _old_signal_handler_SIGSEGV.sa_sigaction(sig, info, secret);
        break;

        // 15
    case SIGTERM:
        g_print("s52ais:_trapSIG(): Signal 'Termination (ANSI)' cought - SIGTERM(%i)\n", sig);
#ifdef S52_USE_ANDROID
        unlink(AIS PID);
#endif
        // CRASH continue with normal sig handling
        //_old_signal_handler_SIGTERM.sa_sigaction(sig, info, secret);

        break;

        // 10
    case SIGUSR1:
        g_print("s52ais:_trapSIG(): Signal 'User-defined 1' cought - SIGUSR1(%i)\n", sig);
        // continue with normal sig handling
        //_old_signal_handler.sa_sigaction(sig, info, secret);
        break;

        // 12
    case SIGUSR2:
        g_print("s52ais:_trapSIG(): Signal 'User-defined 2' cought - SIGUSR2(%i)\n", sig);
        // continue with normal sig handling
        //_old_signal_handler.sa_sigaction(sig, info, secret);

        // re-connect to libS52
        if (NULL == _s52_connection) {
            _flushAIS(FALSE);
            _s52_connection = _s52_init_sock(_localhost, S52_PORT);
            g_print("s52ais:_trapSIG(): re-connect to libS52\n");
        }

        break;

    default:
        g_print("s52ais:_trapSIG():handler not found [%i]\n", sig);
        break;
    }

    return;
}

static int           _initSIG(void)
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

    return TRUE;
}

#ifdef S52AIS_STANDALONE
int main(int argc, char *argv[])
{
    g_print("main():starting: argc=%i, argv[0]=%s\n", argc, argv[0]);

    g_thread_init(NULL);
    g_type_init();

    _initSIG();

#ifdef S52_USE_ANDROID
    {   // save pid
        // FIXME: check if an instance of s52ais is allready running
        // an signal it somehow to reconnect to libS52
        // -OR-
        // kill the previous instance first!
        if (TRUE == g_file_test(AIS PID, (GFileTest) (G_FILE_TEST_EXISTS))) {
            GError    *error  = NULL;
            const char done[] = "/system/bin/sh -c 'kill -SIGINT `cat " AIS PID "`'";
            if (TRUE != g_spawn_command_line_async(done, &error)) {
                g_print("s52ais:g_spawn_command_line_async() failed [%s]\n", AIS);
                return FALSE;
            }
            unlink(AIS PID);
            g_print("s52ais:AIS prog allready running stopped (%s)\n", AIS);
        } else
            g_print("s52ais:AIS prog not running (%s)\n", AIS);


        pid_t pid = getpid();
        FILE *fd  = fopen(AIS PID, "w");
        if (NULL != fd) {
            fprintf(fd, "%i", pid);
            fclose(fd);
        }
    }
#endif

    s52ais_initAIS();

    // FIXME: remove old spurious AIS
    // - S52_getObjList("--6MARIN.000", "vessel");
    // - loop
    //        - S52_getMarObjH(unsigned int S57ID)
    //        - S52_delMarObj(S52ObjectHandle objH);

    _gpsdClientRead(NULL);


#ifdef S52_USE_ANDROID
    // clean up
    unlink(AIS PID);
#endif

    g_print("s52ais:main():exiting: ..\n");

    return TRUE;
}
#endif
