
#include <cairo.h>

#include <SDL.h>

int main() {
    double ang = 0,angv = (M_PI * 2) / 360,angstep = (M_PI * 2) / 4;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;

    SDL_Window *window = SDL_CreateWindow("Cairo",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,320,240,SDL_WINDOW_SHOWN);
    if (window == NULL)
        return 1;

    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (surface == NULL)
        return 1;

    cairo_format_t csurffmt = CAIRO_FORMAT_INVALID;
    if (surface->format->BytesPerPixel == 4) {
        csurffmt = CAIRO_FORMAT_ARGB32;
    }
    else if (surface->format->BytesPerPixel == 3) {
        csurffmt = CAIRO_FORMAT_RGB24;
    }
    else if (surface->format->BytesPerPixel == 2) {
        csurffmt = CAIRO_FORMAT_RGB16_565;
    }
    else {
        return 1;
    }

    cairo_surface_t* csurf = cairo_image_surface_create(csurffmt, 320, 240);
    if (csurf == NULL)
        return 1;

    cairo_t* cactx = cairo_create(csurf);
    if (cactx == NULL)
        return 1;

    bool run = true;
    while (run) {
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_WINDOWEVENT:
                    switch (ev.window.event) {
                        case SDL_WINDOWEVENT_CLOSE:
                            run = false;
                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (ev.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            run = false;
                            break;
                    }
                    break;
            }
        }

        // clear
        cairo_reset_clip(cactx);
        cairo_set_source_rgb(cactx,0.25,0.25,0.25);
        cairo_paint(cactx);

        // draw
        cairo_set_line_width(cactx,8.0);

        {
            unsigned int step=0;

            for (double a=0;a < (M_PI * 2);a += angstep,step++) {
                double x = 160.0 + cos(a+ang)*100.0 + 20;
                double y = 120.0 + sin(a+ang)*100.0 + 20;

                if (step == 0)
                    cairo_move_to(cactx,x,y);
                else
                    cairo_line_to(cactx,x,y);
            }

            cairo_close_path(cactx);
            ang += angv;
        }

        cairo_set_source_rgba(cactx,0,0,0,0.5);
        cairo_fill(cactx);

        {
            unsigned int step=0;

            for (double a=0;a < (M_PI * 2);a += angstep,step++) {
                double x = 160.0 + cos(a+ang)*100.0;
                double y = 120.0 + sin(a+ang)*100.0;

                if (step == 0)
                    cairo_move_to(cactx,x,y);
                else
                    cairo_line_to(cactx,x,y);
            }

            cairo_close_path(cactx);
            ang += angv;
        }

        cairo_set_source_rgb(cactx,1.0,1.0,1.0);
        cairo_fill_preserve(cactx);

        cairo_set_source_rgb(cactx,0.0,0.0,1.0);
        cairo_stroke(cactx);

        // copy Cairo output to display
        cairo_surface_flush(csurf);
        {
            unsigned char *cbuf = cairo_image_surface_get_data(csurf);
            int width = cairo_image_surface_get_width(csurf);
            int height = cairo_image_surface_get_height(csurf);
            int stride = cairo_image_surface_get_stride(csurf);

            SDL_LockSurface(surface);

            if (cbuf != NULL && surface->pixels != NULL) {
                if (width > surface->w) width = surface->w;
                if (height > surface->h) height = surface->h;

                for (int y=0;y < height;y++) {
                    unsigned char *d = (unsigned char*)(surface->pixels) + (surface->pitch * y);
                    unsigned char *s = (unsigned char*)(cbuf) + (stride * y);
                    memcpy(d,s,width*surface->format->BytesPerPixel);
                }
            }

            SDL_UnlockSurface(surface);
            SDL_UpdateWindowSurface(window);
        }

        SDL_Delay(1000 / 30);
    }

    cairo_surface_flush(csurf);
    cairo_destroy(cactx);
    cairo_surface_destroy(csurf);

    // do not free surface, owned by dinwo
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

