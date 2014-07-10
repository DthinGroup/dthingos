#include <std_global.h>
#include <ramsclient.h>
#include <eventsystem.h>
#include <opl_file.h>
#include <jarparser.h>
#include <opl_rams.h>
#include <encoding.h>
#include <upcall.h>
#include <init.h>


/* FSM state definitions of EVT_CMD_DECLARE */
#define DECLARE_FSM_STARTUP  0x01
#define DECLARE_FSM_CONNECT  0x02
#define DECLARE_FSM_READ     0x03
#define DECLARE_FSM_WRITE    0x04
#define DECLARE_FSM_CLOSE    0x05
#define DECLARE_FSM_SHUTDOWN 0x06

#define DATA_BUF_SIZE (256)
/* align up to 4 bytes. */
#define MEM_ALIGN(x) ((int32_t)((x)+3)&(~3))

#define newNormalEvent(eid, fsmState, uData, cb, evt) \
    do \
    { \
        (evt)->evtId = MARK_EVT_ID(RAMSCLIENT_MODULE_ID, (eid)); \
        (evt)->fsm_state = fsmState; \
        (evt)->priority = EP_LOW; \
        (evt)->userData = uData; \
        (evt)->triggerPoint = 0; \
        (evt)->evtcb = cb; \
    } \
    while(FALSE)

static int32_t servInstance;
static int16_t currentFsmState;

/* Global varible, applets list.*/
static AppletProps* appletsList;

/* Do we really need this? -_- */
static AppletProps* curActiveApp;

/* define a safe buffer for easily finalizing */
typedef void (*BufferFree)(void *);
typedef struct SafeBuffer_e
{
    BufferFree buffer_free;
    int32_t    bytes; /* available bytes size */
    uint8_t   *pBuf;
} SafeBuffer;

/**
 * RAMS command actions.
 */
static int32_t ramsClient_cmdAction(Event *evt, void *userData);
static int32_t ramsClient_buildConnection(Event *evt, void *userData);

/* free safe buffer */
static void SafeBufferFree(void *p)
{
    CRTL_freeif(p);
}

/**
 * Create a new safe buffer according to a given safe buffer.
 * The src safe buffer data will copy into the new safe buffer.
 */ 
static SafeBuffer* CreateSafeBuffer(SafeBuffer* src)
{
    SafeBuffer *newSafe;
    int32_t safeBufSize = MEM_ALIGN(src->bytes) + sizeof(SafeBuffer);

    newSafe = (SafeBuffer *)CRTL_malloc(safeBufSize);
    if (newSafe == NULL)
    {
        return NULL;
    }

    CRTL_memcpy(newSafe, src, safeBufSize);
    newSafe->pBuf = (uint8_t*)(((uint8_t*)newSafe) + sizeof(SafeBuffer));

    return newSafe;
}

/**
 * Create a new safe buffer by binary buffer and buffer size.
 * @param buf, the original buffer.
 * @param buf_size, the buffer size in bytes of original buffer.
 */
static SafeBuffer* CreateSafeBufferByBin(uint8_t *buf, int32_t buf_size)
{
    SafeBuffer *newSafe;
    int32_t safeBufSize = buf_size + sizeof(SafeBuffer);

    newSafe = (SafeBuffer *)CRTL_malloc(safeBufSize);
    if (newSafe == NULL)
    {
        return NULL;
    }
    newSafe->pBuf = (uint8_t*)(((uint8_t*)newSafe) + sizeof(SafeBuffer));     

    CRTL_memcpy(newSafe->pBuf, buf, buf_size);
    newSafe->bytes = buf_size;
    newSafe->buffer_free = SafeBufferFree;

    return newSafe;
}

/**
 * Interpret bytes as a little-endian IU16
 * @param p bytes
 * @return little-endian IU16 read from p
 */
static int16_t readleIU16(const uint8_t* p)
{
    return (int16_t)((p[1] << 8) | p[0]);
}

/**
 * Interpret bytes as a little-endian IU32
 * @param p bytes
 * @return little-endian IU32 read from p
 */
static int32_t readleIU32(const uint8_t* p)
{
    return
        (((int32_t)p[3]) << 24) |
        (((int32_t)p[2]) << 16) |
        (((int32_t)p[1]) <<  8) |
        (((int32_t)p[0]));
}

/**************************************************************/
/* streaming util APIs                                        */
/**************************************************************/
/**
 * Interpret bytes as a big-endian IU16
 * @param p bytes
 * @return big-endian IU16 read from p
 */
static int16_t readbeIU16(const uint8_t* p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

/**
 * Interpret bytes as a big-endian IU32
 * @param p bytes
 * @return big-endian IU32 read from p
 */
static int32_t readbeIU32(const uint8_t* p)
{
    return
        (((int32_t)p[0]) << 24) |
        (((int32_t)p[1]) << 16) |
        (((int32_t)p[2]) <<  8) |
        (((int32_t)p[3]));
}

/** 
 * Write an unsigned short into a bytes memory.
 * @p, the memory pointer saves bytes.
 * @num, IU16 number will write to p.
 */
static void writebeIU16(uint8_t* p, uint16_t num)
{
    p[0] = (uint8_t)((num>>8)&0xff);
    p[1] = (uint8_t)(num&0xff);
}

/** 
 * Write an unsigned int into a bytes memory.
 * @p, the memory pointer saves bytes.
 * @num, IU32 number will write to p.
 */
static void writebeIU32(uint8_t* p, uint32_t num)
{
    p[0] = (uint8_t)((num>>24)&0xff);
    p[1] = (uint8_t)((num>>16)&0xff);
    p[2] = (uint8_t)((num>>8)&0xff);
    p[3] = (uint8_t)(num&0xff);
}

/**
 * get default applet installed path.
 * it should be configurable in different project.
 * TODO: change this api to configurable.
 */
static uint16_t* getDefaultInstalledPath()
{
#if defined(WIN32)
    //return L"D:\\nix.long\\ReDvmAll\\dvm\\appdb\\";
    return L"D:\\dvm\\appdb\\";
#elif defined(ARCH_ARM_SPD)
	return file_getDthingDir();
#endif
	return NULL;      
}

static uint8_t* trim(uint8_t* value)
{
    int32_t  len = CRTL_strlen(value);
    uint8_t* ps = value;
    uint8_t* pe = value + len -1;
    while (*ps == 0x20 || *ps == '\t') ps++;
    while (*pe == 0x20 || *pe == '\t') pe--;
    if (pe != (value + len -1))
        *pe = '\0';
    return ps;
}

static bool_t setPropsValue(uint8_t* key, AppletProps* appProp)
{
    int32_t  appNameLen    = (int32_t)CRTL_strlen(APP_NAME);
    int32_t  appVerionLen  = (int32_t)CRTL_strlen(APP_VERSION);
    int32_t  appVendorLen  = (int32_t)CRTL_strlen(APP_VENDOR);
    int32_t  appMainClsLen = (int32_t)CRTL_strlen(MAIN_CLASS);
    uint8_t* propVal;

    if (!CRTL_memcmp(key, APP_NAME, appNameLen) && key[appNameLen] == ':')
    {
        propVal = trim(&key[appNameLen+1]);
        CRTL_memcpy(appProp->name, propVal, CRTL_strlen(propVal));
    }
    else if (!CRTL_memcmp(key, APP_VERSION, appVerionLen) && key[appVerionLen] == ':')
    {
        propVal = trim(&key[appVerionLen+1]);
        CRTL_memcpy(appProp->version, propVal, CRTL_strlen(propVal));
    }
    else if (!CRTL_memcmp(key, APP_VENDOR, appVendorLen) && key[appVendorLen] == ':')
    {
        propVal = trim(&key[appVendorLen+1]);
        CRTL_memcpy(appProp->vendor, propVal, CRTL_strlen(propVal));
    }
    else if (!CRTL_memcmp(key, MAIN_CLASS, appMainClsLen) && key[appMainClsLen] == ':')
    {
        propVal = trim(&key[appMainClsLen+1]);
        CRTL_memcpy(appProp->mainCls, propVal, CRTL_strlen(propVal));
    }
    else
    {
        //Unsupported properties!
        return FALSE;
    }
    return TRUE;
}

static AppletProps* getAppletPropById(int32_t id)
{
    int32_t i;
    AppletProps *pap;
    for (i = 0, pap = appletsList; i < MAX_APPS_NUM; i++)
    {
        if (pap[i].id == id)
        {
            return pap;
        }
    }
    return NULL;
}

/**
 * parse applet properties and save result into appProp.
 * @data, the raw data of applet properties which unzip from manifest.mf
 * @appProp, the ouput variable used to save the parsed properties.
 * NOTE:
 *      the properties including below items:
 *          Applet-Name:
 *          Applet-Vendor:
 *          Applet-Version:
 *          Main-Class:
 *      Others are be igo
 */
static bool_t parseAppletProps(uint8_t* data, int32_t dataBytes, AppletProps* appProp)
{
    int32_t  i;
    uint8_t* ps = data;

    if (data == NULL || appProp == NULL)
    {
        DVMTraceErr("parseAppletProps, Wrong arguments\n");
        return FALSE;
    }
    
    for (i = 0; i < dataBytes; i++)
    {
        if (data[i] != '\r')
        {
            continue;
        }
        data[i] = '\0';
        if (data[i+1] == '\n')
        {
            i++; //skip '\n'.
        }

        setPropsValue(ps, appProp);

        ps = &data[i+1];
    }
    
    if (ps < data + dataBytes)
    {
        //To handle the case there is no line ending in last line.
        setPropsValue(ps, appProp);
    }

    return TRUE;
}

static uint16_t* getFileNameFromPath(uint16_t* path)
{
    uint16_t* rec = NULL;
    uint16_t* p = path;

    while(p != NULL && *p != '\0')
    {
        if (*p == '\\' || *p == '/')
        {
            rec = p+1;
        }
        p++;
    }
    return rec;
}

/**
 * Get the applets list in the specified installed path.
 * if path is null, the default installed path will be used.
 *
 * @param path, the installed path.
 * @return the applets list with pre-defined struct.
 */
static AppletProps* listInstalledApplets(const uint16_t* path)
{
    uint8_t*     data = NULL;
    bool_t       res = TRUE;
    uint16_t*    ins_path = getDefaultInstalledPath();
    AppletProps* appList = NULL;

    if (path != NULL)
    {
        ins_path = (uint16_t*)path;
    }

    do
    {
        int32_t   index = 0;
        int32_t   handle = 0;
        int32_t   result = 0;
        int32_t   pathLen = CRTL_wcslen(ins_path);
        uint16_t  foundJarPath[MAX_FILE_NAME_LEN];

        if (file_listOpen(path, pathLen, &handle) != FILE_RES_SUCCESS)
        {
            res = FALSE;
            break;
        }

        appList = (AppletProps*)CRTL_malloc(sizeof(AppletProps) * MAX_APPS_NUM);
        if (appList == NULL)
        {
            res = FALSE;
            break;
        }
        CRTL_memset(appList, 0x0, sizeof(AppletProps) * MAX_APPS_NUM);
        for (index = 0; index < MAX_APPS_NUM; index++)
        {
            //set all props nodes to default value - PROPS_UNUSED.
            appList[index].id = PROPS_UNUSED;
        }
        index = 0;

        do
        {
            CRTL_memset(foundJarPath, 0x0, MAX_FILE_NAME_LEN);
            result = file_listNextEntry(ins_path, pathLen, foundJarPath, MAX_FILE_NAME_LEN<<1, &handle);
            
            if (!(result > 0))
            {
                if (result < 0) res = FALSE;
                break;
            }
            else if (foundJarPath[0] != '\0')
            {
                int32_t dataBytes;
                int32_t handle;

                if (index == MAX_APPS_NUM)
                {
                    //should we realloc memory here?
                    DVMTraceWar("listInstalledApplets: max app number is reached\n");
                    //res = FALSE;
                    break;
                }

                if ((handle = openJar(foundJarPath)) < 0)
                    continue;
                data = getJarContentByFileName(handle, MANIFEST_MF, &dataBytes);
                if (data == NULL)
                {
                    DVMTraceWar("listInstalledApplets: invalid data\n");
                    closeJar(handle);
                    continue;
                }
                appList[index].id = index; //set id to index
                {
                    /* save file name for delete */
                    int32_t  len;
                    uint16_t *p = getFileNameFromPath(foundJarPath);
                    len = CRTL_wcslen(p);
                    CRTL_memcpy(appList[index].fname, p, len*sizeof(uint16_t));
                    appList[index].fname[len] = '\0';
                }
                parseAppletProps(data, dataBytes, &appList[index++]);
                closeJar(handle);
            }

        } while(foundJarPath[0] != '\0' && result > 0);

        file_listclose(&handle);

    } while(FALSE);

    CRTL_freeif(data);
    if (!res)
    {
        CRTL_freeif(appList);
        return NULL;
    }
    return appList;
}

/**
 * Parse server commands.
 * @param data, raw data from server side.
 * @param dataBytes, raw data size in bytes.
 */
static int32_t parseServerCommands(uint8_t* data, int32_t dataBytes)
{
    uint8_t* p  = data;
    int32_t len = readbeIU32(p);
    int32_t cmd = readbeIU32(p+=4);
    int32_t reserve1 = readbeIU32(p+=4);
    int32_t reserve2 = readbeIU32(p+=4);
    int32_t res = cmd;
    Event   newEvt;

	DVMTraceErr("parseServerCommands:cmd - %d\n",cmd);
    switch (cmd)
    {
    case EVT_CMD_INSTALL:
        //not supported, ignore.
        break;

    case EVT_CMD_LIST:
        if (len != 16)
        {
            DVMTraceErr("parseServerCommands: Error, wrong data length.\n");
            res = EVT_RES_FAILURE;
            break;
        }

        newNormalEvent(EVT_CMD_LIST, FSM_STATE_UNSET, NULL, ramsClient_cmdAction, &newEvt);
        ES_pushEvent(&newEvt);
        break;

    case EVT_CMD_DELETE:
    case EVT_CMD_RUN:
    case EVT_CMD_DESTROY:
        {
            int32_t length = readbeIU32(p+=4);
            int32_t appId  = readbeIU32(p+=4);
            SafeBuffer *safeBuf;
            int32_t safeBufSize;

            if (len != 16 + 4 + length)
            {
                DVMTraceErr("parseServerCommands: Error, wrong data length.\n");
                res = EVT_RES_FAILURE;
                break;
            }
            safeBufSize = sizeof(int32_t) + sizeof(SafeBuffer);
            safeBuf = (SafeBuffer *)CRTL_malloc(safeBufSize);
            if (safeBuf == NULL)
            {
                res = EVT_RES_FAILURE;
                break;
            }
            CRTL_memset(safeBuf, 0x0, safeBufSize);

            safeBuf->pBuf = ((uint8_t*)(safeBuf)+sizeof(SafeBuffer));
            safeBuf->bytes = sizeof(int32_t);
            safeBuf->buffer_free = SafeBufferFree;
            *((int32_t*)(safeBuf->pBuf)) = appId;

            newNormalEvent(cmd, FSM_STATE_UNSET, (void *)safeBuf, ramsClient_cmdAction, &newEvt);
            ES_pushEvent(&newEvt);
        }
        break;

    case EVT_CMD_OTA:
        break;

    case EVT_CMD_NONE:
    default:
        break;
    }
    
    return res;
}

static uint8_t* combineAppPath(uint16_t* appName)
{
    int32_t   len = CRTL_wcslen(getDefaultInstalledPath()) + CRTL_wcslen(appName);
    uint16_t  fUcsPath[MAX_FILE_NAME_LEN] = {0x0,};
    uint8_t*  fpath;

    CRTL_wcscpy(fUcsPath, getDefaultInstalledPath());
    CRTL_wcscat(fUcsPath, appName);

    fpath = CRTL_malloc((len+1)*3);//utf8 encoding.
    if (fpath != NULL) 
    {
        CRTL_memset(fpath, 0x0, (len+1)*3);
        convertUcs2ToUtf8(fUcsPath, len, fpath, len*3);
    }

    return fpath;//this space will be freed in destroyApplet.
}

static int32_t VMThreadProc(int argc, char* argv[])
{
	DVMTraceInf("Enter dvm thread,argc=%d,argv=0x%x\n",argc,(void*)argv);
	DVMTraceInf("argv-0:%s,argv-1:%s,argv-2:%s\n",argv[0],argv[1],argv[2]);
    DVMTraceInf("Enter dvm thread,sleep over,sizeof(int)=%d,sizeof(int32_t)=%d\n",sizeof(int),sizeof(int32_t));    
    DVM_main(argc, argv);
    return 0;
}


static int32_t ramsClient_cmdAction(Event *evt, void *userData)
{
    int32_t evtId = UNMARK_EVT_ID(evt->evtId);
    SafeBuffer *safeBuf;
    int32_t     safeBufSize;
    int32_t     dataSize;
    uint8_t    *pb = NULL;

    switch(evtId)
    {
    case EVT_CMD_LIST:
        {
            AppletProps* p = appletsList;
            int32_t   appNum;
            int32_t   i;

            if (p == NULL) return 0;

            do 
            {
                dataSize = 16;
                for (i = 0, appNum = 0; i < MAX_APPS_NUM; i++)
                {
                    if (p[i].id != PROPS_UNUSED)
                    {
                        uint16_t len = (uint16_t)CRTL_strlen(p[i].name);
                        if (pb != NULL)
                        {
                            writebeIU16(&pb[dataSize], len+2/*2 bytes of appid*/);
                            writebeIU16(&pb[dataSize+2], (uint16_t)p[i].id);
                            CRTL_memcpy(&pb[dataSize+4], p[i].name, len);
                            appNum++;
                        }
                        dataSize += 4; //length + app id
                        dataSize += len;
                    }
                }
                if (pb != NULL)
                {
                    Event newEvt;
                    writebeIU32(&pb[0], dataSize);
                    writebeIU32(&pb[4], ACK_CMD_LIST);
                    writebeIU32(&pb[8], appNum);
                    writebeIU32(&pb[12], 0); //reserved bytes
                    
                    newNormalEvent(EVT_CMD_DECLARE, DECLARE_FSM_WRITE, (void *)safeBuf, ramsClient_buildConnection, &newEvt);
                    ES_pushEvent(&newEvt);
                    break;
                }
                else
                {
                    safeBufSize = dataSize + sizeof(SafeBuffer);
                    safeBuf = CRTL_malloc(safeBufSize);
                    if (safeBuf == NULL)
                    {
                        return EVT_RES_FAILURE;
                    }
                    CRTL_memset(safeBuf, 0x0, safeBufSize);
                    safeBuf->pBuf = ((uint8_t*)(safeBuf)+sizeof(SafeBuffer));
                    safeBuf->bytes = dataSize;
                    safeBuf->buffer_free = SafeBufferFree;
                    pb = safeBuf->pBuf;
                }
            } while(TRUE);
        }
        break;

    case EVT_CMD_DELETE:
        {
            int32_t     appId;
            SafeBuffer *data;
            if (userData != NULL)
            {
                data = (SafeBuffer *)userData;
            }
            else
            {
                data = (SafeBuffer *)evt->userData;
            }
            appId = *((int32_t*)(data->pBuf));
            ramsClient_deleteAppletById(appId);
            data->buffer_free(data);
        }
        break;

    case EVT_CMD_RUN:
        {
            int32_t     appId;
            SafeBuffer *data;
            if (userData != NULL)
            {
                data = (SafeBuffer *)userData;
            }
            else
            {
                data = (SafeBuffer *)evt->userData;
            }
            appId = *((int32_t*)(data->pBuf));
            ramsClient_runApplet(appId);
            data->buffer_free(data);
        }
        break;

    case EVT_CMD_DESTROY:
        {
            int32_t     appId;
            SafeBuffer *data;
            if (userData != NULL)
            {
                data = (SafeBuffer *)userData;
            }
            else
            {
                data = (SafeBuffer *)evt->userData;
            }
            appId = *((int32_t*)(data->pBuf));
            ramsClient_destroyApplet(appId);
            data->buffer_free(data);
        }
        break;
        
    default:
        break;
    }

    return EVT_RES_SUCCESS;
}

/**
 * It's a state machine for RAMS communication with server.
 * @param evt, current event.
 * @param userData, user data.
 */
static int32_t ramsClient_buildConnection(Event *evt, void *userData)
{
    int32_t evtId = UNMARK_EVT_ID(evt->evtId);

    if (evtId == EVT_CMD_DECLARE)
    {
        int32_t fsm_state = FSM_UNMARK(evt->fsm_state);
        
        if (fsm_state)

        switch (fsm_state)
        {
        case DECLARE_FSM_STARTUP:
            if (rams_startupNetwork() == RAMS_RES_SUCCESS)
            {
                evt->fsm_state = DECLARE_FSM_CONNECT;
                ES_pushEvent(evt);
            }
            break;

        case DECLARE_FSM_CONNECT:
            if (rams_connectServer(RS_ADDRESS, RS_PORT, &servInstance) == RAMS_RES_SUCCESS)
            {
                evt->fsm_state = DECLARE_FSM_READ;
                ES_pushEvent(evt);
            }
            break;

        case DECLARE_FSM_READ:
            {
                SafeBuffer *recvBuf = NULL;
                if (userData != NULL)
                {
                    recvBuf = (SafeBuffer *)userData;
                }
                else
                {
                    int32_t bufSize = (DATA_BUF_SIZE * sizeof(uint8_t) + sizeof(SafeBuffer));
                    recvBuf = (SafeBuffer*)CRTL_malloc(bufSize);
                    if (recvBuf == NULL)
                    {
                        return EVT_RES_FAILURE;
                    }
                    CRTL_memset(recvBuf, 0x0, bufSize);
                    recvBuf->pBuf = (uint8_t*)(((uint8_t*)recvBuf) + sizeof(SafeBuffer));
                    recvBuf->buffer_free = SafeBufferFree;
                }
                evt->userData = (void *)recvBuf; //saved into userData;
                recvBuf->bytes = rams_recvData(servInstance, recvBuf->pBuf, DATA_BUF_SIZE);

                if (recvBuf->bytes > 0)
                {
                    Event newEvt;
                    newNormalEvent(EVT_CMD_PARSER, FSM_STATE_UNSET, (void *)CreateSafeBuffer(recvBuf), 
                                   ramsClient_buildConnection, &newEvt);
                    ES_pushEvent(&newEvt);
                }
                if (recvBuf->bytes != EVT_RES_WOULDBLOCK)
                {
                    /* try to get new commands from sever after 2000ms */
                    ES_scheduleAgainAsFirstTimeWithTimeout(2000);
                }
            }
            break;

        case DECLARE_FSM_WRITE:
            {
                SafeBuffer *sendBuf = NULL;
                if (userData != NULL)
                {
                    sendBuf = (SafeBuffer *)userData;
                }
                else
                {
                    sendBuf = (SafeBuffer *)evt->userData;
                }
                rams_sendData(servInstance, sendBuf->pBuf, sendBuf->bytes);
                sendBuf->buffer_free(sendBuf);
            }
            break;

        case DECLARE_FSM_CLOSE:
            rams_closeConnection(servInstance);
            break;

        case DECLARE_FSM_SHUTDOWN:
            rams_shutdownNetwork(servInstance);
            break;

        default:
            break;
        }
    }
    else if (evtId == EVT_CMD_PARSER)
    {
        SafeBuffer *data;
        if (userData != NULL)
        {
            data = (SafeBuffer *)userData;
        }
        else
        {
            data = (SafeBuffer *)evt->userData;
        }

        if (data == NULL)
        {
            /* no data */
            return EVT_RES_FAILURE;
        }
         
        parseServerCommands(data->pBuf, data->bytes);

        data->buffer_free(data);
    }

    return EVT_RES_SUCCESS;
}

/* see amsclient.h */
int32_t ramsClient_lifecycleProcess(Event *evt, void *userData)
{
    int32_t evtId = UNMARK_EVT_ID(evt->evtId);
    Event   newEvt;
    switch (evtId)
    {
    case EVT_SYS_INIT:
        file_startup();
        appletsList = listInstalledApplets(NULL);
        /* push connection event to queue */
        currentFsmState = DECLARE_FSM_STARTUP;
        newNormalEvent(EVT_CMD_DECLARE, currentFsmState, NULL, ramsClient_buildConnection, &newEvt);
        newEvt.priority = EP_HIGH;
        ES_pushEvent(&newEvt);
        break;

    case EVT_SYS_EXIT:
        CRTL_freeif(appletsList);
        file_shutdown();
        break;

    default:
        break;
    }
    return EVT_RES_SUCCESS;
}


/* see ramsclient.h */
bool_t ramsClient_deleteAppletById(int32_t id)
{
    bool_t res = TRUE;
    AppletProps *pAppProp;
    uint16_t     fpath[MAX_FILE_NAME_LEN] = {0x0,};
    uint8_t      ackBuf[16] = {0x0,};
    uint8_t     *pByte;
    SafeBuffer  *safeBuf;
    Event        newEvt;

    do
    {
        if (id < 0)
        {
            DVMTraceWar("deleteAppletById: Invalid id (%d)", id);
            res = FALSE;
            break;
        }

        if ((pAppProp = getAppletPropById(id)) == NULL)
        {
            DVMTraceWar("deleteAppletById: Unknown id (%d)", id);
            res = FALSE;
            break;
        }

        if (pAppProp->isRunning)
        {
            //TODO: stop the running appliction.
            //remove this node from running linked list.
        }

        CRTL_wcscpy(fpath, getDefaultInstalledPath());
        CRTL_wcscat(fpath, pAppProp->fname);

        if (file_delete(fpath, CRTL_wcslen(fpath)) != FILE_RES_SUCCESS)
        {
            DVMTraceWar("deleteAppletById: remove application content failure");
            res = FALSE;
            break;
        }
        CRTL_memset(pAppProp, 0x0, sizeof(AppletProps)); //clear
        pAppProp->id = PROPS_UNUSED;

    } while(FALSE);

    pByte = (uint8_t*)ackBuf;

    writebeIU32(&pByte[0], sizeof(ackBuf));
    writebeIU32(&pByte[4], ACK_RECV_AND_EXEC);
    writebeIU32(&pByte[8], EVT_CMD_DELETE);
    writebeIU32(&pByte[12], (res ? 1 : 0));

    safeBuf = CreateSafeBufferByBin(ackBuf, sizeof(ackBuf));
    newNormalEvent(EVT_CMD_DECLARE, DECLARE_FSM_WRITE, (void *)safeBuf, ramsClient_buildConnection, &newEvt);
    ES_pushEvent(&newEvt);

    return res;
}

/* see ramsclient.h */
bool_t ramsClient_runApplet(int32_t id)
{
    AppletProps *pap;
    uint8_t      ackBuf[16] = {0x0};
    uint8_t     *pByte;
    bool_t       res = TRUE;
    static char *argv[3];
    SafeBuffer  *safeBuf;
    Event        newEvt;

#ifdef NOT_LAUNCH_NET_TASK
    file_startup();
    appletsList = listInstalledApplets(NULL);
#endif
    do
    {
        if ((pap = getAppletPropById(id)) == NULL)
        {
            DVMTraceErr("runApplet failure, no such id(%d)\n", id);
            res = FALSE;
            break;
        }
        pap->fpath = combineAppPath(pap->fname);

        argv[0] = "-run";
        argv[1] = pap->fpath;
        argv[2] = pap->mainCls;
	    DVMTraceInf("argv=0x%x,argv-1:%s,argv-2:%s,argv-3:%s\n",(void*)argv,argv[0],argv[1],argv[2]);

        if (rams_createVMThread(VMThreadProc, 3, argv) < 0)
        {
            DVMTraceErr("lauch VM thread failure\n");
            res = FALSE;
            break;
        }
        curActiveApp = pap;
    }
    while(FALSE);
#ifdef NOT_LAUNCH_NET_TASK
    return FALSE;
#endif

    pByte = (uint8_t*)ackBuf;

    writebeIU32(&pByte[0], sizeof(ackBuf));

    if (res)
    {
        writebeIU32(&pByte[4], ACK_RECV_WITHOUT_EXEC);
        writebeIU32(&pByte[8], 0x1);
        writebeIU32(&pByte[12], 0x0);
    }
    else
    {
        writebeIU32(&pByte[4], ACK_RECV_AND_EXEC);
        writebeIU32(&pByte[8], EVT_CMD_RUN);
        writebeIU32(&pByte[12], 0x0);
    }
    
    safeBuf = CreateSafeBufferByBin(ackBuf, sizeof(ackBuf));
    newNormalEvent(EVT_CMD_DECLARE, DECLARE_FSM_WRITE, (void *)safeBuf, ramsClient_buildConnection, &newEvt);
    ES_pushEvent(&newEvt);

    return res;
}

/* see ramsclient.h */
bool_t ramsClient_destroyApplet(int32_t id)
{
    AppletProps *pap;
    uint8_t      ackBuf[16] = {0x0,};
    uint8_t     *pByte;
    SafeBuffer  *safeBuf;
    Event        newEvt;

    if ((pap = getAppletPropById(id)) == NULL || !pap->isRunning)
    {
        DVMTraceErr("destroyApplet, wrong app id(%d) or this app is not running\n");
        return FALSE;
    }
    CRTL_freeif(pap->fpath);
    upcallDestroyApplet(pap);

    pByte = (uint8_t*)ackBuf;

    writebeIU32(&pByte[0], sizeof(ackBuf));
    writebeIU32(&pByte[4], ACK_RECV_WITHOUT_EXEC);
    writebeIU32(&pByte[8], 0x1);
    writebeIU32(&pByte[12], 0x0);

    safeBuf = CreateSafeBufferByBin(ackBuf, sizeof(ackBuf));
    newNormalEvent(EVT_CMD_DECLARE, DECLARE_FSM_WRITE, (void *)safeBuf, ramsClient_buildConnection, &newEvt);
    ES_pushEvent(&newEvt);

    return TRUE;
}

/* see ramsclient.h */
bool_t ramsClient_sendBackExecResult(int32_t cmd, bool_t res)
{
    uint8_t     ackBuf[16] = {0x0,};
    uint8_t    *pByte;
    SafeBuffer *safeBuf;
    Event       newEvt;

    if (curActiveApp == NULL)
    {
        DVMTraceWar("No Applet in launching state\n");
        return FALSE;
    }
    pByte = (uint8_t*)ackBuf;

    writebeIU32(&pByte[0], sizeof(ackBuf));
    writebeIU32(&pByte[4], ACK_RECV_AND_EXEC);
    writebeIU32(&pByte[8], cmd);
    writebeIU32(&pByte[12], (res ? 1 : 0));

    if (cmd == EVT_CMD_RUN)
    {
        curActiveApp->isRunning = res ? TRUE : FALSE;
    }
    else if (cmd == EVT_CMD_DESTROY)
    {
        curActiveApp->isRunning = res ? FALSE : TRUE;
        curActiveApp = NULL; //destroyed success;
    }
    
    safeBuf = CreateSafeBufferByBin(ackBuf, sizeof(ackBuf));
    newNormalEvent(EVT_CMD_DECLARE, DECLARE_FSM_WRITE, (void *)safeBuf, ramsClient_buildConnection, &newEvt);
    ES_pushEvent(&newEvt);

    return TRUE;
}