// vi: set ts=4 sw=4 :
// vim: set tw=75 :

// mlist.h - class and constants to describe a list of plugins

/*
 * Copyright (c) 2001-2006 Will Day <willday@hpgx.net>
 *
 *    This file is part of Metamod.
 *
 *    Metamod is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at
 *    your option) any later version.
 *
 *    Metamod is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Metamod; if not, write to the Free Software Foundation,
 *    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    In addition, as a special exception, the author gives permission to
 *    link the code of this program with the Half-Life Game Engine ("HL
 *    Engine") and Modified Game Libraries ("MODs") developed by Valve,
 *    L.L.C ("Valve").  You must obey the GNU General Public License in all
 *    respects for all of the code used other than the HL Engine and MODs
 *    from Valve.  If you modify this file, you may extend this exception
 *    to your version of the file, but you are not obligated to do so.  If
 *    you do not wish to do so, delete this exception statement from your
 *    version.
 *
 */

#ifndef MLIST_H
#define MLIST_H

#include "types_meta.h" // mBOOL
#include "mplugin.h"    // class MPlugin
#include "plinfo.h"     // plid_t, etc
#include "new_baseclass.h"

// Max number of plugins we can manage.  This is an arbitrary, fixed number,
// for convenience.  It would probably be better to dynamically grow the
// list as needed, but we do this for now.
#define MAX_PLUGINS 256
// Width required to printf above MAX, for show() functions.
#define WIDTH_MAX_PLUGINS 2


// A list of plugins.
class MPluginList : public class_metamod_new
{
public:
    // data:
    MPlugin plist[MAX_PLUGINS]; // array of plugins
    int     size;               // size of list, ie MAX_PLUGINS
    int     endlist;            // index of last used entry
    char    inifile[PATH_MAX];  // full pathname

    // constructor:
    MPluginList(const char* ifile) DLLINTERNAL;
    // Keep the destructor on the platform ABI (also used by static test lists).
    ~MPluginList();

    // A cursor belongs to one dispatch phase. Rebuilds, including those in
    // nested hooks, cannot consume another cursor's update notification.
    class HookIterator
    {
    public:
        HookIterator(MPluginList& owner, enum_api_t api, bool post) : owner(owner), api(api), phase(post ? P_POST : P_PRE), last_plugin(NULL), generation(owner.hook_generation), cached(owner.hook_lists_valid), position(0), count(owner.hook_lists[api][phase].count), plugs(owner.hook_lists[api][phase].plugs)
        {
        }

        MPlugin* next()
        {
            if (unlikely(generation != owner.hook_generation))
                refresh();
            if (likely(cached))
            {
                if (position == count)
                    return NULL;
                last_plugin = plugs[position++];
                return last_plugin;
            }
            // Allocation failure must not silently disable hooks. Scan the
            // current slots until a later successful rebuild restores caching.
            for (MPlugin* plugin = last_plugin ? last_plugin + 1 : owner.plist;
                 plugin < owner.plist + owner.endlist; ++plugin)
            {
                if (plugin->status == PL_RUNNING &&
                    (phase == P_POST ? plugin->get_api_post_table(api) : plugin->get_api_table(api)))
                {
                    last_plugin = plugin;
                    return plugin;
                }
            }
            return NULL;
        }

    private:
        void DLLINTERNAL   refresh();
        MPluginList&       owner;
        enum_api_t         api;
        int                phase;
        MPlugin*           last_plugin;
        unsigned long long generation;
        bool               cached;
        int                position, count;
        MPlugin* const*    plugs;
    };

    void DLLINTERNAL rebuild_hook_lists();

    // functions:
    void DLLINTERNAL     reset_plugin(MPlugin* pl_find);
    MPlugin* DLLINTERNAL find(int pindex);               // find by index
    MPlugin* DLLINTERNAL find(const char* findpath);     // find by pathname
    MPlugin* DLLINTERNAL find(plid_t id);                // find by plid_t
    MPlugin* DLLINTERNAL find(DLHANDLE handle);          // find by handle
    MPlugin* DLLINTERNAL find_memloc(void* memptr);      // find by memory location
    MPlugin* DLLINTERNAL find_match(const char* prefix); // find by partial prefix match
    MPlugin* DLLINTERNAL find_match(MPlugin* pmatch);    // find by platform_match()
    MPlugin* DLLINTERNAL add(MPlugin* padd);

    mBOOL DLLINTERNAL found_child_plugins(int source_index);
    void DLLINTERNAL  clear_source_plugin_index(int source_index);
    void DLLINTERNAL  trim_list(void);

    mBOOL DLLINTERNAL    ini_startup(void);                                                 // read inifile at startup
    mBOOL DLLINTERNAL    ini_refresh(void);                                                 // re-read inifile
    mBOOL DLLINTERNAL    cmd_addload(const char* args);                                     // load from console command
    MPlugin* DLLINTERNAL plugin_addload(plid_t plid, const char* fname, PLUG_LOADTIME now); //load from plugin

    mBOOL DLLINTERNAL load(void); // load the list, at startup
    mBOOL DLLINTERNAL unload(void);
    mBOOL DLLINTERNAL refresh(PLUG_LOADTIME now);    // update from re-read inifile
    void DLLINTERNAL  unpause_all(void);             // unpause any paused plugins
    void DLLINTERNAL  retry_all(PLUG_LOADTIME now);  // retry any pending plugin actions
    void DLLINTERNAL  show(int source_index);        // list plugins to console
    void DLLINTERNAL  show(void) { show(-1); };      // list plugins to console
    void DLLINTERNAL  show_client(edict_t* pEntity); // list plugins to player client

private:
    enum
    {
        HOOK_API_COUNT   = e_api_studioapi + 1,
        HOOK_PHASE_COUNT = 2
    };
    struct HookList
    {
        int       count;
        MPlugin** plugs;
    };
    HookList           hook_lists[HOOK_API_COUNT][HOOK_PHASE_COUNT];
    MPlugin**          hook_list_data;
    unsigned long long hook_generation;
    bool               hook_lists_valid;

    MPluginList(const MPluginList&)            = delete;
    MPluginList& operator=(const MPluginList&) = delete;
};

#endif /* MLIST_H */
