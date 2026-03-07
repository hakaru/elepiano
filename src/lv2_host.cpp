#ifdef ELEPIANO_ENABLE_LV2

#include "lv2_host.hpp"

#include <lilv/lilv.h>
#include <lv2/core/lv2.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unordered_map>

struct Lv2Host::Impl {
    int sample_rate = 44100;
    int max_block = 8192;
    float wet = 1.0f;

    LilvWorld* world = nullptr;
    bool owns_world = false;      // true if we created the world ourselves
    const LilvPlugin* plugin = nullptr;
    LilvInstance* instance = nullptr;

    uint32_t in_l = UINT32_MAX;
    uint32_t in_r = UINT32_MAX;
    uint32_t out_l = UINT32_MAX;
    uint32_t out_r = UINT32_MAX;
    bool mono_in = false;
    bool mono_out = false;

    struct ControlPortInfo {
        uint32_t index;
        float min_val;
        float max_val;
        float def_val;
    };
    std::vector<ControlPortInfo> control_ports;
    std::vector<float> control_values;
    std::unordered_map<int, uint32_t> cc_to_control_port; // cc -> port index

    std::vector<float> in_l_buf;
    std::vector<float> in_r_buf;
    std::vector<float> out_l_buf;
    std::vector<float> out_r_buf;

    LilvNode* n_audio_port = nullptr;
    LilvNode* n_input_port = nullptr;
    LilvNode* n_output_port = nullptr;
    LilvNode* n_control_port = nullptr;

    // Format: "cc=port,cc=port,..."  e.g. "70=2,71=1"
    bool parse_cc_map(const char* s) {
        if (!s || !*s) return true;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto eq = item.find('=');
            if (eq == std::string::npos) continue;
            int cc = std::atoi(item.substr(0, eq).c_str());
            int port = std::atoi(item.substr(eq + 1).c_str());
            if (cc < 0 || cc > 127 || port < 0) continue;
            cc_to_control_port[cc] = static_cast<uint32_t>(port);
        }
        return true;
    }

    // Override control port default values. Format: "port=value,port=value,..."
    // e.g. "0=0.0,2=0.8" sets port 0 to 0.0 and port 2 to 0.8
    std::unordered_map<uint32_t, float> port_overrides;

    void parse_defaults(const char* s) {
        if (!s || !*s) return;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto eq = item.find('=');
            if (eq == std::string::npos) continue;
            uint32_t port = static_cast<uint32_t>(std::atoi(item.substr(0, eq).c_str()));
            float val = static_cast<float>(std::atof(item.substr(eq + 1).c_str()));
            port_overrides[port] = val;
        }
    }

    bool init(const char* uri, const char* cc_map_str, float wet_val,
              LilvWorld* shared_world, const char* defaults_str = nullptr) {
        if (!uri || !*uri) return false;

        wet = std::clamp(static_cast<double>(wet_val), 0.0, 1.0);
        parse_cc_map(cc_map_str);
        parse_defaults(defaults_str);

        if (shared_world) {
            world = shared_world;
            owns_world = false;
        } else {
            world = lilv_world_new();
            if (!world) return false;
            lilv_world_load_all(world);
            owns_world = true;
        }

        n_audio_port = lilv_new_uri(world, LV2_CORE__AudioPort);
        n_input_port = lilv_new_uri(world, LV2_CORE__InputPort);
        n_output_port = lilv_new_uri(world, LV2_CORE__OutputPort);
        n_control_port = lilv_new_uri(world, LV2_CORE__ControlPort);

        LilvNode* plugin_uri = lilv_new_uri(world, uri);
        const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
        plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
        lilv_node_free(plugin_uri);
        if (!plugin) {
            std::fprintf(stderr, "[LV2] plugin not found: %s\n", uri);
            return false;
        }

        const uint32_t num_ports = lilv_plugin_get_num_ports(plugin);
        for (uint32_t i = 0; i < num_ports; ++i) {
            const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
            if (lilv_port_is_a(plugin, port, n_audio_port) && lilv_port_is_a(plugin, port, n_input_port)) {
                if (in_l == UINT32_MAX) in_l = i;
                else if (in_r == UINT32_MAX) in_r = i;
            } else if (lilv_port_is_a(plugin, port, n_audio_port) && lilv_port_is_a(plugin, port, n_output_port)) {
                if (out_l == UINT32_MAX) out_l = i;
                else if (out_r == UINT32_MAX) out_r = i;
            } else if (lilv_port_is_a(plugin, port, n_control_port)) {
                ControlPortInfo info{i, 0.0f, 1.0f, 0.0f};
                LilvNode* def_node = nullptr;
                LilvNode* min_node = nullptr;
                LilvNode* max_node = nullptr;
                lilv_port_get_range(plugin, port, &def_node, &min_node, &max_node);
                if (min_node) { info.min_val = lilv_node_as_float(min_node); lilv_node_free(min_node); }
                if (max_node) { info.max_val = lilv_node_as_float(max_node); lilv_node_free(max_node); }
                if (def_node) { info.def_val = lilv_node_as_float(def_node); lilv_node_free(def_node); }
                else          { info.def_val = info.min_val; }
                control_ports.push_back(info);
            }
        }

        if (in_l == UINT32_MAX || out_l == UINT32_MAX) {
            std::fprintf(stderr, "[LV2] plugin needs at least one audio in/out port\n");
            return false;
        }
        mono_in  = (in_r == UINT32_MAX);
        mono_out = (out_r == UINT32_MAX);
        if (mono_in)  in_r = in_l;
        if (mono_out) out_r = out_l;

        // Initialize control values with defaults, then apply overrides
        control_values.assign(num_ports, 0.0f);
        for (const auto& cp : control_ports) {
            float val = cp.def_val;
            auto ov = port_overrides.find(cp.index);
            if (ov != port_overrides.end()) {
                val = std::clamp(ov->second, cp.min_val, cp.max_val);
                std::fprintf(stderr, "[LV2]   port %u: %.1f..%.1f (override %.1f)\n",
                             cp.index, cp.min_val, cp.max_val, val);
            } else {
                std::fprintf(stderr, "[LV2]   port %u: %.1f..%.1f (default %.1f)\n",
                             cp.index, cp.min_val, cp.max_val, val);
            }
            control_values[cp.index] = val;
        }

        instance = lilv_plugin_instantiate(plugin, static_cast<double>(sample_rate), nullptr);
        if (!instance) {
            std::fprintf(stderr, "[LV2] instantiate failed\n");
            return false;
        }

        in_l_buf.assign(max_block, 0.0f);
        in_r_buf.assign(max_block, 0.0f);
        out_l_buf.assign(max_block, 0.0f);
        out_r_buf.assign(max_block, 0.0f);

        lilv_instance_connect_port(instance, in_l, in_l_buf.data());
        if (!mono_in)
            lilv_instance_connect_port(instance, in_r, in_r_buf.data());
        lilv_instance_connect_port(instance, out_l, out_l_buf.data());
        if (!mono_out)
            lilv_instance_connect_port(instance, out_r, out_r_buf.data());
        for (const auto& cp : control_ports) {
            lilv_instance_connect_port(instance, cp.index, &control_values[cp.index]);
        }

        lilv_instance_activate(instance);
        std::fprintf(stderr, "[LV2] loaded: %s (%s in, %s out)\n", uri,
                     mono_in ? "mono" : "stereo", mono_out ? "mono" : "stereo");
        return true;
    }

    ~Impl() {
        shutdown();
    }

    void shutdown() {
        if (instance) {
            lilv_instance_deactivate(instance);
            lilv_instance_free(instance);
            instance = nullptr;
        }
        if (n_audio_port) lilv_node_free(n_audio_port);
        if (n_input_port) lilv_node_free(n_input_port);
        if (n_output_port) lilv_node_free(n_output_port);
        if (n_control_port) lilv_node_free(n_control_port);
        n_audio_port = n_input_port = n_output_port = n_control_port = nullptr;
        if (world && owns_world) {
            lilv_world_free(world);
        }
        world = nullptr;
    }
};

Lv2Host::Lv2Host(int sample_rate, int max_block_frames) : p_(std::make_unique<Impl>()) {
    p_->sample_rate = sample_rate;
    p_->max_block = std::max(64, max_block_frames);
}

Lv2Host::~Lv2Host() = default;

bool Lv2Host::initialize_from_env() {
    if (!p_) return false;
    const char* uri = std::getenv("ELEPIANO_LV2_URI");
    if (!uri || !*uri) return false;
    float wet_val = 1.0f;
    if (const char* w = std::getenv("ELEPIANO_LV2_WET"))
        wet_val = static_cast<float>(std::atof(w));
    return p_->init(uri, std::getenv("ELEPIANO_LV2_CC_MAP"), wet_val, nullptr);
}

bool Lv2Host::initialize(const std::string& uri, const char* cc_map, float wet,
                          LilvWorld* shared_world, const char* defaults) {
    if (!p_) return false;
    return p_->init(uri.c_str(), cc_map, wet, shared_world, defaults);
}

void Lv2Host::set_cc(int cc, float normalized) {
    if (!p_ || !p_->instance) return;
    auto it = p_->cc_to_control_port.find(cc);
    if (it == p_->cc_to_control_port.end()) return;
    uint32_t port_idx = it->second;
    if (port_idx >= p_->control_values.size()) return;

    // Find port range info and scale normalized 0-1 to port min..max
    float val = std::clamp(normalized, 0.0f, 1.0f);
    for (const auto& cp : p_->control_ports) {
        if (cp.index == port_idx) {
            val = cp.min_val + val * (cp.max_val - cp.min_val);
            break;
        }
    }
    p_->control_values[port_idx] = val;
}

void Lv2Host::process(float* interleaved_stereo, int frames) {
    if (!p_ || !p_->instance || frames <= 0) return;
    if (frames > p_->max_block) frames = p_->max_block;

    if (p_->mono_in) {
        // Mono plugin: sum L+R to mono input
        for (int i = 0; i < frames; ++i)
            p_->in_l_buf[i] = (interleaved_stereo[i * 2 + 0] + interleaved_stereo[i * 2 + 1]) * 0.5f;
    } else {
        for (int i = 0; i < frames; ++i) {
            p_->in_l_buf[i] = interleaved_stereo[i * 2 + 0];
            p_->in_r_buf[i] = interleaved_stereo[i * 2 + 1];
        }
    }

    lilv_instance_run(p_->instance, static_cast<uint32_t>(frames));

    const float wet = p_->wet;
    const float dry = 1.0f - wet;
    if (p_->mono_out) {
        // Mono output: copy to both channels
        for (int i = 0; i < frames; ++i) {
            const float in_l = interleaved_stereo[i * 2 + 0];
            const float in_r = interleaved_stereo[i * 2 + 1];
            const float out_mono = p_->out_l_buf[i];
            interleaved_stereo[i * 2 + 0] = in_l * dry + out_mono * wet;
            interleaved_stereo[i * 2 + 1] = in_r * dry + out_mono * wet;
        }
    } else {
        for (int i = 0; i < frames; ++i) {
            const float l = interleaved_stereo[i * 2 + 0];
            const float r = interleaved_stereo[i * 2 + 1];
            interleaved_stereo[i * 2 + 0] = l * dry + p_->out_l_buf[i] * wet;
            interleaved_stereo[i * 2 + 1] = r * dry + p_->out_r_buf[i] * wet;
        }
    }
}

#endif
