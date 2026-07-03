#include "Client/SelectionOverlayPresentation.h"

#include "Render/Color.h"
#include "Render/Draw2D.h"

namespace Game::Client
{

void RenderSelectionDragRect(float X, float Y, float W, float H)
{
    Render::g_Draw2D.DrawRect(X, Y, W, H, Render::Color(80, 200, 80, 50));
    Render::g_Draw2D.DrawRectLines(X, Y, W, H, Render::Color(80, 220, 80, 220), 1.0f);
}

} // namespace Game::Client