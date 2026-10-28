// license:GPLv3+

#pragma once

#include "InGameUIPage.h"

namespace VPX::InGameUI
{

class McpSettingsPage final : public InGameUIPage
{
public:
   McpSettingsPage();

private:
   void BuildPage() override;
   void ApplyAndRebuild();
};

}
