#include "dosbox.h"

#ifndef DOSBOX_OUTPUT_TOOLS_XBRZ_H
#define DOSBOX_OUTPUT_TOOLS_XBRZ_H

#if C_XBRZ || C_SURFACE_POSTRENDER_ASPECT
#include <xBRZ/xbrz_tools.h>
#include <cmath>

#if defined(WIN32) && !defined(__MINGW32__) && !defined(HX_DOS)
#define XBRZ_PPL 1
#include <ppl.h>
#endif

#endif /*C_XBRZ || C_SURFACE_POSTRENDER_ASPECT*/

#if C_XBRZ
#include <xBRZ/xbrz.h>

struct SDL_xBRZ {
    // configuration
    bool enable;
    bool postscale_bilinear;
    int task_granularity;
    int fixed_scale_factor;
    int max_scale_factor;

    // runtime
    bool scale_on;
    int scale_factor;
    std::vector<uint32_t> renderbuf;
    std::vector<uint32_t> pixbuf;
};

extern SDL_xBRZ sdl_xbrz;

void xBRZ_Initialize();
void xBRZ_Change_Options(Section_prop* section);
bool xBRZ_SetScaleParameters(int srcWidth, int srcHeight, int dstWidth, int dstHeight);
void xBRZ_Render(const uint32_t* renderBuf, uint32_t* xbrzBuf, const Bit16u *changedLines, const int srcWidth, const int srcHeight, int scalingFactor);
void xBRZ_PostScale(const uint32_t* src, const int srcWidth, const int srcHeight, const int srcPitch,
    uint32_t* tgt, const int tgtWidth, const int tgtHeight, const int tgtPitch,
    const bool bilinear, const int task_granularity);

#endif

#endif /*DOSBOX_OUTPUT_TOOLS_XBRZ_H*/