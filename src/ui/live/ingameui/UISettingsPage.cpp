// license:GPLv3+

#include "core/stdafx.h"
#include "UISettingsPage.h"

#include "ui/live/LiveUI.h"

namespace VPX::InGameUI
{

UISettingsPage::UISettingsPage()
   : InGameUIPage("UI Settings"s, "Touch UI uses larger rows with a bottom action bar. Tap a row to select it, swipe left/right to adjust, long press to reset to default."s, SaveMode::Global)
{
}

void UISettingsPage::BuildPage()
{
   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_TouchUI, //
      [this]() { return m_player->m_ptable->m_settings.GetPlayer_TouchUI(); }, //
      [this](int, int v) { m_player->m_ptable->m_settings.SetPlayer_TouchUI(v, false); }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propPlayer_UIScale, 1.f, "%.2f"s, //
      [this]() { return m_player->m_ptable->m_settings.GetPlayer_UIScale(); }, //
      [this](float, float v) { m_player->m_ptable->m_settings.SetPlayer_UIScale(v, false); }));
}

}
