#include "Client/Selection.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool Condition, const char* Message)
    {
        if (!Condition)
        {
            std::cerr << "Requirement failed: " << Message << "\n";
            std::exit(1);
        }
    }

    void RequireNear(float Actual, float Expected, const char* Message)
    {
        if (std::fabs(Actual - Expected) > 0.01f)
        {
            std::cerr << "Requirement failed: " << Message
                      << " actual=" << Actual << " expected=" << Expected << "\n";
            std::exit(1);
        }
    }
}

int main()
{
    using Game::Client::BuildingFootprintForSelection;
    using Game::Client::BuildingFootprintSize;
    using Game::Client::SelectionCirclePixelSize;
    using Game::Client::SelectionEllipseFromDatBounds;
    using Game::Client::SelectionEllipseFromDatDimensions;
    using Game::Client::SelectionEllipseFromSpriteCircle;
    using Game::Client::SelectionEllipseFromVisualRadius;

    const auto NexusTiles = BuildingFootprintSize(154);
    const auto CannonTiles = BuildingFootprintSize(162);
    const auto AssimilatorTiles = BuildingFootprintSize(157);

    Require(NexusTiles.W == 4.0f && NexusTiles.H == 3.0f,
            "Nexus uses a large building footprint");
    Require(CannonTiles.W == 2.0f && CannonTiles.H == 2.0f,
            "Photon Cannon uses a compact building footprint");
    Require(AssimilatorTiles.W == 4.0f && AssimilatorTiles.H == 2.0f,
            "Assimilator uses a wide refinery footprint");

    const auto Nexus = BuildingFootprintForSelection(100.0f, 100.0f, 154);
    const auto Cannon = BuildingFootprintForSelection(100.0f, 100.0f, 162);
    const auto Assimilator = BuildingFootprintForSelection(100.0f, 100.0f, 157);
    const auto TextureDriven =
        BuildingFootprintForSelection(100.0f, 100.0f, 154, 96.0f, 72.0f);

    Require(Nexus.RadiusX > Cannon.RadiusX,
            "large buildings draw and pick a wider selection ellipse than small buildings");
    Require(Nexus.RadiusY > Cannon.RadiusY,
            "large buildings draw and pick a taller selection ellipse than small buildings");
    Require(Nexus.RadiusX >= 80.0f && Nexus.RadiusY >= 40.0f,
            "large building selection ellipse falls back to its collision footprint");
    Require(Cannon.RadiusX >= 45.0f && Cannon.RadiusY >= 22.0f,
            "small building selection ellipse still wraps the building footprint");
    Require(Assimilator.RadiusX > Cannon.RadiusX && Assimilator.RadiusY > Cannon.RadiusY,
            "refinery footprints are wider without using the large-building height");
    RequireNear(Nexus.CenterX, 100.0f,
                "selection ellipse center X aligns with the collision center");
    RequireNear(Nexus.CenterY, 100.0f,
                "selection ellipse center Y aligns with the collision center");
    RequireNear(TextureDriven.RadiusX, 120.0f,
                "texture bounds produce the outer-circle radius");
    RequireNear(TextureDriven.RadiusY, 60.0f,
                "isometric projection converts the outer circle into a half-height ellipse");
    const auto OffsetVisualRadius =
        SelectionEllipseFromVisualRadius(120.0f, 80.0f, 73.0f, 10.0f);
    RequireNear(OffsetVisualRadius.CenterX, 120.0f,
                "visual-radius selection remains centered on collision X");
    RequireNear(OffsetVisualRadius.CenterY, 80.0f,
                "visual-radius selection remains centered on collision Y");
    RequireNear(OffsetVisualRadius.RadiusX, 73.0f,
                "precomputed visual radius drives the selection ellipse width");
    RequireNear(OffsetVisualRadius.RadiusY, 36.5f,
                "precomputed visual radius is projected to isometric ellipse height");
    const auto MarineDat =
        SelectionEllipseFromDatDimensions(100.0f, 120.0f, 17, 20, 99.0f);
    RequireNear(MarineDat.RadiusX, 8.5f,
                "dat dimensions use the already projected screen-space width");
    RequireNear(MarineDat.RadiusY, 10.0f,
                "dat dimensions use the already projected screen-space height");
    const auto CommandCenterDat =
        SelectionEllipseFromDatDimensions(100.0f, 120.0f, 117, 83, 99.0f);
    RequireNear(CommandCenterDat.RadiusX, 58.5f,
                "building dat dimensions drive the exact screen-space width");
    RequireNear(CommandCenterDat.RadiusY, 41.5f,
                "building dat dimensions are not projected a second time");
    const auto BarracksDat =
        SelectionEllipseFromDatBounds(100.0f, 120.0f, 48, 40, 56, 32, 99.0f);
    RequireNear(BarracksDat.CenterX, 104.5f,
                "asymmetric dat bounds shift the ellipse to the collision center X");
    RequireNear(BarracksDat.CenterY, 116.5f,
                "asymmetric dat bounds shift the ellipse to the collision center Y");
    RequireNear(BarracksDat.RadiusX, 52.5f,
                "asymmetric dat bounds keep exact screen-space half width");
    RequireNear(BarracksDat.RadiusY, 36.5f,
                "asymmetric dat bounds keep exact screen-space half height");
    Require(SelectionCirclePixelSize(0) == 22 &&
            SelectionCirclePixelSize(8) == 146 &&
            SelectionCirclePixelSize(9) == 224,
            "sprite selection circle ids map to original pixel sizes");
    const auto MarineCircle =
        SelectionEllipseFromSpriteCircle(100.0f, 120.0f, 0, 9, 99.0f);
    RequireNear(MarineCircle.CenterY, 129.0f,
                "sprite selection circle vpos moves the ring down from sprite origin");
    RequireNear(MarineCircle.RadiusX, 11.0f,
                "sprite selection circle width comes from sprites.dat circle id");
    RequireNear(MarineCircle.RadiusY, 5.5f,
                "sprite selection circle fallback projects to an isometric ellipse");
    const auto CommandCenterCircle =
        SelectionEllipseFromSpriteCircle(100.0f, 120.0f, 8, 6, 99.0f);
    RequireNear(CommandCenterCircle.RadiusX, 73.0f,
                "large building selection circle uses sprites.dat instead of collision bounds");

    std::cout << "SelectionFootprintTest passed\n";
    return 0;
}
