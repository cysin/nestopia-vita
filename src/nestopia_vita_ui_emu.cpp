//
// NestopiaVitaUiEmu - Nestopia core integration with RGUI menu
// Based on pnes_ui_emu.cpp (pemu) + fbneo-vita RGUI pattern
//

#include <fstream>

#include "fltkui/nstcommon.h"
#include "fltkui/video.h"
#include "fltkui/config.h"
#include "fltkui/audio.h"
#include "fltkui/input.h"

#include "runtime/runtime.h"
#include "nestopia_vita_ui_emu.h"
#include "rgui_main.h"

using namespace c2d;
using namespace pemu;

NestopiaVitaUiEmu *uiEmu;

/// NESTOPIA globals
settings_t conf;

extern nstpaths_t nstpaths;

extern bool (*nst_archive_select)(const char *, char *, size_t);

extern Nes::Core::Input::Controllers *cNstPads;

extern Nes::Api::Emulator emulator;

/// end NESTOPIA globals

NestopiaVitaUiEmu::NestopiaVitaUiEmu(UiMain *ui) : UiEmu(ui) {
    printf("NestopiaVitaUiEmu()\n");
    uiEmu = this;
}

int NestopiaVitaUiEmu::load(const Game &game) {
    nestopia_vita::load_error::clear();
    currentGame = game;

    getUi()->getUiProgressBox()->setTitle(game.name);
    getUi()->getUiProgressBox()->setMessage("Please wait...");
    getUi()->getUiProgressBox()->setProgress(0);
    getUi()->getUiProgressBox()->setVisibility(Visibility::Visible);
    getUi()->getUiProgressBox()->setLayer(1000);
    getUi()->flip();

    // default config
    nestopia_config_init();

    std::string fullPath = game.romsPath + game.path;
    if (nestopia_core_init(fullPath.c_str()) != 0) {
        getUi()->getUiProgressBox()->setVisibility(Visibility::Hidden);
        if (!nestopia_vita::load_error::has()) {
            nestopia_vita::load_error::set(
                    "Load Failed",
                    "Could not load ROM.",
                    game.path);
        }
        stop();
        return -1;
    }

    targetFps = nst_pal() ? 50 : 60;

    getUi()->getUiProgressBox()->setProgress(1);
    getUi()->flip();
    getUi()->delay(500);
    getUi()->getUiProgressBox()->setVisibility(Visibility::Hidden);

    return UiEmu::load(game);
}

void NestopiaVitaUiEmu::stop() {
    printf("NestopiaVitaUiEmu::stop()\n");

    nst_pause();

    // Remove the cartridge and shut down the NES
    nst_unload();

    // Unload the FDS BIOS, NstDatabase.xml, and the custom palette
    nst_db_unload();
    nst_fds_bios_unload();
    nst_palette_unload();

    UiEmu::stop();
}

extern RguiMain *g_rgui;

bool NestopiaVitaUiEmu::onInput(c2d::Input::Player *players) {
    // if RGUI is visible, let it handle input
    if (g_rgui && g_rgui->isVisible()) {
        return g_rgui->onInput(players);
    }

    // intercept menu combo to show RGUI instead of old menu
    if (((players[0].buttons & c2d::Input::Button::Menu1) && (players[0].buttons & c2d::Input::Button::Menu2))) {
        if (g_rgui) {
            pause();
            g_rgui->show(true);
            pMain->getInput()->clear();
            return true;
        }
    }

    // skip base UiEmu::onInput to avoid triggering old menu,
    // go directly to C2DObject::onInput
    return C2DObject::onInput(players);
}

void NestopiaVitaUiEmu::onUpdate() {
    if (isPaused()) {
        return;
    }

    // update nestopia buttons
    auto *players = getUi()->getInput()->getPlayers();

    // update turbo frame counter
    m_turbo_counter++;

    for (int i = 0; i < NUMGAMEPADS; i++) {
        cNstPads->pad[i].buttons = 0;

        // D-pad
        if (players[i].buttons & c2d::Input::Button::Up)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::UP;
        if (players[i].buttons & c2d::Input::Button::Down)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::DOWN;
        if (players[i].buttons & c2d::Input::Button::Left)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::LEFT;
        if (players[i].buttons & c2d::Input::Button::Right)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::RIGHT;

        // Start / Select
        if (players[i].buttons & c2d::Input::Button::Start)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::START;
        if (players[i].buttons & c2d::Input::Button::Select)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::SELECT;

        // Face buttons: Cross=B, Circle=A (standard NES mapping on Vita)
        bool pressA = (players[i].buttons & c2d::Input::Button::B) != 0;
        bool pressB = (players[i].buttons & c2d::Input::Button::A) != 0;

        // Dedicated turbo buttons: Square=Turbo B, Triangle=Turbo A
        // These always auto-fire when held, matching original Nestopia ta/tb
        bool pressTurboA = (players[i].buttons & c2d::Input::Button::Y) != 0;
        bool pressTurboB = (players[i].buttons & c2d::Input::Button::X) != 0;

        // Dedicated turbo buttons always pulse using Nestopia's turbopulse rate
        bool turbo_pulse = (m_turbo_counter % (conf.timing_turbopulse * 2)) < conf.timing_turbopulse;
        if (pressTurboA) pressTurboA = turbo_pulse;
        if (pressTurboB) pressTurboB = turbo_pulse;

        if (pressA || pressTurboA)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::A;
        if (pressB || pressTurboB)
            cNstPads->pad[i].buttons |= Nes::Core::Input::Controllers::Pad::B;
    }

    // step nestopia core
    nst_emuloop();

    UiEmu::onUpdate();
}

/// NESTOPIA AUDIO
static void *audio_buffer = nullptr;

void audio_init() {
    int samples = conf.audio_sample_rate / (nst_pal() ? 50 : 60);
    uiEmu->addAudio(conf.audio_sample_rate, samples);
}

void audio_deinit() {
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = nullptr;
    }
}

void audio_queue() {
    Audio *audio = uiEmu->getAudio();
    if (audio) {
        uiEmu->getAudio()->play(audio_buffer, uiEmu->getAudio()->getSamples(),
                                nst_pal() ? Audio::SyncMode::LowLatency : Audio::SyncMode::None);
    }
}

void audio_pause() {
    if (uiEmu->getAudio()) {
        uiEmu->getAudio()->pause(1);
    }
}

void audio_unpause() {
    if (uiEmu->getAudio()) {
        uiEmu->getAudio()->pause(0);
    }
}

void audio_set_speed(int speed) {
    (void)speed;
}

void audio_set_params(Nes::Api::Sound::Output *soundoutput) {
    Audio *aud = uiEmu->getAudio();

    if (aud) {
        Nes::Api::Sound sound(emulator);

        sound.SetSampleRate((unsigned long) conf.audio_sample_rate);
        sound.SetSpeaker(Nes::Api::Sound::SPEAKER_STEREO);
        sound.SetSpeed(Nes::Api::Sound::DEFAULT_SPEED);

        audio_adj_volume();

        audio_buffer = malloc(aud->getSamplesSize());
        memset(audio_buffer, 0, aud->getSamplesSize());
        soundoutput->samples[0] = audio_buffer;
        soundoutput->length[0] = (unsigned int) aud->getSamples();
        soundoutput->samples[1] = nullptr;
        soundoutput->length[1] = 0;
    }
}

void audio_adj_volume() {
    Nes::Api::Sound sound(emulator);
    sound.SetVolume(Nes::Api::Sound::ALL_CHANNELS, conf.audio_volume);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_SQUARE1, conf.audio_vol_sq1);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_SQUARE2, conf.audio_vol_sq2);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_TRIANGLE, conf.audio_vol_tri);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_NOISE, conf.audio_vol_noise);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_DPCM, conf.audio_vol_dpcm);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_FDS, conf.audio_vol_fds);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_MMC5, conf.audio_vol_mmc5);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_VRC6, conf.audio_vol_vrc6);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_VRC7, conf.audio_vol_vrc7);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_N163, conf.audio_vol_n163);
    sound.SetVolume(Nes::Api::Sound::CHANNEL_S5B, conf.audio_vol_s5b);
}

/// NESTOPIA CONFIG
void NestopiaVitaUiEmu::nestopia_config_init() {
    // Video
    conf.video_filter = 0;
    conf.video_scale_factor = 1;
    conf.video_palette_mode = 0;
    conf.video_decoder = 0;
    conf.video_brightness = 0;
    conf.video_saturation = 0;
    conf.video_contrast = 0;
    conf.video_hue = 0;
    conf.video_ntsc_mode = 0;
    conf.video_ntsc_sharpness = 0;
    conf.video_ntsc_resolution = 0;
    conf.video_ntsc_bleed = 0;
    conf.video_ntsc_artifacts = 0;
    conf.video_ntsc_fringing = 0;
    conf.video_xbr_corner_rounding = 0;
    conf.video_linear_filter = false;
    conf.video_tv_aspect = false;
    conf.video_unmask_overscan = false;
    conf.video_fullscreen = false;
    conf.video_stretch_aspect = false;
    conf.video_unlimited_sprites = false;
    conf.video_xbr_pixel_blending = false;

    // Audio
    conf.audio_stereo = true;
    conf.audio_sample_rate = 48000;
    conf.audio_volume = 85;
    conf.audio_vol_sq1 = 85;
    conf.audio_vol_sq2 = 85;
    conf.audio_vol_tri = 85;
    conf.audio_vol_noise = 85;
    conf.audio_vol_dpcm = 85;
    conf.audio_vol_fds = 85;
    conf.audio_vol_mmc5 = 85;
    conf.audio_vol_vrc6 = 85;
    conf.audio_vol_vrc7 = 85;
    conf.audio_vol_n163 = 85;
    conf.audio_vol_s5b = 85;

    // Override audio frequency from config
    int audio_freq = pMain->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->getInteger();
    if (audio_freq > 0) {
        conf.audio_sample_rate = audio_freq;
    }

    // Timing
    conf.timing_speed = 60;
    conf.timing_ffspeed = 3;
    conf.timing_turbopulse = 1;

    // Misc
    conf.misc_default_system = 0;
    conf.misc_soft_patching = true;
    conf.misc_genie_distortion = false;
    conf.misc_disable_cursor = false;
    conf.misc_disable_cursor_special = false;
    conf.misc_config_pause = false;
    conf.misc_power_state = 0;
    conf.misc_homebrew_exit = -1;
    conf.misc_homebrew_stdout = -1;
    conf.misc_homebrew_stderr = -1;
}

// NESTOPIA CORE INIT
int NestopiaVitaUiEmu::nestopia_core_init(const char *rom_path) {
    // Set up directories
    std::string data_path = pMain->getIo()->getDataPath();
    strncpy(nstpaths.nstdir, data_path.c_str(), sizeof(nstpaths.nstdir));
    strncpy(nstpaths.nstconfdir, data_path.c_str(), sizeof(nstpaths.nstconfdir));
    snprintf(nstpaths.palettepath, sizeof(nstpaths.palettepath), "%s%s", nstpaths.nstdir, "custom.pal");

    // Create directories
    pMain->getIo()->create(data_path);
    pMain->getIo()->create(data_path + "save");
    pMain->getIo()->create(data_path + "state");
    pMain->getIo()->create(data_path + "cheats");
    pMain->getIo()->create(data_path + "screenshots");
    pMain->getIo()->create(data_path + "samples");

    // Set up callbacks
    nst_set_callbacks();

    nst_input_turbo_init();

    // Set archive handler function pointer for ZIP/7z support
    nst_archive_select = &nst_archive_select_file;
    printf("nestopia: archive handler set (libarchive)\n");

    // Set the video dimensions
    video_set_dimensions();

    // Initialize and load FDS BIOS and NstDatabase.xml
    nst_fds_bios_load();
    nst_db_load();

    if (!nst_load(rom_path)) {
        return -1;
    }

    nst_video_set_dimensions_screen(nst_video_get_dimensions_screen());

    // Set play in motion
    nst_play();

    return 0;
}

int nestopia_state_load(const char *path) {
    int ret = -1;
    std::filebuf fb;

    if (fb.open(path, std::ios::in)) {
        std::istream is(&fb);
        Nes::Api::Machine machine(emulator);
        ret = machine.LoadState(is);
        fb.close();
    }

    return ret;
}

int nestopia_state_save(const char *path) {
    int ret;
    std::ofstream ofs;

    ofs.open(path, std::ofstream::out | std::ofstream::binary);
    Nes::Api::Machine machine(emulator);
    ret = machine.SaveState(ofs, Nes::Api::Machine::NO_COMPRESSION);
    ofs.close();

    return ret;
}

void nst_input_turbo_pulse(Nes::Api::Input::Controllers *controllers) {
    (void)controllers;
}

void nst_input_turbo_init() {}

void nst_input_init() {}
