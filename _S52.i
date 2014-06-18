// _S52.i: socket, Websocket, DBus, pipe (in progres), .. network interface to S52
//
// SD 2014MAY23


// Call the correponding S52_* function named 'method' in JSON object
// here 'method' mean function name
// SL4A call it 'method' since JAVA is OOP
// expect:{"id":<int>,"method":"S52_*","params":["x,y,z,whatever"]}
// return:{"id":<int>,"error":"<no error>|<some error>","result":["x,y,z,whatever"]}
//   or  "{\"id\":%i,\"error\":\"%s\",\"result\":%s}"


#ifdef S52_USE_SOCK
// -----------------------------------------------------------------
// listen to socket
//
#include <sys/types.h>
#include <sys/socket.h>
#include <gio/gio.h>
#include "parson.h"

#define SOCK_BUF 2048

static gchar               _setErr(char *err, gchar *errmsg)
{
    g_snprintf(err, SOCK_BUF, "libS52.so:%s", errmsg);

    return TRUE;
}

static int                 _encode(char *buffer, const char *frmt, ...)
{
    va_list argptr;
    va_start(argptr, frmt);
    int n = g_vsnprintf(buffer, SOCK_BUF, frmt, argptr);
    va_end(argptr);

    //PRINTF("n:%i\n", n);

    if (n < (SOCK_BUF-1)) {
        buffer[n]   = '\0';
        buffer[n+1] = '\0';
        return TRUE;
    } else {
        buffer[SOCK_BUF-1] = '\0';
        PRINTF("g_vsnprintf(): fail - no space in buffer\n");
        return FALSE;
    }
}

static int                 _handleS52method(const gchar *str, char *result, char *err)
{
    // FIXME: use btree for name/function lookup
    // -OR-
    // FIXME: order cmdName by frequency


    // reset error string --> 'no error'
    err[0] = '\0';

    // JSON parser
    JSON_Value *val = json_parse_string(str);
    if (NULL == val) {
        _setErr(err, "can't parse json str");
        _encode(result, "[0]");

        PRINTF("WARNING: json_parse_string() failed:%s", str);
        return 0;
    }

    // init JSON Object
    JSON_Object *obj      = json_value_get_object(val);
    double       id       = json_object_get_number(obj, "id");

    // get S52_* Command Name
    const char  *cmdName  = json_object_dotget_string(obj, "method");
    if (NULL == cmdName) {
        _setErr(err, "no cmdName");
        _encode(result, "[0]");
        goto exit;
    }

    //PRINTF("JSON cmdName:%s\n", cmdName);

    // start work - fetch cmdName parameters
    JSON_Array *paramsArr = json_object_get_array(obj, "params");
    if (NULL == paramsArr)
        goto exit;

    // get the number of parameters
    size_t      count     = json_array_get_count(paramsArr);

    // FIXME: check param type


    // ---------------------------------------------------------------------
    //
    // call command - return answer to caller
    //

    //S52ObjectHandle STD S52_newOWNSHP(const char *label);
    //if (0 == S52_strncmp(cmdName, "S52_newOWNSHP", strlen("S52_newOWNSHP"))) {
    if (0 == g_strcmp0(cmdName, "S52_newOWNSHP")) {
        const char *label = json_array_get_string (paramsArr, 0);
        if ((NULL==label) || (1!=count)) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newOWNSHP(label);
        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_newVESSEL(int vesrce, const char *label);
    if (0 == g_strcmp0(cmdName, "S52_newVESSEL")) {
        if (2 != count) {
            _setErr(err, "params 'vesrce'/'label' not found");
            goto exit;
        }

        double      vesrce = json_array_get_number(paramsArr, 0);
        const char *label  = json_array_get_string(paramsArr, 1);
        if (NULL == label) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        S52ObjectHandle objH = S52_newVESSEL(vesrce, label);
        _encode(result, "[%lu]", (long unsigned int *) objH);
        //PRINTF("objH: %lu\n", objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setVESSELlabel(S52ObjectHandle objH, const char *newLabel);
    if (0 == g_strcmp0(cmdName, "S52_setVESSELlabel")) {
        if (2 != count) {
            _setErr(err, "params 'objH'/'newLabel' not found");
            goto exit;
        }

        const char *label = json_array_get_string (paramsArr, 1);
        if (NULL == label) {
            _setErr(err, "params 'label' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        objH = S52_setVESSELlabel(objH, label);
        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_pushPosition(S52ObjectHandle objH, double latitude, double longitude, double data);
    if (0 == g_strcmp0(cmdName, "S52_pushPosition")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'latitude'/'longitude'/'data' not found");
            goto exit;
        }

        long unsigned int lui     = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH      = (S52ObjectHandle) lui;
        double          latitude  = json_array_get_number(paramsArr, 1);
        double          longitude = json_array_get_number(paramsArr, 2);
        double          data      = json_array_get_number(paramsArr, 3);

        objH = S52_pushPosition(objH, latitude, longitude, data);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setVector   (S52ObjectHandle objH, int vecstb, double course, double speed);
    if (0 == g_strcmp0(cmdName, "S52_setVector")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'vecstb'/'course'/'speed' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH   = (S52ObjectHandle) lui;
        double          vecstb = json_array_get_number(paramsArr, 1);
        double          course = json_array_get_number(paramsArr, 2);
        double          speed  = json_array_get_number(paramsArr, 3);

        objH = S52_setVector(objH, vecstb, course, speed);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_setDimension(S52ObjectHandle objH, double a, double b, double c, double d);
    if (0 == g_strcmp0(cmdName, "S52_setDimension")) {
        if (5 != count) {
            _setErr(err, "params 'objH'/'a'/'b'/'c'/'d' not found");
            goto exit;
        }

        long unsigned int lui = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH  = (S52ObjectHandle) lui;
        double          a     = json_array_get_number(paramsArr, 1);
        double          b     = json_array_get_number(paramsArr, 2);
        double          c     = json_array_get_number(paramsArr, 3);
        double          d     = json_array_get_number(paramsArr, 4);

        objH = S52_setDimension(objH, a, b, c, d);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;

    }

    //S52ObjectHandle STD S52_setVESSELstate(S52ObjectHandle objH, int vesselSelect, int vestat, int vesselTurn);
    if (0 == g_strcmp0(cmdName, "S52_setVESSELstate")) {
        if (4 != count) {
            _setErr(err, "params 'objH'/'vesselSelect'/'vestat'/'vesselTurn' not found");
            goto exit;
        }

        long unsigned int lui        = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle objH         = (S52ObjectHandle) lui;
        double          vesselSelect = json_array_get_number(paramsArr, 1);
        double          vestat       = json_array_get_number(paramsArr, 2);
        double          vesselTurn   = json_array_get_number(paramsArr, 3);

        objH = S52_setVESSELstate(objH, vesselSelect, vestat, vesselTurn);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //S52ObjectHandle STD S52_delMarObj(S52ObjectHandle objH);
    if (0 == g_strcmp0(cmdName, "S52_delMarObj")) {
        if (1 != count) {
            _setErr(err, "params 'objH' not found");
            goto exit;
        }

        long unsigned int lui  = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle   objH = (S52ObjectHandle) lui;
        objH = S52_delMarObj(objH);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    // FIXME: not all param paresed
    //S52ObjectHandle STD S52_newMarObj(const char *plibObjName, S52ObjectType objType,
    //                                     unsigned int xyznbrmax, double *xyz, const char *listAttVal);
    if (0 == g_strcmp0(cmdName, "S52_newMarObj")) {
        if (3 != count) {
            _setErr(err, "params 'plibObjName'/'objType'/'xyznbrmax' not found");
            goto exit;
        }

        const char *plibObjName = json_array_get_string(paramsArr, 0);
        double      objType     = json_array_get_number(paramsArr, 1);
        double      xyznbrmax   = json_array_get_number(paramsArr, 2);
        double     *xyz         = NULL;
        gchar      *listAttVal  = NULL;


        S52ObjectHandle objH = S52_newMarObj(plibObjName, objType, xyznbrmax, xyz, listAttVal);

        // debug
        //PRINTF("S52_newMarObj -> objH: %lu\n", (long unsigned int *) objH);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //const char * STD S52_getPalettesNameList(void);
    if (0 == g_strcmp0(cmdName, "S52_getPalettesNameList")) {
        const char *palListstr = S52_getPalettesNameList();

        _encode(result, "[\"%s\"]", palListstr);

        //PRINTF("%s", result);

        goto exit;
    }

    //const char * STD S52_getCellNameList(void);
    if (0 == g_strcmp0(cmdName, "S52_getCellNameList")) {
        const char *cellNmListstr = S52_getCellNameList();

        _encode(result, "[\"%s\"]", cellNmListstr);

        //PRINTF("%s", result);

        goto exit;
    }

    //double STD S52_getMarinerParam(S52MarinerParameter paramID);
    if (0 == g_strcmp0(cmdName, "S52_getMarinerParam")) {
        if (1 != count) {
            _setErr(err, "params 'paramID' not found");
            goto exit;
        }

        double paramID = json_array_get_number(paramsArr, 0);

        double d = S52_getMarinerParam(paramID);

        _encode(result, "[%f]", d);

        //PRINTF("%s", result);

        goto exit;
    }

    //int    STD S52_setMarinerParam(S52MarinerParameter paramID, double val);
    if (0 == g_strcmp0(cmdName, "S52_setMarinerParam")) {
        if (2 != count) {
            _setErr(err, "params 'paramID'/'val' not found");
            goto exit;
        }

        double paramID = json_array_get_number(paramsArr, 0);
        double val     = json_array_get_number(paramsArr, 1);

        double d = S52_setMarinerParam(paramID, val);

        _encode(result, "[%f]", d);

        //PRINTF("%s", result);

        goto exit;
    }

    //DLL int    STD S52_drawBlit(double scale_x, double scale_y, double scale_z, double north);
    if (0 == g_strcmp0(cmdName, "S52_drawBlit")) {
        if (4 != count) {
            _setErr(err, "params 'scale_x'/'scale_y'/'scale_z'/'north' not found");
            goto exit;
        }

        double scale_x = json_array_get_number(paramsArr, 0);
        double scale_y = json_array_get_number(paramsArr, 1);
        double scale_z = json_array_get_number(paramsArr, 2);
        double north   = json_array_get_number(paramsArr, 3);

        int ret = S52_drawBlit(scale_x, scale_y, scale_z, north);
        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_drawBlit(): %s\n", result);

        goto exit;
    }

    //int    STD S52_drawLast(void);
    if (0 == g_strcmp0(cmdName, "S52_drawLast")) {
        int i = S52_drawLast();

        _encode(result, "[%i]", i);

        //PRINTF("SOCK:S52_drawLast(): res:%s\n", result);

        goto exit;
    }

    //int    STD S52_draw(void);
    if (0 == g_strcmp0(cmdName, "S52_draw")) {
        int i = S52_draw();

        _encode(result, "[%i]", i);

        //PRINTF("SOCK:S52_draw(): res:%s\n", result);

        goto exit;
    }

    //int    STD S52_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);
    if (0 == g_strcmp0(cmdName, "S52_getRGB")) {
        if (1 != count) {
            _setErr(err, "params 'colorName' not found");
            goto exit;
        }

        const char *colorName  = json_array_get_string(paramsArr, 0);

        unsigned char R;
        unsigned char G;
        unsigned char B;
        int ret = S52_getRGB(colorName, &R, &G, &B);

        PRINTF("%i, %i, %i\n", R,G,B);

        if (TRUE == ret)
            _encode(result, "[%i,%i,%i]", R, G, B);
        else
            _encode(result, "[0]");

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_setTextDisp(int dispPrioIdx, int count, int state);
    if (0 == g_strcmp0(cmdName, "S52_setTextDisp")) {
        if (3 != count) {
            _setErr(err, "params 'dispPrioIdx' / 'count' / 'state' not found");
            goto exit;
        }

        double dispPrioIdx = json_array_get_number(paramsArr, 0);
        double count       = json_array_get_number(paramsArr, 1);
        double state       = json_array_get_number(paramsArr, 2);

        _encode(result, "[%i]", S52_setTextDisp(dispPrioIdx, count, state));

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_getTextDisp(int dispPrioIdx);
    if (0 == g_strcmp0(cmdName, "S52_getTextDisp")) {
        if (1 != count) {
            _setErr(err, "params 'dispPrioIdx' not found");
            goto exit;
        }

        double dispPrioIdx = json_array_get_number(paramsArr, 0);

        _encode(result, "[%i]", S52_getTextDisp(dispPrioIdx));

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_loadCell        (const char *encPath,  S52_loadObject_cb loadObject_cb);
    if (0 == g_strcmp0(cmdName, "S52_loadCell")) {
        if (1 != count) {
            _setErr(err, "params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);

        int ret = S52_loadCell(encPath, NULL);

        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("%s\n", result);

        goto exit;
    }

    //int    STD S52_doneCell        (const char *encPath);
    if (0 == g_strcmp0(cmdName, "S52_doneCell")) {
        if (1 != count) {
            _setErr(err, "params 'encPath' not found");
            goto exit;
        }

        const char *encPath = json_array_get_string(paramsArr, 0);

        int ret = S52_doneCell(encPath);

        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_doneCell(): %s\n", result);

        goto exit;
    }

    //const char * STD S52_pickAt(double pixels_x, double pixels_y)
    if (0 == g_strcmp0(cmdName, "S52_pickAt")) {
        if (2 != count) {
            _setErr(err, "params 'pixels_x' or 'pixels_y' not found");
            goto exit;
        }

        double pixels_x = json_array_get_number(paramsArr, 0);
        double pixels_y = json_array_get_number(paramsArr, 1);

        const char *ret = S52_pickAt(pixels_x, pixels_y);

        if (NULL == ret)
            _encode(result, "[0]");
        else {
            _encode(result, "[\"%s\"]", ret);
        }

        goto exit;
    }

    // const char * STD S52_getObjList(const char *cellName, const char *className);
    if (0 == g_strcmp0(cmdName, "S52_getObjList")) {
        if (2 != count) {
            _setErr(err, "params 'cellName'/'className' not found");
            goto exit;
        }

        const char *cellName = json_array_get_string (paramsArr, 0);
        if (NULL == cellName) {
            _setErr(err, "params 'cellName' not found");
            goto exit;
        }
        const char *className = json_array_get_string (paramsArr, 1);
        if (NULL == className) {
            _setErr(err, "params 'className' not found");
            goto exit;
        }

        const char *str = S52_getObjList(cellName, className);
        if (NULL == str)
            _encode(result, "[0]");
        else
            _encode(result, "[\"%s\"]", str);

        goto exit;
    }

    // S52ObjectHandle STD S52_getMarObjH(unsigned int S57ID);
    if (0 == g_strcmp0(cmdName, "S52_getMarObjH")) {
        if (1 != count) {
            _setErr(err, "params 'S57ID' not found");
            goto exit;
        }

        long unsigned int S57ID = (long unsigned int) json_array_get_number(paramsArr, 0);
        S52ObjectHandle   objH  = S52_getMarObjH(S57ID);

        _encode(result, "[%lu]", (long unsigned int *) objH);

        goto exit;
    }

    //const char * STD S52_getAttList(unsigned int S57ID);
    if (0 == g_strcmp0(cmdName, "S52_getAttList")) {
        if (1 != count) {
            _setErr(err, "params 'S57ID' not found");
            goto exit;
        }

        long unsigned int S57ID = (long unsigned int) json_array_get_number(paramsArr, 0);
        const char       *str   = S52_getAttList(S57ID);

        if (NULL == str)
            _encode(result, "[0]");
        else
            _encode(result, "[\"%s\"]", str);

        goto exit;
    }

    //DLL int    STD S52_xy2LL (double *pixels_x,  double *pixels_y);
    if (0 == g_strcmp0(cmdName, "S52_xy2LL")) {
        if (2 != count) {
            _setErr(err, "params 'pixels_x'/'pixels_y' not found");
            goto exit;
        }

        double pixels_x = json_array_get_number(paramsArr, 0);
        double pixels_y = json_array_get_number(paramsArr, 1);

        int ret = S52_xy2LL(&pixels_x, &pixels_y);
        if (TRUE == ret)
            _encode(result, "[%f,%f]", pixels_x, pixels_y);
        else {
            _encode(result, "[0]");
        }

        goto exit;
    }

    //DLL int    STD S52_setView(double cLat, double cLon, double rNM, double north);
    if (0 == g_strcmp0(cmdName, "S52_setView")) {
        if (4 != count) {
            _setErr(err, "params 'cLat'/'cLon'/'rNM'/'north' not found");
            goto exit;
        }

        double cLat  = json_array_get_number(paramsArr, 0);
        double cLon  = json_array_get_number(paramsArr, 1);
        double rNM   = json_array_get_number(paramsArr, 2);
        double north = json_array_get_number(paramsArr, 3);

        int ret = S52_setView(cLat, cLon, rNM, north);
        if (TRUE == ret)
            _encode(result, "[1]");
        else
            _encode(result, "[0]");

        //PRINTF("SOCK:S52_setView(): %s\n", result);

        goto exit;
    }

    //DLL int    STD S52_getView(double *cLat, double *cLon, double *rNM, double *north);
    if (0 == g_strcmp0(cmdName, "S52_getView")) {
        double cLat  = 0.0;
        double cLon  = 0.0;
        double rNM   = 0.0;
        double north = 0.0;

        int ret = S52_getView(&cLat, &cLon, &rNM, &north);

        if (TRUE == ret)
            _encode(result, "[%f,%f,%f,%f]", cLat, cLon, rNM, north);
        else
            _encode(result, "[0]");

        //PRINTF("SOCK:S52_getView(): %s\n", result);

        goto exit;
    }

    //DLL int    STD S52_setViewPort(int pixels_x, int pixels_y, int pixels_width, int pixels_height)
    if (0 == g_strcmp0(cmdName, "S52_setViewPort")) {
        if (4 != count) {
            _setErr(err, "params 'pixels_x'/'pixels_y'/'pixels_width'/'pixels_height' not found");
            goto exit;
        }

        double pixels_x      = json_array_get_number(paramsArr, 0);
        double pixels_y      = json_array_get_number(paramsArr, 1);
        double pixels_width  = json_array_get_number(paramsArr, 2);
        double pixels_height = json_array_get_number(paramsArr, 3);

        int ret = S52_setViewPort((int)pixels_x, (int)pixels_y, (int)pixels_width, (int)pixels_height);
        if (TRUE == ret)
            _encode(result, "[1]");
        else {
            _encode(result, "[0]");
        }

        //PRINTF("SOCK:S52_setViewPort(): %s\n", result);

        goto exit;
    }



    //
    _encode(result, "[0,\"WARNING:%s(): call not found\"]", cmdName);


exit:

    //debug
    //PRINTF("OUT STR:%s", result);

    json_value_free(val);
    return (int)id;
}

static gboolean            _sendResp(GIOChannel *source, gchar *str_send, guint len)
// send response
{
    gsize   bytes_written = 0;
    GError *error         = NULL;

    GIOStatus stat = g_io_channel_write_chars(source, str_send, len, &bytes_written, &error);
    if (NULL != error) {
        PRINTF("WARNING: g_io_channel_write_chars(): failed [stat:%i errmsg:%s]\n", stat, error->message);
        g_error_free(error);
        return FALSE;
    }

    if (G_IO_STATUS_ERROR == stat) {
        // 0 - G_IO_STATUS_ERROR  An error occurred.
        // 1 - G_IO_STATUS_NORMAL Success.
        // 2 - G_IO_STATUS_EOF    End of file.
        // 3 - G_IO_STATUS_AGAIN  Resource temporarily unavailable.
        PRINTF("WARNING: g_io_channel_write_chars() failed - GIOStatus:%i\n", stat);

        return FALSE; // will close connection
    }

    g_io_channel_flush(source, NULL);

    return TRUE;
}

static guint               _encodeWebSocket(gchar *str_send, gchar *response, gsize respLen)
{
    // encode response
    guint n = 0;
    if (respLen <= 125) {
        // lenght coded with 7 bits <= 125
        n = g_snprintf(str_send, SOCK_BUF, "\x81%c%s", (char)(respLen & 0x7F), response);
    } else {
        if (respLen < 65536) {
            // lenght coded with 16 bits (code 126)
            // bytesFormatted[1] = 126
            // bytesFormatted[2] = ( bytesRaw.length >> 8 )
            // bytesFormatted[3] = ( bytesRaw.length      )
            n = g_snprintf(str_send, SOCK_BUF, "\x81%c%c%c%s", 126, (char)(respLen>>8), (char)respLen, response);
        } else {
            // if need more then 65536 bytes to transfer (code 127)
            /* dataLen max = 2^64
             bytesFormatted[1] = 127
             bytesFormatted[2] = ( bytesRaw.length >> 56 )
             bytesFormatted[3] = ( bytesRaw.length >> 48 )
             bytesFormatted[4] = ( bytesRaw.length >> 40 )
             bytesFormatted[5] = ( bytesRaw.length >> 32 )
             bytesFormatted[6] = ( bytesRaw.length >> 24 )
             bytesFormatted[7] = ( bytesRaw.length >> 16 )
             bytesFormatted[8] = ( bytesRaw.length >>  8 )
             bytesFormatted[9] = ( bytesRaw.length       )
             */
            // FIXME: n == 0 !
            PRINTF("WebSocket Frame: FIXME: dataLen > 65535 not handled (%i)\n", respLen);
            g_assert(0);
        }
    }

    return n;
}

static gboolean            _handleSocket(GIOChannel *source, gchar *str_read)
//
{
    gchar  response[SOCK_BUF] = {'\0'};
    gchar  result  [SOCK_BUF] = {'\0'};
    gchar  err     [SOCK_BUF] = {'\0'};

    int   id = _handleS52method(str_read, result, err);

    // FIXME: _encode() & g_snprintf() do basically the same thing and the resulting
    // string is the same .. but only g_snprintf() string pass io channel !!!
    guint n  = g_snprintf(response, SOCK_BUF, "{\"id\":%i,\"error\":\"%s\",\"result\":%s}",
                          id, (err[0] == '\0') ? "no error" : err, result);

    return _sendResp(source, response, n);
}

static gboolean            _handleWebSocket(GIOChannel *source, gchar *str_read, gsize length)
// handle multi-msg stream
{
    // seem that only Dart send multi-msg stream!
    while (('\x81' == str_read[0]) && (length > 0)) {
        gchar  response[SOCK_BUF] = {'\0'};  // JSON resp
        gchar  str_send[SOCK_BUF] = {'\0'};  // WedSocket buffer to send

        gchar  result  [SOCK_BUF] = {'\0'};  // S52 call result
        gchar  err     [SOCK_BUF] = {'\0'};  // S52 call error

        //gchar  jsonstr [SOCK_BUF] = {'\0'};  // JSON msg tmp holder

        guint len  = str_read[1] & 0x7F;
        char *key  = str_read + 2;
        char *data = key      + 4;  // or str_read + 6;

        for (guint i = 0; i<len; ++i) {
            data[i] ^= key[i%4];
            //jsonstr[i] = data[i];
        }

        // debug
        //PRINTF("WebSocket Frame: msg in (length:%li len:%u):%s\n", length, len, data);

        // FIXME: copy only JSON part data to a buffer, then
        // call _handleS52method with it so that the JSON parser can't get confuse by the trailling undecode data.
        // Note that the Parson JSON parser have no problem with the tailling undecoded data
        int   id      = _handleS52method(data, result, err);
        guint respLen = g_snprintf(response, SOCK_BUF, "{\"id\":%i,\"error\":\"%s\",\"result\":%s}",
                                   id, (err[0] == '\0') ? "no error" : err, result);

        // debug
        //PRINTF("WebSocket Frame: resp out:%s\n", response);

        guint n = _encodeWebSocket(str_send, response, respLen);

        int ret = _sendResp(source, str_send, n);
        if (FALSE == ret)
            return FALSE;

        str_read += len + 6;
        length   -= len + 6;
    }

    return TRUE;
}

static gboolean            _handshakeWebSocket(GIOChannel *source, gchar *str_read)
{
    gchar buf[SOCK_BUF] = {'\0'};
    sscanf(str_read, "Sec-WebSocket-Key: %s", buf);
    GString *secWebSocketKey = g_string_new(buf);
    secWebSocketKey = g_string_append(secWebSocketKey, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    // debug
    //PRINTF("OUT secWebSocketKey>>>%s<<<\n", secWebSocketKey->str);

    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA1);
    g_checksum_update(checksum, (const guchar *)secWebSocketKey->str, secWebSocketKey->len);
    g_string_free(secWebSocketKey, TRUE);

    guint8 buffer[1024];
    gsize digest_len = 1023;
    g_checksum_get_digest(checksum, buffer, &digest_len);
    g_checksum_free(checksum);
    gchar *acceptstr = g_base64_encode(buffer, digest_len);

    GString *respstr = g_string_new("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                                    "Server: Bla Gateway\r\n"
                                    "Upgrade: WebSocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "Access-Control-Allow-Origin:http://127.0.0.1:3030\r\n"
                                    "Access-Control-Allow-Credentials: true\r\n"
                                    "Access-Control-Allow-Headers: content-type\r\n"
                                   );

    g_string_append_printf(respstr,
                           "Sec-WebSocket-Accept:%s\r\n"
                           "\r\n",
                           acceptstr);

    g_free(acceptstr);

    int ret = _sendResp(source, respstr->str, respstr->len);
    g_string_free(respstr, TRUE);

    //return TRUE;
    return ret;
}

static gboolean            _socket_read_write(GIOChannel *source, GIOCondition cond, gpointer user_data)
{
    // quiet - not used
    (void)user_data;

    switch(cond) {
    	case G_IO_IN: {
            // Note: buffer must be local because we can have more than one connection (thread)
            gchar   str_read[SOCK_BUF] = {'\0'};
            gsize   length             = 0;
            GError *error              = NULL;
            GIOStatus stat = g_io_channel_read_chars(source, str_read, SOCK_BUF, &length, &error);

            // can't read line on raw socket
            //gsize   terminator_pos = 0;
            //GIOStatus stat = g_io_channel_read_line(source, &str_return, &length, &terminator_pos, &error);
            //GIOStatus stat = g_io_channel_read_line_string(source, buffer, &terminator_pos, &error);

            if (NULL != error) {
                PRINTF("WARNING: g_io_channel_read_chars(): failed [stat:%i err:%s]\n", stat, error->message);
                g_error_free(error);
                return FALSE;
            }
            if (G_IO_STATUS_ERROR == stat) {
                // 0 - G_IO_STATUS_ERROR  An error occurred.
                // 1 - G_IO_STATUS_NORMAL Success.
                // 2 - G_IO_STATUS_EOF    End of file.
                // 3 - G_IO_STATUS_AGAIN  Resource temporarily unavailable.
                PRINTF("WARNING: g_io_channel_read_chars(): failed - GIOStatus:%i\n", stat);
                return FALSE;
            }
            if (0 == length ) {
                PRINTF("DEBUG: length=%i\n", length);
                return FALSE;
            }

            // Not a WebSocket connection - normal JSON handling
            if ('{' == str_read[0]) {
                return _handleSocket(source, str_read);
            }

            // in a WebSocket Frame - msg
            if ('\x81' == str_read[0]) {
                return _handleWebSocket(source, str_read, length);
            }

            gchar *WSKeystr = g_strrstr(str_read, "Sec-WebSocket-Key");
            if (NULL != WSKeystr) {
                return _handshakeWebSocket(source, WSKeystr);
            } else {
                PRINTF("ERROR: unknown socket msg\n");
                return FALSE;
            }
        }


    	case G_IO_OUT: PRINTF("G_IO_OUT \n"); return FALSE; break;
    	case G_IO_PRI: PRINTF("G_IO_PRI \n"); break;
    	case G_IO_ERR: PRINTF("G_IO_ERR \n"); break;
    	case G_IO_HUP: PRINTF("G_IO_HUP \n"); break;
    	case G_IO_NVAL:PRINTF("G_IO_NVAL\n"); break;
    }


    return TRUE;
    //return FALSE;  // will close connection
}

static gboolean            _new_connection(GSocketService    *service,
                                           GSocketConnection *connection,
                                           GObject           *source_object,
                                           gpointer           user_data)
{
    // quiet gcc warning (unused param)
    (void)service;
    (void)source_object;
    (void)user_data;


    g_object_ref(connection);    // tell glib not to disconnect

    GSocketAddress *sockaddr = g_socket_connection_get_remote_address(connection, NULL);
    GInetAddress   *addr     = g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(sockaddr));
    guint16         port     = g_inet_socket_address_get_port   (G_INET_SOCKET_ADDRESS(sockaddr));

    PRINTF("New Connection from %s:%d\n", g_inet_address_to_string(addr), port);

    GSocket    *socket  = g_socket_connection_get_socket(connection);
    gint        fd      = g_socket_get_fd(socket);
    GIOChannel *channel = g_io_channel_unix_new(fd);

    //*
    GError     *error   = NULL;
    GIOStatus   stat    = g_io_channel_set_encoding(channel, NULL, &error);
    if (NULL != error) {
        g_object_unref(connection);
        PRINTF("g_io_channel_set_encoding(): failed [stat:%i err:%s]\n", stat, error->message);
        g_error_free(error);
        return FALSE;
    }

    g_io_channel_set_buffered(channel, FALSE);
    //*/

    g_io_add_watch(channel, G_IO_IN , (GIOFunc)_socket_read_write, connection);

    return FALSE;
}

static int                 _initSock(void)
{
    // FIXME: check that the glib loop is UP .. or start one
    //if (FALSE == g_main_loop_is_running(NULL)) {
    //    PRINTF("DEBUG: main loop is NOT running ..\n");
    //}

    GError         *error          = NULL;
    GSocketService *service        = g_socket_service_new();
    GInetAddress   *address        = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    GSocketAddress *socket_address = g_inet_socket_address_new(address, 2950); // GPSD use 2947

    g_socket_listener_add_address(G_SOCKET_LISTENER(service), socket_address, G_SOCKET_TYPE_STREAM,
                                  G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error);

    g_object_unref(socket_address);
    g_object_unref(address);

    if (NULL != error) {
        g_printf("WARNING: g_socket_listener_add_address() failed (%s)\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    g_socket_service_start(service);

    g_signal_connect(service, "incoming", G_CALLBACK(_new_connection), NULL);

    PRINTF("start to listen to socket ..\n");

    return TRUE;
}
#endif


#ifdef S52_USE_DBUS
// ------------ DBUS API  -----------------------
//
// duplicate some S52.h, mostly used for testing Mariners' Object
// async command and thread (here dbus)
// Probably not async since it use glib main loop!
//

// FIXME: use GDBus (in Gio) instead (thread prob with low-level DBus API)
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
static DBusConnection *_dbus    = NULL;
static DBusError       _dbusError;

static DBusHandlerResult   _sendDBusMessage         (DBusConnection *dbus, DBusMessage *reply)
// send the reply && flush the connection
{
    dbus_uint32_t serial = 0;
    //dbus_uint32_t serial = 1;
    if (!dbus_connection_send(dbus, reply, &serial)) {
        fprintf(stderr, "_sendDBusMessage():Out Of Memory!\n");
        exit(1);
    }
    dbus_connection_flush(dbus);

    // free the reply
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult   _dbus_draw               (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    //dbus_int32_t  i   = 0;
    //double        d   = 0;
    dbus_int32_t    ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_draw();
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_draw() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_drawLast           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    //dbus_int32_t  i   = 0;
    //double        d   = 0;
    dbus_int32_t    ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_drawLast();
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_drawLast() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getMarinerParam    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    dbus_int32_t  i   = 0;
    double        ret = 0;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
        g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_getMarinerParam((S52MarinerParameter)i);
    PRINTF("S52_getMarinerParam() val: %i, return: %f\n", i, ret);
    // ret == 0 (false) if first palette
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_getMarinerParam() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_setMarinerParam    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    //char *s;
    dbus_int32_t  i   = 0;
    double        d   = 0;
    dbus_int32_t  ret = FALSE;

    (void)user_data;

    //PRINTF("got S52_setMarinerParam msg !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_DOUBLE, &d, DBUS_TYPE_INVALID)) {
        g_print("received: %i, %f\n", i, d);
        //dbus_free(s);
        //g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_setMarinerParam((S52MarinerParameter)i, d);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_setMarinerParam() failed .. send a dbus error!\n");
        g_assert(0);
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getRGB             (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char *colorName;
    dbus_int32_t  ret = FALSE;
    unsigned char R;
    unsigned char G;
    unsigned char B;

    (void)user_data;

    dbus_error_init(&error);

    //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_DOUBLE, &d, DBUS_TYPE_INVALID)) {
    if (dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &colorName, DBUS_TYPE_INVALID)) {
        //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
        g_print("received: %s\n", colorName);
        //dbus_free(s);
        //g_print("received: %i\n", i);
    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_getRGB(colorName, &R, &G, &B);
    //if (FALSE == ret) {
    //    PRINTF("FIXME: S52_getRGB() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    // ret
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // R
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &R)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // G
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &G)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    // B
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BYTE, &B)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_newMarObj          (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    (void)user_data;

    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    char         *plibObjName = NULL;
    dbus_int32_t  objType     = 0;
    dbus_uint32_t xyznbr      = 0;
    char        **str         = NULL;
    dbus_uint32_t strnbr      = 0;
    char         *listAttVal  = NULL;

    S52ObjectHandle objH      = FALSE;

    //double        xyz[3] = {5.5,6.6,7.7};
    //double       *pxyz   = xyz;
    //dbus_int32_t  x[3] = {5, 6, 7};
    //dbus_int32_t *pxyz   = x;
    //dbus_int32_t  nel    = 3;
    //double          x    = 3.0;
    //double          y    = 3.0;

    dbus_error_init(&error);

    // FIXME: gjs-DBus can't pass array of double
    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_STRING, &plibObjName,
                              DBUS_TYPE_INT32,  &objType,
                              DBUS_TYPE_UINT32, &xyznbr,
                              //DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE, &pxyz, &nel,   // broken
                              //DBUS_TYPE_ARRAY, DBUS_TYPE_INT32,  &pxyz, &nel,   // broken
                              DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,   &str,  &strnbr,    // OK
                              DBUS_TYPE_STRING, &listAttVal,
                              DBUS_TYPE_INVALID)) {

        double *xyz = g_new0(double, xyznbr*3);
        char  **tmp = str;
        for (guint i=0; i<strnbr; i+=3) {
            xyz[i*3 + 0] = g_ascii_strtod(*str++, NULL);
            xyz[i*3 + 1] = g_ascii_strtod(*str++, NULL);
            xyz[i*3 + 2] = g_ascii_strtod(*str++, NULL);
            PRINTF("received: %f, %f, %f\n", xyz[i*3 + 0], xyz[i*3 + 1], xyz[i*3 + 2]);
        }


        // make the S52 call
        objH = S52_newMarObj(plibObjName, (S52ObjectType)objType, xyznbr, xyz, listAttVal);
        if (FALSE == objH) {
            PRINTF("FIXME: send a dbus error!\n");
            //g_assert(0);
        }
        g_free(xyz);
        g_strfreev(tmp);

    } else {
        g_print("received, but error getting message: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    // ret
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT64, &objH)) {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }

    return _sendDBusMessage(dbus, reply);
}

#if 0
static DBusHandlerResult   _dbus_signal_draw        (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    int             ret = FALSE;

    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i\n", (long unsigned int)o, i);
        //dbus_free(s);
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_draw();
    if (FALSE == ret) {
        PRINTF("FIXME: send a dbus error!\n");
        g_assert(0);
    }


    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_signal_drawLast    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    //DBusMessage*    reply;
    //DBusMessageIter args;
    DBusError       error;
    int             ret = FALSE;

    (void)dbus;
    (void)user_data;

    dbus_error_init(&error);

    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i\n", (long unsigned int)o, i);
        //dbus_free(s);
        //g_print("received: %i\n", i);
        ;
    } else {
        g_print("ERROR: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_drawLast();
    if (FALSE == ret) {
        PRINTF("FIXME: send a dbus error!\n");
        g_assert(0);
    }
    return DBUS_HANDLER_RESULT_HANDLED;

    /*
    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    return _sendDBusMessage(dbus, reply);
    */
}
#endif

static DBusHandlerResult   _dbus_setVESSELstate     (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    void *          objH   = NULL;
    dbus_int32_t    select = 0;
    dbus_int32_t    vestat = 0;
    dbus_int32_t    turn   = 0;
    dbus_int64_t    ret    = 0;

    (void)user_data;

    dbus_error_init(&error);

    //if (dbus_message_get_args(message, &error, DBUS_TYPE_INT64, &o, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID)) {
    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_DOUBLE, &objH,
                              DBUS_TYPE_INT32,  &select,
                              DBUS_TYPE_INT32,  &vestat,
                              DBUS_TYPE_INT32,  &turn,
                              DBUS_TYPE_INVALID)) {
        //g_print("received: %lX %i %i\n", (long unsigned int)o, sel, vestat);
    } else {
        g_print("ERROR:: %s\n", error.message);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    ret = S52_setVESSELstate((S52ObjectHandle)objH, select, vestat, turn);
    //if (NULL == ret) {
    //    g_print("FIXME: S52_setVESSELstate() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
    //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
         PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getPLibsIDList     (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPLibsIDList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getPLibsIDList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getPalettesNameList(DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getPalettesNameList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getPalettesNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_getCellNameList    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getCellNameList();
    if (NULL == str) {
        PRINTF("FIXME: S52_getCellNameList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getS57ClassList    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *cellName;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &cellName, DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char * str = S52_getS57ClassList(cellName);
    if (NULL == str) {
        PRINTF("FIXME: S52_getS57ClassList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getObjList         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *cellName;
    char           *className;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &cellName,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // debug
    //PRINTF("%s,%s\n", cellName, className);

    // make the S52 call
    const char *str = S52_getObjList(cellName, className);
    if (NULL == str) {
        PRINTF("FIXME: S52_getObjList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getAttList         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    dbus_uint32_t   s57id  = 0;
    //dbus_int64_t    ret    = 0;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_UINT32, &s57id,
                               DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    const char *str = S52_getAttList(s57id);
    if (NULL == str) {
        PRINTF("FIXME: S52_getAttList() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &str)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_getS57ObjClassSupp (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *className;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // debug
    //PRINTF("_dbus_getS57ObjClassSupp\n");

    // make the S52 call
    dbus_int32_t ret = S52_getS57ObjClassSupp(className);
    //if (NULL == str) {
    //    g_print("FIXME: _dbus_getS57ObjClassSupp() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_setS57ObjClassSupp (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *className;
    dbus_int32_t    value;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &className,
                               DBUS_TYPE_INT32,  &value,
                               DBUS_TYPE_INVALID)) {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int32_t ret = S52_setS57ObjClassSupp(className, value);
    //if (NULL == str) {
    //    g_print("FIXME: _dbus_setS57ObjClassSupp() failed .. send a dbus error!\n");
    //    g_assert(0);
    //}

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &ret)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_loadCell           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *str;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &str,
                               DBUS_TYPE_INVALID))
    {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadCell(str, NULL);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_loadCell() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_loadPLib           (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *str;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &str,
                               DBUS_TYPE_INVALID))
    {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_loadPLib(str);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_loadPLib() failed .. send a dbus error!\n");
        g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}


static DBusHandlerResult   _dbus_dumpS57IDPixels    (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    DBusMessage*    reply;
    DBusMessageIter args;
    DBusError       error;
    char           *fname;
    dbus_uint32_t   s57id = 0;
    dbus_uint32_t   width = 0;
    dbus_uint32_t   height= 0;

    (void)user_data;

    dbus_error_init(&error);

    if (!dbus_message_get_args(message, &error,
                               DBUS_TYPE_STRING, &fname,
                               DBUS_TYPE_UINT32, &s57id,
                               DBUS_TYPE_UINT32, &width,
                               DBUS_TYPE_UINT32, &height,
                               DBUS_TYPE_INVALID))
    {
        PRINTF("ERROR: %s\n", error.message);
        dbus_error_free(&error);

        // debug
        g_assert(0);

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    // make the S52 call
    dbus_int64_t ret = S52_dumpS57IDPixels(fname, s57id, width, height);
    if (FALSE == ret) {
        PRINTF("FIXME: S52_dumpS57IDPixels() return FALSE\n");
        //g_assert(0);
    }

    // -- reply --

    // create a reply from the message
    reply = dbus_message_new_method_return(message);

    // add the arguments to the reply
    dbus_message_iter_init_append(reply, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT64, &ret)) {
        PRINTF("Out Of Memory!\n");
        g_assert(0);
    }

    return _sendDBusMessage(dbus, reply);
}

static DBusHandlerResult   _dbus_selectCall         (DBusConnection *dbus, DBusMessage *message, void *user_data)
{
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_draw")) {
        return _dbus_draw(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_drawLast")) {
        return _dbus_drawLast(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getMarinerParam")) {
        return _dbus_getMarinerParam(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setMarinerParam")) {
        return _dbus_setMarinerParam(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getRGB")) {
        return _dbus_getRGB(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setVESSELstate")) {
        return _dbus_setVESSELstate(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getPLibsIDList")) {
        return _dbus_getPLibsIDList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getPalettesNameList")) {
        return _dbus_getPalettesNameList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getCellNameList")) {
        return _dbus_getCellNameList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getS57ClassList")) {
        return _dbus_getS57ClassList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getObjList")) {
        return _dbus_getObjList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getAttList")) {
        return _dbus_getAttList(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_getS57ObjClassSupp")) {
        return _dbus_getS57ObjClassSupp(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_setS57ObjClassSupp")) {
        return _dbus_setS57ObjClassSupp(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_loadCell")) {
        return _dbus_loadCell(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_loadPLib")) {
        return _dbus_loadPLib(dbus, message, user_data);
    }
    if (dbus_message_is_method_call(message, S52_DBUS_OBJ_NAME, "S52_dumpS57IDPixels")) {
        return _dbus_dumpS57IDPixels(dbus, message, user_data);
    }


    //DBusDispatchStatus dbusDispStatus = dbus_connection_get_dispatch_status(dbus);



    // ----------------------------------------------------------------------------------------

    PRINTF("=== DBus msg not handled ===\n");
    PRINTF("member   : %s\n", dbus_message_get_member(message));
    PRINTF("sender   : %s\n", dbus_message_get_sender(message));
    PRINTF("signature: %s\n", dbus_message_get_signature(message));
    PRINTF("path     : %s\n", dbus_message_get_path(message));
    PRINTF("interface: %s\n", dbus_message_get_interface(message));

    if (0 == g_strcmp0(dbus_message_get_member(message), "Disconnected", 12)) {
        PRINTF("ERROR: received DBus msg member 'Disconnected' .. \n" \
               "DBus force exit if dbus_connection_set_exit_on_disconnect(_dbus, TRUE);!\n");


        //DBusMessage*    reply = dbus_message_new_method_return(message);;
        //DBusMessageIter args;

        // add the arguments to the reply
        //dbus_message_iter_init_append(reply, &args);

        //if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_DOUBLE, &ret)) {
        //    fprintf(stderr, "Out Of Memory!\n");
        //    g_assert(0);
        //}

        //return _sendDBusMessage(dbus, reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    // this will exit thread (sometime!)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int                 _initDBus()
{
    int ret;

    if (NULL != _dbus)
        return FALSE;

    PRINTF("starting DBus ..\n");

    dbus_g_thread_init();

    dbus_error_init(&_dbusError);
    _dbus = dbus_bus_get(DBUS_BUS_SESSION, &_dbusError);
    if (!_dbus) {
        g_warning("Failed to connect to the D-BUS daemon: %s", _dbusError.message);
        dbus_error_free(&_dbusError);
        return 1;
    }

    //if (!dbus_bus_name_has_owner(_dbus, S52_DBUS_OBJ_NAME, &_dbusError)) {
    //    g_warning("Name has no owner on the bus!\n");
    //    return EXIT_FAILURE;
    //}

    ret = dbus_bus_request_name(_dbus, S52_DBUS_OBJ_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &_dbusError);
    if (-1 == ret) {
        PRINTF("%s:%s\n", _dbusError.name, _dbusError.message);
        dbus_error_free(&_dbusError);
        return 1;
    } else {
        if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER == ret)
            PRINTF("dbus_bus_request_name() reply OK: DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER (%i)\n", ret);
        else
            PRINTF("dbus_bus_request_name() reply OK (%i)\n", ret);
    }

    dbus_connection_setup_with_g_main(_dbus, NULL);

    // do not exit on disconnect
    dbus_connection_set_exit_on_disconnect(_dbus, FALSE);

    // listening to messages from all objects, as no path is specified
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_draw'",                &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_drawLast'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getMarinerParam'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getRGB'",              &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setVESSELstate'",      &_dbusError);

    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPLibsIDList'",      &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getPalettesNameList'", &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getCellNameList'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getS57ClassList'",     &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getObjList'",          &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getAttList'",          &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_getS57ObjClassSupp'",  &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_setS57ObjClassSupp'",  &_dbusError);

    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_loadCell'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_loadPLib'",            &_dbusError);
    dbus_bus_add_match(_dbus, "type='method_call',interface='nav.ecs.dbus',member='S52_dumpS57IDPixels'",     &_dbusError);
    //dbus_bus_add_match(_dbus, "type='method_call',sender='org.ecs.dbus',member='S52_newMarObj'",       &_dbusError);

    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_draw'",          &_dbusError);
    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_drawLast'",      &_dbusError);
    //dbus_bus_add_match(_dbus, "type='signal',sender='org.ecs.dbus',member='signal_S52_setState'",      &_dbusError);

    PRINTF("%s:%s\n", _dbusError.name, _dbusError.message);

    if (FALSE == dbus_connection_add_filter(_dbus, _dbus_selectCall, NULL, NULL)) {
        PRINTF("fail .. \n");
        exit(0);
    }

    return TRUE;
}
#endif // S52_USE_DBUS


// -----------------------------------------------------------------
// listen to pipe - work in progres
//
#ifdef S52_USE_PIPE
// mkfifo
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>     // unlink()
#define PIPENAME "/tmp/S52_pipe_01"
static gboolean            _pipeReadWrite(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    GError    *error = NULL;
    GString   *str   = g_string_new("");
    GIOStatus  stat  = g_io_channel_read_line_string(source, str, NULL, &error);
    PRINTF("GIOStatus: %i\n", stat);

    if (NULL != error) {
        PRINTF("ERROR: %s\n", error->message);
        g_error_free(error);
    }

    PRINTF("PIPE: |%s|\n", str->str);


    gchar** palNmL = g_strsplit(str->str, ",", 0);
    gchar** palNm  = palNmL;

    //const char * STD S52_getPLibsIDList(void);
    if (0 == ("S52_getPLibsIDList", *palNm)) {
        GError   *error = NULL;
        gsize     bout  = 0;
        PRINTF("PIPE: %s\n", *palNm);
        GIOStatus stat  = g_io_channel_write_chars(source, S52_getPLibsIDList(), -1, &bout, &error);

        PRINTF("GIOStatus: %i\n", stat);

        if (NULL != error) {
            PRINTF("ERROR: %s\n", error->message);
            g_error_free(error);
        }
    }


    //while (NULL != *palNm) {
    //    switch(type): {
    //        case 's':
    //    }
    //    palNm++;
    //}
    g_strfreev(palNmL);

    g_string_free(str, TRUE);

    return TRUE;
}

static int                 _pipeWatch(gpointer dummy)
// add watch to pipe
{
    // use FIFO pipe instead of DBug
    // less overhead - bad on ARM

    unlink(PIPENAME);

    int fdpipe = mkfifo(PIPENAME, S_IFIFO | S_IRUSR | S_IWUSR);

    int fd     = open(PIPENAME, O_RDWR);

    GIOChannel   *pipe      = g_io_channel_unix_new(fd);
    guint         watchID   = g_io_add_watch(pipe, G_IO_IN, _pipeReadWrite, NULL);

    // FIXME: case of no main loop
    //GMainContext *_pipeCtx  = g_main_context_new();
    //GMainLoop    *_pipeLoop = g_main_loop_new(_pipeCtx, TRUE);
    //g_main_loop_run(_pipeLoop);

    return TRUE;
}
#endif
