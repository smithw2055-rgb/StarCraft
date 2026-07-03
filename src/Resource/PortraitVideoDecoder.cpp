#include "Resource/PortraitVideoDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace Resource::Graphics
{

#ifdef _WIN32
namespace
{
    std::optional<std::string> NextPpmToken(const std::vector<uint8_t>& Bytes, size_t& Pos)
    {
        while (Pos < Bytes.size())
        {
            if (std::isspace(Bytes[Pos]))
            {
                ++Pos;
                continue;
            }
            if (Bytes[Pos] == '#')
            {
                while (Pos < Bytes.size() && Bytes[Pos] != '\n')
                    ++Pos;
                continue;
            }
            break;
        }

        if (Pos >= Bytes.size())
            return std::nullopt;

        const size_t Start = Pos;
        while (Pos < Bytes.size() && !std::isspace(Bytes[Pos]) && Bytes[Pos] != '#')
            ++Pos;
        return std::string((const char*)Bytes.data() + Start, Pos - Start);
    }

    void SkipPpmWhitespace(const std::vector<uint8_t>& Bytes, size_t& Pos)
    {
        while (Pos < Bytes.size())
        {
            if (std::isspace(Bytes[Pos]))
            {
                ++Pos;
                continue;
            }
            if (Bytes[Pos] == '#')
            {
                while (Pos < Bytes.size() && Bytes[Pos] != '\n')
                    ++Pos;
                continue;
            }
            break;
        }
    }
}
#endif

std::optional<std::vector<DecodedVideoFrame>> DecodeVideoFramesFromFile(const std::filesystem::path& Path)
{
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;

    const std::wstring WidePath = Path.wstring();
    if (WidePath.empty())
        return std::nullopt;

    ComPtr<IMFAttributes> Attributes;
    if (FAILED(MFCreateAttributes(&Attributes, 2)))
        return std::nullopt;
    Attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);

    ComPtr<IMFSourceReader> Reader;
    HRESULT Hr = MFCreateSourceReaderFromURL(WidePath.c_str(), Attributes.Get(), &Reader);
    if (FAILED(Hr))
        return std::nullopt;

    ComPtr<IMFMediaType> MediaType;
    Hr = MFCreateMediaType(&MediaType);
    if (FAILED(Hr))
        return std::nullopt;
    Hr = MediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(Hr))
        return std::nullopt;
    Hr = MediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(Hr))
        return std::nullopt;
    Hr = Reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, MediaType.Get());
    if (FAILED(Hr))
        return std::nullopt;

    ComPtr<IMFMediaType> CurrentType;
    Hr = Reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &CurrentType);
    if (FAILED(Hr) || !CurrentType)
        return std::nullopt;

    UINT32 Width = 0;
    UINT32 Height = 0;
    Hr = MFGetAttributeSize(CurrentType.Get(), MF_MT_FRAME_SIZE, &Width, &Height);
    if (FAILED(Hr) || Width == 0 || Height == 0)
        return std::nullopt;

    LONG Stride = (LONG)Width * 4;
    Hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, Width, &Stride);
    if (FAILED(Hr) || Stride == 0)
        Stride = (LONG)Width * 4;

    std::vector<DecodedVideoFrame> Frames;
    bool HavePendingFrame = false;
    DecodedVideoFrame PendingFrame = {};
    LONGLONG PendingTimestamp = 0;

    for (;;)
    {
        DWORD StreamIndex = 0;
        DWORD Flags = 0;
        LONGLONG Timestamp = 0;
        ComPtr<IMFSample> Sample;
        Hr = Reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                0,
                                &StreamIndex,
                                &Flags,
                                &Timestamp,
                                &Sample);
        if (FAILED(Hr))
            return std::nullopt;

        if (Flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;

        if (!Sample)
            continue;

        ComPtr<IMFMediaBuffer> Buffer;
        Hr = Sample->ConvertToContiguousBuffer(&Buffer);
        if (FAILED(Hr) || !Buffer)
            continue;

        BYTE* Data = nullptr;
        DWORD CurrentLength = 0;
        Hr = Buffer->Lock(&Data, nullptr, &CurrentLength);
        if (FAILED(Hr) || !Data)
            continue;

        DecodedVideoFrame Current = {};
        Current.Image.Width = (int)Width;
        Current.Image.Height = (int)Height;
        Current.Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);

        const int AbsStride = std::abs(Stride);
        for (UINT32 Y = 0; Y < Height; ++Y)
        {
            const BYTE* SrcRow = Data + (Stride >= 0
                ? (size_t)Y * (size_t)AbsStride
                : (size_t)(Height - 1u - Y) * (size_t)AbsStride);
            uint8_t* DstRow = Current.Image.Rgba.data() + (size_t)Y * (size_t)Width * 4u;
            for (UINT32 X = 0; X < Width; ++X)
            {
                const size_t Src = (size_t)X * 4u;
                const size_t Dst = (size_t)X * 4u;
                DstRow[Dst + 0] = SrcRow[Src + 2];
                DstRow[Dst + 1] = SrcRow[Src + 1];
                DstRow[Dst + 2] = SrcRow[Src + 0];
                DstRow[Dst + 3] = SrcRow[Src + 3];
            }
        }
        Buffer->Unlock();

        if (HavePendingFrame)
        {
            const float DeltaSeconds = (float)std::max<LONGLONG>(1, Timestamp - PendingTimestamp) / 10000000.0f;
            PendingFrame.DurationSeconds = std::clamp(DeltaSeconds, 1.0f / 30.0f, 0.25f);
            Frames.push_back(std::move(PendingFrame));
        }

        PendingTimestamp = Timestamp;
        PendingFrame = std::move(Current);
        HavePendingFrame = true;
    }

    if (HavePendingFrame)
        Frames.push_back(std::move(PendingFrame));

    if (Frames.empty())
        return std::nullopt;

    return Frames;
#else
    (void)Path;
    return std::nullopt;
#endif
}

std::optional<DecodedVideoFrame> DecodeFirstVideoFrameWithFfmpeg(const std::filesystem::path& Path)
{
#ifdef _WIN32
    std::string EscapedPath = Path.string();
    size_t Quote = 0;
    while ((Quote = EscapedPath.find('"', Quote)) != std::string::npos)
    {
        EscapedPath.insert(Quote, "\\");
        Quote += 2;
    }

    const std::string Command =
        "ffmpeg -v error -i \"" + EscapedPath + "\" -frames:v 1 -f image2pipe -vcodec ppm -";
    FILE* Pipe = _popen(Command.c_str(), "rb");
    if (!Pipe)
        return std::nullopt;

    std::vector<uint8_t> Bytes;
    uint8_t Buffer[16384];
    for (;;)
    {
        const size_t Read = std::fread(Buffer, 1, sizeof(Buffer), Pipe);
        if (Read > 0)
            Bytes.insert(Bytes.end(), Buffer, Buffer + Read);
        if (Read < sizeof(Buffer))
        {
            if (std::feof(Pipe) || std::ferror(Pipe))
                break;
        }
    }
    _pclose(Pipe);

    size_t Pos = 0;
    auto Magic = NextPpmToken(Bytes, Pos);
    auto WidthToken = NextPpmToken(Bytes, Pos);
    auto HeightToken = NextPpmToken(Bytes, Pos);
    auto MaxToken = NextPpmToken(Bytes, Pos);
    if (!Magic || *Magic != "P6" || !WidthToken || !HeightToken || !MaxToken)
        return std::nullopt;

    const int Width = std::atoi(WidthToken->c_str());
    const int Height = std::atoi(HeightToken->c_str());
    const int MaxValue = std::atoi(MaxToken->c_str());
    SkipPpmWhitespace(Bytes, Pos);
    if (Width <= 0 || Height <= 0 || MaxValue != 255)
        return std::nullopt;

    const size_t RgbSize = (size_t)Width * (size_t)Height * 3u;
    if (Pos + RgbSize > Bytes.size())
        return std::nullopt;

    DecodedVideoFrame Frame = {};
    Frame.Image.Width = Width;
    Frame.Image.Height = Height;
    Frame.Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);
    for (size_t I = 0, J = Pos; I < (size_t)Width * (size_t)Height; ++I, J += 3u)
    {
        Frame.Image.Rgba[I * 4u + 0u] = Bytes[J + 0u];
        Frame.Image.Rgba[I * 4u + 1u] = Bytes[J + 1u];
        Frame.Image.Rgba[I * 4u + 2u] = Bytes[J + 2u];
        Frame.Image.Rgba[I * 4u + 3u] = 255;
    }
    Frame.DurationSeconds = 1.0f / 12.0f;
    return Frame;
#else
    (void)Path;
    return std::nullopt;
#endif
}

} // namespace Resource::Graphics
