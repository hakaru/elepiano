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
    const LilvPlugin* plugin = nullptr;
    LilvInstance* instance = nullptr;

    uint32_t in_l = UINT32_MAX;
    uint32_t in_r = UINT32_MAX;
    uint32_t out_l = UINT32_MAX;
    uint32_t out_r = UINT32_MAX;

    std::vector<uint32_t> control_ports;
    std::vector<float> control_values;
    std::unordered_map<int, uint32_t> cc_to_control_port;

    std::vector<float> in_l_buf;
    std::vector<float> in_r_buf;
    std::vector<float> out_l_buf;
    std::vector<float> out_r_buf;

    LilvNode* n_audio_port = nullptr;
    LilvNode* n_input_port = nullptr;
    LilvNode* n_output_port = nullptr;
    LilvNode* n_control_port = nullptr;

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

    bool init_from_env() {
        const char* uri = std::getenv("ELEPIANO_LV2_URI");
        if (!uri || !*uri) return false;

        if (const char* wet_env = std::getenv("ELEPIANO_LV2_WET")) {
            wet = std::clamp(std::atof(wet_env), 0.0, 1.0);
        }
        parse_cc_map(std::getenv("ELEPIANO_LV2_CC_MAP"));

        world = lilv_world_new();
        if (!world) return false;
        lilv_world_load_all(world);

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
            if (lilv_plugin_has_port_with_class(plugin, i, n_audio_port, n_input_port)) {
                if (in_l == UINT32_MAX) in_l = i;
                else if (in_r == UINT32_MAX) in_r = i;
            } else if (lilv_plugin_has_port_with_class(plugin, i, n_audio_port, n_output_port)) {
                if (out_l == UINT32_MAX) out_l = i;
                else if (out_r == UINT32_MAX) out_r = i;
            } else if (lilv_plugin_has_port_with_class(plugin, i, n_control_port)) {
                control_ports.push_back(i);
            }
        }

        if (in_l == UINT32_MAX || out_l == UINT32_MAX) {
            std::fprintf(stderr, "[LV2] plugin needs at least one audio in/out port\n");
            return false;
        }
        if (in_r == UINT32_MAX) in_r = in_l;
        if (out_r == UINT32_MAX) out_r = out_l;

        control_values.assign(num_ports, 0.0f);
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
        lilv_instance_connect_port(instance, in_r, in_r_buf.data());
        lilv_instance_connect_port(instance, out_l, out_l_buf.data());
        lilv_instance_connect_port(instance, out_r, out_r_buf.data());
        for (uint32_t p : control_ports) {
            lilv_instance_connect_port(instance, p, &control_values[p]);
        }

        lilv_instance_activate(instance);
        std::fprintf(stderr, "[LV2] loaded: %s\n", uri);
        return true;
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
        if (world) {
            lilv_world_free(world);
            world = nullptr;
        }
    }
};

Lv2Host::Lv2Host(int sample_rate, int max_block_frames) : p_(new Impl()) {
    p_->sample_rate = sample_rate;
    p_->max_block = std::max(64, max_block_frames);
}

Lv2Host::~Lv2Host() {
    if (p_) {
        p_->shutdown();
        delete p_;
    }
}

bool Lv2Host::initialize_from_env() {
    return p_ && p_->init_from_env();
}

void Lv2Host::set_cc(int cc, float normalized) {
    if (!p_ || !p_->instance) return;
    auto it = p_->cc_to_control_port.find(cc);
    if (it == p_->cc_to_control_port.end()) return;
    uint32_t port = it->second;
    if (port >= p_->control_values.size()) return;
    p_->control_values[port] = std::clamp(normalized, 0.0f, 1.0f);
}

void Lv2Host::process(float* interleaved_stereo, int frames) {
    if (!p_ || !p_->instance || frames <= 0) return;
    if (frames > p_->max_block) frames = p_->max_block;

    for (int i = 0; i < frames; ++i) {
        p_->in_l_buf[i] = interleaved_stereo[i * 2 + 0];
        p_->in_r_buf[i] = interleaved_stereo[i * 2 + 1];
    }

    lilv_instance_run(p_->instance, static_cast<uint32_t>(frames));

    const float wet = p_->wet;
    const float dry = 1.0f - wet;
    for (int i = 0; i < frames; ++i) {
        const float l = interleaved_stereo[i * 2 + 0];
        const float r = interleaved_stereo[i * 2 + 1];
        interleaved_stereo[i * 2 + 0] = l * dry + p_->out_l_buf[i] * wet;
        interleaved_stereo[i * 2 + 1] = r * dry + p_->out_r_buf[i] * wet;
    }
}

#endif
