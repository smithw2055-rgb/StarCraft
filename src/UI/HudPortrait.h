#pragma once

#include <string>

#include "Core/Platform/Platform.h"
#include "Game/Races/RaceDef.h"
#include "Resource/UIAssetCacheView.h"

namespace Game::Client::UI
{

inline std::string ResolvePortraitKeyForChkUnitId(int ChkUnitId)
{
    switch (ChkUnitId)
    {
        case 0: return "tmarine";
        case 1: return "tghost";
        case 2: return "tvulture";
        case 3:
        case 4: return "tgoliath";
        case 5:
        case 6:
        case 30:
        case 31: return "ttank";
        case 7: return "tscv";
        case 8:
        case 21: return "tphoenix";
        case 9:
        case 22: return "tvessel";
        case 10:
        case 32: return "tfirebat";
        case 11: return "tdropshi";
        case 12:
        case 27:
        case 28:
        case 29: return "tbattlec";
        case 16: return "ukerr";
        case 19:
        case 20: return "ujim";
        case 23:
        case 24:
        case 25:
        case 26: return "uduke";
        case 17:
        case 18:
        case 100: return "talex";
        case 34: return "tmedic";
        case 58: return "tbomber";
        case 99:
        case 104: return "tsam";
        case 102: return "tdugal";

        case 35: return "zlarva";
        case 36: return "zegg";
        case 37:
        case 54: return "zzergl";
        case 38:
        case 53: return "zhydra";
        case 39:
        case 48: return "zultra";
        case 40: return "zbrood";
        case 41: return "zdrone";
        case 42:
        case 57: return "zover";
        case 43:
        case 55: return "zmutalid";
        case 44:
        case 56: return "zguard";
        case 45:
        case 49: return "zqueen";
        case 46:
        case 52: return "zdefiler";
        case 47: return "zavenger";
        case 59:
        case 97: return "zegg";
        case 50: return "zinfestt";
        case 51: return "ukerr";
        case 62: return "zdevour";
        case 103: return "zlurker";

        case 64: return "pprobe";
        case 65: return "pzealot";
        case 66: return "pdragoon";
        case 67:
        case 87: return "ptemplar";
        case 68:
        case 76: return "parchon";
        case 69: return "pshuttle";
        case 70:
        case 80: return "pscout";
        case 71:
        case 86: return "parbiter";
        case 72:
        case 82: return "pcarrier";
        case 73: return "pinterce";
        case 61:
        case 74: return "udtemplar";
        case 77: return "ufenzeal";
        case 78: return "ufendrag";
        case 75: return "uzeratul";
        case 79: return "utassadar";
        case 84: return "pwitness";
        case 63: return "pdarchon";
        case 60: return "pcorsair";
        case 88: return "partanis";

        case 89: return "nredcrit";
        case 92: return "nbiker";
        case 90: return "njungcrit";
        case 93: return "ngastank";
        case 95: return "nlavacrit";
        case 96: return "nicecrit";
        default: return {};
    }
}

inline std::string ResolvePortraitKeyForSelection(int TerranDefId)
{
    if (TerranDefId < 0)
        return {};

    const auto Def = Game::Races::GetUnitDefByID(TerranDefId);
    if (Def.Name == "Unknown")
        return {};

    return ResolvePortraitKeyForChkUnitId(Def.ChkUnitID);
}

inline const Resource::Graphics::GpuImage* ResolvePortraitFrameForSelection(
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int TerranDefId,
    const Resource::Graphics::GpuImage* FallbackImage)
{
    const std::string PortraitKey = ResolvePortraitKeyForSelection(TerranDefId);
    const auto* Frame = PortraitKey.empty()
        ? nullptr
        : UIAssets.GetPortraitFrame(PortraitKey, Core::Platform::GetAbsoluteTime());
    if (Frame && Frame->Image.Valid())
        return &Frame->Image;

    return FallbackImage;
}

inline const Resource::Graphics::GpuImage* ResolvePortraitFrameForChkUnitId(
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int ChkUnitId,
    const Resource::Graphics::GpuImage* FallbackImage)
{
    const std::string PortraitKey = ResolvePortraitKeyForChkUnitId(ChkUnitId);
    const auto* Frame = PortraitKey.empty()
        ? nullptr
        : UIAssets.GetPortraitFrame(PortraitKey, Core::Platform::GetAbsoluteTime());
    if (Frame && Frame->Image.Valid())
        return &Frame->Image;

    return FallbackImage;
}

} // namespace Game::Client::UI
