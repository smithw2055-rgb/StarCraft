#ifdef _MSC_VER
#pragma warning(push, 0)
#pragma warning(disable: 4701)
#endif
#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio/miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    void DecodeOgg(const char* Path)
    {
        ma_decoder_config Config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_decoder Decoder;
        ma_result Result = ma_decoder_init_file(Path, &Config, &Decoder);
        Require(Result == MA_SUCCESS, "campaign ogg opens with miniaudio/stb_vorbis");

        ma_format Format = ma_format_unknown;
        ma_uint32 Channels = 0;
        ma_uint32 SampleRate = 0;
        Result = ma_decoder_get_data_format(&Decoder, &Format, &Channels, &SampleRate, nullptr, 0);
        Require(Result == MA_SUCCESS, "campaign ogg exposes decoded format");
        Require(Format == ma_format_f32, "campaign ogg decodes to f32");
        Require(Channels > 0, "campaign ogg has at least one channel");
        Require(SampleRate > 0, "campaign ogg has a sample rate");

        float Frames[4096] = {};
        ma_uint64 FramesRead = 0;
        Result = ma_decoder_read_pcm_frames(&Decoder, Frames, 256, &FramesRead);
        Require(Result == MA_SUCCESS || Result == MA_AT_END, "campaign ogg reads pcm frames");
        Require(FramesRead > 0, "campaign ogg returns audio frames");

        ma_decoder_uninit(&Decoder);
    }
}

int main()
{
    DecodeOgg("assets/locales/zhCN/Assets/campaign/EXPTerran/Terran02/staredit/wav/t2m00stu.ogg");
    DecodeOgg("assets/locales/zhCN/Assets/campaign/EXPTerran/Terran02/staredit/wav/tdrtra01.ogg");
    std::printf("OggVorbisDecodeTest passed\n");
    return 0;
}
