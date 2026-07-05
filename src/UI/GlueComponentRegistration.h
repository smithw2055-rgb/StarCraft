#pragma once

#include "UI/GlueAnimatedImage.h"
#include "UI/GlueMenuController.h"

#include <NsCore/Factory.h>
#include <NsCore/RegisterComponent.h>

namespace Game::Client::UI::Glue
{

inline void RegisterGlueComponents(bool IncludeMenuController)
{
    if (!Aero::Factory::IsComponentRegistered(Aero::TypeOf<GlueAnimatedImage>()->GetTypeId()))
        Aero::RegisterComponent<GlueAnimatedImage>();

    if (IncludeMenuController &&
        !Aero::Factory::IsComponentRegistered(Aero::TypeOf<GlueMenuController>()->GetTypeId()))
    {
        Aero::RegisterComponent<GlueMenuController>();
    }
}

} // namespace Game::Client::UI::Glue
