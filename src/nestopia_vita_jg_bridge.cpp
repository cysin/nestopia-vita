#include "nestopia_vita_jg_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

#include <archive.h>
#include <archive_entry.h>

#include "jg/jg.h"
#include "nestopia_vita_ui_emu.h"
#include "rgui_cheats.h"
#include "runtime/runtime.h"

using namespace c2d;
using namespace pemu;

namespace {

constexpr const char *kCorePath = "app0:";
constexpr const char *kSaveDirName = "save";
constexpr const char *kSamplesDirName = "samples";
constexpr const char *kScreenshotsDirName = "screenshots";
constexpr const char *kStatesDirName = "states";
constexpr size_t kArchiveReadChunk = 16384;

void *g_bridge_impl = nullptr;

std::string trim_message(const char *message) {
    std::string value = message ? message : "";
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string basename_from_path(const std::string &path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string stem_from_path(const std::string &path) {
    std::string base = basename_from_path(path);
    const size_t pos = base.find_last_of('.');
    return pos == std::string::npos ? base : base.substr(0, pos);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string extension_from_path(const std::string &path) {
    const std::string base = basename_from_path(path);
    const size_t pos = base.find_last_of('.');
    return pos == std::string::npos ? std::string{} : lowercase(base.substr(pos));
}

bool is_supported_rom_ext(const std::string &path) {
    const std::string ext = extension_from_path(path);
    return ext == ".nes" || ext == ".nez" || ext == ".unf" || ext == ".unif" ||
           ext == ".fds" || ext == ".nsf" || ext == ".xml" || ext == ".bin";
}

bool is_archive_ext(const std::string &path) {
    const std::string ext = extension_from_path(path);
    return ext == ".zip" || ext == ".7z" || ext == ".gz" || ext == ".bz2" ||
           ext == ".xz" || ext == ".rar";
}

bool read_file_to_vector(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        out.clear();
        return false;
    }

    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(out.data()), size);
    return file.good();
}

bool load_archive_first_rom(const std::string &path, std::vector<uint8_t> &out) {
    archive *arc = archive_read_new();
    archive_read_support_filter_all(arc);
    archive_read_support_format_all(arc);
    archive_read_support_format_raw(arc);

    if (archive_read_open_filename(arc, path.c_str(), kArchiveReadChunk) != ARCHIVE_OK) {
        archive_read_free(arc);
        return false;
    }

    archive_entry *entry = nullptr;
    bool loaded = false;

    while (archive_read_next_header(arc, &entry) == ARCHIVE_OK) {
        const char *entry_name = archive_entry_pathname(entry);
        const std::string current_name = entry_name ? entry_name : "";
        const bool direct_stream = current_name == "data" && !archive_entry_size_is_set(entry);

        if (!direct_stream && !is_supported_rom_ext(current_name)) {
            archive_read_data_skip(arc);
            continue;
        }

        out.clear();

        std::vector<uint8_t> chunk(kArchiveReadChunk);
        la_ssize_t read_size = 0;
        while ((read_size = archive_read_data(arc, chunk.data(), chunk.size())) > 0) {
            out.insert(out.end(), chunk.begin(), chunk.begin() + read_size);
        }

        loaded = read_size >= 0 && !out.empty();
        break;
    }

    archive_read_free(arc);
    return loaded;
}

struct PortState {
    const jg_inputinfo_t *info = nullptr;
    jg_inputstate_t state{};
    std::vector<int16_t> axes;
    std::vector<uint8_t> buttons;
    std::vector<int32_t> coords;
    std::vector<int32_t> rel;
};

int find_button_index(const PortState &port, const char *name) {
    if (!port.info || !port.info->defs || port.info->numbuttons <= 0) {
        return -1;
    }

    for (int i = 0; i < port.info->numbuttons; ++i) {
        const char *def = port.info->defs[port.info->numaxes + i];
        if (def && std::strcmp(def, name) == 0) {
            return i;
        }
    }

    return -1;
}

void set_button(PortState &port, const char *name, bool pressed) {
    const int idx = find_button_index(port, name);
    if (idx >= 0 && idx < static_cast<int>(port.buttons.size())) {
        port.buttons[idx] = pressed ? 1 : 0;
    }
}

bool is_trigger_axis_binding(int value) {
    return value == (SDL_CONTROLLER_AXIS_TRIGGERLEFT + 100) ||
           value == (SDL_CONTROLLER_AXIS_TRIGGERRIGHT + 100);
}

bool is_raw_binding_pressed(const Input::Player &player, int binding) {
    if (!player.data || binding == SDL_CONTROLLER_BUTTON_INVALID) {
        return false;
    }

    auto *pad = static_cast<SDL_GameController *>(player.data);
    if (is_trigger_axis_binding(binding)) {
        return SDL_GameControllerGetAxis(pad, static_cast<SDL_GameControllerAxis>(binding - 100)) > player.dz;
    }

    return SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(binding)) > 0;
}

} // namespace

struct NestopiaVitaCoreBridge::Impl {
    explicit Impl(NestopiaVitaUiEmu *owner)
        : emu(owner) {}

    NestopiaVitaUiEmu *emu = nullptr;
    std::vector<PortState> ports;
    std::vector<uint8_t> game_data;
    std::vector<uint32_t> video_buffer;
    std::vector<int16_t> audio_buffer;
    std::string full_path;
    std::string game_name;
    std::string game_fname;
    std::string data_path;
    std::string save_path;
    std::string last_error;
    int audio_rate = 48000;
    float target_fps = 60.0f;
    int rewind_state = JG_REWIND_STOPPED;
    bool rewind_requested = false;
    bool rewind_blocked_until_release = true;
    bool initialized = false;
    bool loaded = false;

    void set_last_error(const std::string &message) {
        last_error = trim_message(message.c_str());
        if (last_error.empty()) {
            last_error = "Could not load ROM.";
        }
    }

    void ensure_directories() {
        auto *io = emu->getUi()->getIo();
        data_path = io->getDataPath();
        save_path = data_path + kSaveDirName;

        io->create(data_path);
        io->create(data_path + "configs");
        io->create(data_path + "saves");
        io->create(data_path + kSaveDirName);
        io->create(data_path + kStatesDirName);
        io->create(data_path + "roms");
        io->create(data_path + "cheats");
        io->create(data_path + kScreenshotsDirName);
        io->create(data_path + kSamplesDirName);
    }

    void configure_paths() {
        jg_pathinfo_t paths{};
        paths.base = data_path.c_str();
        paths.user = data_path.c_str();
        paths.bios = data_path.c_str();
        paths.save = save_path.c_str();
        paths.core = kCorePath;
        jg_set_paths(paths);
    }

    void prepare_ports() {
        ports.clear();
        jg_coreinfo_t *coreinfo = jg_get_coreinfo("nes");
        if (!coreinfo) {
            return;
        }

        ports.resize(coreinfo->numinputs);
        for (size_t i = 0; i < ports.size(); ++i) {
            jg_set_inputstate(&ports[i].state, static_cast<int>(i));
        }
    }

    int get_rewind_binding() const {
        auto *opt = emu->getUi()->getConfig()->get(PEMUConfig::OptId::JOY_REWIND, false);
        return opt ? opt->getInteger() : KEY_JOY_LT_DEFAULT;
    }

    void on_rewind_state_changed(int state) {
        rewind_state = state;
        if (state == JG_REWIND_STOPPED) {
            rewind_requested = false;
        }
    }

    void configure_rewind() {
        rewind_requested = false;
        rewind_blocked_until_release = true;
        rewind_state = JG_REWIND_STOPPED;
        if (jg_rewind_enable(1)) {
            jg_rewind_enable_sound(0);
            jg_rewind_set_speed(1);
            printf("nestopia: rewind enabled (2x speed)\n");
        } else {
            printf("nestopia: rewind enable failed\n");
        }
    }

    void set_rewind_active(bool active) {
        if (!loaded) {
            rewind_requested = false;
            return;
        }

        if (active == rewind_requested && (active || rewind_state == JG_REWIND_STOPPED)) {
            return;
        }

        if (jg_rewind_set_direction(active ? JG_REWIND_BACKWARD : JG_REWIND_FORWARD)) {
            rewind_requested = active;
        } else if (!active) {
            rewind_requested = false;
        }
    }

    void suspend_hotkeys_until_release() {
        set_rewind_active(false);
        rewind_blocked_until_release = true;
    }

    bool should_rewind(const Input::Player &player) {
        bool pressed = is_raw_binding_pressed(player, get_rewind_binding());
        if (rewind_blocked_until_release) {
            if (!pressed) {
                rewind_blocked_until_release = false;
            }
            pressed = false;
        }

        set_rewind_active(pressed);
        return rewind_requested || rewind_state != JG_REWIND_STOPPED;
    }

    bool reset_backend() {
        if (loaded) {
            jg_game_unload();
            loaded = false;
        }

        if (initialized) {
            jg_deinit();
            initialized = false;
        }

        ensure_directories();

        audio_rate = emu->getUi()->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->getInteger();
        if (audio_rate <= 0) {
            audio_rate = 48000;
        }

        jg_audioinfo_t *audioinfo = jg_get_audioinfo();
        audioinfo->rate = static_cast<unsigned>(audio_rate);
        audioinfo->channels = 2;
        audioinfo->spf = static_cast<unsigned>(audio_rate / 60);
        audioinfo->buf = nullptr;

        configure_paths();

        g_bridge_impl = this;
        jg_set_cb_audio(&Impl::audio_callback);
        jg_set_cb_frametime(&Impl::frametime_callback);
        jg_set_cb_log(&Impl::log_callback);
        jg_set_cb_rewind(&Impl::rewind_callback);

        if (!jg_init()) {
            set_last_error("Failed to initialize Nestopia core.");
            g_bridge_impl = nullptr;
            return false;
        }

        jg_videoinfo_t *videoinfo = jg_get_videoinfo();
        video_buffer.assign(static_cast<size_t>(videoinfo->wmax) * videoinfo->hmax, 0);
        videoinfo->buf = video_buffer.data();

        initialized = true;
        return true;
    }

    bool read_game_image() {
        game_data.clear();
        last_error.clear();

        if (is_archive_ext(full_path)) {
            if (!load_archive_first_rom(full_path, game_data)) {
                set_last_error("Archive did not contain a supported NES image.");
                return false;
            }
            return true;
        }

        if (!read_file_to_vector(full_path, game_data)) {
            set_last_error("Could not read ROM data.");
            return false;
        }

        return true;
    }

    void allocate_inputs() {
        for (size_t i = 0; i < ports.size(); ++i) {
            PortState &port = ports[i];
            port.info = jg_get_inputinfo(static_cast<int>(i));
            if (!port.info) {
                continue;
            }

            port.axes.assign(std::max(0, port.info->numaxes), 0);
            port.buttons.assign(std::max(0, port.info->numbuttons), 0);
            port.coords.assign(3, 0);
            port.rel.assign(2, 0);

            port.state.axis = port.axes.empty() ? nullptr : port.axes.data();
            port.state.button = port.buttons.empty() ? nullptr : port.buttons.data();
            port.state.coord = port.coords.data();
            port.state.rel = port.rel.data();
        }
    }

    void clear_inputs() {
        for (PortState &port : ports) {
            std::fill(port.axes.begin(), port.axes.end(), 0);
            std::fill(port.buttons.begin(), port.buttons.end(), 0);
            std::fill(port.coords.begin(), port.coords.end(), 0);
            std::fill(port.rel.begin(), port.rel.end(), 0);
        }
    }

    void build_video_surface() {
        jg_videoinfo_t *videoinfo = jg_get_videoinfo();
        const Vector2i size(static_cast<int>(videoinfo->w), static_cast<int>(videoinfo->h));
        const int aspect_width = std::max(1, static_cast<int>(videoinfo->aspect * 1000.0));
        emu->addVideo(nullptr, nullptr, size, {aspect_width, 1000}, Texture::Format::RGB565);
        upload_video_frame();
    }

    void configure_audio() {
        jg_audioinfo_t *audioinfo = jg_get_audioinfo();
        audio_buffer.assign(static_cast<size_t>(audioinfo->spf) * audioinfo->channels, 0);
        audioinfo->buf = audio_buffer.data();
        emu->addAudio(static_cast<int>(audioinfo->rate), static_cast<int>(audioinfo->spf));
        jg_setup_audio();
    }

    void upload_video_frame() {
        if (!emu->getVideo()) {
            return;
        }

        jg_videoinfo_t *videoinfo = jg_get_videoinfo();
        if (!videoinfo || !videoinfo->buf || videoinfo->w == 0 || videoinfo->h == 0) {
            return;
        }

        uint8_t *dst_pixels = nullptr;
        int pitch = 0;
        emu->getVideo()->lock(&dst_pixels, &pitch, emu->getVideo()->getTextureRect());

        auto *dst = reinterpret_cast<uint16_t *>(dst_pixels);
        const auto *src = static_cast<const uint32_t *>(videoinfo->buf);
        const unsigned src_pitch = videoinfo->p;

        for (unsigned y = 0; y < videoinfo->h; ++y) {
            uint16_t *dst_row = reinterpret_cast<uint16_t *>(dst_pixels + y * pitch);
            const uint32_t *src_row = src + ((videoinfo->y + y) * src_pitch) + videoinfo->x;
            for (unsigned x = 0; x < videoinfo->w; ++x) {
                const uint32_t pixel = src_row[x];
                const uint16_t r = static_cast<uint16_t>((pixel >> 16) & 0xFF);
                const uint16_t g = static_cast<uint16_t>((pixel >> 8) & 0xFF);
                const uint16_t b = static_cast<uint16_t>(pixel & 0xFF);
                dst_row[x] = static_cast<uint16_t>(((r & 0xF8) << 8) |
                                                   ((g & 0xFC) << 3) |
                                                   (b >> 3));
            }
        }

        emu->getVideo()->unlock();
    }

    void push_pad_state(const Input::Player &player, PortState &port, unsigned int turbo_counter) {
        if (!port.info || port.info->type != JG_INPUT_CONTROLLER || port.buttons.empty()) {
            return;
        }

        const bool turbo_pulse = (turbo_counter % 3U) == 0U;
        const bool press_a = (player.buttons & Input::Button::B) != 0;
        const bool press_b = (player.buttons & Input::Button::A) != 0;
        const bool press_turbo_a = (player.buttons & Input::Button::Y) != 0;
        const bool press_turbo_b = (player.buttons & Input::Button::X) != 0;

        set_button(port, "Up", (player.buttons & Input::Button::Up) != 0);
        set_button(port, "Down", (player.buttons & Input::Button::Down) != 0);
        set_button(port, "Left", (player.buttons & Input::Button::Left) != 0);
        set_button(port, "Right", (player.buttons & Input::Button::Right) != 0);
        set_button(port, "Select", (player.buttons & Input::Button::Select) != 0);
        set_button(port, "Start", (player.buttons & Input::Button::Start) != 0);
        set_button(port, "A", press_a || (press_turbo_a && turbo_pulse));
        set_button(port, "B", press_b || (press_turbo_b && turbo_pulse));
        set_button(port, "TurboA", false);
        set_button(port, "TurboB", false);
    }

    void exec_frame(Input::Player *players, unsigned int turbo_counter) {
        if (!loaded) {
            return;
        }

        clear_inputs();
        const bool rewinding = players && should_rewind(players[0]);
        if (!rewinding) {
            const size_t player_count = std::min(ports.size(), static_cast<size_t>(PLAYER_MAX));
            for (size_t i = 0; i < player_count; ++i) {
                push_pad_state(players[i], ports[i], turbo_counter);
            }
        }

        jg_exec_frame();
        upload_video_frame();
    }

    static Impl *active_impl() {
        return static_cast<Impl *>(g_bridge_impl);
    }

    static void frametime_callback(double fps) {
        if (Impl *impl = active_impl()) {
            impl->target_fps = static_cast<float>(fps);
        }
    }

    static void rewind_callback(int state) {
        if (Impl *impl = active_impl()) {
            impl->on_rewind_state_changed(state);
        }
    }

    static void audio_callback(size_t samples) {
        Impl *impl = active_impl();
        if (!impl || !impl->emu->getAudio() || impl->audio_buffer.empty()) {
            return;
        }

        if (impl->rewind_state == JG_REWIND_PREPARING) {
            return;
        }

        // Clamp to buffer capacity to prevent overread
        const size_t max_samples = impl->audio_buffer.size() / 2;
        if (samples > max_samples) {
            samples = max_samples;
        }

        const Audio::SyncMode sync_mode =
            (impl->rewind_state == JG_REWIND_STOPPED && impl->target_fps < 55.0f)
                ? Audio::SyncMode::LowLatency
                : Audio::SyncMode::None;
        impl->emu->getAudio()->play(impl->audio_buffer.data(), static_cast<int>(samples),
                                    sync_mode);
    }

    static void log_callback(int level, const char *format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        std::fputs(buffer, stderr);
        if (level == JG_LOG_ERR) {
            if (Impl *impl = active_impl()) {
                impl->set_last_error(buffer);
            }
        }
    }
};

NestopiaVitaCoreBridge::NestopiaVitaCoreBridge(NestopiaVitaUiEmu *emu)
    : m_impl(std::make_unique<Impl>(emu)) {
}

NestopiaVitaCoreBridge::~NestopiaVitaCoreBridge() {
    unload();
    if (m_impl->initialized) {
        jg_deinit();
        m_impl->initialized = false;
    }
    if (g_bridge_impl == m_impl.get()) {
        g_bridge_impl = nullptr;
    }
}

int NestopiaVitaCoreBridge::load(const std::string &full_path) {
    m_impl->full_path = full_path;
    m_impl->game_name = stem_from_path(full_path);
    m_impl->game_fname = basename_from_path(full_path);
    m_impl->last_error.clear();

    if (!m_impl->initialized ||
        m_impl->audio_rate != m_impl->emu->getUi()->getConfig()->get(PEMUConfig::OptId::EMU_AUDIO_FREQ, true)->getInteger()) {
        if (!m_impl->reset_backend()) {
            return -1;
        }
    }

    m_impl->prepare_ports();

    if (!m_impl->read_game_image()) {
        return -1;
    }

    jg_fileinfo_t info{};
    info.data = m_impl->game_data.data();
    info.size = m_impl->game_data.size();
    info.path = m_impl->full_path.c_str();
    info.name = m_impl->game_name.c_str();
    info.fname = m_impl->game_fname.c_str();
    jg_set_gameinfo(info);

    if (!jg_game_load()) {
        return -1;
    }

    m_impl->loaded = true;
    m_impl->allocate_inputs();
    jg_setup_video();
    m_impl->build_video_surface();
    m_impl->configure_audio();
    m_impl->configure_rewind();
    return 0;
}

void NestopiaVitaCoreBridge::unload() {
    if (!m_impl->loaded) {
        return;
    }

    m_impl->suspend_hotkeys_until_release();
    m_impl->rewind_state = JG_REWIND_STOPPED;
    jg_game_unload();
    m_impl->loaded = false;
    m_impl->rewind_requested = false;
    m_impl->game_data.clear();
    m_impl->ports.clear();
    m_impl->audio_buffer.clear();
}

void NestopiaVitaCoreBridge::execFrame(c2d::Input::Player *players, unsigned int turbo_counter) {
    m_impl->exec_frame(players, turbo_counter);
}

int NestopiaVitaCoreBridge::saveState(const char *path) {
    if (!m_impl->loaded) {
        return -1;
    }

    const int result = jg_state_save(path);
    return result == 1 ? 0 : -1;
}

int NestopiaVitaCoreBridge::loadState(const char *path) {
    if (!m_impl->loaded) {
        return -1;
    }

    const int result = jg_state_load(path);
    if (result == 1) {
        jg_rewind_reset();
        m_impl->rewind_requested = false;
        m_impl->rewind_state = JG_REWIND_STOPPED;
        m_impl->rewind_blocked_until_release = true;
        m_impl->upload_video_frame();
        return 0;
    }
    return -1;
}

void NestopiaVitaCoreBridge::applyCheats(const std::vector<NestopiaCheat> &cheats) {
    if (!m_impl->loaded) {
        return;
    }

    jg_cheat_clear();
    for (const auto &cheat : cheats) {
        if (cheat.enabled && !cheat.gg_code.empty()) {
            jg_cheat_set(cheat.gg_code.c_str());
        }
    }
}

void NestopiaVitaCoreBridge::suspendHotkeysUntilRelease() {
    m_impl->suspend_hotkeys_until_release();
}

void NestopiaVitaCoreBridge::stopRewind() {
    m_impl->set_rewind_active(false);
}

float NestopiaVitaCoreBridge::getTargetFps() const {
    return m_impl->target_fps;
}

bool NestopiaVitaCoreBridge::isPal() const {
    return m_impl->target_fps < 55.0f;
}

const std::string &NestopiaVitaCoreBridge::getLastError() const {
    return m_impl->last_error;
}
