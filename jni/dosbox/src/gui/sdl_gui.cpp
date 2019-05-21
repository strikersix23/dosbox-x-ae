/*
 *  Copyright (C) 2002-2015  The DOSBox Team
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


#include "SDL.h"
#include "menu.h"
#include "../libs/gui_tk/gui_tk.h"

#include "build_timestamp.h"

#include "dosbox.h"
#include "keyboard.h"
#include "video.h"
#include "render.h"
#include "mapper.h"
#include "setup.h"
#include "control.h"
#include "shell.h"
#include "cpu.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <functional>
#include <assert.h>

#include "SDL_syswm.h"

#ifdef DOSBOXMENU_EXTERNALLY_MANAGED
static DOSBoxMenu guiMenu;
#endif

/* helper class for command execution */
class VirtualBatch : public BatchFile {
public:
                            VirtualBatch(DOS_Shell *host, const std::string& cmds);
    bool                    ReadLine(char *line);
protected:
    std::istringstream      lines;
};

extern Bit8u                int10_font_14[256 * 14];
extern Program*             first_shell;

extern uint32_t             GFX_Rmask;
extern unsigned char        GFX_Rshift;
extern uint32_t             GFX_Gmask;
extern unsigned char        GFX_Gshift;
extern uint32_t             GFX_Bmask;
extern unsigned char        GFX_Bshift;

extern bool                 dos_kernel_disabled;
extern Bitu                 currentWindowWidth, currentWindowHeight;

extern bool                 MSG_Write(const char *);
extern void                 LoadMessageFile(const char * fname);
extern void                 GFX_SetTitle(Bit32s cycles,Bits frameskip,Bits timing,bool paused);

static int                  cursor;
static bool                 running;
static int                  saved_bpp;
static bool                 shell_idle;
static bool                 in_gui = false;
#if !defined(C_SDL2)
static int                  old_unicode;
#endif
static bool                 mousetoggle;
static bool                 shortcut=false;
static SDL_Surface*         screenshot = NULL;
static SDL_Surface*         background = NULL;
#ifdef DOSBOXMENU_EXTERNALLY_MANAGED
static bool                 gui_menu_init = true;
#endif

void                        GFX_GetSizeAndPos(int &x,int &y,int &width, int &height, bool &fullscreen);

#if defined(WIN32) && !defined(HX_DOS)
void                        WindowsTaskbarUpdatePreviewRegion(void);
void                        WindowsTaskbarResetPreviewRegion(void);
#endif

/* Prepare screen for UI */
void GUI_LoadFonts(void) {
    GUI::Font::addFont("default",new GUI::BitmapFont(int10_font_14,14,10));
}

static void getPixel(Bits x, Bits y, int &r, int &g, int &b, int shift)
{
    if (x >= (Bits)render.src.width) x = (Bits)render.src.width-1;
    if (y >= (Bits)render.src.height) x = (Bits)render.src.height-1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    Bit8u* src = (Bit8u *)&scalerSourceCache;
    Bit32u pixel;
    switch (render.scale.inMode) {
        case scalerMode8:
            pixel = *((unsigned int)x+(Bit8u*)(src+(unsigned int)y*(unsigned int)render.scale.cachePitch));
            r += (int)((unsigned int)render.pal.rgb[pixel].red >> (unsigned int)shift);
            g += (int)((unsigned int)render.pal.rgb[pixel].green >> (unsigned int)shift);
            b += (int)((unsigned int)render.pal.rgb[pixel].blue >> (unsigned int)shift);
            break;
        case scalerMode15:
            pixel = *((unsigned int)x+(Bit16u*)(src+(unsigned int)y*(unsigned int)render.scale.cachePitch));
            r += (int)((pixel >> (7u+(unsigned int)shift)) & (0xf8u >> (unsigned int)shift));
            g += (int)((pixel >> (2u+(unsigned int)shift)) & (0xf8u >> (unsigned int)shift));
            b += (int)((pixel << (3u-(unsigned int)shift)) & (0xf8u >> (unsigned int)shift));
            break;
        case scalerMode16:
            pixel = *((unsigned int)x+(Bit16u*)(src+(unsigned int)y*(unsigned int)render.scale.cachePitch));
            r += (int)((pixel >> (8u+(unsigned int)shift)) & (0xf8u >> shift));
            g += (int)((pixel >> (3u+(unsigned int)shift)) & (0xfcu >> shift));
            b += (int)((pixel << (3u-(unsigned int)shift)) & (0xf8u >> shift));
            break;
        case scalerMode32:
            pixel = *((unsigned int)x+(Bit32u*)(src+(unsigned int)y*(unsigned int)render.scale.cachePitch));
            r += (int)(((pixel & GFX_Rmask) >> (GFX_Rshift + shift)) & (0xffu >> shift));
            g += (int)(((pixel & GFX_Gmask) >> (GFX_Gshift + shift)) & (0xffu >> shift));
            b += (int)(((pixel & GFX_Bmask) >> (GFX_Bshift + shift)) & (0xffu >> shift));
            break;
    }
}

bool gui_menu_exit(DOSBoxMenu * const menu,DOSBoxMenu::item * const menuitem) {
    (void)menu;//UNUSED
    (void)menuitem;//UNUSED
    running = false;
    return true;
}

static GUI::ScreenSDL *UI_Startup(GUI::ScreenSDL *screen) {
    in_gui = true;

    GFX_EndUpdate(0);
    GFX_SetTitle(-1,-1,-1,true);
    if(!screen) { //Coming from DOSBox. Clean up the keyboard buffer.
        KEYBOARD_ClrBuffer();//Clear buffer
    }
    GFX_LosingFocus();//Release any keys pressed (buffer gets filled again). (could be in above if, but clearing the mapper input when exiting the mapper is sensible as well
    SDL_Delay(20);

    LoadMessageFile(static_cast<Section_prop*>(control->GetSection("dosbox"))->Get_string("language"));

    // Comparable to the code of intro.com, but not the same! (the code of intro.com is called from within a com file)
    shell_idle = !dos_kernel_disabled && first_shell && (DOS_PSP(dos.psp()).GetSegment() == DOS_PSP(dos.psp()).GetParent());

    int sx, sy, sw, sh;
    bool fs;
    GFX_GetSizeAndPos(sx, sy, sw, sh, fs);

    int dw,dh;
#if defined(C_SDL2)
    {
        dw = 640; dh = 480;

        SDL_Window* GFX_GetSDLWindow(void);
        SDL_Window *w = GFX_GetSDLWindow();
        SDL_GetWindowSize(w,&dw,&dh);
    }
#elif defined(C_HX_DOS)
    /* FIXME: HX DOS builds are not updating the window dimensions vars.. */
    /*        However our window is always fullscreen (maximized) */
    {
        dw = GetSystemMetrics(SM_CXSCREEN);
        dh = GetSystemMetrics(SM_CYSCREEN);
    }
#else
    void UpdateWindowDimensions(void);
    UpdateWindowDimensions();
    dw = (int)currentWindowWidth;
    dh = (int)currentWindowHeight;
#endif

    if (dw < 640) dw = 640;
    if (dh < 480) dh = 480;

    assert(sx < dw);
    assert(sy < dh);

    int sw_draw = sw,sh_draw = sh;

    if ((sx+sw_draw) > dw) sw_draw = dw-sx;
    if ((sy+sh_draw) > dh) sh_draw = dh-sy;

    assert((sx+sw_draw) <= dw);
    assert((sy+sh_draw) <= dh);

    assert(sw_draw <= sw);
    assert(sh_draw <= sh);

#if !defined(C_SDL2)
    old_unicode = SDL_EnableUNICODE(1);
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,SDL_DEFAULT_REPEAT_INTERVAL);
#endif

    if (sw_draw > 0 && sh_draw > 0) {
        screenshot = SDL_CreateRGBSurface(SDL_SWSURFACE, dw, dh, 32, GUI::Color::RedMask, GUI::Color::GreenMask, GUI::Color::BlueMask, 0);
        SDL_FillRect(screenshot,0,0);

        unsigned int rs = screenshot->format->Rshift, gs = screenshot->format->Gshift, bs = screenshot->format->Bshift;

        // create screenshot for fade effect
        for (unsigned int y = 0; (int)y < sh_draw; y++) {
            Bit32u *bg = (Bit32u*)((y+sy)*(unsigned int)screenshot->pitch + (char*)screenshot->pixels) + sx;
            for (unsigned int x = 0; (int)x < sw_draw; x++) {
                int r = 0, g = 0, b = 0;
                getPixel((int)(x*(unsigned int)render.src.width/(unsigned int)sw),
                        (int)(y*(unsigned int)render.src.height/(unsigned int)sh),
                        r, g, b, 0);
                bg[x] = ((unsigned int)r << (unsigned int)rs) |
                    ((unsigned int)g << (unsigned int)gs) |
                    ((unsigned int)b << (unsigned int)bs);
            }
        }

        background = SDL_CreateRGBSurface(SDL_SWSURFACE, dw, dh, 32, GUI::Color::RedMask, GUI::Color::GreenMask, GUI::Color::BlueMask, 0);
        SDL_FillRect(background,0,0);
        for (int y = 0; y < sh_draw; y++) {
            Bit32u *bg = (Bit32u*)((y+sy)*(unsigned int)background->pitch + (char*)background->pixels) + sx;
            for (int x = 0; x < sw_draw; x++) {
                int r = 0, g = 0, b = 0;
                getPixel(x    *(int)render.src.width/sw, y    *(int)render.src.height/sh, r, g, b, 3); 
                getPixel((x-1)*(int)render.src.width/sw, y    *(int)render.src.height/sh, r, g, b, 3); 
                getPixel(x    *(int)render.src.width/sw, (y-1)*(int)render.src.height/sh, r, g, b, 3); 
                getPixel((x-1)*(int)render.src.width/sw, (y-1)*(int)render.src.height/sh, r, g, b, 3); 
                getPixel((x+1)*(int)render.src.width/sw, y    *(int)render.src.height/sh, r, g, b, 3); 
                getPixel(x    *(int)render.src.width/sw, (y+1)*(int)render.src.height/sh, r, g, b, 3); 
                getPixel((x+1)*(int)render.src.width/sw, (y+1)*(int)render.src.height/sh, r, g, b, 3); 
                getPixel((x-1)*(int)render.src.width/sw, (y+1)*(int)render.src.height/sh, r, g, b, 3); 
                int r1 = (int)((r * 393 + g * 769 + b * 189) / 1351); // 1351 -- tweak colors 
                int g1 = (int)((r * 349 + g * 686 + b * 168) / 1503); // 1203 -- for a nice 
                int b1 = (int)((r * 272 + g * 534 + b * 131) / 2340); // 2140 -- golden hue 
                bg[x] = ((unsigned int)r1 << (unsigned int)rs) |
                    ((unsigned int)g1 << (unsigned int)gs) |
                    ((unsigned int)b1 << (unsigned int)bs); 
            }
        }
    }

    cursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);

    mousetoggle = mouselocked;
    if (mouselocked) GFX_CaptureMouse();

#if defined(C_SDL2)
    extern SDL_Window * GFX_SetSDLSurfaceWindow(Bit16u width, Bit16u height);

    void GFX_SetResizeable(bool enable);
    GFX_SetResizeable(false);

    SDL_Window* window = GFX_SetSDLSurfaceWindow(dw, dh);
    if (window == NULL) E_Exit("Could not initialize video mode for mapper: %s",SDL_GetError());
    SDL_Surface* sdlscreen = SDL_GetWindowSurface(window);
    if (sdlscreen == NULL) E_Exit("Could not initialize video mode for mapper: %s",SDL_GetError());

    if (screenshot != NULL && background != NULL) {
        // fade out
        // Jonathan C: do it FASTER!
        SDL_Event event;
        SDL_SetSurfaceBlendMode(screenshot, SDL_BLENDMODE_BLEND);
        for (int i = 0xff; i > 0; i -= 0x40) {
            SDL_SetSurfaceAlphaMod(screenshot, i); 
            SDL_BlitSurface(background, NULL, sdlscreen, NULL); 
            SDL_BlitSurface(screenshot, NULL, sdlscreen, NULL);
            SDL_Window* GFX_GetSDLWindow(void);
            SDL_UpdateWindowSurface(GFX_GetSDLWindow());
            while (SDL_PollEvent(&event)); 
            SDL_Delay(40); 
        } 
        SDL_SetSurfaceBlendMode(screenshot, SDL_BLENDMODE_NONE);
    }
#else
    SDL_Surface* sdlscreen = SDL_SetVideoMode(dw, dh, 32, SDL_SWSURFACE|(fs?SDL_FULLSCREEN:0));
    if (sdlscreen == NULL) E_Exit("Could not initialize video mode %ix%ix32 for UI: %s", dw, dh, SDL_GetError());

    if (screenshot != NULL && background != NULL) {
        // fade out
        // Jonathan C: do it FASTER!
        SDL_Event event; 
        for (int i = 0xff; i > 0; i -= 0x40) {
            SDL_SetAlpha(screenshot, SDL_SRCALPHA, i); 
            SDL_BlitSurface(background, NULL, sdlscreen, NULL); 
            SDL_BlitSurface(screenshot, NULL, sdlscreen, NULL); 
            SDL_UpdateRect(sdlscreen, 0, 0, 0, 0); 
            while (SDL_PollEvent(&event)); 
            SDL_Delay(40); 
        }
    }
#endif
 
    if (screenshot != NULL && background != NULL)
        SDL_BlitSurface(background, NULL, sdlscreen, NULL);
#if defined(C_SDL2)
    SDL_Window* GFX_GetSDLWindow(void);
    SDL_UpdateWindowSurface(GFX_GetSDLWindow());
#else   
    SDL_UpdateRect(sdlscreen, 0, 0, 0, 0);
#endif

#if defined(WIN32) && !defined(HX_DOS)
    WindowsTaskbarResetPreviewRegion();
#endif

#ifdef DOSBOXMENU_EXTERNALLY_MANAGED
    if (gui_menu_init) {
        gui_menu_init = false;

        {
            DOSBoxMenu::item &item = guiMenu.alloc_item(DOSBoxMenu::submenu_type_id,"ConfigGuiMenu");
            item.set_text("Configuration GUI");
        }

        {
            DOSBoxMenu::item &item = guiMenu.alloc_item(DOSBoxMenu::item_type_id,"ExitGUI");
            item.set_callback_function(gui_menu_exit);
            item.set_text("Exit configuration GUI");
        }

        guiMenu.displaylist_clear(guiMenu.display_list);

        guiMenu.displaylist_append(
                guiMenu.display_list,
                guiMenu.get_item_id_by_name("ConfigGuiMenu"));

        {
            guiMenu.displaylist_append(
                    guiMenu.get_item("ConfigGuiMenu").display_list, guiMenu.get_item_id_by_name("ExitGUI"));
        }
    }

    guiMenu.rebuild();
    DOSBox_SetMenu(guiMenu);
#endif

    if (screen) screen->setSurface(sdlscreen);
    else screen = new GUI::ScreenSDL(sdlscreen);

    saved_bpp = render.src.bpp;
    render.src.bpp = 0;
    running = true;

#if defined(MACOSX)
    void osx_reload_touchbar(void);
    osx_reload_touchbar();
#endif

    return screen;
}

/* Restore screen */
static void UI_Shutdown(GUI::ScreenSDL *screen) {
    SDL_Surface *sdlscreen = screen->getSurface();
    render.src.bpp = (Bitu)saved_bpp;

#ifdef DOSBOXMENU_EXTERNALLY_MANAGED
    DOSBox_SetMenu(mainMenu);
#endif

#if defined(MACOSX)
    void osx_reload_touchbar(void);
    osx_reload_touchbar();
#endif

#if defined(C_SDL2)
    if (screenshot != NULL && background != NULL) {
        // fade in
        // Jonathan C: do it FASTER!
        SDL_Event event;
        SDL_SetSurfaceBlendMode(screenshot, SDL_BLENDMODE_BLEND);
        for (unsigned int i = 0x00; i < 0xff; i += 0x60) {
            SDL_SetSurfaceAlphaMod(screenshot, i); 
            SDL_BlitSurface(background, NULL, sdlscreen, NULL); 
            SDL_BlitSurface(screenshot, NULL, sdlscreen, NULL);
            SDL_Window* GFX_GetSDLWindow(void);
            SDL_UpdateWindowSurface(GFX_GetSDLWindow());
            while (SDL_PollEvent(&event)); 
            SDL_Delay(40); 
        } 
        SDL_SetSurfaceBlendMode(screenshot, SDL_BLENDMODE_NONE);
    }

    void GFX_SetResizeable(bool enable);
    GFX_SetResizeable(true);
#else
    if (screenshot != NULL && background != NULL) {
        // fade in
        // Jonathan C: do it FASTER!
        SDL_Event event;
        for (unsigned int i = 0x00; i < 0xff; i += 0x60) {
            SDL_SetAlpha(screenshot, SDL_SRCALPHA, i);
            SDL_BlitSurface(background, NULL, sdlscreen, NULL);
            SDL_BlitSurface(screenshot, NULL, sdlscreen, NULL);
            SDL_UpdateRect(sdlscreen, 0, 0, 0, 0);
            while (SDL_PollEvent(&event)) {};
            SDL_Delay(40); 
        }
    }
#endif

    // clean up
    if (mousetoggle) GFX_CaptureMouse();
    SDL_ShowCursor(cursor);
    if (background != NULL) {
        SDL_FreeSurface(background);
        background = NULL;
    }
    if (screenshot != NULL) {
        SDL_FreeSurface(screenshot);
        screenshot = NULL;
    }
    SDL_FreeSurface(sdlscreen);
    screen->setSurface(NULL);
#ifdef WIN32
    void res_init(void);
    void change_output(int output);
    res_init();
    change_output(7);
#else
#if 1
    GFX_RestoreMode();
#else
    GFX_ResetScreen();
#endif
#endif

#if defined(WIN32) && !defined(HX_DOS)
    WindowsTaskbarUpdatePreviewRegion();
#endif

#if !defined(C_SDL2)
    SDL_EnableUNICODE(old_unicode);
    SDL_EnableKeyRepeat(0,0);
#endif
    GFX_SetTitle(-1,-1,-1,false);

    void GFX_ForceRedrawScreen(void);
    GFX_ForceRedrawScreen();

    in_gui = false;
}

bool GUI_IsRunning(void) {
    return in_gui;
}

static void UI_RunCommands(GUI::ScreenSDL *s, const std::string &cmds) {
    DOS_Shell temp;
    temp.call = true;
    UI_Shutdown(s);
    Bit16u n=1; Bit8u c='\n';
    DOS_WriteFile(STDOUT,&c,&n);
    temp.bf = new VirtualBatch(&temp, cmds);
    temp.RunInternal();
    temp.ShowPrompt();
    UI_Startup(s);
}

VirtualBatch::VirtualBatch(DOS_Shell *host, const std::string& cmds) : BatchFile(host, "CON", "", ""), lines(cmds) {
}

bool VirtualBatch::ReadLine(char *line) {
    std::string l;

    if (!std::getline(lines,l)) {
        delete this;
        return false;
    }

    strcpy(line,l.c_str());
    return true;
}

/* stringification and conversion from the c++ FAQ */
class BadConversion : public std::runtime_error {
public: BadConversion(const std::string& s) : std::runtime_error(s) { }
};

template<typename T> inline std::string stringify(const T& x, std::ios_base& ( *pf )(std::ios_base&) = NULL) {
    std::ostringstream o;
    if (pf) o << pf;
    if (!(o << x)) throw BadConversion(std::string("stringify(") + typeid(x).name() + ")");
    return o.str();
}

template<typename T> inline void convert(const std::string& s, T& x, bool failIfLeftoverChars = true, std::ios_base& ( *pf )(std::ios_base&) = NULL) {
    std::istringstream i(s);
    if (pf) i >> pf;
    char c;
    if (!(i >> x) || (failIfLeftoverChars && i.get(c))) throw BadConversion(s);
}

/*****************************************************************************************************************************************/
/* UI classes */

class PropertyEditor : public GUI::Window, public GUI::ActionEventSource_Callback {
protected:
    Section_prop * section;
    Property *prop;
public:
    PropertyEditor(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        Window(parent, x, y, 240, 30), section(section), prop(prop) { }

    virtual bool prepare(std::string &buffer) = 0;

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        // HACK: Attempting to cast a String to void causes "forming reference to void" errors when building with GCC 4.7
        (void)arg.size();//UNUSED
        std::string line;
        if (prepare(line)) {
            prop->SetValue(GUI::String(line));
        }
    }
};

class PropertyEditorBool : public PropertyEditor {
    GUI::Checkbox *input;
public:
    PropertyEditorBool(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        PropertyEditor(parent, x, y, section, prop) {
        input = new GUI::Checkbox(this, 0, 3, prop->propname.c_str());
        input->setChecked(static_cast<bool>(prop->GetValue()));
    }

    bool prepare(std::string &buffer) {
        if (input->isChecked() == static_cast<bool>(prop->GetValue())) return false;
        buffer.append(input->isChecked()?"true":"false");
        return true;
    }
};

class PropertyEditorString : public PropertyEditor {
protected:
    GUI::Input *input;
public:
    PropertyEditorString(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        PropertyEditor(parent, x, y, section, prop) {
        new GUI::Label(this, 0, 5, prop->propname);
        input = new GUI::Input(this, 130, 0, 110);
        std::string temps = prop->GetValue().ToString();
        input->setText(stringify(temps));
    }

    bool prepare(std::string &buffer) {
        std::string temps = prop->GetValue().ToString();
        if (input->getText() == GUI::String(temps)) return false;
        buffer.append(static_cast<const std::string&>(input->getText()));
        return true;
    }
};

class PropertyEditorFloat : public PropertyEditor {
protected:
    GUI::Input *input;
public:
    PropertyEditorFloat(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        PropertyEditor(parent, x, y, section, prop) {
        new GUI::Label(this, 0, 5, prop->propname);
        input = new GUI::Input(this, 130, 0, 50);
        input->setText(stringify((double)prop->GetValue()));
    }

    bool prepare(std::string &buffer) {
        double val;
        convert(input->getText(), val, false);
        if (val == (double)prop->GetValue()) return false;
        buffer.append(stringify(val));
        return true;
    }
};

class PropertyEditorHex : public PropertyEditor {
protected:
    GUI::Input *input;
public:
    PropertyEditorHex(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        PropertyEditor(parent, x, y, section, prop) {
        new GUI::Label(this, 0, 5, prop->propname);
        input = new GUI::Input(this, 130, 0, 50);
        std::string temps = prop->GetValue().ToString();
        input->setText(temps.c_str());
    }

    bool prepare(std::string &buffer) {
        int val;
        convert(input->getText(), val, false, std::hex);
        if ((Hex)val ==  prop->GetValue()) return false;
        buffer.append(stringify(val, std::hex));
        return true;
    }
};

class PropertyEditorInt : public PropertyEditor {
protected:
    GUI::Input *input;
public:
    PropertyEditorInt(Window *parent, int x, int y, Section_prop *section, Property *prop) :
        PropertyEditor(parent, x, y, section, prop) {
        new GUI::Label(this, 0, 5, prop->propname);
        input = new GUI::Input(this, 130, 0, 50);
        //Maybe use ToString() of Value
        input->setText(stringify(static_cast<int>(prop->GetValue())));
    };

    bool prepare(std::string &buffer) {
        int val;
        convert(input->getText(), val, false);
        if (val == static_cast<int>(prop->GetValue())) return false;
        buffer.append(stringify(val));
        return true;
    };
};

static std::map< std::vector<GUI::Char>, GUI::ToplevelWindow* > cfg_windows_active;

class HelpWindow : public GUI::MessageBox2 {
public:
    std::vector<GUI::Char> cfg_sname;
public:
    HelpWindow(GUI::Screen *parent, int x, int y, Section *section) :
        MessageBox2(parent, x, y, 580, "", "") { // 740
        if (section == NULL) {
            LOG_MSG("BUG: HelpWindow constructor called with section == NULL\n");
            return;
        }

        std::string title(section->GetName());
        title.at(0) = std::toupper(title.at(0));
        setTitle("Help for "+title);

        Section_prop* sec = dynamic_cast<Section_prop*>(section);
        if (sec) {
            std::string msg;
            Property *p;
            int i = 0;
            while ((p = sec->Get_prop(i++))) {
                msg += std::string("\033[34m")+p->propname+":\033[0m "+p->Get_help()+"\n";
            }
            if (!msg.empty()) msg.replace(msg.end()-1,msg.end(),"");
            setText(msg);
        } else {
        std::string name = section->GetName();
        std::transform(name.begin(), name.end(), name.begin(), (int(*)(int))std::toupper);
        name += "_CONFIGFILE_HELP";
        setText(MSG_Get(name.c_str()));
        }
    };

    ~HelpWindow() {
        if (!cfg_sname.empty()) {
            auto i = cfg_windows_active.find(cfg_sname);
            if (i != cfg_windows_active.end()) cfg_windows_active.erase(i);
        }
    }
};

class SectionEditor : public GUI::ToplevelWindow {
    Section_prop * section;
    GUI::Button * closeButton;
public:
    std::vector<GUI::Char> cfg_sname;
public:
    SectionEditor(GUI::Screen *parent, int x, int y, Section_prop *section) :
        ToplevelWindow(parent, x, y, 510, 442, ""), section(section) {
        if (section == NULL) {
            LOG_MSG("BUG: SectionEditor constructor called with section == NULL\n");
            return;
        }

        int first_row_y = 40;
        int row_height = 30;
        int first_column_x = 5;
        int column_width = 250;
        int button_row_h = 26;
        int button_row_padding_y = 5 + 5;
        bool showSettingsText = true;

        if (parent->getHeight() < 550) {
            first_row_y = 5;
            row_height = 25;
            showSettingsText = false;
            button_row_padding_y = 3 + 3;
        }

        int num_prop = 0;
        while (section->Get_prop(num_prop) != NULL) num_prop++;

        int allowed_dialog_y = parent->getHeight() - 25 - (border_top + border_bottom);

        int items_per_col_max =
            (allowed_dialog_y - (button_row_h + button_row_padding_y + row_height - 1)) / row_height;
        if (items_per_col_max < 4) items_per_col_max = 4;
        int items_per_col = 1;
        int columns = 1;

        /* HACK: The titlebar doesn't look very good if the dialog is one column wide
         *       and the text spills over the nearby UI elements... */
        if ((strlen(section->GetName())+18) > 26)
            columns++;

        /* NTS: Notice assign from compute then compare */
        while ((items_per_col=((num_prop+columns-1)/columns)) > items_per_col_max)
            columns++;

        int button_row_y = first_row_y + (items_per_col * row_height);
        int button_w = 70;
        int button_pad_w = 10;
        int button_row_w = ((button_pad_w + button_w) * 3) - button_pad_w;
        int button_row_cx = first_column_x + (((columns * column_width) + first_column_x - button_row_w) / 2);

        resize(first_column_x + (columns * column_width) + first_column_x + border_left + border_right,
               button_row_y + button_row_h + button_row_padding_y + border_top + border_bottom);

        if ((this->y + this->getHeight()) > parent->getHeight())
            move(this->x,parent->getHeight() - this->getHeight());

        std::string title(section->GetName());
        title[0] = std::toupper(title[0]);
        setTitle("Configuration for "+title);

        if (showSettingsText)
            new GUI::Label(this, 5, 10, "Settings:");

        GUI::Button *b = new GUI::Button(this, button_row_cx, button_row_y, "Cancel", button_w);
        b->addActionHandler(this);
        closeButton = b;

        b = new GUI::Button(this, button_row_cx + (button_w + button_pad_w), button_row_y, "Help", button_w);
        b->addActionHandler(this);

        b = new GUI::Button(this, button_row_cx + (button_w + button_pad_w)*2, button_row_y, "OK", button_w);

        int i = 0;
        Property *prop;
        while ((prop = section->Get_prop(i))) {
            Prop_bool   *pbool   = dynamic_cast<Prop_bool*>(prop);
            Prop_int    *pint    = dynamic_cast<Prop_int*>(prop);
            Prop_double  *pdouble  = dynamic_cast<Prop_double*>(prop);
            Prop_hex    *phex    = dynamic_cast<Prop_hex*>(prop);
            Prop_string *pstring = dynamic_cast<Prop_string*>(prop);
            Prop_multival* pmulti = dynamic_cast<Prop_multival*>(prop);
            Prop_multival_remain* pmulti_remain = dynamic_cast<Prop_multival_remain*>(prop);

            PropertyEditor *p;
            if (pbool) p = new PropertyEditorBool(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (phex) p = new PropertyEditorHex(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (pint) p = new PropertyEditorInt(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (pdouble) p = new PropertyEditorFloat(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (pstring) p = new PropertyEditorString(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (pmulti) p = new PropertyEditorString(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else if (pmulti_remain) p = new PropertyEditorString(this, first_column_x+column_width*(i/items_per_col), first_row_y+(i%items_per_col)*row_height, section, prop);
            else { i++; continue; }
            b->addActionHandler(p);
            i++;
        }
        b->addActionHandler(this);
    }

    ~SectionEditor() {
        if (!cfg_sname.empty()) {
            auto i = cfg_windows_active.find(cfg_sname);
            if (i != cfg_windows_active.end()) cfg_windows_active.erase(i);
        }
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        if (arg == "OK" || arg == "Cancel" || arg == "Close") { close(); if(shortcut) running=false; }
        else if (arg == "Help") {
            std::vector<GUI::Char> new_cfg_sname;

            if (!cfg_sname.empty()) {
//              new_cfg_sname = "help_";
                new_cfg_sname.resize(5);
                new_cfg_sname[0] = 'h';
                new_cfg_sname[1] = 'e';
                new_cfg_sname[2] = 'l';
                new_cfg_sname[3] = 'p';
                new_cfg_sname[4] = '_';
                new_cfg_sname.insert(new_cfg_sname.end(),cfg_sname.begin(),cfg_sname.end());
            }

            auto lookup = cfg_windows_active.find(new_cfg_sname);
            if (lookup == cfg_windows_active.end()) {
                auto *np = new HelpWindow(static_cast<GUI::Screen*>(parent), getX()-10, getY()-10, section);
                cfg_windows_active[new_cfg_sname] = np;
                np->cfg_sname = new_cfg_sname;
                np->raise();
            }
            else {
                lookup->second->raise();
            }
        }
        else
            ToplevelWindow::actionExecuted(b, arg);
    }

    virtual bool keyDown(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyDown(key)) return true;
        return false;
    }

    virtual bool keyUp(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyUp(key)) return true;

        if (key.special == GUI::Key::Escape) {
            closeButton->executeAction();
            return true;
        }

        return false;
    }

};

class AutoexecEditor : public GUI::ToplevelWindow {
    GUI::Button *closeButton;
    Section_line * section;
    GUI::Input *content;
public:
    std::vector<GUI::Char> cfg_sname;
public:
    AutoexecEditor(GUI::Screen *parent, int x, int y, Section_line *section) :
        ToplevelWindow(parent, x, y, 450, 300, ""), section(section) {
        if (section == NULL) {
            LOG_MSG("BUG: AutoexecEditor constructor called with section == NULL\n");
            return;
        }

        std::string title(section->GetName());
        title[0] = std::toupper(title[0]);
        setTitle("Edit "+title);
        new GUI::Label(this, 5, 10, "Content:");
        content = new GUI::Input(this, 5, 30, 420, 185);
        content->setText(section->data);
        if (first_shell) (new GUI::Button(this, 5, 220, "Append History"))->addActionHandler(this);
        if (shell_idle) (new GUI::Button(this, 180, 220, "Execute Now"))->addActionHandler(this);
        (closeButton = new GUI::Button(this, 290, 220, "Cancel", 70))->addActionHandler(this);
        (new GUI::Button(this, 360, 220, "OK", 70))->addActionHandler(this);
    }

    ~AutoexecEditor() {
        if (!cfg_sname.empty()) {
            auto i = cfg_windows_active.find(cfg_sname);
            if (i != cfg_windows_active.end()) cfg_windows_active.erase(i);
        }
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        if (arg == "OK") section->data = *(std::string*)content->getText();
        if (arg == "OK" || arg == "Cancel" || arg == "Close") { close(); if(shortcut) running=false; }
        else if (arg == "Append History") {
            DOS_Shell *s = static_cast<DOS_Shell *>(first_shell);
            std::list<std::string>::reverse_iterator i = s->l_history.rbegin();
            std::string lines = *(std::string*)content->getText();
            while (i != s->l_history.rend()) {
                lines += "\n";
                lines += *i;
                ++i;
            }
            content->setText(lines);
        } else if (arg == "Execute Now") {
            UI_RunCommands(dynamic_cast<GUI::ScreenSDL*>(getScreen()), content->getText());
        } else ToplevelWindow::actionExecuted(b, arg);
    }

    virtual bool keyDown(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyDown(key)) return true;
        return false;
    }

    virtual bool keyUp(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyUp(key)) return true;

        if (key.special == GUI::Key::Escape) {
            closeButton->executeAction();
            return true;
        }

        return false;
    }

};

class SaveDialog : public GUI::ToplevelWindow {
protected:
    GUI::Input *name;
public:
    SaveDialog(GUI::Screen *parent, int x, int y, const char *title) :
        ToplevelWindow(parent, x, y, 400, 150, title) {
        new GUI::Label(this, 5, 10, "Enter filename for configuration file:");
        name = new GUI::Input(this, 5, 30, 350);
        extern std::string capturedir;
        std::string fullpath,file;
        Cross::GetPlatformConfigName(file);
        const size_t last_slash_idx = capturedir.find_last_of("\\/");
        if (std::string::npos != last_slash_idx) {
            fullpath = capturedir.substr(0, last_slash_idx);
            fullpath += CROSS_FILESPLIT;
            fullpath += file;
        } else
            fullpath = "dosbox.conf";
        name->setText(fullpath.c_str());
        (new GUI::Button(this, 120, 70, "Cancel", 70))->addActionHandler(this);
        (new GUI::Button(this, 210, 70, "OK", 70))->addActionHandler(this);
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        // HACK: Attempting to cast a String to void causes "forming reference to void" errors when building with GCC 4.7
        (void)arg.size();//UNUSED
        if (arg == "OK") control->PrintConfig(name->getText());
        close();
        if(shortcut) running=false;
    }
};

class SaveLangDialog : public GUI::ToplevelWindow {
protected:
    GUI::Input *name;
public:
    SaveLangDialog(GUI::Screen *parent, int x, int y, const char *title) :
        ToplevelWindow(parent, x, y, 400, 150, title) {
        new GUI::Label(this, 5, 10, "Enter filename for language file:");
        name = new GUI::Input(this, 5, 30, 350);
        name->setText("messages.txt");
        (new GUI::Button(this, 120, 70, "Cancel", 70))->addActionHandler(this);
        (new GUI::Button(this, 210, 70, "OK", 70))->addActionHandler(this);
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        if (arg == "OK") MSG_Write(name->getText());
        close();
        if(shortcut) running=false;
    }
};

// override Input field with one that responds to the Enter key as a keyboard-based cue to click "OK"
class InputWithEnterKey : public GUI::Input {
public:
                                        InputWithEnterKey(Window *parent, int x, int y, int w, int h = 0) : GUI::Input(parent,x,y,w,h) { };
public:
    void                                set_trigger_target(GUI::ToplevelWindow *_who) { trigger_who = _who; };
protected:
    GUI::ToplevelWindow*                trigger_who = NULL;
public:
    std::string                         trigger_enter = "OK";
    std::string                         trigger_esc = "Cancel";
public:
    virtual bool                        keyDown(const GUI::Key &key) {
        if (key.special == GUI::Key::Special::Enter) {
            if (trigger_who != NULL && !trigger_enter.empty())
                trigger_who->actionExecuted(this, trigger_enter);

            return true;
        }
        else if (key.special == GUI::Key::Special::Escape) {
            if (trigger_who != NULL && !trigger_esc.empty())
                trigger_who->actionExecuted(this, trigger_esc);

            return true;
        }
        else {
            return GUI::Input::keyDown(key);
        }
    }
};

class SetCycles : public GUI::ToplevelWindow {
protected:
    InputWithEnterKey *name;
public:
    SetCycles(GUI::Screen *parent, int x, int y, const char *title) :
        ToplevelWindow(parent, x, y, 400, 150, title) {
        new GUI::Label(this, 5, 10, "Enter CPU cycles:");
//      name = new GUI::Input(this, 5, 30, 350);
        name = new InputWithEnterKey(this, 5, 30, 350);
        name->set_trigger_target(this);
        std::ostringstream str;
        str << "fixed " << CPU_CycleMax;

        std::string cycles=str.str();
        name->setText(cycles.c_str());
        (new GUI::Button(this, 120, 70, "Cancel", 70))->addActionHandler(this);
        (new GUI::Button(this, 210, 70, "OK", 70))->addActionHandler(this);

        name->raise(); /* make sure keyboard focus is on the text field, ready for the user */
        name->posToEnd(); /* position the cursor at the end where the user is most likely going to edit */
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        if (arg == "OK") {
            Section* sec = control->GetSection("cpu");
            if (sec) {
                std::string tmp("cycles=");
                tmp.append((const char*)(name->getText()));
                sec->HandleInputline(tmp);
            }
        }
        close();
        if(shortcut) running=false;
    }
};

class SetVsyncrate : public GUI::ToplevelWindow {
protected:
    GUI::Input *name;
public:
    SetVsyncrate(GUI::Screen *parent, int x, int y, const char *title) :
        ToplevelWindow(parent, x, y, 400, 150, title) {
        new GUI::Label(this, 5, 10, "Enter vertical syncrate (Hz):");
        name = new GUI::Input(this, 5, 30, 350);
        Section_prop * sec = static_cast<Section_prop *>(control->GetSection("vsync"));
        if (sec)
            name->setText(sec->Get_string("vsyncrate"));
        else
            name->setText("");
        (new GUI::Button(this, 120, 70, "Cancel", 70))->addActionHandler(this);
        (new GUI::Button(this, 210, 70, "OK", 70))->addActionHandler(this);
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        Section_prop * sec = static_cast<Section_prop *>(control->GetSection("vsync"));
        if (arg == "OK") {
            if (sec) {
                const char* well = name->getText();
                std::string s(well, 20);
                std::string tmp("vsyncrate=");
                tmp.append(s);
                sec->HandleInputline(tmp);
                delete well;
            }
        }
        LOG_MSG("GUI: Current Vertical Sync Rate: %s Hz", sec->Get_string("vsyncrate"));
        close();
        if(shortcut) running=false;
    }
};

class SetLocalSize : public GUI::ToplevelWindow {
protected:
    GUI::Input *name;
public:
    SetLocalSize(GUI::Screen *parent, int x, int y, const char *title) :
        ToplevelWindow(parent, x, y, 450, 150, title) {
            new GUI::Label(this, 5, 10, "Enter default local freesize (MB, min=0, max=1024):");
            name = new GUI::Input(this, 5, 30, 400);
            extern unsigned int hdd_defsize;
            unsigned int human_readable = 512u * 32u * hdd_defsize / 1024u / 1024u;
            char buffer[6];
            sprintf(buffer, "%u", human_readable);
            name->setText(buffer);
            (new GUI::Button(this, 120, 70, "Cancel", 70))->addActionHandler(this);
            (new GUI::Button(this, 210, 70, "OK", 70))->addActionHandler(this);
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        (void)b;//UNUSED
        if (arg == "OK") {
            extern unsigned int hdd_defsize;
            int human_readable = atoi(name->getText());
            if (human_readable < 0)
                hdd_defsize = 0u;
            else if (human_readable > 1024)
                hdd_defsize = 256000u;
            else
                hdd_defsize = (unsigned int)human_readable * 1024u * 1024u / 512u / 32u;
            LOG_MSG("GUI: Current default freesize for local disk: %dMB", 512u * 32u * hdd_defsize / 1024u / 1024u);
        }
        close();
        if (shortcut) running = false;
    }
};

class ConfigurationWindow : public GUI::ToplevelWindow {
public:
    GUI::Button *closeButton;
    ConfigurationWindow(GUI::Screen *parent, GUI::Size x, GUI::Size y, GUI::String title) :
        GUI::ToplevelWindow(parent, (int)x, (int)y, 30/*initial*/, 30/*initial*/, title) {
        cfg_windows_active.clear();

        GUI::Menubar *bar = new GUI::Menubar(this, 0, 0, getWidth()/*initial*/);
        bar->addMenu("Configuration");
        bar->addItem(0,"Save...");
        bar->addItem(0,"Save Language File...");
        bar->addItem(0,"");
        bar->addItem(0,"Close");
        bar->addMenu("Settings");
        bar->addMenu("Help");
        bar->addItem(2,"Introduction");
        bar->addItem(2,"Getting Started");
        bar->addItem(2,"CD-ROM Support");
        bar->addItem(2,"");
        bar->addItem(2,"About");
        bar->addActionHandler(this);

        new GUI::Label(this, 10, 30, "Choose a settings group to configure:");

        Section *sec;
        int gridbtnwidth = 130;
        int gridbtnheight = 28;
        int gridbtnx = 12;
        int gridbtny = 50;
        int btnperrow = 4;
        int i = 0;

        std::function< std::pair<int,int>(const int) > gridfunc = [&/*access to locals here*/](const int i){
            return std::pair<int,int>(gridbtnx+(i%btnperrow)*gridbtnwidth, gridbtny+(i/btnperrow)*gridbtnheight);
        };

        while ((sec = control->GetSection(i))) {
            std::string name = sec->GetName();
            name[0] = std::toupper(name[0]);
            const auto sz = gridfunc(i);
            GUI::Button *b = new GUI::Button(this, sz.first, sz.second, name, gridbtnwidth, gridbtnheight);
            b->addActionHandler(this);
            bar->addItem(1, name);
            i++;
        }

        const auto finalgridpos = gridfunc(i - 1);
        int closerow_y = finalgridpos.second + 12 + gridbtnheight;

        (closeButton = new GUI::Button(this, 240, closerow_y, "Close", 80))->addActionHandler(this);

        resize(gridbtnx + (gridbtnwidth * btnperrow) + 12 + border_left + border_right,
               closerow_y + closeButton->getHeight() + 12 + border_top + border_bottom);

        bar->resize(getWidth(),bar->getHeight());
    }

    ~ConfigurationWindow() { running = false; cfg_windows_active.clear(); }

    virtual bool keyDown(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyDown(key)) return true;
        return false;
    }

    virtual bool keyUp(const GUI::Key &key) {
        if (GUI::ToplevelWindow::keyUp(key)) return true;

        if (key.special == GUI::Key::Escape) {
            closeButton->executeAction();
            return true;
        }

        return false;
    }

    void actionExecuted(GUI::ActionEventSource *b, const GUI::String &arg) {
        GUI::String sname = arg;
        sname.at(0) = (unsigned int)std::tolower((int)sname.at(0));
        Section *sec;
        if (arg == "Close" || arg == "Cancel" || arg == "Close") {
            running = false;
        } else if (sname == "autoexec") {
            auto lookup = cfg_windows_active.find(sname);
            if (lookup == cfg_windows_active.end()) {
                Section_line *section = static_cast<Section_line *>(control->GetSection((const char *)sname));
                auto *np = new AutoexecEditor(getScreen(), 50, 30, section);
                cfg_windows_active[sname] = np;
                np->cfg_sname = sname;
                np->raise();
            }
            else {
                lookup->second->raise();
            }
        } else if ((sec = control->GetSection((const char *)sname))) {
            auto lookup = cfg_windows_active.find(sname);
            if (lookup == cfg_windows_active.end()) {
                Section_prop *section = static_cast<Section_prop *>(sec);
                auto *np = new SectionEditor(getScreen(), 50, 30, section);
                cfg_windows_active[sname] = np;
                np->cfg_sname = sname;
                np->raise();
            }
            else {
                lookup->second->raise();
            }
        } else if (arg == "About") {
            const char *msg = PACKAGE_STRING " (C) 2002-" COPYRIGHT_END_YEAR " The DOSBox Team\nA fork of DOSBox 0.74 by TheGreatCodeholio\nBuild date: " UPDATED_STR "\n\nFor more info visit http://dosbox-x.com\nBased on DOSBox (http://dosbox.com)\n\n";
            new GUI::MessageBox2(getScreen(), 100, 150, 480, "About DOSBox-X", msg);
        } else if (arg == "Introduction") {
            new GUI::MessageBox2(getScreen(), 20, 50, 600, "Introduction", MSG_Get("PROGRAM_INTRO"));
        } else if (arg == "Getting Started") {
            std::string msg = MSG_Get("PROGRAM_INTRO_MOUNT_START");
#ifdef WIN32
            msg += MSG_Get("PROGRAM_INTRO_MOUNT_WINDOWS");
#else
            msg += MSG_Get("PROGRAM_INTRO_MOUNT_OTHER");
#endif
            msg += MSG_Get("PROGRAM_INTRO_MOUNT_END");

            new GUI::MessageBox2(getScreen(), 20, 50, 600, std::string("Introduction"), msg);
        } else if (arg == "CD-ROM Support") {
            new GUI::MessageBox2(getScreen(), 20, 50, 600, "Introduction", MSG_Get("PROGRAM_INTRO_CDROM"));
        } else if (arg == "Save...") {
            new SaveDialog(getScreen(), 90, 100, "Save Configuration...");
        } else if (arg == "Save Language File...") {
            new SaveLangDialog(getScreen(), 90, 100, "Save Language File...");
        } else {
            return ToplevelWindow::actionExecuted(b, arg);
        }
    }
};

/*********************************************************************************************************************/
/* UI control functions */

static void UI_Execute(GUI::ScreenSDL *screen) {
    SDL_Surface *sdlscreen;
    SDL_Event event;

    sdlscreen = screen->getSurface();
    auto *cfg_wnd = new ConfigurationWindow(screen, 30, 30, "DOSBox Configuration");
    cfg_wnd->raise();

    // event loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
#if !defined(C_SDL2) && defined(_WIN32) && !defined(HX_DOS)
                case SDL_SYSWMEVENT : {
                    switch ( event.syswm.msg->msg ) {
                        case WM_COMMAND:
# if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
                            if (GetMenu(GetHWND())) {
                                if (guiMenu.mainMenuWM_COMMAND((unsigned int)LOWORD(event.syswm.msg->wParam))) return;
                            }
# endif
                            break;
                    }
                } break;
#endif
                default:
                    break;
            }

            if (!screen->event(event)) {
                if (event.type == SDL_QUIT) running = false;
            }
        }

        //Selecting keyboard will create a new surface.
        screen->watchTime();
        sdlscreen = screen->getSurface();

        if (background)
            SDL_BlitSurface(background, NULL, sdlscreen, NULL);
        else
            SDL_FillRect(sdlscreen,0,0);

        screen->update(screen->getTime());

#if defined(C_SDL2)
        SDL_Window* GFX_GetSDLWindow(void);
        SDL_UpdateWindowSurface(GFX_GetSDLWindow());
#else
        SDL_UpdateRect(sdlscreen, 0, 0, 0, 0);
#endif

        SDL_Delay(40);
    }
}

static void UI_Select(GUI::ScreenSDL *screen, int select) {
    SDL_Surface *sdlscreen = NULL;
    Section_line *section2 = NULL;
    Section_prop *section = NULL;
    Section *sec = NULL;
    SDL_Event event;

    sdlscreen = screen->getSurface();
    switch (select) {
        case 0:
            new GUI::MessageBox2(screen, 200, 150, 280, "", "");
            running=false;
            break;
        case 1:
            new SaveDialog(screen, 90, 100, "Save Configuration...");
            break;
        case 2: {
            sec = control->GetSection("sdl");
            section=static_cast<Section_prop *>(sec); 
            auto *p = new SectionEditor(screen,50,30,section);
            p->raise();
            } break;
        case 3:
            sec = control->GetSection("dosbox");
            section=static_cast<Section_prop *>(sec); 
            new SectionEditor(screen,50,30,section);
            break;
        case 4:
            sec = control->GetSection("mixer");
            section=static_cast<Section_prop *>(sec); 
            new SectionEditor(screen,50,30,section);
            break;
        case 5:
            sec = control->GetSection("serial");
            section=static_cast<Section_prop *>(sec); 
            new SectionEditor(screen,50,30,section);
            break;
        case 6:
            sec = control->GetSection("ne2000");
            section=static_cast<Section_prop *>(sec); 
            new SectionEditor(screen,50,30,section);
            break;
        case 7:
            section2 = static_cast<Section_line *>(control->GetSection("autoexec"));
            new AutoexecEditor(screen, 50, 30, section2);
            break;
        case 8:
            sec = control->GetSection("glide");
            section=static_cast<Section_prop *>(sec); 
            new SectionEditor(screen,50,30,section);
            break;
        case 9:
            new SaveLangDialog(screen, 90, 100, "Save Language File...");
            break;
        case 10: {
            auto *np = new ConfigurationWindow(screen, 30, 30, "DOSBox Configuration");
            np->raise();
            } break;
        case 11:
            sec = control->GetSection("parallel");
            section=static_cast<Section_prop *>(sec);
            new SectionEditor(screen,50,30,section);
            break;
        case 12:
            sec = control->GetSection("printer");
            section=static_cast<Section_prop *>(sec);
            new SectionEditor(screen,50,30,section);
            break;
        case 13:
            sec = control->GetSection("cpu");
            section=static_cast<Section_prop *>(sec);
            new SectionEditor(screen,50,30,section);
            break;
        case 14:
            sec = control->GetSection("dos");
            section=static_cast<Section_prop *>(sec);
            new SectionEditor(screen,50,30,section);
            break;
        case 15:
            sec = control->GetSection("midi");
            section=static_cast<Section_prop *>(sec);
            new SectionEditor(screen,50,30,section);
            break;
        case 16: {
            auto *np1 = new SetCycles(screen, 90, 100, "Set CPU Cycles...");
            np1->raise();
            } break;
        case 17: {
            auto *np2 = new SetVsyncrate(screen, 90, 100, "Set vertical syncrate...");
            np2->raise();
            } break;
        case 18: {
            auto *np3 = new SetLocalSize(screen, 90, 100, "Set Default Local Freesize...");
            np3->raise();
            } break;
        default:
            break;
    }

    // event loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
#if !defined(C_SDL2) && defined(_WIN32) && !defined(HX_DOS)
                case SDL_SYSWMEVENT : {
                    switch ( event.syswm.msg->msg ) {
                        case WM_COMMAND:
# if DOSBOXMENU_TYPE == DOSBOXMENU_HMENU
                            if (GetMenu(GetHWND())) {
                                if (guiMenu.mainMenuWM_COMMAND((unsigned int)LOWORD(event.syswm.msg->wParam))) return;
                            }
# endif
                            break;
                    }
                } break;
#endif
                default:
                    break;
            }

            if (!screen->event(event)) {
                if (event.type == SDL_QUIT) running = false;
            }
        }

        if (background)
            SDL_BlitSurface(background, NULL, sdlscreen, NULL);
        else
            SDL_FillRect(sdlscreen,0,0);

        screen->update(4);
#if defined(C_SDL2)
        SDL_Window* GFX_GetSDLWindow(void);
        SDL_UpdateWindowSurface(GFX_GetSDLWindow());
#else   
        SDL_UpdateRect(sdlscreen, 0, 0, 0, 0);
#endif
        SDL_Delay(20);
    }
}

void GUI_Shortcut(int select) {
    if(!select || running) return;

    bool GFX_GetPreventFullscreen(void);

    /* Sorry, the UI screws up 3Dfx OpenGL emulation.
     * Remove this block when fixed. */
    if (GFX_GetPreventFullscreen()) {
        LOG_MSG("GUI is not available while 3Dfx OpenGL emulation is running");
        return;
    }

    shortcut=true;
    GUI::ScreenSDL *screen = UI_Startup(NULL);
    UI_Select(screen,select);
    UI_Shutdown(screen);
    shortcut=false;
    delete screen;
}

void GUI_Run(bool pressed) {
    if (pressed || running) return;

    bool GFX_GetPreventFullscreen(void);

    /* Sorry, the UI screws up 3Dfx OpenGL emulation.
     * Remove this block when fixed. */
    if (GFX_GetPreventFullscreen()) {
        LOG_MSG("GUI is not available while 3Dfx OpenGL emulation is running");
        return;
    }

    GUI::ScreenSDL *screen = UI_Startup(NULL);
    UI_Execute(screen);
    UI_Shutdown(screen);
    delete screen;
}
