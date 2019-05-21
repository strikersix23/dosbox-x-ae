/*
 *  Copyright (C) 2002-2013  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "../dos/drives.h"
#include "control.h"
#include "cpu.h"
#include "render.h"
#include "menu.h"
#include "SDL.h"
#include "SDL_syswm.h"
#include "bios_disk.h"
#include "ide.h" // for ide support
#include "mapper.h"
#include "keyboard.h"
#include "timer.h"
#include "inout.h"

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
unsigned int min_sdldraw_menu_width = 500;
unsigned int min_sdldraw_menu_height = 300;
#endif

#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X menu handle */
void                                                sdl_hax_nsMenuAddApplicationMenu(void *nsMenu);
void*                                               sdl_hax_nsMenuItemFromTag(void *nsMenu, unsigned int tag);
void                                                sdl_hax_nsMenuItemUpdateFromItem(void *nsMenuItem, DOSBoxMenu::item &item);
void                                                sdl_hax_nsMenuItemSetTag(void *nsMenuItem, unsigned int id);
void                                                sdl_hax_nsMenuItemSetSubmenu(void *nsMenuItem,void *nsMenu);
void                                                sdl_hax_nsMenuAddItem(void *nsMenu,void *nsMenuItem);
void*                                               sdl_hax_nsMenuAllocSeparator(void);
void*                                               sdl_hax_nsMenuAlloc(const char *initWithText);
void                                                sdl_hax_nsMenuRelease(void *nsMenu);
void*                                               sdl_hax_nsMenuItemAlloc(const char *initWithText);
void                                                sdl_hax_nsMenuItemRelease(void *nsMenuItem);
#endif

#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
extern "C" void                                     SDL1_hax_SetMenu(HMENU menu);
#endif

void                                                reflectmenu_INITMENU_cb();
bool                                                GFX_GetPreventFullscreen(void);
void                                                RENDER_CallBack( GFX_CallBackFunctions_t function );
bool                                                OpenGL_using(void);

void                                                MAPPER_TriggerEventByName(const std::string name);
void                                                RENDER_CallBack( GFX_CallBackFunctions_t function );

const DOSBoxMenu::mapper_event_t                    DOSBoxMenu::unassigned_mapper_event; /* empty std::string */

static DOSBoxMenu::item_handle_t                    separator_alloc = 0;
static std::vector<DOSBoxMenu::item_handle_t>       separators;

static std::string                                  not_recommended = "Mounting C:\\ is NOT recommended.\nDo you want to continue?";

/* this is THE menu */
DOSBoxMenu                                          mainMenu;

extern const char*                                  scaler_menu_opts[][2];
extern int                                          NonUserResizeCounter;

extern bool                                         dos_kernel_disabled;
extern bool                                         dos_shell_running_program;

bool                                                GFX_GetPreventFullscreen(void);
void                                                DOSBox_ShowConsole();

void                                                GUI_ResetResize(bool pressed);

MENU_Block                                          menu;

unsigned int                                        hdd_defsize=16000;
char                                                hdd_size[20]="";

extern "C" void                                     (*SDL1_hax_INITMENU_cb)();

/* top level menu ("") */
static const char *def_menu__toplevel[] =
{
    "MainMenu",
    "CpuMenu",
    "VideoMenu",
    "SoundMenu",
    "DOSMenu",
#if !defined(C_EMSCRIPTEN)
    "CaptureMenu",
#endif
    NULL
};

/* main menu ("MainMenu") */
static const char *def_menu_main[] =
{
    "mapper_mapper",
    "mapper_gui",
    "--",
    "MainSendKey",
    "--",
#if !defined(C_EMSCRIPTEN)
    "wait_on_error",
#endif
    "showdetails",
#if C_DEBUG
    "--",
    "mapper_debugger",
#endif
#if !defined(MACOSX) && !defined(LINUX) && !defined(HX_DOS) && !defined(C_EMSCRIPTEN)
    "show_console",
#endif
    "--",
    "mapper_capmouse",
    "auto_lock_mouse",
#if !defined(C_EMSCRIPTEN)//FIXME: Reset causes problems with Emscripten
    "--",
    "mapper_pause",
    "mapper_pauseints",
#endif
#if !defined(C_EMSCRIPTEN)//FIXME: Reset causes problems with Emscripten
    "--",
    "mapper_reset",
#endif
#if !defined(C_EMSCRIPTEN)//FIXME: Shutdown causes problems with Emscripten
    "--",
#endif
#if !defined(C_EMSCRIPTEN)//FIXME: Shutdown causes problems with Emscripten
    "mapper_shutdown",
#endif
    NULL
};

/* main -> send key menu ("MenuSendKey") */
static const char *def_menu_main_sendkey[] =
{
    "sendkey_ctrlesc",
    "sendkey_alttab",
    "sendkey_winlogo",
    "sendkey_winmenu",
    "--",
    "sendkey_cad",
    NULL
};

/* cpu -> core menu ("CpuCoreMenu") */
static const char *def_menu_cpu_core[] =
{
    "mapper_cycauto",
    "--",
    "mapper_normal",
#if !defined(C_EMSCRIPTEN)//FIXME: Shutdown causes problems with Emscripten
    "mapper_full",
    "mapper_simple",
#endif
#if defined(C_DYNAMIC_X86) || defined(C_DYNREC)
    "mapper_dynamic",
#endif
    NULL
};

/* cpu -> type menu ("CpuTypeMenu") */
static const char *def_menu_cpu_type[] =
{
    "cputype_auto",
    "--",
    "cputype_8086",
    "cputype_8086_prefetch",
    "cputype_80186",
    "cputype_80186_prefetch",
    "cputype_286",
    "cputype_286_prefetch",
    "cputype_386",
    "cputype_386_prefetch",
    "cputype_486old",
    "cputype_486old_prefetch",
    "cputype_486",
    "cputype_486_prefetch",
    "cputype_pentium",
    "cputype_pentium_mmx",
    "cputype_ppro_slow",
    NULL
};

/* cpu menu ("CpuMenu") */
static const char *def_menu_cpu[] =
{
    "mapper_speedlock2", /* NTS: "mapper_speedlock" doesn't work for a menu item because it requires holding the key */
    "mapper_speednorm",
    "mapper_speedup",
    "mapper_slowdown",
    "--",
    "mapper_cycleup",
    "mapper_cycledown",
    "mapper_editcycles",
    "--",
    "CpuCoreMenu",
    "CpuTypeMenu",
    NULL
};

/* video frameskip menu ("VideoFrameskipMenu") */
static const char *def_menu_video_frameskip[] =
{
    "frameskip_0",
    "frameskip_1",
    "frameskip_2",
    "frameskip_3",
    "frameskip_4",
    "frameskip_5",
    "frameskip_6",
    "frameskip_7",
    "frameskip_8",
    "frameskip_9",
    "frameskip_10",
    NULL
};

/* video scaler menu ("VideoScalerMenu") */
static const char *def_menu_video_scaler[] =
{
    NULL
};

/* video output menu ("VideoOutputMenu") */
static const char *def_menu_video_output[] =
{
    "output_surface",
#if !defined(C_SDL2) && !defined(HX_DOS)
# if (HAVE_D3D9_H) && defined(WIN32)
    "output_direct3d",
# endif
#endif
#if defined(C_OPENGL) && !defined(HX_DOS)
    "output_opengl",
    "output_openglnb",
#endif
    NULL
};

/* video vsync menu ("VideoVsyncMenu") */
static const char *def_menu_video_vsync[] =
{
#if !defined(C_SDL2)
    "vsync_on",
    "vsync_force",
    "vsync_host",
    "vsync_off",
    "--",
    "vsync_set_syncrate",
#endif
    NULL
};

/* video overscan menu ("VideoOverscanMenu") */
static const char *def_menu_video_overscan[] =
{
    "overscan_0",
    "overscan_1",
    "overscan_2",
    "overscan_3",
    "overscan_4",
    "overscan_5",
    "overscan_6",
    "overscan_7",
    "overscan_8",
    "overscan_9",
    "overscan_10",
    NULL
};

/* video output menu ("VideoCompatMenu") */
static const char *def_menu_video_compat[] =
{
    "vga_9widetext",
    "doublescan",
    NULL
};

/* video output menu ("VideoPC98Menu") */
static const char *def_menu_video_pc98[] =
{
    "pc98_5mhz_gdc",
    "pc98_allow_200scanline",
    "pc98_allow_4partitions",
    "--",
    "pc98_enable_egc",
    "pc98_enable_grcg",
    "pc98_enable_analog",
    "pc98_enable_analog256",
    "pc98_enable_188user",
    "--",
    "pc98_clear_text",
    "pc98_clear_graphics",
    NULL
};

/* video output debug ("VideoDebugMenu") */
static const char *def_menu_video_debug[] =
{
    "mapper_blankrefreshtest",
    "--",
    "debug_pageflip",
    "debug_retracepoll",
    NULL
};

/* video menu ("VideoMenu") */
static const char *def_menu_video[] =
{
    "mapper_aspratio",
    "--",
#if !defined(HX_DOS)
    "mapper_fullscr",
    "--",
#endif
#if !defined(HX_DOS) && (defined(LINUX) || !defined(C_SDL2))
    "alwaysontop",
#endif
#if !defined(C_SDL2) && defined(MACOSX)
    "highdpienable",
#endif
#if !defined(C_SDL2)
    "doublebuf",
    "--",
#endif
#ifndef MACOSX
    "mapper_togmenu",
# if !defined(HX_DOS)
    "--",
# endif
#endif
#if !defined(HX_DOS)
    "mapper_resetsize",
#endif
    "--",
    "VideoFrameskipMenu",
    "--",
    "scaler_forced",
    "VideoScalerMenu",
    "VideoOutputMenu",
#if !defined(C_SDL2)
    "VideoVsyncMenu",
#endif
    "VideoOverscanMenu",
    "VideoCompatMenu",
    "VideoPC98Menu",
    "--",
    "VideoDebugMenu",
    NULL
};

/* DOS menu ("DOSMenu") */
static const char *def_menu_dos[] =
{
    "DOSMouseMenu",
    "--",
    "DOSPC98Menu",
    "--",
    "mapper_swapimg",
    "mapper_swapcd",
    NULL
};

/* DOS mouse menu ("DOSMouseMenu") */
static const char *def_menu_dos_mouse[] =
{
    "dos_mouse_enable_int33",
    "dos_mouse_y_axis_reverse",
    "--",
    "dos_mouse_sensitivity",
    NULL
};

/* DOS pc-98 menu ("DOSPC98Menu") */
static const char *def_menu_dos_pc98[] =
{
    "dos_pc98_pit_4mhz",
    "dos_pc98_pit_5mhz",
    NULL
};

/* sound menu ("SoundMenu") */
static const char *def_menu_sound[] =
{
    "mapper_volup",
    "mapper_voldown",
    "--",
    "mixer_mute",
    "mixer_swapstereo",
    NULL
};

/* capture menu ("CaptureMenu") */
static const char *def_menu_capture[] =
{
#if defined(C_SSHOT)
    "mapper_scrshot",
    "--",
#endif
#if !defined(C_EMSCRIPTEN)
# if (C_SSHOT)
    "CaptureFormatMenu",
    "--",
# endif
    "mapper_video",
    "mapper_recwave",
    "mapper_recmtwave",
    "mapper_caprawopl",
    "mapper_caprawmidi",
#endif
    NULL
};

#if !defined(C_EMSCRIPTEN)
# if (C_SSHOT)
/* capture format menu ("CaptureFormatMenu") */
static const char *def_menu_capture_format[] =
{
    "capture_fmt_avi_zmbv",
#  if (C_AVCODEC)
    "capture_fmt_mpegts_h264",
#  endif
    NULL
};
# endif
#endif

bool DOSBox_isMenuVisible(void) {
    return menu.toggle;
}

DOSBoxMenu::DOSBoxMenu() {
}

DOSBoxMenu::~DOSBoxMenu() {
    unbuild();
    clear_all_menu_items();
}

DOSBoxMenu::displaylist::displaylist() {
}

DOSBoxMenu::displaylist::~displaylist() {
}
        
bool DOSBoxMenu::item_exists(const std::string &name) {
    const auto i = name_map.find(name);

    if (i == name_map.end())
       return false;

    return item_exists(i->second);
}

bool DOSBoxMenu::item_exists(const item_handle_t i) {
    if (i == unassigned_item_handle)
        return false;
    else if (i >= master_list.size())
        return false;

    item &ret = master_list[(size_t)i];

    if (!ret.status.allocated || ret.master_id == unassigned_item_handle)
        return false;
    else if (ret.master_id != i)
        return false;

    return true;
}

DOSBoxMenu::item_handle_t DOSBoxMenu::get_item_id_by_name(const std::string &name) {
    const auto i = name_map.find(name);

    if (i == name_map.end())
        return unassigned_item_handle;

    return i->second;
}

DOSBoxMenu::item& DOSBoxMenu::get_item(const std::string &name) {
    const item_handle_t handle = get_item_id_by_name(name);

    if (handle == unassigned_item_handle)
        E_Exit("DOSBoxMenu::get_item() No such item '%s'",name.c_str());

    return get_item(handle);
}

DOSBoxMenu::item& DOSBoxMenu::get_item(const item_handle_t i) {
    if (i == unassigned_item_handle)
        E_Exit("DOSBoxMenu::get_item() attempt to get unassigned handle");
    else if (i >= master_list.size())
        E_Exit("DOSBoxMenu::get_item() attempt to get out of range handle");

    item &ret = master_list[(size_t)i];

    if (!ret.status.allocated || ret.master_id == unassigned_item_handle)
        E_Exit("DOSBoxMenu::get_item() attempt to read unallocated item");
    else if (ret.master_id != i)
        E_Exit("DOSBoxMenu::get_item() ID mismatch");

    return ret;
}

DOSBoxMenu::item& DOSBoxMenu::alloc_item(const enum item_type_t type,const std::string &name) {
    if (type >= MAX_id)
        E_Exit("DOSBoxMenu::alloc_item() illegal menu type value");

    if (name_map.find(name) != name_map.end())
        E_Exit("DOSBoxMenu::alloc_item() name '%s' already taken",name.c_str());

    while (master_list_alloc < master_list.size()) {
        if (!master_list[master_list_alloc].status.allocated) {
            name_map[name] = master_list_alloc;
            return master_list[master_list_alloc].allocate(master_list_alloc,type,name);
        }

        master_list_alloc++;
    }

    if (master_list_alloc >= master_list_limit)
        E_Exit("DOSBoxMenu::alloc_item() no slots are free");

    size_t newsize = master_list.size() + (master_list.size() / 2);
    if (newsize < 64) newsize = 64;
    if (newsize > master_list_limit) newsize = master_list_limit;
    master_list.resize(newsize);

    assert(master_list_alloc < master_list.size());

    name_map[name] = master_list_alloc;
    return master_list[master_list_alloc].allocate(master_list_alloc,type,name);
}

void DOSBoxMenu::delete_item(const item_handle_t i) {
    if (i == unassigned_item_handle)
        E_Exit("DOSBoxMenu::delete_item() attempt to get unassigned handle");
    else if (i >= master_list.size())
        E_Exit("DOSBoxMenu::delete_item() attempt to get out of range handle");

    {
        const auto it = name_map.find(master_list[i].name);
        if (it != name_map.end()) {
            if (it->second != i) E_Exit("DOSBoxMenu::delete_item() master_id mismatch");
            name_map.erase(it);
        }
    }

    master_list[i].deallocate();
    master_list_alloc = i;
}

const char *DOSBoxMenu::TypeToString(const enum item_type_t type) {
    switch (type) {
        case item_type_id:              return "Item";
        case submenu_type_id:           return "Submenu";
        case separator_type_id:         return "Separator";
        case vseparator_type_id:        return "VSeparator";
        default:                        break;
    };

    return "";
}

void DOSBoxMenu::dump_log_displaylist(DOSBoxMenu::displaylist &ls, unsigned int indent) {
    std::string prep;

    for (unsigned int i=0;i < indent;i++)
        prep += "+ ";

    for (const auto &id : ls.disp_list) {
        DOSBoxMenu::item &item = get_item(id);

        if (!item.is_allocated()) {
            LOG_MSG("%s (NOT ALLOCATED!!!)",prep.c_str());
            continue;
        }

        LOG_MSG("%sid=%u type=\"%s\" name=\"%s\" text=\"%s\"",
            prep.c_str(),
            (unsigned int)item.master_id,
            TypeToString(item.type),
            item.name.c_str(),
            item.text.c_str());

        if (item.type == submenu_type_id)
            dump_log_displaylist(item.display_list, indent+1);
    }
}

void DOSBoxMenu::dump_log_debug(void) {
    LOG_MSG("Menu dump log (%p)",(void*)this);
    LOG_MSG("---- Master list ----");
    for (auto &id : master_list) {
        if (id.is_allocated()) {
            LOG_MSG("+ id=%u type=\"%s\" name=\"%s\" text=\"%s\" shortcut=\"%s\" desc=\"%s\"",
                (unsigned int)id.master_id,
                TypeToString(id.type),
                id.name.c_str(),
                id.text.c_str(),
                id.shortcut_text.c_str(),
                id.description.c_str());

            if (!id.get_mapper_event().empty())
                LOG_MSG("+ + mapper_event=\"%s\"",id.get_mapper_event().c_str());
        }
    }
    LOG_MSG("---- display list ----");
    dump_log_displaylist(display_list, 1);
}

void DOSBoxMenu::clear_all_menu_items(void) {
    for (auto &id : master_list) {
        if (id.is_allocated())
            id.deallocate();
    }
    master_list_alloc = 0;
    master_list.clear();
    name_map.clear();
}

DOSBoxMenu::item::item() {
}

DOSBoxMenu::item::~item() {
}

DOSBoxMenu::item &DOSBoxMenu::item::allocate(const item_handle_t id,const enum item_type_t new_type,const std::string &new_name) {
    if (master_id != unassigned_item_handle || status.allocated)
        E_Exit("DOSBoxMenu::item::allocate() called on item already allocated");

    status.allocated = 1;
    name = new_name;
    type = new_type;
    master_id = id;
    return *this;
}

void DOSBoxMenu::item::deallocate(void) {
    if (master_id == unassigned_item_handle || !status.allocated)
        E_Exit("DOSBoxMenu::item::deallocate() called on item already deallocated");

    master_id = unassigned_item_handle;
    status.allocated = 0;
    status.changed = 1;
    shortcut_text.clear();
    description.clear();
    text.clear();
    name.clear();
}

void DOSBoxMenu::displaylist_append(displaylist &ls,const DOSBoxMenu::item_handle_t item_id) {
    DOSBoxMenu::item &item = get_item(item_id);

    if (item.status.in_use)
        E_Exit("DOSBoxMenu::displaylist_append() item already in use");

    ls.disp_list.push_back(item.master_id);
    item.status.in_use = true;
    ls.order_changed = true;
}

void DOSBoxMenu::displaylist_clear(DOSBoxMenu::displaylist &ls) {
    for (auto &id : ls.disp_list) {
        if (id != DOSBoxMenu::unassigned_item_handle) {
            id = DOSBoxMenu::unassigned_item_handle;
        }
    }

    ls.disp_list.clear();
    ls.items_changed = true;
    ls.order_changed = true;
}

void DOSBoxMenu::rebuild(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU /* Windows menu handle */
    if (winMenu == NULL) {
        if (!winMenuInit())
            return;
    }
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X menu handle */
    if (nsMenu == NULL) {
        if (!nsMenuInit())
            return;
    }
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW /* SDL drawn menus */
    layoutMenu();
#endif
}

void DOSBoxMenu::unbuild(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU /* Windows menu handle */
    winMenuDestroy();
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X menu handle */
    nsMenuDestroy();
#endif
}

#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X menu handle */
bool DOSBoxMenu::mainMenuAction(unsigned int id) {
    if (id < nsMenuMinimumID) return false;
    id -= nsMenuMinimumID;

    if (id >= master_list.size()) return false;

    item &item = master_list[id];
    if (!item.status.allocated || item.master_id == unassigned_item_handle) return false;

    dispatchItemCommand(item);
    return true;
}

void DOSBoxMenu::item::nsAppendMenu(void* parent_nsMenu) {
    if (type == separator_type_id) {
        sdl_hax_nsMenuAddItem(parent_nsMenu, sdl_hax_nsMenuAllocSeparator());
    }
    else if (type == vseparator_type_id) {
        sdl_hax_nsMenuAddItem(parent_nsMenu, sdl_hax_nsMenuAllocSeparator());
    }
    else if (type == submenu_type_id) {
        if (nsMenu != NULL) {
            // NTS: You have to make a menu ITEM who's submenu is the submenu object
            nsMenuItem = sdl_hax_nsMenuItemAlloc(text.c_str());

            sdl_hax_nsMenuItemSetTag(nsMenuItem, master_id + nsMenuMinimumID);
            sdl_hax_nsMenuItemSetSubmenu(nsMenuItem, nsMenu);
            sdl_hax_nsMenuAddItem(parent_nsMenu, nsMenuItem);
            sdl_hax_nsMenuItemUpdateFromItem(nsMenuItem, *this);
            sdl_hax_nsMenuItemRelease(nsMenuItem);
        }
    }
    else if (type == item_type_id) {
        nsMenuItem = sdl_hax_nsMenuItemAlloc(text.c_str());

        sdl_hax_nsMenuItemSetTag(nsMenuItem, master_id + nsMenuMinimumID);
        sdl_hax_nsMenuAddItem(parent_nsMenu, nsMenuItem);
        sdl_hax_nsMenuItemUpdateFromItem(nsMenuItem, *this);
        sdl_hax_nsMenuItemRelease(nsMenuItem);
    }
}

bool DOSBoxMenu::nsMenuSubInit(DOSBoxMenu::item &p_item) {
    if (p_item.nsMenu == NULL) {
        p_item.nsMenu = sdl_hax_nsMenuAlloc(p_item.get_text().c_str());
        if (p_item.nsMenu != NULL) {
            for (const auto id : p_item.display_list.disp_list) {
                DOSBoxMenu::item &item = get_item(id);

                /* if a submenu, make the submenu */
                if (item.type == submenu_type_id) {
                    item.parent_id = p_item.master_id;
                    nsMenuSubInit(item);
                }

                item.nsAppendMenu(p_item.nsMenu);
            }
        }
    }

    return true;
}

bool DOSBoxMenu::nsMenuInit(void) {
    if (nsMenu == NULL) {
        if ((nsMenu = sdl_hax_nsMenuAlloc("")) == NULL)
            return false;

        /* For whatever reason, Mac OS X will always make the first top level menu
           the Application menu and will put the name of the app there NO MATTER WHAT */
        sdl_hax_nsMenuAddApplicationMenu(nsMenu);

        /* top level */
        for (const auto id : display_list.disp_list) {
            DOSBoxMenu::item &item = get_item(id);

            /* if a submenu, make the submenu */
            if (item.type == submenu_type_id) {
                item.parent_id = unassigned_item_handle;
                nsMenuSubInit(item);
            }

            item.nsAppendMenu(nsMenu);
        }

        /* release our handle on the nsMenus. Mac OS X will keep them alive with it's
           reference until the menu is destroyed at which point all items and submenus
           will be automatically destroyed */
        for (auto &id : master_list) {
            if (id.nsMenu != NULL) {
                sdl_hax_nsMenuRelease(id.nsMenu);
                id.nsMenu = NULL;
            }
        }
    }

    return true;
}

void DOSBoxMenu::nsMenuDestroy(void) {
    if (nsMenu != NULL) {
        sdl_hax_nsMenuRelease(nsMenu);
        nsMenu = NULL;
    }
}

void* DOSBoxMenu::getNsMenu(void) const {
    return nsMenu;
}
#endif

#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU /* Windows menu handle */
std::string DOSBoxMenu::item::winConstructMenuText(void) {
    std::string r;

    /* copy text, converting '&' to '&&' for Windows.
     * TODO: Use accelerator to place '&' for underline */
    for (auto i=text.begin();i!=text.end();i++) {
        const char c = *i;

        if (c == '&') {
            r += "&&";
        }
        else {
            r += c;
        }
    }

    /* then the shortcut text */
    if (!shortcut_text.empty()) {
        r += "\t";

        for (auto i=shortcut_text.begin();i!=shortcut_text.end();i++) {
            const char c = *i;

            if (c == '&') {
                r += "&&";
            }
            else {
                r += c;
            }
        }
    }

    return r;
}

void DOSBoxMenu::item::winAppendMenu(HMENU handle) {
    if (type == separator_type_id) {
        AppendMenu(handle, MF_SEPARATOR, 0, NULL);
    }
    else if (type == vseparator_type_id) {
        AppendMenu(handle, MF_MENUBREAK, 0, NULL);
    }
    else if (type == submenu_type_id) {
        if (winMenu != NULL)
            AppendMenu(handle, MF_POPUP | MF_STRING, (uintptr_t)winMenu, winConstructMenuText().c_str());
    }
    else if (type == item_type_id) {
        unsigned int attr = MF_STRING;

        attr |= (status.checked) ? MF_CHECKED : MF_UNCHECKED;
        attr |= (status.enabled) ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);

        AppendMenu(handle, attr, (uintptr_t)(master_id + winMenuMinimumID), winConstructMenuText().c_str());
    }
}

bool DOSBoxMenu::winMenuSubInit(DOSBoxMenu::item &p_item) {
    if (p_item.winMenu == NULL) {
        p_item.winMenu = CreatePopupMenu();
        if (p_item.winMenu != NULL) {
            for (const auto id : p_item.display_list.disp_list) {
                DOSBoxMenu::item &item = get_item(id);

                /* if a submenu, make the submenu */
                if (item.type == submenu_type_id) {
                    item.parent_id = p_item.master_id;
                    winMenuSubInit(item);
                }

                item.winAppendMenu(p_item.winMenu);
            }
        }
    }

    return true;
}

bool DOSBoxMenu::winMenuInit(void) {
    if (winMenu == NULL) {
        winMenu = CreateMenu();
        if (winMenu == NULL) return false;

        /* top level */
        for (const auto id : display_list.disp_list) {
            DOSBoxMenu::item &item = get_item(id);

            /* if a submenu, make the submenu */
            if (item.type == submenu_type_id) {
                item.parent_id = unassigned_item_handle;
                winMenuSubInit(item);
            }

            item.winAppendMenu(winMenu);
        }
    }

    return true;
}

void DOSBoxMenu::winMenuDestroy(void) {
    if (winMenu != NULL) {
        /* go through all menu items, and clear the menu handle */
        for (auto &id : master_list)
            id.winMenu = NULL;

        /* destroy the menu.
         * By MSDN docs it destroys submenus automatically */
        DestroyMenu(winMenu);
        winMenu = NULL;
    }
}

HMENU DOSBoxMenu::getWinMenu(void) const {
    return winMenu;
}

/* call this from WM_COMMAND */
bool DOSBoxMenu::mainMenuWM_COMMAND(unsigned int id) {
    if (id < winMenuMinimumID) return false;
    id -= winMenuMinimumID;

    if (id >= master_list.size()) return false;

    item &item = master_list[id];
    if (!item.status.allocated || item.master_id == unassigned_item_handle) return false;

    dispatchItemCommand(item);
    return true;
}
#endif

void DOSBoxMenu::item::refresh_item(DOSBoxMenu &menu) {
    (void)menu;//POSSIBLY UNUSED
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU /* Windows menu handle */
    if (menu.winMenu != NULL && status.in_use && status.changed) {
        HMENU phandle = NULL;

        if (parent_id != unassigned_item_handle)
            phandle = menu.get_item(parent_id).winMenu;
        else
            phandle = menu.winMenu;

        if (phandle != NULL) {
            if (type == separator_type_id) {
                /* none */
            }
            else if (type == vseparator_type_id) {
                /* none */
            }
            else if (type == submenu_type_id) {
                /* TODO: Can't change by ID, have to change by position */
            }
            else if (type == item_type_id) {
                unsigned int attr = MF_STRING;

                attr |= (status.checked) ? MF_CHECKED : MF_UNCHECKED;
                attr |= (status.enabled) ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);

                ModifyMenu(phandle, (uintptr_t)(master_id + winMenuMinimumID),
                    attr | MF_BYCOMMAND, (uintptr_t)(master_id + winMenuMinimumID),
                    winConstructMenuText().c_str());
            }
        }
    }

    status.changed = false;
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X menu handle */
    if (nsMenuItem != NULL)
        sdl_hax_nsMenuItemUpdateFromItem(nsMenuItem, *this);
#endif
}

void DOSBoxMenu::dispatchItemCommand(item &item) {
    if (item.callback_func)
        item.callback_func(this,&item);

    if (item.mapper_event != unassigned_mapper_event)
        MAPPER_TriggerEventByName(item.mapper_event);
}

static std::string separator_id(const DOSBoxMenu::item_handle_t r) {
    char tmp[32];

    sprintf(tmp,"%u",(unsigned int)r);
    return std::string("_separator_") + std::string(tmp);
}

static DOSBoxMenu::item_handle_t separator_get(const DOSBoxMenu::item_type_t t=DOSBoxMenu::separator_type_id) {
    assert(separator_alloc <= separators.size());
    if (separator_alloc == separators.size()) {
        DOSBoxMenu::item &nitem = mainMenu.alloc_item(t, separator_id(separator_alloc));
        separators.push_back(nitem.get_master_id());
    }

    assert(separator_alloc < separators.size());
    mainMenu.get_item(separators[separator_alloc]).set_type(t);
    return separators[separator_alloc++];
}

void ConstructSubMenu(DOSBoxMenu::item_handle_t item_id, const char * const * list) {
    for (size_t i=0;list[i] != NULL;i++) {
        const char *ref = list[i];

        /* NTS: This code calls mainMenu.get_item(item_id) every iteration.
         *      
         *      This seemingly inefficient method of populating the display
         *      list is REQUIRED because DOSBoxMenu::item& is a reference
         *      to a std::vector, and the reference becomes invalid when
         *      the vector reallocates to accomodate more entries.
         *
         *      Holding onto one reference for the entire loop risks a
         *      segfault (use after free) bug if the vector should reallocate
         *      in separator_get() -> alloc_item()
         *
         *      Since get_item(item_id) is literally just a constant time
         *      array lookup, this is not very inefficient at all. */

        if (!strcmp(ref,"--")) {
            mainMenu.displaylist_append(
                mainMenu.get_item(item_id).display_list, separator_get(DOSBoxMenu::separator_type_id));
        }
        else if (mainMenu.item_exists(ref)) {
            mainMenu.displaylist_append(
                mainMenu.get_item(item_id).display_list, mainMenu.get_item_id_by_name(ref));
        }
    }
}

void ConstructMenu(void) {
    mainMenu.displaylist_clear(mainMenu.display_list);
    separator_alloc = 0;

    /* top level */
    for (size_t i=0;def_menu__toplevel[i] != NULL;i++)
        mainMenu.displaylist_append(
            mainMenu.display_list,
            mainMenu.get_item_id_by_name(def_menu__toplevel[i]));

    /* main menu */
    ConstructSubMenu(mainMenu.get_item("MainMenu").get_master_id(), def_menu_main);

    /* main sendkey menu */
    ConstructSubMenu(mainMenu.get_item("MainSendKey").get_master_id(), def_menu_main_sendkey);

    /* cpu menu */
    ConstructSubMenu(mainMenu.get_item("CpuMenu").get_master_id(), def_menu_cpu);

    /* cpu core menu */
    ConstructSubMenu(mainMenu.get_item("CpuCoreMenu").get_master_id(), def_menu_cpu_core);

    /* cpu type menu */
    ConstructSubMenu(mainMenu.get_item("CpuTypeMenu").get_master_id(), def_menu_cpu_type);

    /* video menu */
    ConstructSubMenu(mainMenu.get_item("VideoMenu").get_master_id(), def_menu_video);

    /* video frameskip menu */
    ConstructSubMenu(mainMenu.get_item("VideoFrameskipMenu").get_master_id(), def_menu_video_frameskip);

    /* video scaler menu */
    ConstructSubMenu(mainMenu.get_item("VideoScalerMenu").get_master_id(), def_menu_video_scaler);
    {
        size_t count=0;

        for (size_t i=0;scaler_menu_opts[i][0] != NULL;i++) {
            const std::string name = std::string("scaler_set_") + scaler_menu_opts[i][0];

            if (mainMenu.item_exists(name)) {
                mainMenu.displaylist_append(
                    mainMenu.get_item("VideoScalerMenu").display_list,
                    mainMenu.get_item_id_by_name(name));

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
                if ((count % 15) == 14) {
                    mainMenu.displaylist_append(
                        mainMenu.get_item("VideoScalerMenu").display_list,
                        separator_get(DOSBoxMenu::vseparator_type_id));
                }
#endif

                count++;
            }
        }
    }

    /* video output menu */
    ConstructSubMenu(mainMenu.get_item("VideoOutputMenu").get_master_id(), def_menu_video_output);

    /* video vsync menu */
    ConstructSubMenu(mainMenu.get_item("VideoVsyncMenu").get_master_id(), def_menu_video_vsync);

    /* video overscan menu */
    ConstructSubMenu(mainMenu.get_item("VideoOverscanMenu").get_master_id(), def_menu_video_overscan);

    /* video compat menu */
    ConstructSubMenu(mainMenu.get_item("VideoCompatMenu").get_master_id(), def_menu_video_compat);

    /* video PC-98 menu */
    ConstructSubMenu(mainMenu.get_item("VideoPC98Menu").get_master_id(), def_menu_video_pc98);

    /* video debug menu */
    ConstructSubMenu(mainMenu.get_item("VideoDebugMenu").get_master_id(), def_menu_video_debug);

    /* sound menu */
    ConstructSubMenu(mainMenu.get_item("SoundMenu").get_master_id(), def_menu_sound);

    /* DOS menu */
    ConstructSubMenu(mainMenu.get_item("DOSMenu").get_master_id(), def_menu_dos);

    /* DOS mouse menu */
    ConstructSubMenu(mainMenu.get_item("DOSMouseMenu").get_master_id(), def_menu_dos_mouse);

    /* DOS PC-98 menu */
    ConstructSubMenu(mainMenu.get_item("DOSPC98Menu").get_master_id(), def_menu_dos_pc98);

#if !defined(C_EMSCRIPTEN)
    /* capture menu */
    ConstructSubMenu(mainMenu.get_item("CaptureMenu").get_master_id(), def_menu_capture);
#endif

#if !defined(C_EMSCRIPTEN)
# if (C_SSHOT)
    /* capture format menu */
    ConstructSubMenu(mainMenu.get_item("CaptureFormatMenu").get_master_id(), def_menu_capture_format);
# endif
#endif
}

bool MENU_SetBool(std::string secname, std::string value) {
    Section_prop * sec = static_cast<Section_prop *>(control->GetSection(secname));
    if(sec) SetVal(secname, value, sec->Get_bool(value) ? "false" : "true");
    return sec->Get_bool(value);
}

// Sets the scaler 'forced' flag.
void SetScaleForced(bool forced)
{
    render.scale.forced = forced;

    Section_prop * section=static_cast<Section_prop *>(control->GetSection("render"));
    Prop_multival* prop = section->Get_multival("scaler");
    std::string scaler = prop->GetSection()->Get_string("type");

    auto value = scaler + (render.scale.forced ? " forced" : "");
    SetVal("render", "scaler", value);

    RENDER_CallBack(GFX_CallBackReset);
    mainMenu.get_item("scaler_forced").check(render.scale.forced).refresh_item(mainMenu);
}

// Sets the scaler to use.
void SetScaler(scalerOperation_t op, Bitu size, std::string prefix)
{
    auto value = prefix + (render.scale.forced ? " forced" : "");
    SetVal("render", "scaler", value);
    render.scale.size = size;
    render.scale.op = op;
    RENDER_CallBack(GFX_CallBackReset);
}

std::string MSCDEX_Output(int num) {
    std::string MSCDEX_MSG = "GUI: MSCDEX ";
    std::string MSCDEX_MSG_Failure = "Failure: ";
    switch (num) {
    case 0: return MSCDEX_MSG + "installed";
    case 1: return MSCDEX_MSG + MSCDEX_MSG_Failure + "Drive-letters of multiple CDRom-drives have to be continuous.";
    case 2: return MSCDEX_MSG + MSCDEX_MSG_Failure + "Not yet supported.";
    case 3: return MSCDEX_MSG + MSCDEX_MSG_Failure + "Path not valid.";
    case 4: return MSCDEX_MSG + MSCDEX_MSG_Failure + "Too many CDRom-drives (max: 5). MSCDEX Installation failed";
    case 5: return MSCDEX_MSG + "Mounted subdirectory: limited support.";
    case 6: return MSCDEX_MSG + MSCDEX_MSG_Failure + "Unknown error";
    default: return 0;
    }
}

void SetVal(const std::string secname, std::string preval, const std::string val) {
    if(preval=="keyboardlayout" && !dos_kernel_disabled) {
        DOS_MCB mcb(dos.psp()-1);
        static char name[9];
        mcb.GetFileName(name);
        if (strlen(name)) {
            LOG_MSG("GUI: Exit %s running in DOSBox, and then try again.",name);
            return;
        }
    }
    Section* sec = control->GetSection(secname);
    if(sec) {
        std::string real_val=preval+"="+val;
        sec->HandleInputline(real_val);
    }
}

void DOSBox_SetMenu(DOSBoxMenu &altMenu) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    /* nothing to do */
    (void)altMenu;
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* TODO: Move to menu.cpp DOSBox_SetMenu() and add setmenu(NULL) to DOSBox_NoMenu() @emendelson request showmenu=false */
    void sdl_hax_macosx_setmenu(void *nsMenu);
    void menu_osx_set_menuobj(DOSBoxMenu *altMenu);
    sdl_hax_macosx_setmenu(altMenu.getNsMenu());
    menu_osx_set_menuobj(&altMenu);
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    if(!menu.gui) return;
    if(!menu.toggle) return;

    LOG(LOG_MISC,LOG_DEBUG)("Win32: loading and attaching custom menu resource to DOSBox's window");

    NonUserResizeCounter=1;
    SDL1_hax_SetMenu(altMenu.getWinMenu());
#endif
}

void DOSBox_SetMenu(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    {
        menu.toggle=true;
        mainMenu.showMenu();
        mainMenu.setRedraw();
        GFX_ResetScreen();
    }
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* TODO: Move to menu.cpp DOSBox_SetMenu() and add setmenu(NULL) to DOSBox_NoMenu() @emendelson request showmenu=false */
    void sdl_hax_macosx_setmenu(void *nsMenu);
    sdl_hax_macosx_setmenu(mainMenu.getNsMenu());
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    if(!menu.gui) return;

    LOG(LOG_MISC,LOG_DEBUG)("Win32: loading and attaching menu resource to DOSBox's window");

    menu.toggle=true;
    NonUserResizeCounter=1;
    SDL1_hax_SetMenu(mainMenu.getWinMenu());
    mainMenu.get_item("mapper_togmenu").check(!menu.toggle).refresh_item(mainMenu);

    Reflect_Menu();

    if(menu.startup) {
        RENDER_CallBack( GFX_CallBackReset );
    }

    void DOSBox_SetSysMenu(void);
    DOSBox_SetSysMenu();
#endif
}

void DOSBox_NoMenu(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    {
        menu.toggle=false;
        mainMenu.showMenu(false);
        mainMenu.setRedraw();
        GFX_ResetScreen();
    }
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU
    void sdl_hax_macosx_setmenu(void *nsMenu);
    sdl_hax_macosx_setmenu(NULL);
#endif
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    if(!menu.gui) return;
    menu.toggle=false;
    NonUserResizeCounter=1;
    SDL1_hax_SetMenu(NULL);
    mainMenu.get_item("mapper_togmenu").check(!menu.toggle).refresh_item(mainMenu);
    RENDER_CallBack( GFX_CallBackReset );

    void DOSBox_SetSysMenu(void);
    DOSBox_SetSysMenu();
#endif
}

void ToggleMenu(bool pressed) {
    bool GFX_GetPreventFullscreen(void);

    /* prevent removing the menu in 3Dfx mode */
    if (GFX_GetPreventFullscreen())
        return;

    menu.resizeusing=true;
    int width, height; bool fullscreen;
    void GFX_GetSize(int &width, int &height, bool &fullscreen);
    GFX_GetSize(width, height, fullscreen);
    if(!menu.gui || !pressed || fullscreen) return;
    if(!menu.toggle) {
        menu.toggle=true;
        DOSBox_SetMenu();
    } else {
        menu.toggle=false;
        DOSBox_NoMenu();
    }

    void DOSBox_SetSysMenu(void);
    DOSBox_SetSysMenu();
}

#if !(defined(WIN32) && !defined(C_SDL2) && !defined(HX_DOS))
int Reflect_Menu(void) {
    return 0;
}

void DOSBox_RefreshMenu(void) {
}

void DOSBox_CheckOS(int &id, int &major, int &minor) {
    id=major=minor=0;
}
#endif

#if defined(WIN32)
# if defined(HX_DOS) || !defined(C_SDL2)
HWND GetHWND(void) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);

    if(!SDL_GetWMInfo(&wmi)) {
        return NULL;
    }
    return wmi.window;
}

HWND GetSurfaceHWND(void) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);

    if (!SDL_GetWMInfo(&wmi)) {
        return NULL;
    }
    return wmi.child_window;
}
# endif
#endif

#if defined(WIN32) && !defined(C_SDL2) && !defined(HX_DOS)
#include <shlobj.h>

void GetDefaultSize(void) {
    char sizetemp[20]="512,32,32765,";
    char sizetemp2[20]="";
    sprintf(sizetemp2,"%d",hdd_defsize);
    strcat(sizetemp,sizetemp2);
    sprintf(hdd_size,sizetemp);
}

void SearchFolder( char path[MAX_PATH], char drive, std::string drive_type ) {
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;

    hFind = FindFirstFile ( "*.*", &FindFileData );

    if ( hFind != INVALID_HANDLE_VALUE ) MountDrive_2(drive,path,drive_type);
    FindClose ( hFind );
}

void BrowseFolder( char drive , std::string drive_type ) {
#if !defined(HX_DOS)
    if (Drives[drive-'A']) {
        LOG_MSG("Unmount drive %c first, and then try again.",drive);
        return;
    }
    std::string title = "Select a drive/directory to mount";
    char path[MAX_PATH];
    BROWSEINFO bi = { 0 };
    if(drive_type=="CDROM")
        bi.lpszTitle = ( title + " CD-ROM\nMounting a directory as CD-ROM gives an limited support" ).c_str();
    else if(drive_type=="FLOPPY")
        bi.lpszTitle = ( title + " as Floppy" ).c_str();
    else if(drive_type=="LOCAL")
        bi.lpszTitle = ( title + " as Local").c_str();
    else
        bi.lpszTitle = (title.c_str());
    LPITEMIDLIST pidl = SHBrowseForFolder ( &bi );

    if ( pidl != 0 ) {
        SHGetPathFromIDList ( pidl, path );
//      SetCurrentDirectory ( path );
        SearchFolder( path , drive, drive_type );
        IMalloc * imalloc = 0;
        if ( SUCCEEDED( SHGetMalloc ( &imalloc )) ) {
            imalloc->Free ( pidl );
            imalloc->Release ( );
        }
    }
#endif
}

void mem_conf(std::string memtype, int option) {
    std::string tmp;
    Section* sec = control->GetSection("dos");
    Section_prop * section=static_cast<Section_prop *>(sec); 
    if (!option) {
        tmp = section->Get_bool(memtype) ? "false" : "true";
    } else {
        switch (option) {
            case 1: tmp = "true"; break;
            case 2: tmp = "false"; break;
            case 3: tmp = "emsboard"; break;
            case 4: tmp = "emm386"; break;
            default: return;
        }
    }
    if(sec) {
        memtype += "=" + tmp;
        sec->HandleInputline(memtype);
    }
}

void UnMount(int i_drive) {
    if (dos_kernel_disabled)
        return;

    i_drive = toupper(i_drive);
    if(i_drive-'A' == DOS_GetDefaultDrive()) {
        DOS_MCB mcb(dos.psp()-1);
        static char name[9];
        mcb.GetFileName(name);
        if (!strlen(name)) goto umount;
        LOG_MSG("GUI:Drive %c is being used. Aborted.",i_drive);
        return;
    };
umount:
    if (i_drive-'A' < DOS_DRIVES && i_drive-'A' >= 0 && Drives[i_drive-'A']) {
        switch (DriveManager::UnmountDrive(i_drive-'A')) {
        case 0:
            Drives[i_drive-'A'] = 0;
            if(i_drive-'A' == DOS_GetDefaultDrive()) 
                DOS_SetDrive(toupper('Z') - 'A');
            LOG_MSG("GUI:Drive %c has succesfully been removed.",i_drive); break;
        case 1:
            LOG_MSG("GUI:Virtual Drives can not be unMOUNTed."); break;
        case 2:
            LOG_MSG(MSCDEX_Output(1).c_str()); break;
        }
    }
}

void MountDrive_2(char drive, const char drive2[DOS_PATHLENGTH], std::string drive_type) {
    (void)drive_type;
    (void)drive2;
    (void)drive;
}

void MountDrive(char drive, const char drive2[DOS_PATHLENGTH]) {
    (void)drive2;
    (void)drive;
}

void Mount_Img_Floppy(char drive, std::string realpath) {
    (void)realpath;
    (void)drive;
}

void Mount_Img_HDD(char drive, std::string realpath) {
    (void)realpath;
    (void)drive;
}

void Mount_Img(char drive, std::string realpath) {
    (void)realpath;
    (void)drive;
}

void DOSBox_SetSysMenu(void) {
#if !defined(HX_DOS)
    MENUITEMINFO mii;
    HMENU sysmenu;

    sysmenu = GetSystemMenu(GetHWND(), TRUE); // revert, so we can reapply menu items
    sysmenu = GetSystemMenu(GetHWND(), FALSE);
    if (sysmenu == NULL) return;

    AppendMenu(sysmenu, MF_SEPARATOR, -1, "");

    {
        const char *msg = "Show menu &bar";

        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mii.fState = (menu.toggle ? MFS_CHECKED : 0) | (GFX_GetPreventFullscreen() ? MFS_DISABLED : MFS_ENABLED);
        mii.wID = ID_WIN_SYSMENU_TOGGLEMENU;
        mii.dwTypeData = (LPTSTR)(msg);
        mii.cch = (UINT)(strlen(msg)+1);

        InsertMenuItem(sysmenu, GetMenuItemCount(sysmenu), TRUE, &mii);
    }

    AppendMenu(sysmenu, MF_SEPARATOR, -1, "");

    {
        const char *msg = "&Pause";

        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mii.fState = MFS_ENABLED;
        mii.wID = ID_WIN_SYSMENU_PAUSE;
        mii.dwTypeData = (LPTSTR)(msg);
        mii.cch = (UINT)(strlen(msg) + 1);

        InsertMenuItem(sysmenu, GetMenuItemCount(sysmenu), TRUE, &mii);
    }

    AppendMenu(sysmenu, MF_SEPARATOR, -1, "");

    {
        const char *msg = "Show &mapper interface";

        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mii.fState = MFS_ENABLED;
        mii.wID = ID_WIN_SYSMENU_MAPPER;
        mii.dwTypeData = (LPTSTR)(msg);
        mii.cch = (UINT)(strlen(msg) + 1);

        InsertMenuItem(sysmenu, GetMenuItemCount(sysmenu), TRUE, &mii);
    }

    {
        const char *msg = "Show configuration &GUI";

        memset(&mii, 0, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mii.fState = MFS_ENABLED;
        mii.wID = ID_WIN_SYSMENU_CFG_GUI;
        mii.dwTypeData = (LPTSTR)(msg);
        mii.cch = (UINT)(strlen(msg) + 1);

        InsertMenuItem(sysmenu, GetMenuItemCount(sysmenu), TRUE, &mii);
    }
#endif
}

void DOSBox_CheckOS(int &id, int &major, int &minor) {
    OSVERSIONINFO osi;
    ZeroMemory(&osi, sizeof(OSVERSIONINFO));
    osi.dwOSVersionInfoSize = sizeof(osi);
    GetVersionEx(&osi);
    id=osi.dwPlatformId;
    if(id==1) { major=0; minor=0; return; }
    major=osi.dwMajorVersion;
    minor=osi.dwMinorVersion;
}

bool DOSBox_Kor(void) {
    if(menu.compatible) return false;
    char Buffer[30];
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SABBREVLANGNAME, Buffer, sizeof(Buffer));
    return (!strcmp(Buffer,"KOR") ? true : false);
}

void DOSBox_RefreshMenu(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    int width, height; bool fullscreen;
    void GFX_GetSize(int &width, int &height, bool &fullscreen);
    GFX_GetSize(width,height,fullscreen);
    void SDL_Prepare(void);
    SDL_Prepare();
    if(!menu.gui) return;

    bool GFX_GetPreventFullscreen(void);

    /* prevent removing the menu in 3Dfx mode */
    if (GFX_GetPreventFullscreen())
        return;

    if(fullscreen) {
        NonUserResizeCounter=1;
        SetMenu(GetHWND(), NULL);
        DrawMenuBar(GetHWND());
        return;
    }
    DOSBox_SetSysMenu();
    if(menu.toggle)
        DOSBox_SetMenu();
    else
        DOSBox_NoMenu();
#endif
}

void DOSBox_RefreshMenu2(void) {
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    if(!menu.gui) return;
   int width, height; bool fullscreen;
   void GFX_GetSize(int &width, int &height, bool &fullscreen);
   GFX_GetSize(width,height,fullscreen);
    void SDL_Prepare(void);
    SDL_Prepare();
    if(!menu.gui) return;

    if(fullscreen) {
        NonUserResizeCounter=1;
        return;
    }
    if(menu.toggle) {
        menu.toggle=true;
        NonUserResizeCounter=1;
        SDL1_hax_SetMenu(mainMenu.getWinMenu());
    } else {
        menu.toggle=false;
        NonUserResizeCounter=1;
        SDL1_hax_SetMenu(NULL);
    }

    void DOSBox_SetSysMenu(void);
    DOSBox_SetSysMenu();
#endif
}

void MENU_Check_Drive(HMENU handle, int cdrom, int floppy, int local, int image, int automount, int umount, char drive) {
#if !defined(HX_DOS)
    std::string full_drive(1, drive);
    Section_prop * sec = static_cast<Section_prop *>(control->GetSection("dos"));
    full_drive += ":\\";
    EnableMenuItem(handle, cdrom, (Drives[drive - 'A'] || menu.boot) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(handle, floppy, (Drives[drive - 'A'] || menu.boot) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(handle, local, (Drives[drive - 'A'] || menu.boot) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(handle, image, (Drives[drive - 'A'] || menu.boot) ? MF_GRAYED : MF_ENABLED);
    if(sec) EnableMenuItem(handle, automount, AUTOMOUNT(full_drive.c_str(), drive) && !menu.boot && sec->Get_bool("automount") ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(handle, umount, (!Drives[drive - 'A']) || menu.boot ? MF_GRAYED : MF_ENABLED);
#endif
}

void MENU_KeyDelayRate(int delay, int rate) {
    IO_Write(0x60,0xf3); IO_Write(0x60,(Bit8u)(((delay-1)<<5)|(32-rate)));
    LOG_MSG("GUI: Keyboard rate %d, delay %d", rate, delay);
}

int Reflect_Menu(void) {
#if !defined(HX_DOS)
    SDL1_hax_INITMENU_cb = reflectmenu_INITMENU_cb;
#endif
    return 1;
}

void reflectmenu_INITMENU_cb() {
    /* WARNING: SDL calls this from Parent Window Thread!
                This executes in the context of the Parent Window Thread, NOT the main thread!
                As stupid as that seems, this is the only way the Parent Window Thread can make
                sure to keep Windows waiting while we take our time to reset the checkmarks in
                the menus before the menu is displayed. */
    Reflect_Menu();
}

void MSG_WM_COMMAND_handle(SDL_SysWMmsg &Message) {
    bool GFX_GetPreventFullscreen(void);

    if (Message.msg != WM_COMMAND) return;
#if defined(WIN32) && !defined(HX_DOS)
    bool MAPPER_IsRunning(void);
    bool GUI_IsRunning(void);

    if (!MAPPER_IsRunning() && !GUI_IsRunning()) {
        if (LOWORD(Message.wParam) == ID_WIN_SYSMENU_MAPPER) {
            extern void MAPPER_Run(bool pressed);
            MAPPER_Run(false);
        }
        if (LOWORD(Message.wParam) == ID_WIN_SYSMENU_CFG_GUI) {
            extern void GUI_Run(bool pressed);
            GUI_Run(false);
        }
        if (LOWORD(Message.wParam) == ID_WIN_SYSMENU_PAUSE) {
            extern void PauseDOSBox(bool pressed);
            PauseDOSBox(true);
        }
    }
#endif
    if (!menu.gui || GetSetSDLValue(1, "desktop.fullscreen", 0)) return;
    if (!GetMenu(GetHWND())) return;
#if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
    if (mainMenu.mainMenuWM_COMMAND((unsigned int)LOWORD(Message.wParam))) return;
#endif
}
#else
void DOSBox_SetSysMenu(void) {
}
#endif

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
void DOSBoxMenu::item::showItem(DOSBoxMenu &menu,bool show) {
    (void)menu;//UNUSED
    if (itemVisible != show) {
        itemVisible = show;
        needRedraw = true;
    }
    else {
    }
}

DOSBoxMenu::item &DOSBoxMenu::item::setHilight(DOSBoxMenu &menu,bool hi) {
    (void)menu;//UNUSED
    if (itemHilight != hi) {
        itemHilight = hi;
    }

    return *this;
}

DOSBoxMenu::item &DOSBoxMenu::item::setHover(DOSBoxMenu &menu,bool ho) {
    (void)menu;//UNUSED
    if (itemHover != ho) {
        itemHover = ho;
    }

    return *this;
}

void DOSBoxMenu::item::removeFocus(DOSBoxMenu &menu) {
    if (menu.menuUserAttentionAt == master_id) {
        menu.menuUserAttentionAt = unassigned_item_handle;
        setHilight(menu,false);
    }
}

void DOSBoxMenu::item::removeHover(DOSBoxMenu &menu) {
    if (menu.menuUserHoverAt == master_id) {
        menu.menuUserHoverAt = unassigned_item_handle;
        setHover(menu,false);
    }
}

void DOSBoxMenu::showMenu(bool show) {
    if (menuVisible != show) {
        menuVisible = show;
        needRedraw = true;
        removeFocus();
        updateRect();
    }
}

void DOSBoxMenu::removeFocus(void) {
    if (menuUserAttentionAt != unassigned_item_handle) {
        for (auto &id : master_list) {
            id.removeFocus(*this);
            id.showItem(*this,false);
        }
        menuUserAttentionAt = unassigned_item_handle;
        needRedraw = true;
    }
}

void DOSBoxMenu::setScale(size_t s) {
    if (s == 0) s = 1;
    if (s > 2) s = 2;

    if (fontCharScale != s) {
        fontCharScale = s;
        menuBarHeight = menuBarHeightBase * fontCharScale;
        fontCharWidth = fontCharWidthBase * fontCharScale;
        fontCharHeight = fontCharHeightBase * fontCharScale;
        updateRect();
        layoutMenu();
    }
}

void DOSBoxMenu::removeHover(void) {
    if (menuUserHoverAt != unassigned_item_handle) {
        get_item(menuUserHoverAt).removeHover(*this);
        menuUserHoverAt = unassigned_item_handle;
        needRedraw = true;
    }
}

void DOSBoxMenu::updateRect(void) {
    menuBox.x = 0;
    menuBox.y = 0;
    menuBox.w = menuVisible ? (unsigned int)screenWidth : 0;
    menuBox.h = menuVisible ? (unsigned int)menuBarHeight : 0;
#if 0
    LOG_MSG("SDL menuBox w=%d h=%d",menuBox.w,menuBox.h);
#endif
    layoutMenu();
}

void DOSBoxMenu::layoutMenu(void) {
    int x, y;

    x = menuBox.x;
    y = menuBox.y;

    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++) {
        DOSBoxMenu::item &item = get_item(*i);

        item.placeItem(*this, x, y, /*toplevel*/true);
        x += item.screenBox.w;
    }

    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++)
        get_item(*i).placeItemFinal(*this, /*finalwidth*/x - menuBox.x, /*toplevel*/true);

    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++)
        get_item(*i).layoutSubmenu(*this, /*toplevel*/true);

#if 0
    LOG_MSG("Layout complete");
#endif
}

void DOSBoxMenu::item::layoutSubmenu(DOSBoxMenu &menu, bool isTopLevel) {
    int x, y, minx, maxx;

    x = screenBox.x;
    y = screenBox.y;

    if (isTopLevel) {
        y += textBox.h;
    }
    else {
        x += screenBox.w + 2/*popup border*/;
    }

    popupBox.x = x;
    popupBox.y = y;

    minx = x;
    maxx = x;

    auto arr_follow=display_list.disp_list.begin();
    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++) {
        DOSBoxMenu::item &item = menu.get_item(*i);

        if (item.get_type() == DOSBoxMenu::vseparator_type_id) {
            for (;arr_follow < i;arr_follow++)
                menu.get_item(*arr_follow).placeItemFinal(menu, /*finalwidth*/maxx - minx, /*toplevel*/false);

            x = maxx;

            item.screenBox.x = x;
            item.screenBox.y = popupBox.y;
            item.screenBox.w = (unsigned int)((4 * menu.fontCharScale) + 1);
            item.screenBox.h = y - popupBox.y;

            minx = maxx = x = item.screenBox.x + item.screenBox.w;
            y = popupBox.y;
        }
        else {
            item.placeItem(menu, x, y, /*toplevel*/false);
            y += item.screenBox.h;

            if (maxx < (item.screenBox.x + item.screenBox.w))
                maxx = (item.screenBox.x + item.screenBox.w);
        }
    }

    for (;arr_follow < display_list.disp_list.end();arr_follow++)
        menu.get_item(*arr_follow).placeItemFinal(menu, /*finalwidth*/maxx - minx, /*toplevel*/false);

    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++) {
        DOSBoxMenu::item &item = menu.get_item(*i);
        int my = item.screenBox.y + item.screenBox.h;
        if (y < my) y = my;
    }

    popupBox.w = maxx - popupBox.x;
    popupBox.h = y - popupBox.y;

    /* keep it on the screen if possible */
    {
        int new_y = 0;

        new_y = popupBox.y;
        if ((new_y + (int)popupBox.h) > (int)menu.screenHeight)
            new_y = (int)menu.screenHeight - popupBox.h;
        if (new_y < ((int)menu.menuBarHeight - 1))
            new_y = ((int)menu.menuBarHeight - 1);

        int adj_y = new_y - popupBox.y;
        if (adj_y != 0) {
            popupBox.y += adj_y;

            for (auto &i : display_list.disp_list) {
                DOSBoxMenu::item &item = menu.get_item(i);
                item.screenBox.y += adj_y;
            }
        }
    }
    {
        int new_x = 0;

        new_x = popupBox.x;
        if ((new_x + (int)popupBox.w) > (int)menu.screenWidth)
            new_x = (int)menu.screenWidth - popupBox.w;
        if (new_x < (int)0)
            new_x = (int)0;

        int adj_x = new_x - popupBox.x;
        if (adj_x != 0) {
            popupBox.x += adj_x;

            for (auto &i : display_list.disp_list) {
                DOSBoxMenu::item &item = menu.get_item(i);
                item.screenBox.x += adj_x;
            }
        }
    }

    /* 1 pixel border, top */
    if (!isTopLevel) {
        borderTop = true;
        popupBox.y -= 1;
        popupBox.h += 1;
    }
    else {
        borderTop = false;
    }
    /* 1 pixel border, left */
    popupBox.x -= 1;
    popupBox.w += 1;
    /* 1 pixel border, right */
    popupBox.w += 1;
    /* 1 pixel border, bottom */
    popupBox.h += 1;

    for (auto i=display_list.disp_list.begin();i!=display_list.disp_list.end();i++)
        menu.get_item(*i).layoutSubmenu(menu, /*toplevel*/false);
}

void DOSBoxMenu::item::placeItemFinal(DOSBoxMenu &menu,int finalwidth,bool isTopLevel) {
    if (type < separator_type_id) {
        int x = 0,rx = 0;

        if (!isTopLevel) {
            screenBox.w = finalwidth;
        }

        /* from the left */
        checkBox.x = x;
        x += checkBox.w;

        textBox.x = x;
        x += textBox.w;

        /* from the right */
        rx = screenBox.w;

        rx -= (int)menu.fontCharWidth;

        rx -= shortBox.w;
        shortBox.x = rx;

        if (!isTopLevel) {
            screenBox.w = finalwidth;
        }

        /* check */
        if (x > rx) LOG_MSG("placeItemFinal warning: text and shorttext overlap by %d pixels",x-rx);
    }
    else if (type == separator_type_id) {
        if (!isTopLevel) {
            screenBox.w = finalwidth;
        }
    }

#if 0
    LOG_MSG("Item id=%u name=\"%s\" placed at x,y,w,h=%d,%d,%d,%d. text:x,y,w,h=%d,%d,%d,%d",
        master_id,name.c_str(),
        screenBox.x,screenBox.y,
        screenBox.w,screenBox.h,
        textBox.x,textBox.y,
        textBox.w,textBox.h);
#endif

    boxInit = true;
}

void DOSBoxMenu::item::placeItem(DOSBoxMenu &menu,int x,int y,bool isTopLevel) {
    if (type < separator_type_id) {
        screenBox.x = x;
        screenBox.y = y;
        screenBox.w = 0;
        screenBox.h = (unsigned int)menu.fontCharHeight;

        checkBox.x = 0;
        checkBox.y = 0;
        checkBox.w = (unsigned int)menu.fontCharWidth;
        checkBox.h = (unsigned int)menu.fontCharHeight;
        screenBox.w += (unsigned int)checkBox.w;

        textBox.x = 0;
        textBox.y = 0;
        textBox.w = (unsigned int)menu.fontCharWidth * (unsigned int)text.length();
        textBox.h = (unsigned int)menu.fontCharHeight;
        screenBox.w += (unsigned int)textBox.w;

        shortBox.x = 0;
        shortBox.y = 0;
        shortBox.w = 0;
        shortBox.h = 0;
        if (!isTopLevel && !shortcut_text.empty()) {
            screenBox.w += (unsigned int)menu.fontCharWidth;
            shortBox.w += (unsigned int)menu.fontCharWidth * (unsigned int)shortcut_text.length();
            shortBox.h = (unsigned int)menu.fontCharHeight;
            screenBox.w += (unsigned int)shortBox.w;
        }

        if (!isTopLevel && type == submenu_type_id)
            screenBox.w += (unsigned int)menu.fontCharWidth;

        screenBox.w += (unsigned int)menu.fontCharWidth;
    }
    else {
        screenBox.x = x;
        screenBox.y = y;
        screenBox.w = (unsigned int)menu.fontCharWidth * 2;
        screenBox.h = (unsigned int)((4 * menu.fontCharScale) + 1);

        checkBox.x = 0;
        checkBox.y = 0;
        checkBox.w = 0;
        checkBox.h = 0;

        textBox.x = 0;
        textBox.y = 0;
        textBox.w = 0;
        textBox.h = 0;

        shortBox.x = 0;
        shortBox.y = 0;
        shortBox.w = 0;
        shortBox.h = 0;
    }
}
#endif

