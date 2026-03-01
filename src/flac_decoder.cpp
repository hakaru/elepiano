#include "flac_decoder.hpp"
#include <FLAC/stream_decoder.h>
#include <memory>
#include <stdexcept>
#include <cstdio>

// ============================================================
// FLAC デコードコールバック
// ============================================================
struct FlacCtx {
    std::vector<float>* pcm;
    int channels        = 1;
    int bits_per_sample = 24;
    int sample_rate     = 44100;
    const char* path    = nullptr;
};

static FLAC__StreamDecoderWriteStatus write_cb(
    const FLAC__StreamDecoder*,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data)
{
    auto* ctx = static_cast<FlacCtx*>(client_data);
    const float scale = 1.0f / static_cast<float>(1 << (ctx->bits_per_sample - 1));
    const int   n     = static_cast<int>(frame->header.blocksize);
    ctx->pcm->reserve(ctx->pcm->size() + n);
    for (int i = 0; i < n; ++i) {
        ctx->pcm->push_back(buffer[0][i] * scale);
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void meta_cb(
    const FLAC__StreamDecoder*,
    const FLAC__StreamMetadata* meta,
    void* client_data)
{
    auto* ctx = static_cast<FlacCtx*>(client_data);
    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->channels        = static_cast<int>(meta->data.stream_info.channels);
        ctx->bits_per_sample = static_cast<int>(meta->data.stream_info.bits_per_sample);
        ctx->sample_rate     = static_cast<int>(meta->data.stream_info.sample_rate);
    }
}

static void error_cb(
    const FLAC__StreamDecoder*,
    FLAC__StreamDecoderErrorStatus status,
    void* client_data)
{
    // SpCA フレーム0 の CRC 不一致は想定内
    if (status != FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC &&
        status != FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER) {
        auto* ctx = static_cast<FlacCtx*>(client_data);
        fprintf(stderr, "[FlacDecoder] error in %s: %d\n",
                ctx->path ? ctx->path : "(unknown)", static_cast<int>(status));
    }
}

// ============================================================
// RAII ラッパー
// ============================================================
struct FlacDecoderDeleter {
    void operator()(FLAC__StreamDecoder* d) const {
        if (d) {
            FLAC__stream_decoder_finish(d);
            FLAC__stream_decoder_delete(d);
        }
    }
};
using FlacDecoderPtr = std::unique_ptr<FLAC__StreamDecoder, FlacDecoderDeleter>;

// ============================================================
// 公開 API
// ============================================================
DecodedAudio decode_flac_file(const std::string& path)
{
    DecodedAudio result;
    FlacCtx ctx;
    ctx.pcm  = &result.pcm;
    ctx.path = path.c_str();

    FlacDecoderPtr dec(FLAC__stream_decoder_new());
    if (!dec) throw std::runtime_error("FLAC__stream_decoder_new 失敗");

    FLAC__stream_decoder_set_md5_checking(dec.get(), false);

    auto init_status = FLAC__stream_decoder_init_file(
        dec.get(), path.c_str(), write_cb, meta_cb, error_cb, &ctx);
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        throw std::runtime_error("FLAC init 失敗: " + path);
    }

    FLAC__stream_decoder_process_until_end_of_stream(dec.get());
    // dec は FlacDecoderDeleter により自動で finish + delete

    if (result.pcm.empty()) {
        throw std::runtime_error("FLAC デコード結果が空: " + path);
    }

    result.sample_rate     = ctx.sample_rate;
    result.num_channels    = ctx.channels;
    result.bits_per_sample = ctx.bits_per_sample;
    return result;
}
