#pragma once

#include "third_party/sokol/sokol_gfx.h"

namespace Render
{

bool Initialize();
void Shutdown();
void BeginFrame();
void EndFrame();
bool IsActive();
sg_pass_action GetDefaultPassAction();
void SetClearColor(float R, float G, float B, float A = 1.0f);

} // namespace Render
