// Exercise the production dispatcher and API callers, including nested calls.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <extdll.h>
#include "api_hook.h"
#include "metamod.h"
#include "mm_test_stubs.h"

#define CHECK(condition)                                            \
    do                                                              \
    {                                                               \
        if (!(condition))                                           \
        {                                                           \
            fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
            abort();                                                \
        }                                                           \
    } while (0)

static std::vector<int> calls;
static bool             recording = true;
static void (*on_call)(int);
static META_RES results[7];
static int      values[7];
static META_RES seen_status[7], seen_prev[7];
static int      seen_original[7];
static bool     typed_call;

#ifdef MMFG_TEST_WRAP_REALLOC
static bool      fail_realloc, move_realloc;
static void*     last_allocation;
static size_t    last_allocation_size;
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_realloc(void* ptr, size_t size)
{
    if (fail_realloc)
    {
        errno = ENOMEM;
        return NULL;
    }
    void* result;
    if (move_realloc)
    {
        CHECK(!ptr || ptr == last_allocation);
        result = malloc(size);
        CHECK(result);
        if (ptr) memcpy(result, ptr, size < last_allocation_size ? size : last_allocation_size);
        free(ptr);
    }
    else
        result = __real_realloc(ptr, size);
    if (result)
    {
        last_allocation      = result;
        last_allocation_size = size;
    }
    return result;
}
#endif

static int record(int id)
{
    if (recording) calls.push_back(id);
    if (id != 6)
    {
        seen_status[id] = PublicMetaGlobals.status;
        seen_prev[id]   = PublicMetaGlobals.prev_mres;
        if (typed_call && id >= 3)
            seen_original[id] = *static_cast<int*>(PublicMetaGlobals.orig_ret);
    }
    if (on_call) on_call(id);
    if (id != 6) PublicMetaGlobals.mres = results[id];
    return values[id];
}

template <int Id> static void hook_void() { record(Id); }
template <int Id> static int  hook_int() { return record(Id); }
template <int Id> static void hook_entity(edict_t*) { record(Id); }
template <int Id> static int  hook_spawn(edict_t*) { return record(Id); }
template <int Id> static int  hook_collide(edict_t*, edict_t*) { return record(Id); }
template <int Id> static void hook_bones(model_t*, float, int, const float*, const float*, const byte*, const byte*, int, const edict_t*) { record(Id); }

struct Tables
{
    enginefuncs_t           engine = {};
    DLL_FUNCTIONS           dll    = {};
    NEW_DLL_FUNCTIONS       newer  = {};
    sv_blending_interface_t studio = {};

    template <int Id> void init()
    {
        engine.pfnServerExecute       = hook_void<Id>;
        engine.pfnNumberOfEntities    = hook_int<Id>;
        dll.pfnGameInit               = hook_void<Id>;
        dll.pfnSpawn                  = hook_spawn<Id>;
        newer.pfnOnFreeEntPrivateData = hook_entity<Id>;
        newer.pfnShouldCollide        = hook_collide<Id>;
        studio.SV_StudioSetupBones    = hook_bones<Id>;
    }
    api_tables_t pointers() { return {&engine, &dll, &newer, &studio}; }
};

struct Fixture
{
    MPluginList*  list;
    Tables        pre[3], post[3], original;
    plugin_info_t info = {};
    Fixture()
    {
        mm_test_reset();
        calls.clear();
        on_call    = NULL;
        typed_call = false;
        for (int i = 0; i < 7; ++i)
        {
            results[i] = MRES_IGNORED;
            values[i]  = 10 + i;
        }
        list            = new MPluginList("plugins.ini");
        Plugins         = list;
        info.unloadable = PT_ANYPAUSE;
        pre[0].init<0>();
        pre[1].init<1>();
        pre[2].init<2>();
        post[0].init<3>();
        post[1].init<4>();
        post[2].init<5>();
        original.init<6>();
        // HL_enginefuncs_t is the engine-side wrapper around the SDK table.
        memcpy(Engine.funcs, &original.engine, sizeof(original.engine));
        GameDLL.funcs.dllapi_table     = &original.dll;
        GameDLL.funcs.newapi_table     = &original.newer;
        GameDLL.funcs.studio_blend_api = &original.studio;
        GameDLL.file                   = const_cast<char*>("test-game");
        for (int i = 0; i < 3; ++i)
        {
            MPlugin& p = list->plist[i];
            p.status   = PL_RUNNING;
            p.info     = &info;
            p.file     = p.filename;
            strcpy(p.filename, "test-plugin");
            p.tables      = pre[i].pointers();
            p.post_tables = post[i].pointers();
        }
        list->endlist = 3;
        list->rebuild_hook_lists();
    }
    ~Fixture()
    {
        delete list;
        Plugins = NULL;
    }
};

static int dispatch(enum_api_t api, bool typed)
{
    typed_call                         = typed;
    unsigned int           info_offset = 0, function_offset = 0;
    pack_args_type_void    noargs(0);
    pack_args_type_p       entity(NULL);
    pack_args_type_2p      entities(NULL, NULL);
    pack_args_type_pfi4pip bones(NULL, 0.0f, 0, NULL, NULL, NULL, NULL, 0, NULL);
    const void*            args = &noargs;
    switch (api)
    {
        case e_api_engine:
            info_offset     = typed ? offsetof(engine_info_t, pfnNumberOfEntities) : offsetof(engine_info_t, pfnServerExecute);
            function_offset = typed ? offsetof(enginefuncs_t, pfnNumberOfEntities) : offsetof(enginefuncs_t, pfnServerExecute);
            break;
        case e_api_dllapi:
            info_offset     = typed ? offsetof(dllapi_info_t, pfnSpawn) : offsetof(dllapi_info_t, pfnGameInit);
            function_offset = typed ? offsetof(DLL_FUNCTIONS, pfnSpawn) : offsetof(DLL_FUNCTIONS, pfnGameInit);
            if (typed) args = &entity;
            break;
        case e_api_newapi:
            info_offset     = typed ? offsetof(newapi_info_t, pfnShouldCollide) : offsetof(newapi_info_t, pfnOnFreeEntPrivateData);
            function_offset = typed ? offsetof(NEW_DLL_FUNCTIONS, pfnShouldCollide) : offsetof(NEW_DLL_FUNCTIONS, pfnOnFreeEntPrivateData);
            args            = typed ? static_cast<const void*>(&entities) : &entity;
            break;
        case e_api_studioapi:
            CHECK(!typed);
            info_offset     = offsetof(studioapi_info_t, SV_StudioSetupBones);
            function_offset = offsetof(sv_blending_interface_t, SV_StudioSetupBones);
            args            = &bones;
            break;
    }
    if (typed)
        return static_cast<int>(reinterpret_cast<intptr_t>(main_hook_function(class_ret_t(0), info_offset, api, function_offset, args)));
    main_hook_function_void(info_offset, api, function_offset, args);
    return 0;
}

static void expect(std::initializer_list<int> expected)
{
    CHECK(std::vector<int>(expected) == calls);
    calls.clear();
}

static void test_dispatch()
{
    for (int api = 0; api < 4; ++api)
        for (int typed = 0; typed < (api == e_api_studioapi ? 1 : 2); ++typed)
        {
            Fixture f;
            CHECK((typed ? 16 : 0) == dispatch(static_cast<enum_api_t>(api), typed != 0));
            expect({0, 1, 2, 6, 3, 4, 5});
            CHECK(f.list->plist[1].pause());
            dispatch(static_cast<enum_api_t>(api), typed != 0);
            expect({0, 2, 6, 3, 5});
            f.list->unpause_all();
            dispatch(static_cast<enum_api_t>(api), typed != 0);
            expect({0, 1, 2, 6, 3, 4, 5});
            results[0] = MRES_HANDLED;
            results[1] = MRES_SUPERCEDE;
            CHECK((typed ? 11 : 0) == dispatch(static_cast<enum_api_t>(api), typed != 0));
            expect({0, 1, 2, 3, 4, 5});
            CHECK(MRES_HANDLED == seen_prev[1]);
            CHECK(MRES_SUPERCEDE == seen_status[2]);
            if (typed)
            {
                CHECK(11 == seen_original[3]);
                results[4] = MRES_OVERRIDE;
                // A pre SUPERCEDE has higher status than a post OVERRIDE.
                CHECK(11 == dispatch(static_cast<enum_api_t>(api), true));
                calls.clear();
                results[1] = MRES_IGNORED;
                CHECK(14 == dispatch(static_cast<enum_api_t>(api), true));
                CHECK(16 == seen_original[3]);
                calls.clear();
            }
        }
}

static void test_filtering()
{
    for (int api = 0; api < 4; ++api)
    {
        Fixture          f;
        const enum_api_t group = static_cast<enum_api_t>(api);
        // Only the selected group has a pre-only and a post-only provider.
        for (int i = 0; i < 3; ++i)
        {
            f.list->plist[i].tables      = {};
            f.list->plist[i].post_tables = {};
        }
        switch (group)
        {
            case e_api_engine:
                f.list->plist[0].tables.engine      = &f.pre[0].engine;
                f.list->plist[2].post_tables.engine = &f.post[2].engine;
                break;
            case e_api_dllapi:
                f.list->plist[0].tables.dllapi      = &f.pre[0].dll;
                f.list->plist[2].post_tables.dllapi = &f.post[2].dll;
                break;
            case e_api_newapi:
                f.list->plist[0].tables.newapi      = &f.pre[0].newer;
                f.list->plist[2].post_tables.newapi = &f.post[2].newer;
                break;
            case e_api_studioapi:
                f.list->plist[0].tables.studio_blend_api      = &f.pre[0].studio;
                f.list->plist[2].post_tables.studio_blend_api = &f.post[2].studio;
                break;
        }
        f.list->rebuild_hook_lists();
        for (int other = 0; other < 4; ++other)
        {
            dispatch(static_cast<enum_api_t>(other), false);
            if (api == other) expect({0, 6, 5});
            else
                expect({6});
        }
        // Non-null tables with null function slots must still be skipped.
        f.pre[0]  = {};
        f.post[2] = {};
        dispatch(group, false);
        expect({6});
        for (PLUG_STATUS status : {PL_EMPTY, PL_VALID, PL_OPENED, PL_FAILED, PL_PAUSED})
        {
            f.pre[0].init<0>();
            f.list->plist[0].status = status;
            f.list->rebuild_hook_lists();
            dispatch(group, false);
            expect({6});
        }
    }
}

static int        trigger, scenario;
static enum_api_t active_api;
static bool       active_typed, nested;

static void mutate(int id)
{
    if (id != trigger || nested) return;
    switch (scenario)
    {
        case 0: CHECK(Plugins->plist[1].pause()); break;   // remove later
        case 1: CHECK(Plugins->plist[0].pause()); break;   // remove current
        case 2: CHECK(Plugins->plist[1].unpause()); break; // add later
        case 3: CHECK(Plugins->plist[0].pause()); break;   // remove earlier
        case 4: CHECK(Plugins->plist[0].unpause()); break; // add earlier
        case 5:                                            // Remove everything, including the storage the outer loop uses.
            for (int i = 0; i < 3; ++i) CHECK(Plugins->plist[i].pause());
            break;
        case 6: // Rebuild, then re-enter: inner dispatch must not hide the update.
        case 7: // Rebuild inside the nested callback, then resume the outer loop.
        {
            if (scenario == 6) CHECK(Plugins->plist[1].pause());
            meta_globals_t saved = PublicMetaGlobals;
            nested               = true;
            if (scenario == 7)
                on_call = [](int inner_id) {
                    if (inner_id == 0) CHECK(Plugins->plist[1].pause());
                };
            dispatch(active_api, !active_typed && active_api != e_api_studioapi);
            typed_call = active_typed;
            on_call    = mutate;
            nested     = false;
            // Original calls run with the existing dispatcher call_count policy;
            // only plugin callbacks promise restoration of these globals.
            if (trigger != 6) CHECK(memcmp(&saved, &PublicMetaGlobals, sizeof(saved)) == 0);
            break;
        }
        case 8: // Several rebuilds while all existing cursors remain suspended.
            CHECK(Plugins->plist[1].pause());
            CHECK(Plugins->plist[1].unpause());
            CHECK(Plugins->plist[1].pause());
            break;
    }
}

static void test_mutations()
{
    for (int api = 0; api < 4; ++api)
        for (int typed = 0; typed < (api == e_api_studioapi ? 1 : 2); ++typed)
            for (bool post : {false, true})
                for (scenario = 0; scenario <= 8; ++scenario)
                {
                    Fixture f;
                    active_api   = static_cast<enum_api_t>(api);
                    active_typed = typed != 0;
                    trigger      = post ? 3 : 0;
                    if (scenario == 3 || scenario == 4) ++trigger;
                    if (scenario == 2) CHECK(f.list->plist[1].pause());
                    if (scenario == 4) CHECK(f.list->plist[0].pause());
                    on_call = mutate;
#ifdef MMFG_TEST_WRAP_REALLOC
                    move_realloc = true; // Deterministically invalidate the cached address.
#endif
                    dispatch(active_api, active_typed);
#ifdef MMFG_TEST_WRAP_REALLOC
                    move_realloc = false;
#endif
                    std::vector<int> expected_pre  = {0, 1, 2};
                    std::vector<int> expected_post = {3, 4, 5};
                    if (post && scenario == 2) expected_pre = {0, 2};
                    if (scenario == 4) expected_pre = {1, 2};
                    if (!post)
                    {
                        if (scenario == 0 || scenario == 8) expected_pre = {0, 2};
                        if (scenario == 0 || scenario == 8) expected_post = {3, 5};
                        if (scenario == 1 || scenario == 3) expected_post = {4, 5};
                        if (scenario == 5)
                        {
                            expected_pre = {0};
                            expected_post.clear();
                        }
                    }
                    else
                    {
                        if (scenario == 0 || scenario == 8) expected_post = {3, 5};
                        if (scenario == 4) expected_post = {4, 5};
                        if (scenario == 5) expected_post = {3};
                    }
                    if (scenario == 6 || scenario == 7)
                    {
                        // The nested call also observes the new lists in both phases.
                        if (!post) expected_pre = {0, 0, 2, 6, 3, 5, 2};
                        if (post) expected_post = {3, 0, 2, 6, 3, 5, 5};
                        else
                            expected_post = {3, 5};
                    }
                    std::vector<int> expected = expected_pre;
                    expected.push_back(6);
                    expected.insert(expected.end(), expected_post.begin(), expected_post.end());
                    CHECK(expected == calls);
                }
    // Changes in the original function must be visible to the post phase.
    for (int s : {0, 6, 7})
    {
        Fixture f;
        active_api   = e_api_dllapi;
        active_typed = true;
        scenario     = s;
        trigger      = 6;
        on_call      = mutate;
        dispatch(active_api, active_typed);
        if (s == 0) expect({0, 1, 2, 6, 3, 5});
        else
            expect({0, 1, 2, 6, 0, 2, 6, 3, 5, 3, 5});
    }
}

static void cross_api_mutation(int id)
{
    if (id != trigger) return;
    meta_globals_t saved = PublicMetaGlobals;
    on_call              = [](int inner_id) {
        if (inner_id == 0) CHECK(Plugins->plist[1].pause());
    };
    dispatch(static_cast<enum_api_t>((active_api + 1) % 4), false);
    typed_call = active_typed;
    on_call    = cross_api_mutation;
    CHECK(memcmp(&saved, &PublicMetaGlobals, sizeof(saved)) == 0);
}

static void test_cross_api_reentry()
{
    for (int api = 0; api < 4; ++api)
        for (int typed = 0; typed < (api == e_api_studioapi ? 1 : 2); ++typed)
            for (bool post : {false, true})
            {
                Fixture f;
                active_api   = static_cast<enum_api_t>(api);
                active_typed = typed != 0;
                trigger      = post ? 3 : 0;
                on_call      = cross_api_mutation;
                dispatch(active_api, active_typed);
                if (post) expect({0, 1, 2, 6, 3, 0, 2, 6, 3, 5, 5});
                else
                    expect({0, 0, 2, 6, 3, 5, 2, 6, 3, 5});
            }
}

#ifdef MMFG_TEST_WRAP_REALLOC
static void test_allocation_failure()
{
    for (int api = 0; api < 4; ++api)
        for (bool post : {false, true})
        {
            Fixture f;
            active_api   = static_cast<enum_api_t>(api);
            active_typed = false;
            scenario     = 7;
            trigger      = post ? 3 : 0;
            on_call      = mutate;
            fail_realloc = true;
            dispatch(active_api, false); // Failure inside the nested dispatch.
            fail_realloc = false;
            if (post) expect({0, 1, 2, 6, 3, 0, 2, 6, 3, 5, 5});
            else
                expect({0, 0, 2, 6, 3, 5, 2, 6, 3, 5});
            on_call = NULL;
            dispatch(active_api, false); // Still in scan fallback; no lost hooks.
            expect({0, 2, 6, 3, 5});
            CHECK(f.list->plist[1].unpause()); // Successful rebuild recovers.
            dispatch(active_api, false);
            expect({0, 1, 2, 6, 3, 4, 5});
        }
    Fixture f;
    f.list->endlist = 0;
    f.list->rebuild_hook_lists(); // Release the previous block.
    f.list->endlist = 3;
    fail_realloc    = true;
    f.list->rebuild_hook_lists(); // First allocation also fails safely.
    fail_realloc = false;
    dispatch(e_api_dllapi, true);
    expect({0, 1, 2, 6, 3, 4, 5});
    // A cursor that started in fallback must also notice recovery mid-call.
    on_call = [](int id) {
        if (id == 0) CHECK(Plugins->plist[1].pause());
    };
    dispatch(e_api_dllapi, true);
    expect({0, 2, 6, 3, 5});
    on_call = NULL;
}
#endif

static void test_full_capacity()
{
    Fixture f;
    for (int i = 3; i < MAX_PLUGINS; ++i)
    {
        f.list->plist[i].status      = PL_RUNNING;
        f.list->plist[i].file        = f.list->plist[0].file;
        f.list->plist[i].tables      = f.pre[0].pointers();
        f.list->plist[i].post_tables = f.post[0].pointers();
    }
    f.list->endlist = MAX_PLUGINS;
    f.list->rebuild_hook_lists();
    for (int api = 0; api < 4; ++api)
    {
        dispatch(static_cast<enum_api_t>(api), false);
        CHECK(2 * MAX_PLUGINS + 1 == calls.size());
        CHECK(6 == calls[MAX_PLUGINS]);
        CHECK(0 == calls[MAX_PLUGINS - 1]);
        CHECK(3 == calls.back());
        calls.clear();
    }
}

static void benchmark()
{
    const int iterations = 1000000;
    recording            = false;
    for (int slots : {3, 32, 256})
    {
        Fixture f;
        // Three active table providers; the other slots model running plugins
        // that do not supply this API group. Also measure a dense table case.
        for (bool dense : {false, true})
        {
            if (slots == 3 && dense) continue;
            for (int i = 3; i < slots; ++i)
            {
                f.list->plist[i].status      = PL_RUNNING;
                f.list->plist[i].file        = f.list->plist[0].file;
                f.list->plist[i].tables      = dense ? f.pre[0].pointers() : api_tables_t{};
                f.list->plist[i].post_tables = dense ? f.post[0].pointers() : api_tables_t{};
            }
            f.list->endlist = slots;
            f.list->rebuild_hook_lists();
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i) dispatch(e_api_dllapi, false);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            printf("slots=%d providers=%d iterations=%d ns/dispatch=%.2f\n", slots, dense ? slots : 3, iterations, double(ns) / iterations);
        }
    }
}

typedef void (*ConfigureFixture)(int (*)(int), int, int);

struct LifecycleFixture : Fixture
{
    DLHANDLE         module;
    ConfigureFixture configure;
    LifecycleFixture()
    {
        // Keep a reference for fixture controls; production load/unload still
        // opens/closes its own reference and owns all copied function tables.
        module = DLOPEN(MMFG_TEST_HOOK_PLUGIN);
        CHECK(module);
        configure = reinterpret_cast<ConfigureFixture>(DLSYM(module, "ConfigureFixture"));
        CHECK(configure);
        configure(record, 0, 0);
        MPlugin& p    = list->plist[1];
        p.tables      = {};
        p.post_tables = {};
        p.status      = PL_VALID;
        p.info        = NULL;
        p.action      = PA_LOAD;
        strcpy(p.pathname, MMFG_TEST_HOOK_PLUGIN);
        RegCmds  = new MRegCmdList();
        RegCvars = new MRegCvarList();
        list->rebuild_hook_lists();
    }
    ~LifecycleFixture()
    {
        on_call = NULL;
        configure(record, 0, 0);
        MPlugin& p = list->plist[1];
        if (p.status >= PL_RUNNING)
        {
            p.action = PA_UNLOAD;
            CHECK(p.unload(PT_ANYTIME, PNL_COMMAND, PNL_COMMAND));
        }
        else
            CHECK(p.clear());
        CHECK(0 == DLCLOSE(module));
        delete RegCmds;
        RegCmds = NULL;
        delete RegCvars;
        RegCvars = NULL;
    }
};

static bool load_during_hook;
static void lifecycle_mutation(int id)
{
    if (id != trigger) return;
    MPlugin& p = Plugins->plist[1];
    if (load_during_hook)
    {
        p.action = PA_LOAD;
        CHECK(p.load(PT_STARTUP));
    }
    else
    {
        p.action = PA_UNLOAD;
        CHECK(p.unload(PT_ANYTIME, PNL_COMMAND, PNL_COMMAND));
    }
}

static void test_lifecycle()
{
    for (int api = 0; api < 4; ++api)
        for (int typed = 0; typed < (api == e_api_studioapi ? 1 : 2); ++typed)
            for (bool post : {false, true})
                for (bool loading : {false, true})
                {
                    LifecycleFixture f;
                    if (!loading) CHECK(f.list->plist[1].load(PT_STARTUP));
                    load_during_hook = loading;
                    trigger          = post ? 3 : 0;
                    on_call          = lifecycle_mutation;
                    dispatch(static_cast<enum_api_t>(api), typed != 0);
                    if (loading && post) expect({0, 2, 6, 3, 4, 5});
                    else if (loading)
                        expect({0, 1, 2, 6, 3, 4, 5});
                    else if (post)
                        expect({0, 1, 2, 6, 3, 5});
                    else
                        expect({0, 2, 6, 3, 5});
                    on_call = NULL;
                }
    LifecycleFixture f;
    MPlugin&         p = f.list->plist[1];
    f.configure(record, 1, 0);
    CHECK(!p.load(PT_STARTUP));
    dispatch(e_api_dllapi, false);
    expect({0, 2, 6, 3, 5});
    f.configure(record, 0, 0);
    CHECK(p.retry(PT_STARTUP, PNL_DELAYED));
    dispatch(e_api_dllapi, false);
    expect({0, 1, 2, 6, 3, 4, 5});
    f.configure(record, 0, 1);
    p.action = PA_UNLOAD;
    CHECK(!p.unload(PT_ANYTIME, PNL_COMMAND, PNL_COMMAND));
    dispatch(e_api_dllapi, false);
    expect({0, 1, 2, 6, 3, 4, 5});
    f.configure(record, 0, 0);
    p.action = PA_RELOAD;
    CHECK(p.reload(PT_STARTUP, PNL_COMMAND));
    dispatch(e_api_studioapi, false);
    expect({0, 1, 2, 6, 3, 4, 5});
    // Preserve PT_NEVER even when a forced unload/reload is requested.
    p.info->unloadable = PT_NEVER;
    for (PL_UNLOAD_REASON reason : {PNL_COMMAND, PNL_CMD_FORCED, PNL_RELOAD})
    {
        p.action = PA_UNLOAD;
        CHECK(!p.unload(PT_ANYTIME, reason, reason));
        CHECK(ME_NOTALLOWED == meta_errno);
        dispatch(e_api_studioapi, false);
        expect({0, 1, 2, 6, 3, 4, 5});
    }
    p.info->unloadable = PT_ANYPAUSE;
}

#ifdef MMFG_TEST_WRAP_REALLOC
static void test_lifecycle_allocation_failure()
{
    for (int api = 0; api < 4; ++api)
        for (bool post : {false, true})
            for (bool loading : {false, true})
            {
                LifecycleFixture f;
                if (!loading) CHECK(f.list->plist[1].load(PT_STARTUP));
                load_during_hook = loading;
                trigger          = post ? 3 : 0;
                on_call          = lifecycle_mutation;
                fail_realloc     = true;
                dispatch(static_cast<enum_api_t>(api), false);
                fail_realloc = false;
                if (loading && post) expect({0, 2, 6, 3, 4, 5});
                else if (loading)
                    expect({0, 1, 2, 6, 3, 4, 5});
                else if (post)
                    expect({0, 1, 2, 6, 3, 5});
                else
                    expect({0, 2, 6, 3, 5});
                on_call = NULL;
            }
}
#endif

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0)
    {
        benchmark();
        return 0;
    }
    test_dispatch();
    test_filtering();
    test_mutations();
    test_cross_api_reentry();
    test_lifecycle();
    test_full_capacity();
#ifdef MMFG_TEST_WRAP_REALLOC
    test_allocation_failure();
    test_lifecycle_allocation_failure();
#endif
    puts("Hook dispatch tests passed");
}
