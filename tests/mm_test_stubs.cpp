//
// metamod - strong definitions of the process-wide globals that the module
// normally receives from metamod.cpp / h_export.cpp / mutil.cpp, plus a
// minimal engine function table and a small message capture buffer.
//
// Tests link the real production translation units against these stubs instead
// of the module roots, so the code under test stays the shipped code. The
// linker resolves everything from the sources that are explicitly listed in
// tests/CMakeLists.txt; nothing here duplicates a linked production symbol.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <extdll.h>

#include "metamod.h"
#include "log_meta.h"
#include "engine_t.h"
#include "mutil.h"

#include "mm_test_stubs.h"

// ============================================================
// Globals normally owned by metamod.cpp
// ============================================================

DLHANDLE           metamod_handle = NULL;
cvar_t             meta_version   = {(char*)"metamod_version", (char*)"test", 0, 0, NULL};
gamedll_t          GameDLL;
MConfig*           Config   = NULL;
MPluginList*       Plugins  = NULL;
MRegCmdList*       RegCmds  = NULL;
MRegCvarList*      RegCvars = NULL;
MRegMsgList*       RegMsgs  = NULL;
meta_globals_t     PublicMetaGlobals;
meta_globals_t     PrivateMetaGlobals;
meta_enginefuncs_t g_plugin_engfuncs;
MPlayerList        g_Players;
int                requestid_counter  = 0;
int                metamod_not_loaded = 0;

DLL_FUNCTIONS*     g_pHookedDllFunctions    = NULL;
NEW_DLL_FUNCTIONS* g_pHookedNewDllFunctions = NULL;

// ============================================================
// Globals normally owned by h_export.cpp
// ============================================================

HL_enginefuncs_t    g_engfuncs;
static globalvars_t mock_globalvars;
globalvars_t*       gpGlobals = &mock_globalvars;
engine_t            Engine;

// ============================================================
// Globals normally owned by mutil.cpp (full hooking implementation)
// ============================================================

mutil_funcs_t MetaUtilFunctions;

// ============================================================
// Globals normally owned by api_info.cpp
//
// Only used to walk a plugin's function tables in MPlugin::show(). An empty
// table means "no function named", which is what the tests want, and keeps the
// api_hook machinery out of the link.
// ============================================================

#ifndef MMFG_TEST_REAL_API_INFO
const dllapi_info_t    dllapi_info    = {};
const newapi_info_t    newapi_info    = {};
const studioapi_info_t studioapi_info = {};
const engine_info_t    engine_info    = {};
#endif

// ============================================================
// Captured engine output
// ============================================================

#define MM_TEST_MAX_MSGS 32
#define MM_TEST_MSG_LEN  1024

static char mock_alert_msgs[MM_TEST_MAX_MSGS][MM_TEST_MSG_LEN];
static int  mock_alert_count;
static char mock_gamedir[PATH_MAX];

int mm_test_alert_count(void)
{
    return mock_alert_count;
}

const char* mm_test_alert_msg(int index)
{
    if (index < 0 || index >= mock_alert_count)
        return "";
    return mock_alert_msgs[index];
}

void mm_test_set_gamedir(const char* dir)
{
    STRNCPY(mock_gamedir, dir, sizeof(mock_gamedir));
}

// ============================================================
// Engine function stubs
// ============================================================

static void mock_pfnAlertMessage(ALERT_TYPE atype, const char* szFmt, ...)
{
    (void)atype;
    if (mock_alert_count < MM_TEST_MAX_MSGS)
    {
        va_list ap;
        va_start(ap, szFmt);
        vsnprintf(mock_alert_msgs[mock_alert_count], MM_TEST_MSG_LEN, szFmt, ap);
        va_end(ap);
        mock_alert_count++;
    }
}

static void mock_pfnServerPrint(const char* msg)
{
    (void)msg;
}

static float mock_pfnCVarGetFloat(const char* szVarName)
{
    (void)szVarName;
    return 0.0f;
}

static const char* mock_pfnCVarGetString(const char* szVarName)
{
    (void)szVarName;
    return "";
}

static void mock_pfnCVarSetFloat(const char* szVarName, float value)
{
    (void)szVarName;
    (void)value;
}

static void mock_pfnCVarSetString(const char* szVarName, const char* value)
{
    (void)szVarName;
    (void)value;
}

static void mock_pfnCVarRegister(cvar_t* variable)
{
    (void)variable;
}

static cvar_t* mock_pfnCVarGetPointer(const char* szVarName)
{
    (void)szVarName;
    return NULL;
}

static void mock_pfnAddServerCommand(char* cmd_name, void (*function)(void))
{
    (void)cmd_name;
    (void)function;
}

static int mock_pfnRegUserMsg(const char* pszName, int iSize)
{
    (void)iSize;
    free((void*)pszName);
    return 0;
}

static void mock_pfnMessageBegin(int msg_dest, int msg_type, const float* pOrigin, edict_t* ed)
{
    (void)msg_dest;
    (void)msg_type;
    (void)pOrigin;
    (void)ed;
}

static void mock_pfnMessageEnd(void) {}
static void mock_pfnWriteByte(int val) { (void)val; }
static void mock_pfnWriteChar(int val) { (void)val; }
static void mock_pfnWriteShort(int val) { (void)val; }
static void mock_pfnWriteLong(int val) { (void)val; }
static void mock_pfnWriteAngle(float val) { (void)val; }
static void mock_pfnWriteCoord(float val) { (void)val; }
static void mock_pfnWriteString(const char* s) { (void)s; }
static void mock_pfnWriteEntity(int val) { (void)val; }

static edict_t mock_edicts[33];

static edict_t* mock_pfnPEntityOfEntIndex(int iEntIndex)
{
    if (iEntIndex < 0 || iEntIndex >= 33)
        return NULL;
    return &mock_edicts[iEntIndex];
}

static int mock_pfnIndexOfEdict(const edict_t* pEdict)
{
    (void)pEdict;
    return 0;
}

static int mock_pfnEntOffsetOfPEntity(const edict_t* pEdict)
{
    return pEdict ? 1 : 0;
}

static char* mock_pfnGetInfoKeyBuffer(edict_t* e)
{
    static char buf[256] = "";
    (void)e;
    return buf;
}

static char* mock_pfnInfoKeyValue(char* infobuffer, char* key)
{
    static char empty[] = "";
    (void)infobuffer;
    (void)key;
    return empty;
}

static void mock_pfnSetKeyValue(char* infobuffer, char* key, char* value)
{
    (void)infobuffer;
    (void)key;
    (void)value;
}

static void mock_pfnClientPrintf(edict_t* pEdict, PRINT_TYPE ptype, const char* szMsg)
{
    (void)pEdict;
    (void)ptype;
    (void)szMsg;
}

static void mock_pfnGetGameDir(char* szGetGameDir)
{
    strcpy(szGetGameDir, mock_gamedir);
}

static void mock_pfnServerCommand(char* str)
{
    (void)str;
}

static byte* mock_pfnLoadFileForMe(char* filename, int* pLength)
{
    (void)filename;
    if (pLength)
        *pLength = 0;
    return NULL;
}

static void mock_pfnFreeFile(void* buffer)
{
    (void)buffer;
}

static int mock_pfnCmd_Argc(void)
{
    return 0;
}

static const char* mock_pfnCmd_Argv(int argc)
{
    (void)argc;
    return "";
}

static const char* mock_pfnCmd_Args(void)
{
    return "";
}

// ============================================================
// Reset
// ============================================================

void mm_test_reset(void)
{
    memset(&mock_globalvars, 0, sizeof(mock_globalvars));
    mock_globalvars.maxClients = 32;
    gpGlobals                  = &mock_globalvars;

    memset(&g_engfuncs, 0, sizeof(g_engfuncs));
    g_engfuncs.pfnAlertMessage       = mock_pfnAlertMessage;
    g_engfuncs.pfnServerPrint        = mock_pfnServerPrint;
    g_engfuncs.pfnCVarGetFloat       = mock_pfnCVarGetFloat;
    g_engfuncs.pfnCVarGetString      = mock_pfnCVarGetString;
    g_engfuncs.pfnCVarSetFloat       = mock_pfnCVarSetFloat;
    g_engfuncs.pfnCVarSetString      = mock_pfnCVarSetString;
    g_engfuncs.pfnCVarRegister       = mock_pfnCVarRegister;
    g_engfuncs.pfnCVarGetPointer     = mock_pfnCVarGetPointer;
    g_engfuncs.pfnAddServerCommand   = mock_pfnAddServerCommand;
    g_engfuncs.pfnRegUserMsg         = mock_pfnRegUserMsg;
    g_engfuncs.pfnMessageBegin       = mock_pfnMessageBegin;
    g_engfuncs.pfnMessageEnd         = mock_pfnMessageEnd;
    g_engfuncs.pfnWriteByte          = mock_pfnWriteByte;
    g_engfuncs.pfnWriteChar          = mock_pfnWriteChar;
    g_engfuncs.pfnWriteShort         = mock_pfnWriteShort;
    g_engfuncs.pfnWriteLong          = mock_pfnWriteLong;
    g_engfuncs.pfnWriteAngle         = mock_pfnWriteAngle;
    g_engfuncs.pfnWriteCoord         = mock_pfnWriteCoord;
    g_engfuncs.pfnWriteString        = mock_pfnWriteString;
    g_engfuncs.pfnWriteEntity        = mock_pfnWriteEntity;
    g_engfuncs.pfnPEntityOfEntIndex  = mock_pfnPEntityOfEntIndex;
    g_engfuncs.pfnIndexOfEdict       = mock_pfnIndexOfEdict;
    g_engfuncs.pfnEntOffsetOfPEntity = mock_pfnEntOffsetOfPEntity;
    g_engfuncs.pfnGetInfoKeyBuffer   = mock_pfnGetInfoKeyBuffer;
    g_engfuncs.pfnInfoKeyValue       = mock_pfnInfoKeyValue;
    g_engfuncs.pfnSetKeyValue        = mock_pfnSetKeyValue;
    g_engfuncs.pfnClientPrintf       = mock_pfnClientPrintf;
    g_engfuncs.pfnGetGameDir         = mock_pfnGetGameDir;
    g_engfuncs.pfnServerCommand      = mock_pfnServerCommand;
    g_engfuncs.pfnLoadFileForMe      = mock_pfnLoadFileForMe;
    g_engfuncs.pfnFreeFile           = mock_pfnFreeFile;
    g_engfuncs.pfnCmd_Argc           = mock_pfnCmd_Argc;
    g_engfuncs.pfnCmd_Argv           = mock_pfnCmd_Argv;
    g_engfuncs.pfnCmd_Args           = mock_pfnCmd_Args;

    mock_gamedir[0] = '\0';

    memset(&Engine, 0, sizeof(Engine));
    Engine.funcs    = &g_engfuncs;
    Engine.pl_funcs = &g_engfuncs;
    Engine.globals  = gpGlobals;

    memset(&GameDLL, 0, sizeof(GameDLL));
    memset(&PublicMetaGlobals, 0, sizeof(PublicMetaGlobals));
    memset(&PrivateMetaGlobals, 0, sizeof(PrivateMetaGlobals));
    memset(&g_plugin_engfuncs, 0, sizeof(g_plugin_engfuncs));
    memset(&MetaUtilFunctions, 0, sizeof(MetaUtilFunctions));

    metamod_handle = NULL;
    Config         = NULL;
    Plugins        = NULL;
    RegCmds        = NULL;
    RegCvars       = NULL;
    RegMsgs        = NULL;

    g_pHookedDllFunctions    = NULL;
    g_pHookedNewDllFunctions = NULL;

    metamod_not_loaded = 0;
    requestid_counter  = 0;

    mock_alert_count = 0;
    memset(mock_alert_msgs, 0, sizeof(mock_alert_msgs));
}
