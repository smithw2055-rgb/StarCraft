#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Game::Client::GameLoop
{

std::string SanitizeCampaignText(std::string Text);
std::vector<std::string> WrapCampaignText(const std::string& Text, size_t MaxChars, size_t MaxLines);
const char* CampaignMessageKind(uint8_t ActionID);

} // namespace Game::Client::GameLoop
