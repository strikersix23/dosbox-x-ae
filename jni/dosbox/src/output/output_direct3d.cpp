#include <sys/types.h>
#include <assert.h>
#include <math.h>

#include "dosbox.h"
#include <output/output_direct3d.h>

#include "sdlmain.h"

using namespace std;

#if C_DIRECT3D

CDirect3D* d3d = NULL;

static void d3d_init(void) 
{
    sdl.desktop.want_type = SCREEN_DIRECT3D;
    if (!sdl.using_windib) 
    {
        LOG_MSG("Resetting to WINDIB mode");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        putenv("SDL_VIDEODRIVER=windib");
        sdl.using_windib = true;
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)<0) E_Exit("Can't init SDL Video %s", SDL_GetError());
        GFX_SetIcon(); GFX_SetTitle(-1, -1, -1, false);
        if (!sdl.desktop.fullscreen) DOSBox_RefreshMenu();
    }

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);

    if (!SDL_GetWMInfo(&wmi)) 
    {
        LOG_MSG("SDL:Error retrieving window information");
        LOG_MSG("Failed to get window info");
        OUTPUT_SURFACE_Select();
    }
    else 
    {
        if (sdl.desktop.fullscreen) 
            GFX_CaptureMouse();

        if (d3d) delete d3d;
        d3d = new CDirect3D(640, 400);

        if (!d3d) 
        {
            LOG_MSG("Failed to create d3d object");
            OUTPUT_SURFACE_Select();
            return;
        }
        else if (d3d->InitializeDX(wmi.child_window, sdl.desktop.doublebuf) != S_OK) 
        {
            LOG_MSG("Unable to initialize DirectX");
            OUTPUT_SURFACE_Select();
            return;
        }
    }

# if (C_D3DSHADERS)
    if (d3d) {
        Section_prop *section = static_cast<Section_prop *>(control->GetSection("sdl"));
        Prop_multival* prop = section->Get_multival("pixelshader");
        if (SUCCEEDED(d3d->LoadPixelShader(prop->GetSection()->Get_string("type"), 0, 0)))
            if (menu.startup)
                GFX_ResetScreen();
    }
# endif
}


// output API below

void OUTPUT_DIRECT3D_Initialize()
{
    // nothing to initialize (yet?)
}

void OUTPUT_DIRECT3D_Select()
{
    sdl.desktop.want_type = SCREEN_DIRECT3D;
    render.aspectOffload = true;
    d3d_init();

#if defined(WIN32) && !defined(C_SDL2)
    SDL1_hax_inhibit_WM_PAINT = 1;
#endif
}

Bitu OUTPUT_DIRECT3D_GetBestMode(Bitu flags)
{
    flags |= GFX_SCALING;
    if (GCC_UNLIKELY(d3d->bpp16))
        flags &= ~(GFX_CAN_8 | GFX_CAN_15 | GFX_CAN_32);
    else
        flags &= ~(GFX_CAN_8 | GFX_CAN_15 | GFX_CAN_16);
    return flags;
}

Bitu OUTPUT_DIRECT3D_SetSize()
{
    Bitu retFlags = 0;

    Bit16u fixedWidth;
    Bit16u fixedHeight;
    Bit16u windowWidth;
    Bit16u windowHeight;
    Bitu adjTexWidth = sdl.draw.width;
    Bitu adjTexHeight = sdl.draw.height;

    if (sdl.desktop.fullscreen) 
    {
        fixedWidth = sdl.desktop.full.fixed ? sdl.desktop.full.width : 0;
        fixedHeight = sdl.desktop.full.fixed ? sdl.desktop.full.height : 0;
    }
    else 
    {
        fixedWidth = sdl.desktop.window.width;
        fixedHeight = sdl.desktop.window.height;
    }

    if (fixedWidth == 0 || fixedHeight == 0) 
    {
        Bitu consider_height = menu.maxwindow ? currentWindowHeight : 0;
        Bitu consider_width = menu.maxwindow ? currentWindowWidth : 0;
        int final_height = max(consider_height, userResizeWindowHeight);
        int final_width = max(consider_width, userResizeWindowWidth);

        fixedWidth = final_width;
        fixedHeight = final_height;
    }

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    /* scale the menu bar if the window is large enough */
    {
        int cw = fixedWidth, ch = fixedHeight;
        Bitu scale = 1;

        if (cw == 0) cw = (Bit16u)(sdl.draw.width * sdl.draw.scalex);
        if (ch == 0) ch = (Bit16u)(sdl.draw.height * sdl.draw.scaley);

        while ((cw / scale) >= (640 * 2) && (ch / scale) >= (400 * 2))
            scale++;

        LOG_MSG("menuScale=%lu", (unsigned long)scale);
        mainMenu.setScale(scale);

        if (mainMenu.isVisible()) 
            fixedHeight -= mainMenu.menuBox.h;
    }
#endif

    sdl.clip.x = sdl.clip.y = 0;
    if (fixedWidth && fixedHeight)
    {
        sdl.clip.w = windowWidth = fixedWidth;
        sdl.clip.h = windowHeight = fixedHeight;
        if (render.aspect) aspectCorrectFitClip(sdl.clip.w, sdl.clip.h, sdl.clip.x, sdl.clip.y, fixedWidth, fixedHeight);
    }
    else 
    {
        windowWidth = (Bit16u)(sdl.draw.width * sdl.draw.scalex);
        windowHeight = (Bit16u)(sdl.draw.height * sdl.draw.scaley);
        if (render.aspect) aspectCorrectExtend(windowWidth, windowHeight);
        sdl.clip.w = windowWidth; sdl.clip.h = windowHeight;
    }

    // when xBRZ scaler is used, we can adjust render target size to exactly what xBRZ scaler will output, leaving final scaling to default D3D scaler / shaders
#if C_XBRZ
    if (sdl_xbrz.enable && xBRZ_SetScaleParameters(sdl.draw.width, sdl.draw.height, sdl.clip.w, sdl.clip.h)) 
    {
        adjTexWidth = sdl.draw.width * sdl_xbrz.scale_factor;
        adjTexHeight = sdl.draw.height * sdl_xbrz.scale_factor;
    }
#endif
    // Calculate texture size
    if ((!d3d->square) && (!d3d->pow2)) 
    {
        d3d->dwTexWidth = adjTexWidth;
        d3d->dwTexHeight = adjTexHeight;
    }
    else if (d3d->square) 
    {
        int texsize = 2 << int_log2(adjTexWidth > adjTexHeight ? adjTexWidth : adjTexHeight);
        d3d->dwTexWidth = d3d->dwTexHeight = texsize;
    }
    else 
    {
        d3d->dwTexWidth = 2 << int_log2(adjTexWidth);
        d3d->dwTexHeight = 2 << int_log2(adjTexHeight);
    }

    LOG(LOG_MISC, LOG_DEBUG)("GFX_SetSize Direct3D texture=%ux%u window=%ux%u clip=x,y,w,h=%d,%d,%d,%d",
        (unsigned int)d3d->dwTexWidth,
        (unsigned int)d3d->dwTexHeight,
        (unsigned int)windowWidth,
        (unsigned int)windowHeight,
        (unsigned int)sdl.clip.x,
        (unsigned int)sdl.clip.y,
        (unsigned int)sdl.clip.w,
        (unsigned int)sdl.clip.h);

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    if (mainMenu.isVisible()) {
        windowHeight += mainMenu.menuBox.h;
        sdl.clip.y += mainMenu.menuBox.h;
    }
#endif

#if (C_D3DSHADERS)
    Section_prop *section = static_cast<Section_prop *>(control->GetSection("sdl"));
    if (section) 
    {
        Prop_multival* prop = section->Get_multival("pixelshader");
        std::string f = prop->GetSection()->Get_string("force");
        d3d->LoadPixelShader(prop->GetSection()->Get_string("type"), sdl.draw.scalex, sdl.draw.scaley, (f == "forced"));
    }
    else 
    {
        LOG_MSG("SDL:D3D:Could not get pixelshader info, shader disabled");
    }
#endif

    d3d->aspect = false;
    d3d->autofit = false;

    // Create a dummy sdl surface
    // D3D will hang or crash when using fullscreen with ddraw surface, therefore we hack SDL to provide
    // a GDI window with an additional 0x40 flag. If this fails or stock SDL is used, use WINDIB output
    if (GCC_UNLIKELY(d3d->bpp16)) 
    {
        sdl.surface = SDL_SetVideoMode(windowWidth, windowHeight, 16, sdl.desktop.fullscreen ? SDL_FULLSCREEN | 0x40 : SDL_RESIZABLE | 0x40);
        sdl.deferred_resize = false;
        sdl.must_redraw_all = true;
        retFlags = GFX_CAN_16 | GFX_SCALING;
    }
    else 
    {
        sdl.surface = SDL_SetVideoMode(windowWidth, windowHeight, 0, sdl.desktop.fullscreen ? SDL_FULLSCREEN | 0x40 : SDL_RESIZABLE | 0x40);
        sdl.deferred_resize = false;
        sdl.must_redraw_all = true;
        retFlags = GFX_CAN_32 | GFX_SCALING;
    }

    if (sdl.surface == NULL)
        E_Exit("Could not set video mode %ix%i-%i: %s", sdl.clip.w, sdl.clip.h, d3d->bpp16 ? 16 : 32, SDL_GetError());

    if (d3d->dynamic) retFlags |= GFX_HARDWARE;

    if (GCC_UNLIKELY(d3d->Resize3DEnvironment(windowWidth, windowHeight, sdl.clip.x, sdl.clip.y, sdl.clip.w, sdl.clip.h, adjTexWidth, adjTexHeight, sdl.desktop.fullscreen) != S_OK)) 
        retFlags = 0;

#if LOG_D3D
    LOG_MSG("SDL:D3D:Display mode set to: %dx%d with %fx%f scale", sdl.clip.w, sdl.clip.h, sdl.draw.scalex, sdl.draw.scaley);
#endif

#if DOSBOXMENU_TYPE == DOSBOXMENU_SDLDRAW
    mainMenu.screenWidth = sdl.surface->w;
    mainMenu.screenHeight = sdl.surface->h;
    mainMenu.updateRect();
    mainMenu.setRedraw();
    GFX_DrawSDLMenu(mainMenu, mainMenu.display_list);
#endif

    return retFlags;
}

bool OUTPUT_DIRECT3D_StartUpdate(Bit8u* &pixels, Bitu &pitch)
{
#if C_XBRZ
    if (sdl_xbrz.enable && sdl_xbrz.scale_on) 
    {
        sdl_xbrz.renderbuf.resize(sdl.draw.width * sdl.draw.height);
        pixels = sdl_xbrz.renderbuf.empty() ? nullptr : reinterpret_cast<Bit8u*>(&sdl_xbrz.renderbuf[0]);
        pitch = sdl.draw.width * sizeof(uint32_t);
        sdl.updating = true;
    }
    else
    {
        sdl.updating = d3d->LockTexture(pixels, pitch);
    }
#else
    sdl.updating = d3d->LockTexture(pixels, pitch);
#endif
    return sdl.updating;
}

void OUTPUT_DIRECT3D_EndUpdate(const Bit16u *changedLines)
{
#if C_XBRZ
    if (sdl_xbrz.enable && sdl_xbrz.scale_on) 
    {
        // we have xBRZ pseudo render buffer to be output to the pre-sized texture, do the xBRZ part
        const int srcWidth = sdl.draw.width;
        const int srcHeight = sdl.draw.height;
        if (sdl_xbrz.renderbuf.size() == srcWidth * srcHeight && srcWidth > 0 && srcHeight > 0)
        {
            // we assume render buffer is *not* scaled!
            int xbrzWidth = srcWidth * sdl_xbrz.scale_factor;
            int xbrzHeight = srcHeight * sdl_xbrz.scale_factor;
            sdl_xbrz.pixbuf.resize(xbrzWidth * xbrzHeight);

            const uint32_t* renderBuf = &sdl_xbrz.renderbuf[0]; // help VS compiler a little + support capture by value
            uint32_t* xbrzBuf = &sdl_xbrz.pixbuf[0];
            xBRZ_Render(renderBuf, xbrzBuf, changedLines, srcWidth, srcHeight, sdl_xbrz.scale_factor);

            // D3D texture can be not of exactly size we expect, so we copy xBRZ buffer to the texture there, adjusting for texture pitch
            Bit8u *tgtPix;
            Bitu tgtPitch;
            if (d3d->LockTexture(tgtPix, tgtPitch) && tgtPix) // if locking fails, target texture can be nullptr
            {
                uint32_t* tgtTex = reinterpret_cast<uint32_t*>(static_cast<Bit8u*>(tgtPix));
# if defined(XBRZ_PPL)
                concurrency::task_group tg;
                for (int i = 0; i < xbrzHeight; i += sdl_xbrz.task_granularity)
                {
                    tg.run([=] {
                        const int iLast = min(i + sdl_xbrz.task_granularity, xbrzHeight);
                        xbrz::pitchChange(&xbrzBuf[0], &tgtTex[0], xbrzWidth, xbrzHeight, xbrzWidth * sizeof(uint32_t), tgtPitch, i, iLast, [](uint32_t pix) { return pix; });
                    });
                }
                tg.wait();
# else
                xbrz::pitchChange(&xbrzBuf[0], &tgtTex[0], xbrzWidth, xbrzHeight, xbrzWidth * sizeof(uint32_t), tgtPitch, 0, xbrzHeight, [](uint32_t pix) { return pix; });
# endif
            }
        }
    }
#endif

    if (!menu.hidecycles) frames++; //implemented
    if (GCC_UNLIKELY(!d3d->UnlockTexture(changedLines)))
        E_Exit("Failed to draw screen!");
}

void OUTPUT_DIRECT3D_Shutdown()
{
    if (d3d)
        delete d3d;
}

#endif /*C_DIRECT3D*/
