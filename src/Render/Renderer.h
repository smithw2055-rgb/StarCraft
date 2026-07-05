#pragma once

namespace Render
{

bool Initialize();
void Shutdown();
void BeginFrame();
void EndPass();
void PrepareNativeOverlay();
void Commit();
void EndFrame();
void ResetStateCache();
bool IsActive();
void SetClearColor(float R, float G, float B, float A = 1.0f);

} // namespace Render
