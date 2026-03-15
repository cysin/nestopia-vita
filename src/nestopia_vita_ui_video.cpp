/*
 * Nestopia UE
 *
 * Copyright (C) 2007-2008 R. Belmont
 * Copyright (C) 2012-2018 R. Danbrook
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include "runtime/runtime.h"
#include "nestopia_vita_ui_emu.h"

#include "core/api/NstApiEmulator.hpp"
#include "core/api/NstApiVideo.hpp"
#include "core/api/NstApiNsf.hpp"

#include "fltkui/nstcommon.h"
#include "fltkui/video.h"
#include "fltkui/config.h"
#include "fltkui/font.h"

using namespace c2d;
using namespace pemu;
using namespace Nes::Api;

static Video::RenderState::Filter filter;
static Video::RenderState renderstate;

static dimensions_t basesize, rendersize, screensize;

extern void *custompalette;

extern nstpaths_t nstpaths;
extern Nes::Api::Emulator emulator;

// pemu
extern NestopiaVitaUiEmu *uiEmu;

void nst_ogl_init() {
    uiEmu->addVideo(nullptr, nullptr, {basesize.w, basesize.h});
    uiEmu->getVideo()->getTexture()->setFilter(
            conf.video_linear_filter ? Texture::Filter::Linear : Texture::Filter::Point);
}

void nst_ogl_deinit() {}

void nst_ogl_render() {}

void nst_video_refresh() {
    nst_ogl_deinit();
    nst_ogl_init();
}

void video_init() {
    nst_ogl_deinit();

    video_set_dimensions();
    video_set_filter();

    nst_ogl_init();

    if (nst_nsf()) {
        video_clear_buffer();
        video_disp_nsf();
    }
}

void video_toggle_filterupdate() {
    Video video(emulator);
    video.ClearFilterUpdateFlag();
}

void video_toggle_fullscreen() {
    if (!nst_playing()) { return; }
    conf.video_fullscreen ^= 1;
}

void video_toggle_filter() {
    conf.video_filter++;
    if (conf.video_filter > 5) { conf.video_filter = 0; }
}

void video_set_filter() {
    Video video(emulator);
    int scalefactor = conf.video_scale_factor;
    if (conf.video_scale_factor > 4) { scalefactor = 4; }
    if ((conf.video_scale_factor > 3) && (conf.video_filter == 5)) { scalefactor = 3; }

    switch (conf.video_filter) {
        default:
        case 0:
            filter = Video::RenderState::FILTER_NONE;
            break;
    }

    video.EnableUnlimSprites(conf.video_unlimited_sprites);

    // Set Palette options
    switch (conf.video_palette_mode) {
        case 0:
            video.GetPalette().SetMode(Video::Palette::MODE_YUV);
            break;
        case 1:
            video.GetPalette().SetMode(Video::Palette::MODE_RGB);
            break;
        case 2:
            video.GetPalette().SetMode(Video::Palette::MODE_CUSTOM);
            video.GetPalette().SetCustom((const unsigned char (*)[3]) custompalette, Video::Palette::EXT_PALETTE);
            break;
        default:
            break;
    }

    if (video.GetPalette().GetMode() == Video::Palette::MODE_YUV) {
        switch (conf.video_decoder) {
            case 0:
                video.SetDecoder(Video::DECODER_CONSUMER);
                break;
            case 1:
                video.SetDecoder(Video::DECODER_CANONICAL);
                break;
            case 2:
                video.SetDecoder(Video::DECODER_ALTERNATIVE);
                break;
            default:
                break;
        }
    }

    video.SetBrightness(conf.video_brightness);
    video.SetSaturation(conf.video_saturation);
    video.SetContrast(conf.video_contrast);
    video.SetHue(conf.video_hue);

    // Set up the render state parameters - 16-bit RGB565
    renderstate.filter = filter;
    renderstate.width = basesize.w;
    renderstate.height = basesize.h;
    renderstate.bits.count = 16;
    renderstate.bits.mask.r = 0xf800;
    renderstate.bits.mask.g = 0x07e0;
    renderstate.bits.mask.b = 0x001f;

    if (NES_FAILED(video.SetRenderState(renderstate))) {
        fprintf(stderr, "Nestopia core rejected render state\n");
    }
}

dimensions_t nst_video_get_dimensions_render() {
    return rendersize;
}

dimensions_t nst_video_get_dimensions_screen() {
    return screensize;
}

void nst_video_set_dimensions_screen(dimensions_t scrsize) {
    screensize = scrsize;
}

void video_set_dimensions() {
    int scalefactor = conf.video_scale_factor;
    if (conf.video_scale_factor > 4) { scalefactor = 4; }
    if ((conf.video_scale_factor > 3) && (conf.video_filter == 5)) { scalefactor = 3; }
    int wscalefactor = conf.video_scale_factor;
    int tvwidth = nst_pal() ? PAL_TV_WIDTH : TV_WIDTH;

    switch (conf.video_filter) {
        case 0:
        default:
            basesize.w = Video::Output::WIDTH;
            basesize.h = Video::Output::HEIGHT;
            conf.video_tv_aspect ? rendersize.w = tvwidth * wscalefactor : rendersize.w = basesize.w * wscalefactor;
            rendersize.h = basesize.h * wscalefactor;
            break;
    }

    if (!conf.video_unmask_overscan) {
        rendersize.h -= (OVERSCAN_TOP + OVERSCAN_BOTTOM) * scalefactor;
    }

    if (conf.video_fullscreen) {
        float aspect = (float) screensize.h / (float) rendersize.h;
        rendersize.w *= aspect;
        rendersize.h *= aspect;
    }
}

long video_lock_screen(void *&ptr) {
    int pitch;
    uiEmu->getVideo()->getTexture()->lock((uint8_t **) &ptr, &pitch);
    return pitch;
}

void video_unlock_screen(void *) {
    uiEmu->getVideo()->getTexture()->unlock();
}

void video_screenshot_flip(unsigned char *pixels, int width, int height, int bytes) {
    int rowsize = width * bytes;
    unsigned char *row = (unsigned char *) malloc(rowsize);
    unsigned char *low = pixels;
    unsigned char *high = &pixels[(height - 1) * rowsize];

    for (; low < high; low += rowsize, high -= rowsize) {
        memcpy(row, low, rowsize);
        memcpy(low, high, rowsize);
        memcpy(high, row, rowsize);
    }
    free(row);
}

void video_screenshot(const char *filename) {
    if (filename == nullptr) {
        char sshotpath[512];
        snprintf(sshotpath, sizeof(sshotpath), "%sscreenshots/%s-%ld-%d.png", nstpaths.nstdir, nstpaths.gamename,
                 time(nullptr), rand() % 899 + 100);
        uiEmu->getVideo()->getTexture()->save(sshotpath);
    } else {
        uiEmu->getVideo()->getTexture()->save(filename);
    }
}

void video_clear_buffer() {
    void *ptr;
    int pitch;
    uiEmu->getVideo()->getTexture()->lock((uint8_t **) &ptr, &pitch);
    memset(ptr, 0x00000000, (size_t) uiEmu->getVideo()->getTextureRect().height * pitch);
}

void video_disp_nsf() {
    Nsf nsf(emulator);
    nst_ogl_render();
}

void nst_video_print(const char *text, int xpos, int ypos, int seconds, bool bg) {
    (void)text; (void)xpos; (void)ypos; (void)seconds; (void)bg;
}

void nst_video_print_time(const char *timebuf, bool drawtime) {
    (void)timebuf; (void)drawtime;
}

void nst_video_text_draw(const char *text, int xpos, int ypos, bool bg) {
    (void)text; (void)xpos; (void)ypos; (void)bg;
}

void nst_video_text_match(const char *text, int *xpos, int *ypos, int strpos) {
    (void)text; (void)xpos; (void)ypos; (void)strpos;
}
