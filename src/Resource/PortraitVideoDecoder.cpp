#include "Resource/PortraitVideoDecoder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#ifdef HAS_LIBVPX
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#endif

namespace Resource::Graphics
{

namespace
{
    struct Element
    {
        uint64_t Id = 0;
        uint64_t Size = 0;
        size_t DataOffset = 0;
        size_t EndOffset = 0;
    };

    struct EbmlReader
    {
        const std::vector<uint8_t>& Bytes;
        size_t Pos = 0;
        size_t End = 0;

        EbmlReader(const std::vector<uint8_t>& InBytes, size_t InStart, size_t InEnd)
            : Bytes(InBytes), Pos(InStart), End(std::min(InEnd, InBytes.size()))
        {
        }

        bool ReadVint(uint64_t& Value, int& Length, bool RemoveMarker)
        {
            if (Pos >= End)
                return false;

            const uint8_t First = Bytes[Pos];
            uint8_t Mask = 0x80;
            Length = 1;
            while (Length <= 8 && (First & Mask) == 0)
            {
                Mask >>= 1;
                ++Length;
            }
            if (Length > 8 || Pos + (size_t)Length > End)
                return false;

            Value = RemoveMarker ? (uint64_t)(First & ~Mask) : First;
            for (int I = 1; I < Length; ++I)
                Value = (Value << 8u) | Bytes[Pos + (size_t)I];
            Pos += (size_t)Length;
            return true;
        }

        bool ReadElement(Element& Out)
        {
            uint64_t Id = 0;
            uint64_t Size = 0;
            int IdLength = 0;
            int SizeLength = 0;
            if (!ReadVint(Id, IdLength, false) || !ReadVint(Size, SizeLength, true))
                return false;

            if (Size > (uint64_t)(End - Pos))
                return false;

            Out.Id = Id;
            Out.Size = Size;
            Out.DataOffset = Pos;
            Out.EndOffset = Pos + (size_t)Size;
            Pos = Out.EndOffset;
            return true;
        }
    };

    std::optional<std::vector<uint8_t>> ReadWholeFile(const std::filesystem::path& Path)
    {
        std::ifstream File(Path, std::ios::binary);
        if (!File)
            return std::nullopt;

        File.seekg(0, std::ios::end);
        const std::streamoff Size = File.tellg();
        if (Size <= 0)
            return std::nullopt;

        std::vector<uint8_t> Bytes((size_t)Size);
        File.seekg(0, std::ios::beg);
        File.read(reinterpret_cast<char*>(Bytes.data()), Size);
        if (!File)
            return std::nullopt;
        return Bytes;
    }

    uint64_t ReadUnsigned(const std::vector<uint8_t>& Bytes, const Element& Elem)
    {
        uint64_t Value = 0;
        const size_t Count = std::min<size_t>(Elem.Size, 8);
        for (size_t I = 0; I < Count; ++I)
            Value = (Value << 8u) | Bytes[Elem.DataOffset + I];
        return Value;
    }

    double ReadFloat(const std::vector<uint8_t>& Bytes, const Element& Elem)
    {
        if (Elem.Size == 4)
        {
            uint32_t Raw = 0;
            for (size_t I = 0; I < 4; ++I)
                Raw = (Raw << 8u) | Bytes[Elem.DataOffset + I];
            float Value = 0.0f;
            std::memcpy(&Value, &Raw, sizeof(Value));
            return Value;
        }
        if (Elem.Size == 8)
        {
            uint64_t Raw = 0;
            for (size_t I = 0; I < 8; ++I)
                Raw = (Raw << 8u) | Bytes[Elem.DataOffset + I];
            double Value = 0.0;
            std::memcpy(&Value, &Raw, sizeof(Value));
            return Value;
        }
        return 0.0;
    }

    std::string ReadString(const std::vector<uint8_t>& Bytes, const Element& Elem)
    {
        return std::string(reinterpret_cast<const char*>(Bytes.data() + Elem.DataOffset),
                           reinterpret_cast<const char*>(Bytes.data() + Elem.EndOffset));
    }

    struct WebmTrack
    {
        uint64_t Number = 0;
        std::string CodecId;
        int Width = 0;
        int Height = 0;
        bool IsVideo = false;
    };

    struct EncodedFramePacket
    {
        double TimeSeconds = 0.0;
        float DurationSeconds = 0.0f;
        size_t ColorOffset = 0;
        size_t ColorSize = 0;
        size_t AlphaOffset = 0;
        size_t AlphaSize = 0;
    };

    struct ParsedBlock
    {
        uint64_t TrackNumber = 0;
        int16_t RelativeTimecode = 0;
        bool Laced = false;
        size_t PayloadOffset = 0;
        size_t PayloadSize = 0;
    };

    bool ParseBlock(const std::vector<uint8_t>& Bytes, size_t Start, size_t End, ParsedBlock& Out)
    {
        EbmlReader Reader(Bytes, Start, End);
        int TrackLength = 0;
        if (!Reader.ReadVint(Out.TrackNumber, TrackLength, true))
            return false;
        if (Reader.Pos + 3 > End)
            return false;

        Out.RelativeTimecode = static_cast<int16_t>(
            (uint16_t(Bytes[Reader.Pos]) << 8u) | uint16_t(Bytes[Reader.Pos + 1]));
        Reader.Pos += 2;

        const uint8_t Flags = Bytes[Reader.Pos++];
        Out.Laced = (Flags & 0x06u) != 0;
        Out.PayloadOffset = Reader.Pos;
        Out.PayloadSize = End - Reader.Pos;
        return true;
    }

    void ParseTrackVideo(const std::vector<uint8_t>& Bytes, const Element& VideoElem, WebmTrack& Track)
    {
        EbmlReader Reader(Bytes, VideoElem.DataOffset, VideoElem.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0xB0)
                Track.Width = (int)ReadUnsigned(Bytes, Elem);
            else if (Elem.Id == 0xBA)
                Track.Height = (int)ReadUnsigned(Bytes, Elem);
        }
    }

    WebmTrack ParseTrackEntry(const std::vector<uint8_t>& Bytes, const Element& TrackEntry)
    {
        WebmTrack Track = {};
        uint64_t TrackType = 0;
        EbmlReader Reader(Bytes, TrackEntry.DataOffset, TrackEntry.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0xD7)
                Track.Number = ReadUnsigned(Bytes, Elem);
            else if (Elem.Id == 0x83)
                TrackType = ReadUnsigned(Bytes, Elem);
            else if (Elem.Id == 0x86)
                Track.CodecId = ReadString(Bytes, Elem);
            else if (Elem.Id == 0xE0)
                ParseTrackVideo(Bytes, Elem, Track);
        }
        Track.IsVideo = TrackType == 1;
        return Track;
    }

    void ParseTracks(const std::vector<uint8_t>& Bytes, const Element& TracksElem, std::vector<WebmTrack>& Tracks)
    {
        EbmlReader Reader(Bytes, TracksElem.DataOffset, TracksElem.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0xAE)
                Tracks.push_back(ParseTrackEntry(Bytes, Elem));
        }
    }

    void ParseInfo(const std::vector<uint8_t>& Bytes, const Element& InfoElem, uint64_t& TimecodeScale)
    {
        EbmlReader Reader(Bytes, InfoElem.DataOffset, InfoElem.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0x2AD7B1)
                TimecodeScale = ReadUnsigned(Bytes, Elem);
        }
    }

    void AddBlockPacket(const ParsedBlock& Block,
                        uint64_t VideoTrackNumber,
                        uint64_t ClusterTimecode,
                        uint64_t TimecodeScale,
                        size_t AlphaOffset,
                        size_t AlphaSize,
                        std::vector<EncodedFramePacket>& Packets)
    {
        if (Block.TrackNumber != VideoTrackNumber || Block.Laced || Block.PayloadSize == 0)
            return;

        EncodedFramePacket Packet = {};
        const int64_t AbsoluteTimecode = (int64_t)ClusterTimecode + (int64_t)Block.RelativeTimecode;
        Packet.TimeSeconds = (double)AbsoluteTimecode * (double)TimecodeScale / 1000000000.0;
        Packet.ColorOffset = Block.PayloadOffset;
        Packet.ColorSize = Block.PayloadSize;
        Packet.AlphaOffset = AlphaOffset;
        Packet.AlphaSize = AlphaSize;
        Packets.push_back(std::move(Packet));
    }

    void ParseBlockAdditions(const std::vector<uint8_t>& Bytes,
                             const Element& AdditionsElem,
                             size_t& AlphaOffset,
                             size_t& AlphaSize)
    {
        EbmlReader Additions(Bytes, AdditionsElem.DataOffset, AdditionsElem.EndOffset);
        Element BlockMore = {};
        while (Additions.ReadElement(BlockMore))
        {
            if (BlockMore.Id != 0xA6)
                continue;

            uint64_t AddId = 1;
            Element Additional = {};
            EbmlReader More(Bytes, BlockMore.DataOffset, BlockMore.EndOffset);
            Element Elem = {};
            while (More.ReadElement(Elem))
            {
                if (Elem.Id == 0xEE)
                    AddId = ReadUnsigned(Bytes, Elem);
                else if (Elem.Id == 0xA5)
                    Additional = Elem;
            }

            if (AddId == 1 && Additional.Size > 0)
            {
                AlphaOffset = Additional.DataOffset;
                AlphaSize = Additional.Size;
            }
        }
    }

    void ParseBlockGroup(const std::vector<uint8_t>& Bytes,
                         const Element& GroupElem,
                         uint64_t VideoTrackNumber,
                         uint64_t ClusterTimecode,
                         uint64_t TimecodeScale,
                         std::vector<EncodedFramePacket>& Packets)
    {
        std::optional<ParsedBlock> Block;
        size_t AlphaOffset = 0;
        size_t AlphaSize = 0;
        float BlockDurationSeconds = 0.0f;

        EbmlReader Reader(Bytes, GroupElem.DataOffset, GroupElem.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0xA1)
            {
                ParsedBlock Parsed = {};
                if (ParseBlock(Bytes, Elem.DataOffset, Elem.EndOffset, Parsed))
                    Block = Parsed;
            }
            else if (Elem.Id == 0x75A1)
            {
                ParseBlockAdditions(Bytes, Elem, AlphaOffset, AlphaSize);
            }
            else if (Elem.Id == 0x9B)
            {
                BlockDurationSeconds =
                    (float)((double)ReadUnsigned(Bytes, Elem) * (double)TimecodeScale / 1000000000.0);
            }
        }

        if (!Block)
            return;

        const size_t Before = Packets.size();
        AddBlockPacket(*Block, VideoTrackNumber, ClusterTimecode, TimecodeScale,
                       AlphaOffset, AlphaSize, Packets);
        if (Packets.size() > Before && BlockDurationSeconds > 0.0f)
            Packets.back().DurationSeconds = BlockDurationSeconds;
    }

    void ParseCluster(const std::vector<uint8_t>& Bytes,
                      const Element& ClusterElem,
                      uint64_t VideoTrackNumber,
                      uint64_t TimecodeScale,
                      std::vector<EncodedFramePacket>& Packets)
    {
        uint64_t ClusterTimecode = 0;
        EbmlReader Reader(Bytes, ClusterElem.DataOffset, ClusterElem.EndOffset);
        Element Elem = {};
        while (Reader.ReadElement(Elem))
        {
            if (Elem.Id == 0xE7)
            {
                ClusterTimecode = ReadUnsigned(Bytes, Elem);
            }
            else if (Elem.Id == 0xA3)
            {
                ParsedBlock Block = {};
                if (ParseBlock(Bytes, Elem.DataOffset, Elem.EndOffset, Block))
                    AddBlockPacket(Block, VideoTrackNumber, ClusterTimecode, TimecodeScale, 0, 0, Packets);
            }
            else if (Elem.Id == 0xA0)
            {
                ParseBlockGroup(Bytes, Elem, VideoTrackNumber, ClusterTimecode, TimecodeScale, Packets);
            }
        }
    }

    std::optional<std::pair<WebmTrack, std::vector<EncodedFramePacket>>> DemuxWebm(const std::vector<uint8_t>& Bytes)
    {
        Element Segment = {};
        EbmlReader Root(Bytes, 0, Bytes.size());
        Element Elem = {};
        while (Root.ReadElement(Elem))
        {
            if (Elem.Id == 0x18538067)
            {
                Segment = Elem;
                break;
            }
        }
        if (Segment.Id == 0)
            return std::nullopt;

        uint64_t TimecodeScale = 1000000;
        std::vector<WebmTrack> Tracks;
        std::vector<EncodedFramePacket> Packets;

        EbmlReader SegmentReader(Bytes, Segment.DataOffset, Segment.EndOffset);
        while (SegmentReader.ReadElement(Elem))
        {
            if (Elem.Id == 0x1549A966)
                ParseInfo(Bytes, Elem, TimecodeScale);
            else if (Elem.Id == 0x1654AE6B)
                ParseTracks(Bytes, Elem, Tracks);
        }

        const auto TrackIt = std::find_if(Tracks.begin(), Tracks.end(), [](const WebmTrack& Track)
        {
            return Track.IsVideo && (Track.CodecId == "V_VP9" || Track.CodecId == "V_VP8");
        });
        if (TrackIt == Tracks.end() || TrackIt->Number == 0)
            return std::nullopt;

        SegmentReader.Pos = Segment.DataOffset;
        while (SegmentReader.ReadElement(Elem))
        {
            if (Elem.Id == 0x1F43B675)
                ParseCluster(Bytes, Elem, TrackIt->Number, TimecodeScale, Packets);
        }

        std::sort(Packets.begin(), Packets.end(), [](const EncodedFramePacket& A, const EncodedFramePacket& B)
        {
            return A.TimeSeconds < B.TimeSeconds;
        });

        for (size_t I = 0; I + 1 < Packets.size(); ++I)
        {
            if (Packets[I].DurationSeconds <= 0.0f)
            {
                const double Delta = Packets[I + 1].TimeSeconds - Packets[I].TimeSeconds;
                Packets[I].DurationSeconds = (float)std::clamp(Delta, 1.0 / 120.0, 0.25);
            }
        }
        if (!Packets.empty() && Packets.back().DurationSeconds <= 0.0f)
            Packets.back().DurationSeconds = Packets.size() > 1 ? Packets[Packets.size() - 2].DurationSeconds : 0.1f;

        return std::make_pair(*TrackIt, std::move(Packets));
    }

    uint8_t ClampByte(int Value)
    {
        return (uint8_t)std::clamp(Value, 0, 255);
    }

#ifdef HAS_LIBVPX
    std::optional<const vpx_codec_iface_t*> CodecIfaceForTrack(const WebmTrack& Track)
    {
        if (Track.CodecId == "V_VP9")
            return vpx_codec_vp9_dx();
        if (Track.CodecId == "V_VP8")
            return vpx_codec_vp8_dx();
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> DecodeAlphaPlane(vpx_codec_ctx_t& Codec,
                                                         const uint8_t* Payload,
                                                         size_t PayloadSize,
                                                         int Width,
                                                         int Height)
    {
        if (!Payload || PayloadSize == 0)
            return std::nullopt;

        if (vpx_codec_decode(&Codec, Payload, (unsigned int)PayloadSize, nullptr, 0) != VPX_CODEC_OK)
            return std::nullopt;

        vpx_codec_iter_t Iter = nullptr;
        vpx_image_t* Image = vpx_codec_get_frame(&Codec, &Iter);
        if (!Image || Image->d_w <= 0 || Image->d_h <= 0)
            return std::nullopt;

        std::vector<uint8_t> Alpha((size_t)Width * (size_t)Height, 255);
        const int SrcWidth = (int)Image->d_w;
        const int SrcHeight = (int)Image->d_h;
        const int CopyW = std::min(Width, SrcWidth);
        const int CopyH = std::min(Height, SrcHeight);
        for (int Y = 0; Y < CopyH; ++Y)
        {
            const uint8_t* Src = Image->planes[VPX_PLANE_Y] + (size_t)Y * (size_t)Image->stride[VPX_PLANE_Y];
            uint8_t* Dst = Alpha.data() + (size_t)Y * (size_t)Width;
            std::copy(Src, Src + CopyW, Dst);
        }
        return Alpha;
    }

    std::optional<DecodedRgbaImage> DecodeColorFrame(vpx_codec_ctx_t& Codec,
                                                     const uint8_t* Payload,
                                                     size_t PayloadSize,
                                                     const std::optional<std::vector<uint8_t>>& AlphaPlane)
    {
        if (!Payload || PayloadSize == 0)
            return std::nullopt;

        if (vpx_codec_decode(&Codec, Payload, (unsigned int)PayloadSize, nullptr, 0) != VPX_CODEC_OK)
            return std::nullopt;

        vpx_codec_iter_t Iter = nullptr;
        vpx_image_t* Image = vpx_codec_get_frame(&Codec, &Iter);
        if (!Image || Image->d_w <= 0 || Image->d_h <= 0)
            return std::nullopt;

        DecodedRgbaImage Out = {};
        Out.Width = (int)Image->d_w;
        Out.Height = (int)Image->d_h;
        Out.Rgba.resize((size_t)Out.Width * (size_t)Out.Height * 4u);

        const int XShift = Image->x_chroma_shift;
        const int YShift = Image->y_chroma_shift;
        for (int Y = 0; Y < Out.Height; ++Y)
        {
            const uint8_t* YRow = Image->planes[VPX_PLANE_Y] + (size_t)Y * (size_t)Image->stride[VPX_PLANE_Y];
            const uint8_t* URow = Image->planes[VPX_PLANE_U] + (size_t)(Y >> YShift) * (size_t)Image->stride[VPX_PLANE_U];
            const uint8_t* VRow = Image->planes[VPX_PLANE_V] + (size_t)(Y >> YShift) * (size_t)Image->stride[VPX_PLANE_V];
            for (int X = 0; X < Out.Width; ++X)
            {
                const int YValue = YRow[X];
                const int UValue = URow[X >> XShift];
                const int VValue = VRow[X >> XShift];

                const int C = YValue - 16;
                const int D = UValue - 128;
                const int E = VValue - 128;
                const int R = (298 * C + 409 * E + 128) >> 8;
                const int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                const int B = (298 * C + 516 * D + 128) >> 8;

                const size_t Dst = ((size_t)Y * (size_t)Out.Width + (size_t)X) * 4u;
                Out.Rgba[Dst + 0u] = ClampByte(R);
                Out.Rgba[Dst + 1u] = ClampByte(G);
                Out.Rgba[Dst + 2u] = ClampByte(B);
                Out.Rgba[Dst + 3u] = AlphaPlane ? (*AlphaPlane)[(size_t)Y * (size_t)Out.Width + (size_t)X] : 255;
            }
        }

        return Out;
    }
#endif
}

struct WebmVideoDecoder::Impl
{
#ifndef HAS_LIBVPX
    bool Open(const std::filesystem::path&) { return false; }
    void Close() {}
    bool Rewind() { return false; }
    bool ReadNextFrame(DecodedVideoFrame&) { return false; }
    bool Valid() const { return false; }
    int Width() const { return 0; }
    int Height() const { return 0; }
    size_t EncodedFrameCount() const { return 0; }
#else
    std::vector<uint8_t> Bytes;
    WebmTrack Track;
    std::vector<EncodedFramePacket> Packets;
    size_t PacketIndex = 0;
    const vpx_codec_iface_t* CodecIface = nullptr;
    vpx_codec_ctx_t ColorCodec = {};
    vpx_codec_ctx_t AlphaCodec = {};
    bool ColorCodecInitialized = false;
    bool AlphaCodecInitialized = false;

    ~Impl()
    {
        DestroyCodecs();
    }

    void DestroyCodecs()
    {
        if (AlphaCodecInitialized)
        {
            vpx_codec_destroy(&AlphaCodec);
            AlphaCodec = {};
            AlphaCodecInitialized = false;
        }
        if (ColorCodecInitialized)
        {
            vpx_codec_destroy(&ColorCodec);
            ColorCodec = {};
            ColorCodecInitialized = false;
        }
    }

    bool InitCodecs()
    {
        DestroyCodecs();
        if (!CodecIface)
            return false;

        if (vpx_codec_dec_init(&ColorCodec, CodecIface, nullptr, 0) != VPX_CODEC_OK)
            return false;
        ColorCodecInitialized = true;

        const bool HasAlphaPayload = std::any_of(Packets.begin(), Packets.end(),
            [](const EncodedFramePacket& Packet) { return Packet.AlphaSize > 0; });
        if (HasAlphaPayload &&
            vpx_codec_dec_init(&AlphaCodec, CodecIface, nullptr, 0) == VPX_CODEC_OK)
        {
            AlphaCodecInitialized = true;
        }
        return true;
    }

    bool Open(const std::filesystem::path& Path)
    {
        Close();

        auto FileBytes = ReadWholeFile(Path);
        if (!FileBytes)
            return false;

        auto Demuxed = DemuxWebm(*FileBytes);
        if (!Demuxed || Demuxed->second.empty())
            return false;

        const auto Iface = CodecIfaceForTrack(Demuxed->first);
        if (!Iface)
            return false;

        Bytes = std::move(*FileBytes);
        Track = std::move(Demuxed->first);
        Packets = std::move(Demuxed->second);
        PacketIndex = 0;
        CodecIface = *Iface;

        if (!InitCodecs())
        {
            Close();
            return false;
        }
        return true;
    }

    void Close()
    {
        DestroyCodecs();
        Bytes.clear();
        Track = {};
        Packets.clear();
        PacketIndex = 0;
        CodecIface = nullptr;
    }

    bool Rewind()
    {
        if (Packets.empty())
            return false;
        PacketIndex = 0;
        return InitCodecs();
    }

    bool ReadNextFrame(DecodedVideoFrame& OutFrame)
    {
        while (PacketIndex < Packets.size())
        {
            const EncodedFramePacket& Packet = Packets[PacketIndex++];
            if (Packet.ColorSize == 0 || Packet.ColorOffset + Packet.ColorSize > Bytes.size())
                continue;

            std::optional<std::vector<uint8_t>> AlphaPlane;
            if (AlphaCodecInitialized &&
                Packet.AlphaSize > 0 &&
                Packet.AlphaOffset + Packet.AlphaSize <= Bytes.size())
            {
                AlphaPlane = DecodeAlphaPlane(AlphaCodec,
                                              Bytes.data() + Packet.AlphaOffset,
                                              Packet.AlphaSize,
                                              Track.Width,
                                              Track.Height);
            }

            auto Decoded = DecodeColorFrame(ColorCodec,
                                            Bytes.data() + Packet.ColorOffset,
                                            Packet.ColorSize,
                                            AlphaPlane);
            if (!Decoded)
                continue;

            OutFrame = {};
            OutFrame.Image = std::move(*Decoded);
            OutFrame.DurationSeconds = Packet.DurationSeconds > 0.0f ? Packet.DurationSeconds : 0.1f;
            return true;
        }
        return false;
    }

    bool Valid() const { return ColorCodecInitialized && !Packets.empty(); }
    int Width() const { return Track.Width; }
    int Height() const { return Track.Height; }
    size_t EncodedFrameCount() const { return Packets.size(); }
#endif
};

WebmVideoDecoder::WebmVideoDecoder()
    : mImpl(std::make_unique<Impl>())
{
}

WebmVideoDecoder::~WebmVideoDecoder() = default;

WebmVideoDecoder::WebmVideoDecoder(WebmVideoDecoder&&) noexcept = default;
WebmVideoDecoder& WebmVideoDecoder::operator=(WebmVideoDecoder&&) noexcept = default;

bool WebmVideoDecoder::Open(const std::filesystem::path& Path)
{
    return mImpl->Open(Path);
}

void WebmVideoDecoder::Close()
{
    mImpl->Close();
}

bool WebmVideoDecoder::Rewind()
{
    return mImpl->Rewind();
}

bool WebmVideoDecoder::ReadNextFrame(DecodedVideoFrame& OutFrame)
{
    return mImpl->ReadNextFrame(OutFrame);
}

bool WebmVideoDecoder::Valid() const
{
    return mImpl->Valid();
}

int WebmVideoDecoder::Width() const
{
    return mImpl->Width();
}

int WebmVideoDecoder::Height() const
{
    return mImpl->Height();
}

size_t WebmVideoDecoder::EncodedFrameCount() const
{
    return mImpl->EncodedFrameCount();
}

std::optional<std::vector<DecodedVideoFrame>> DecodeVideoFramesFromFile(const std::filesystem::path& Path)
{
    WebmVideoDecoder Decoder;
    if (!Decoder.Open(Path))
        return std::nullopt;

    std::vector<DecodedVideoFrame> Frames;
    Frames.reserve(Decoder.EncodedFrameCount());
    DecodedVideoFrame Frame;
    while (Decoder.ReadNextFrame(Frame))
        Frames.push_back(std::move(Frame));

    if (Frames.empty())
        return std::nullopt;
    return Frames;
}

} // namespace Resource::Graphics
