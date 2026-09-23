// license:GPLv3+

#pragma once

#include "InGameUIPage.h"

namespace VPX::InGameUI
{

class UISettingsPage final : public InGameUIPage
{
public:
   UISettingsPage();

private:
   void BuildPage() override;
};

}
