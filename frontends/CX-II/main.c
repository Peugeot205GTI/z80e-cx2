#include <z80e/ti/asic.h>
#include <z80e/debugger/debugger.h>
#include <z80e/disassembler/disassemble.h>
#include <z80e/runloop/runloop.h>
#include <z80e/debugger/commands.h>
#include <z80e/ti/hardware/t6a04.h>
#include <z80e/ti/hardware/keyboard.h>
#include <z80e/ti/hardware/interrupts.h>

#include <SDL/SDL.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>

/* hardcoded TI84pSE */
typedef struct {
    ti_device_type device;
    asic_t *device_asic;
    int scale;
    SDL_Surface *screen;
} appContext_t;

static appContext_t context;

/* Hardcode device & scale */
appContext_t create_context(void) {
    appContext_t ctx;
    ctx.device = TI84pSE;
    ctx.device_asic = NULL;
    ctx.scale = 4; /* adjust scale for Nspire screen / testing */
    ctx.screen = NULL;
    return ctx;
}

/* LCD change hook */
static int lcd_changed = 0;
void lcd_changed_hook(void *data, ti_bw_lcd_t *lcd) {
    (void)data;
    lcd_changed = 1;
}

/* small wrapper for reading bw lcd pixel */
static inline uint8_t _bw_lcd_read_screen(ti_bw_lcd_t *lcd, int Y, int X) {
    if (!lcd->display_on) return 0;
    return bw_lcd_read_screen(lcd, Y, X);
}

/* set pixel in an SDL surface (handles bpp) */
static void set_pixel(SDL_Surface *surf, int x, int y, Uint32 color) {
    int bpp = surf->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surf->pixels + y * surf->pitch + x * bpp;
    switch (bpp) {
        case 1:
            *p = color;
            break;
        case 2:
            *(Uint16 *)p = (Uint16)color;
            break;
        case 3:
            if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                p[0] = (color >> 16) & 0xff;
                p[1] = (color >> 8) & 0xff;
                p[2] = color & 0xff;
            } else {
                p[0] = color & 0xff;
                p[1] = (color >> 8) & 0xff;
                p[2] = (color >> 16) & 0xff;
            }
            break;
        case 4:
            *(Uint32 *)p = color;
            break;
    }
}

/* Render the LCD into the SDL surface scaled by context.scale */
void print_lcd(void *data, ti_bw_lcd_t *lcd) {
    (void)data;
    if (!context.screen) return;

    SDL_Surface *screen = context.screen;
    int scale = context.scale;

    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);

    /* Clear background (use light greenish as original CLEAR color) */
    Uint32 color_clear = SDL_MapRGB(screen->format, 0xc6, 0xe6, 0xc6);
    /* fill whole screen quickly */
    if (screen->format->BytesPerPixel == 1) {
        memset(screen->pixels, (Uint8)color_clear, screen->h * screen->pitch);
    } else {
        int y;
        for (y = 0; y < screen->h; ++y) {
            int x;
            for (x = 0; x < screen->w; ++x) {
                set_pixel(screen, x, y, color_clear);
            }
        }
    }

    /* Draw LCD pixels:
     *      Original coordinates: 64 columns (X?), 96 rows (?) — original code iterates cX over 64 and cY over 96.
     *      The original SDL2 code drew renderer points at (cY, cX) which implies x=cY, y=cX.
     */
    int cX, cY;
    Uint32 col_on = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
    Uint32 col_off = SDL_MapRGB(screen->format, 0x99, 0xb1, 0x99);

    for (cX = 0; cX < 64; ++cX) {
        for (cY = 0; cY < 96; ++cY) {
            int cXZ = (cX + lcd->Z) % 64;
            uint8_t bit = _bw_lcd_read_screen(lcd, cY, cXZ);
            Uint32 chosen = bit ? col_on : col_off;

            /* Draw scaled pixel block at (px, py) where px = cY * scale, py = cX * scale */
            int px0 = cY * scale;
            int py0 = cX * scale;
            int dx, dy;
            for (dy = 0; dy < scale; ++dy) {
                for (dx = 0; dx < scale; ++dx) {
                    int px = px0 + dx;
                    int py = py0 + dy;
                    if ((px >= 0 && px < screen->w) && (py >= 0 && py < screen->h)) {
                        set_pixel(screen, px, py, chosen);
                    }
                }
            }
        }
    }

    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    SDL_Flip(screen);
    lcd_changed = 0;
}

/* timer callback to print lcd when changed */
void lcd_timer_tick(asic_t *asic, void *data) {
    ti_bw_lcd_t *lcd = (ti_bw_lcd_t *)data;
    if (lcd_changed) {
        print_lcd(asic, lcd);
    }
}

/* Map SDL1 key events to calculator scancodes */
void key_tap(asic_t *asic, int scancode, int down) {
    if (down) depress_key(asic->cpu->devices[0x01].device, scancode);
    else release_key(asic->cpu->devices[0x01].device, scancode);
}

/* Simplified SDL1 event loop hook */
void sdl_events_hook(asic_t *device, void *unused) {
    (void)unused;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int down = (event.type == SDL_KEYDOWN);
                SDLKey ks = event.key.keysym.sym;
                /* Map a sensible subset — tweak as you like for Nspire */
                switch (ks) {
                    case SDLK_UP:    key_tap(device, 0x03, down); break;
                    case SDLK_DOWN:  key_tap(device, 0x00, down); break;
                    case SDLK_LEFT:  key_tap(device, 0x01, down); break;
                    case SDLK_RIGHT: key_tap(device, 0x02, down); break;
                    case SDLK_RETURN: key_tap(device, 0x10, down); break;
                    case SDLK_SPACE: key_tap(device, 0x40, down); break;
                    case SDLK_BACKSPACE: key_tap(device, 0x16, down); break; /* Clear */
                    case SDLK_LSHIFT: key_tap(device, 0x65, down); break; /* 2nd */
                    case SDLK_LCTRL: key_tap(device, 0x57, down); break;  /* Alpha */
                        /* numbers & letters common mapping */
                        case SDLK_0: key_tap(device, 0x40, down); break;
                        case SDLK_1: key_tap(device, 0x41, down); break;
                        case SDLK_2: key_tap(device, 0x31, down); break;
                        case SDLK_3: key_tap(device, 0x21, down); break;
                        case SDLK_4: key_tap(device, 0x42, down); break;
                        case SDLK_5: key_tap(device, 0x32, down); break;
                        case SDLK_6: key_tap(device, 0x22, down); break;
                        case SDLK_7: key_tap(device, 0x43, down); break;
                        case SDLK_8: key_tap(device, 0x33, down); break;
                        case SDLK_9: key_tap(device, 0x23, down); break;
                        /* letters: conservative choices */
                        case SDLK_a: key_tap(device, 0x56, down); break; /* Math */
                        case SDLK_b: key_tap(device, 0x46, down); break; /* Apps */
                        case SDLK_c: key_tap(device, 0x36, down); break; /* Programs */
                        case SDLK_d: key_tap(device, 0x55, down); break; /* x^-1 */
                        case SDLK_e: key_tap(device, 0x45, down); break; /* sin( */
                        case SDLK_f: key_tap(device, 0x35, down); break; /* cos( */
                        case SDLK_g: key_tap(device, 0x25, down); break; /* tan( */
                        case SDLK_PLUS:
                        case SDLK_EQUALS: key_tap(device, 0x47, down); break; /* + */
                        case SDLK_MINUS:
                        case SDLK_UNDERSCORE: key_tap(device, 0x20, down); break; /* - */
                        case SDLK_DELETE: key_tap(device, 0x67, down); break; /* DEL */
                        case SDLK_ESCAPE: /* MODE */
                            key_tap(device, 0x66, down);
                            break;
                        default:
                            break;
                }
                break;
            }
                        case SDL_VIDEOEXPOSE:
                            /* request redraw */
                            lcd_changed = 1;
                            break;
                        case SDL_QUIT:
                            exit(0);
                            break;
                        default:
                            break;
        }
    }
}

/* SIGINT handler to stop emulator */
void sigint_handler(int sig) {
    (void)sig;
    if (context.device_asic) {
        context.device_asic->stopped = 1;
    } else {
        exit(0);
    }
}

/* Main — minimal: hardcoded device TI84pSE, try load rom.bin else start debugger */
int main(int argc, char **argv) {
    int w = context.scale * 96;
    int h = context.scale * 64;
    SDL_Surface *screen = SDL_SetVideoMode(w, h, 16, SDL_SWSURFACE | SDL_RESIZABLE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 0, 0));
    SDL_Flip(screen);
    (void)argc; (void)argv;

    context = create_context();
    signal(SIGINT, sigint_handler);

    disassembler_init();

    /* init logless (use existing APIs) */
    log_t *log = init_log_z80e(NULL, 0, L_WARN); /* frontend_log removed, pass NULL */
    asic_t *device = asic_init(context.device, log);
    context.device_asic = device;

    /* Load ROM from ./rom.bin if present, else fall back to debugger */
    FILE *file = fopen("rom.bin.tns", "rb");
    if (!file) {
        /* start debugger if no rom */
        device->debugger = init_debugger(device);
        device->debugger->state = DEBUGGER_ENABLED;
    } else {
        fseek(file, 0L, SEEK_END);
        int length = ftell(file);
        fseek(file, 0L, SEEK_SET);
        if (length != device->mmu->settings.flash_pages * 0x4000) {
            /* accept mismatch but print message to stdout */
            printf("Warning: rom.bin.tns size %d != expected %d\n", length, device->mmu->settings.flash_pages * 0x4000);
        }
        fread(device->mmu->flash, 0x4000, device->mmu->settings.flash_pages, file);
        fclose(file);
    }

    /* SDL1 init */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }



    SDL_WM_SetCaption("z80e (SDL1)", NULL);
    context.screen = screen;

    /* add hooks and timers */
    hook_add_lcd_update(device->hook, NULL, lcd_changed_hook);
    asic_add_timer(device, 0, 60, lcd_timer_tick, device->cpu->devices[0x10].device);
    asic_add_timer(device, 0, 100, sdl_events_hook, device->cpu->devices[0x10].device);


        /* run main loop: tick at ~60Hz */
        while (!device->stopped) {
            runloop_tick(device->runloop);
            //msleep(16);
        }


    /* cleanup */
    asic_free(device);
    SDL_Quit();
    return 0;
}
