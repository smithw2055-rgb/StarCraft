#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Color.h"
#include "Core/Math/Vec2.h"
#include "Core/Utils/JsonLite.h"
#include "Engine/DataAssetRegistry.h"
#include "Render/RenderQueue2D.h"

namespace Engine::UI
{

struct StyleBox2D
{
    std::string Name;
    Core::Color BackgroundColor{16, 24, 32, 220};
    Core::Color BorderColor{60, 210, 110, 255};
    Core::Color HighlightColor{100, 255, 150, 255};
    float BorderThickness = 1.0f;
};

class ThemeDB
{
public:
    ThemeDB()
    {
        RegisterDefaultFactionThemes();
    }

    void RegisterStyleBox(std::string Name, StyleBox2D Style)
    {
        Style.Name = Name;
        mStyles[std::move(Name)] = std::move(Style);
    }

    const StyleBox2D* FindStyleBox(std::string_view Name) const
    {
        auto It = mStyles.find(std::string(Name));
        return It != mStyles.end() ? &It->second : nullptr;
    }

    size_t GetStyleCount() const noexcept { return mStyles.size(); }

    void RegisterDefaultFactionThemes()
    {
        RegisterStyleBox("TerranConsole", {"TerranConsole", {18, 28, 22, 230}, {64, 220, 96, 255}, {120, 255, 140, 255}, 1.5f});
        RegisterStyleBox("ProtossConsole", {"ProtossConsole", {22, 24, 42, 230}, {90, 180, 255, 255}, {160, 220, 255, 255}, 1.5f});
        RegisterStyleBox("ZergConsole", {"ZergConsole", {36, 18, 24, 230}, {210, 96, 72, 255}, {255, 140, 110, 255}, 1.5f});
    }

private:
    std::unordered_map<std::string, StyleBox2D> mStyles;
};

struct HDUIAtlasAsset
{
    std::string SourceName;
    std::string AtlasFile;
    uint32_t FrameCount = 0;
    int32_t CellWidthPx = 0;
    int32_t CellHeightPx = 0;
    int32_t AtlasWidthPx = 0;
    int32_t AtlasHeightPx = 0;
};

struct UIControlNode2D
{
    std::string Name;
    Core::Math::Rect ScreenRect{0.0f, 0.0f, 0.0f, 0.0f};
    std::string StyleBoxName = "TerranConsole";
    std::string LabelText;
    std::string BoundActionName;
    uint32_t IconImageID = 0;
    uint16_t IconFrameIndex = 0;
    bool Visible = true;
    bool Enabled = true;
    std::vector<std::unique_ptr<UIControlNode2D>> Children;

    UIControlNode2D& AddChild(std::string ChildName,
                              Core::Math::Rect Rect,
                              std::string ActionName = {},
                              uint16_t FrameIdx = 0)
    {
        auto Node = std::make_unique<UIControlNode2D>();
        Node->Name = std::move(ChildName);
        Node->ScreenRect = Rect;
        Node->StyleBoxName = StyleBoxName;
        Node->BoundActionName = std::move(ActionName);
        Node->IconFrameIndex = FrameIdx;
        UIControlNode2D& Ref = *Node;
        Children.push_back(std::move(Node));
        return Ref;
    }

    const UIControlNode2D* HitTest(Core::Math::Vec2 Point) const
    {
        if (!Visible || !Enabled)
            return nullptr;

        for (auto It = Children.rbegin(); It != Children.rend(); ++It)
        {
            if (const auto* HitChild = (*It)->HitTest(Point))
                return HitChild;
        }

        if (ScreenRect.Width > 0.0f && ScreenRect.Height > 0.0f && ScreenRect.Contains(Point))
            return this;
        return nullptr;
    }
};

// ============================================================================
// UIServer2D (Phase 48):
// Referenced from Godot 4 Control / ThemeDB / CanvasLayer & Unity UI Toolkit:
// - Manages declarative 2D UI control hierarchies, faction ThemeDB styles,
//   3x3 RTS Command Grid layout generation, pointer event routing to semantic
//   input actions, RenderQueue2D ScreenUI extraction, and cloud HD UI Atlas
//   manifest registration (T_UI_Command_Icons_HD_Atlas_390Frames.png, etc.).
// ============================================================================
class UIServer2D
{
public:
    void Clear()
    {
        mRoot = UIControlNode2D{"UIRoot", {0.0f, 0.0f, 1280.0f, 720.0f}};
        mUIAtlases.Clear();
        mTotalPointerClicksRouted = 0;
    }

    ThemeDB& Theme() noexcept { return mTheme; }
    const ThemeDB& Theme() const noexcept { return mTheme; }

    UIControlNode2D& Root() noexcept { return mRoot; }
    const UIControlNode2D& Root() const noexcept { return mRoot; }

    // Builds a standard 3x3 RTS Command Card under Root()
    UIControlNode2D& BuildCommandGrid3x3(
        Core::Math::Vec2 Origin,
        float ButtonSizePx,
        float SpacingPx,
        const std::vector<std::pair<std::string, uint16_t>>& ActionAndIconSlots)
    {
        const float TotalW = 3.0f * ButtonSizePx + 2.0f * SpacingPx;
        const float TotalH = 3.0f * ButtonSizePx + 2.0f * SpacingPx;
        auto& GridPanel = mRoot.AddChild("CommandGrid3x3", {Origin.X, Origin.Y, TotalW, TotalH});

        for (size_t Idx = 0; Idx < 9; ++Idx)
        {
            const size_t Row = Idx / 3u;
            const size_t Col = Idx % 3u;
            const float BX = Origin.X + static_cast<float>(Col) * (ButtonSizePx + SpacingPx);
            const float BY = Origin.Y + static_cast<float>(Row) * (ButtonSizePx + SpacingPx);
            const std::string Action = (Idx < ActionAndIconSlots.size()) ? ActionAndIconSlots[Idx].first : "";
            const uint16_t IconFrame = (Idx < ActionAndIconSlots.size()) ? ActionAndIconSlots[Idx].second : 0u;
            GridPanel.AddChild(
                "CmdBtn_" + std::to_string(Idx),
                {BX, BY, ButtonSizePx, ButtonSizePx},
                Action,
                IconFrame);
        }
        return GridPanel;
    }

    // Routes a screen-space pointer click and returns the hit control's bound semantic action
    bool RoutePointerClick(Core::Math::Vec2 ScreenPos, std::string& OutBoundAction) const
    {
        const UIControlNode2D* Hit = mRoot.HitTest(ScreenPos);
        if (!Hit || Hit->BoundActionName.empty())
            return false;
        OutBoundAction = Hit->BoundActionName;
        ++mTotalPointerClicksRouted;
        return true;
    }

    // Extracts visible UI controls into RenderQueue2D on the ScreenUI layer
    size_t ExtractToRenderQueue(::Render::RenderQueue2D& Queue) const
    {
        size_t Emitted = 0;
        Queue.SetSortContext(::Render::RenderLayer2D::ScreenUI);
        ExtractNodeRecursive(mRoot, Queue, Emitted);
        return Emitted;
    }

    // Imports HD UI & Effect Atlases from cloud Terrain_CHK_And_HD_Effects_Manifest.json
    size_t ImportHDUIAtlasesFromManifest(const std::string& ManifestJsonPath)
    {
        Core::Utils::JsonValue RootJson;
        if (!Core::Utils::JsonParser::ParseFile(ManifestJsonPath, RootJson) || !RootJson.IsObject())
            return 0;

        const auto& AtlasesArr = RootJson["hd_dds_grp_atlases"];
        if (!AtlasesArr.IsArray())
            return 0;

        size_t Imported = 0;
        uint32_t NextID = 1;
        for (size_t i = 0; i < AtlasesArr.Size(); ++i)
        {
            const auto& Item = AtlasesArr[i];
            if (!Item.IsObject())
                continue;

            HDUIAtlasAsset Asset{};
            Asset.SourceName = Item["source"].AsString();
            Asset.AtlasFile = Item["atlas_file"].AsString();
            Asset.FrameCount = static_cast<uint32_t>(Item["frame_count"].AsInt());
            if (Item["cell_size_px"].IsArray() && Item["cell_size_px"].Size() >= 2)
            {
                Asset.CellWidthPx = static_cast<int32_t>(Item["cell_size_px"][0].AsInt());
                Asset.CellHeightPx = static_cast<int32_t>(Item["cell_size_px"][1].AsInt());
            }
            if (Item["atlas_resolution"].IsArray() && Item["atlas_resolution"].Size() >= 2)
            {
                Asset.AtlasWidthPx = static_cast<int32_t>(Item["atlas_resolution"][0].AsInt());
                Asset.AtlasHeightPx = static_cast<int32_t>(Item["atlas_resolution"][1].AsInt());
            }

            const std::string Key = Asset.AtlasFile.empty() ? Asset.SourceName : Asset.AtlasFile;
            mUIAtlases.Register(NextID++, Key, std::move(Asset));
            ++Imported;
        }
        return Imported;
    }

    const Engine::DataAssetTable<HDUIAtlasAsset>& GetUIAtlasTable() const noexcept
    {
        return mUIAtlases;
    }

    uint64_t GetTotalPointerClicksRouted() const noexcept { return mTotalPointerClicksRouted; }

private:
    void ExtractNodeRecursive(const UIControlNode2D& Node,
                              ::Render::RenderQueue2D& Queue,
                              size_t& OutCount) const
    {
        if (!Node.Visible)
            return;

        if (Node.ScreenRect.Width > 0.0f && Node.ScreenRect.Height > 0.0f && &Node != &mRoot)
        {
            ::Render::Color FillColor(18, 28, 22, 220);
            if (const auto* Style = mTheme.FindStyleBox(Node.StyleBoxName))
            {
                FillColor = ::Render::Color(
                    Style->BackgroundColor.R,
                    Style->BackgroundColor.G,
                    Style->BackgroundColor.B,
                    Style->BackgroundColor.A);
            }
            Queue.SubmitRect(
                Node.ScreenRect.X,
                Node.ScreenRect.Y,
                Node.ScreenRect.Width,
                Node.ScreenRect.Height,
                FillColor);
            ++OutCount;
        }

        for (const auto& Child : Node.Children)
        {
            if (Child)
                ExtractNodeRecursive(*Child, Queue, OutCount);
        }
    }

    ThemeDB mTheme;
    UIControlNode2D mRoot{"UIRoot", {0.0f, 0.0f, 1280.0f, 720.0f}};
    Engine::DataAssetTable<HDUIAtlasAsset> mUIAtlases;
    mutable uint64_t mTotalPointerClicksRouted = 0;
};

} // namespace Engine::UI
