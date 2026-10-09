// A real loadable plugin fixture exposing all eight API tables.
#include <cstring>
#include <extdll.h>
#include <meta_api.h>

static meta_globals_t* globals;
static int (*record_call)(int);
static bool          reject_attach, reject_detach;
static plugin_info_t info = {
    META_INTERFACE_VERSION, "Hook fixture", "1", "test", "test", "", "TEST",
    PT_ANYPAUSE, PT_ANYPAUSE};

C_DLLEXPORT void ConfigureFixture(int (*callback)(int), int attach_fails, int detach_fails)
{
    record_call   = callback;
    reject_attach = attach_fails != 0;
    reject_detach = detach_fails != 0;
}

template <int Id> static int invoke()
{
    globals->mres = MRES_IGNORED;
    return record_call ? record_call(Id) : 0;
}
template <int Id> static void noargs() { invoke<Id>(); }
template <int Id> static int  count() { return invoke<Id>(); }
template <int Id> static void entity(edict_t*) { invoke<Id>(); }
template <int Id> static int  spawn(edict_t*) { return invoke<Id>(); }
template <int Id> static int  collide(edict_t*, edict_t*) { return invoke<Id>(); }
template <int Id> static void bones(model_t*, float, int, const float*, const float*, const byte*, const byte*, int, const edict_t*) { invoke<Id>(); }
template <int Id> static int  engine_table(enginefuncs_t* table, int*)
{
    table->pfnServerExecute    = noargs<Id>;
    table->pfnNumberOfEntities = count<Id>;
    return TRUE;
}
template <int Id> static int dll_table(DLL_FUNCTIONS* table, int*)
{
    table->pfnGameInit = noargs<Id>;
    table->pfnSpawn    = spawn<Id>;
    return TRUE;
}
template <int Id> static int new_table(NEW_DLL_FUNCTIONS* table, int*)
{
    table->pfnOnFreeEntPrivateData = entity<Id>;
    table->pfnShouldCollide        = collide<Id>;
    return TRUE;
}
template <int Id> static int studio_table(sv_blending_interface_t* table, int*)
{
    table->version             = SV_BLENDING_INTERFACE_VERSION;
    table->SV_StudioSetupBones = bones<Id>;
    return TRUE;
}

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t*, globalvars_t*) {}
C_DLLEXPORT int         Meta_Query(const char*, plugin_info_t** out, mutil_funcs_t*)
{
    *out = &info;
    return TRUE;
}
C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME, META_FUNCTIONS* table, meta_globals_t* shared, gamedll_funcs_t*)
{
    if (reject_attach) return FALSE;
    globals                                   = shared;
    table->pfnGetEngineFunctions              = engine_table<1>;
    table->pfnGetEngineFunctions_Post         = engine_table<4>;
    table->pfnGetEntityAPI2                   = dll_table<1>;
    table->pfnGetEntityAPI2_Post              = dll_table<4>;
    table->pfnGetNewDLLFunctions              = new_table<1>;
    table->pfnGetNewDLLFunctions_Post         = new_table<4>;
    table->pfnGetStudioBlendingInterface      = studio_table<1>;
    table->pfnGetStudioBlendingInterface_Post = studio_table<4>;
    return TRUE;
}
C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON) { return !reject_detach; }
