// license:GPLv3+

#include "core/stdafx.h"
#include "InGameUIPage.h"

#include "core/VPApp.h"
#include "fonts/IconsForkAwesome.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/SensorSetupPage.h"

#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"

namespace VPX::InGameUI
{

InGameUIPage::InGameUIPage(const string& title, const string& info, SaveMode saveMode)
   : m_player(g_pplayer)
   , m_title(title)
   , m_info(info)
   , m_saveMode(saveMode)
{
   assert(m_player);
}

void InGameUIPage::Open(bool isBackwardAnimation)
{
   m_openAnimElapsed = 0.f;
   m_openAnimTarget = 0.f;
   // Only apply if page is no more on screen (otherwise it would jump)
   if (m_openAnimPos == -1.f || m_openAnimPos == 1.f)
      m_openAnimPos = isBackwardAnimation ? -1.f : 1.f;
   m_openAnimStart = m_openAnimPos;
   m_selectedItem = 0;
   m_pressedItemLabel.clear();
   RequestRebuild();
}

void InGameUIPage::Close(bool isBackwardAnimation)
{
   m_openAnimElapsed = 0.f;
   m_openAnimTarget = isBackwardAnimation ? 1.f : -1.f;
   m_openAnimStart = m_openAnimPos;
}

void InGameUIPage::ClearItems()
{
   assert(m_isBuildingPage);
   m_items.clear();
}

InGameUIItem& InGameUIPage::AddItem(std::unique_ptr<InGameUIItem> item)
{
   assert(m_isBuildingPage);
   m_items.push_back(std::move(item));
   return *m_items.back();
}

Settings& InGameUIPage::GetSettings() { return m_player->m_ptable->m_settings; }

bool InGameUIPage::IsAdjustable() const
{
   for (const auto& item : m_items)
      if (item->IsAdjustable())
         return true;
   return false;
}

bool InGameUIPage::IsDefaults() const
{
   for (const auto& item : m_items)
      if (!item->m_excludeFromDefault && !item->IsDefaultValue())
         return false;
   return true;
}

bool InGameUIPage::IsModified() const
{
   for (const auto& item : m_items)
      if (item->IsModified())
         return true;
   return false;
}

InGameUIItem* InGameUIPage::GetItem(const string& label) const
{
   for (const auto& item : m_items)
      if (item->m_label == label)
         return item.get();
   return nullptr;
}

void InGameUIPage::ResetToStoredValues()
{
   if (!IsModified())
      return;
   // Note that changing the value of items may result in changing the content of m_items (page rebuilding)
   assert(!m_resettingToInitialValues);
   m_resettingToInitialValues = true;
   for (size_t i = 0; i < m_items.size(); i++)
      m_items[i]->ResetToStoredValue();
   g_pplayer->m_liveUI->PushNotification("Changes were undone"s, 5000);
   if (!IsModified())
      m_selectedItem = 0;
   m_resettingToInitialValues = false;
}

void InGameUIPage::ResetToDefaults()
{
   if (IsDefaults())
      return;
   // Note that changing the value of items may result in changing the content of m_items (page rebuilding)
   assert(!m_resettingToDefaults);
   m_resettingToDefaults = true;
   for (size_t i = 0; i < m_items.size(); i++)
      if (!m_items[i]->m_excludeFromDefault)
         m_items[i]->ResetToDefault();
   m_resetNotifId = g_pplayer->m_liveUI->PushNotification("Settings reset to defaults"s, 5000, m_resetNotifId);
   if (IsDefaults())
      m_selectedItem = 0;
   m_resettingToDefaults = false;
}

class SelectGlobalOrOverridePage final : public InGameUIPage
{
public:
   explicit SelectGlobalOrOverridePage(InGameUIPage* page, bool canSaveTableOverrides)
      : InGameUIPage(canSaveTableOverrides ? "Save globally or as a table override ?"s : "Save changes ?"s,
           canSaveTableOverrides
              ? "If saved globally, your changes will apply for all played tables.\nIf saved as an override, only the settings adjusted for this specific table will be saved."s
              : "This table was not saved yet, therefore the changes can only be saved globally."s,
           SaveMode::None)
      , m_page(page)
      , m_canSaveTableOverrides(canSaveTableOverrides)
   {
   }

private:
   void BuildPage()
   {
      AddItem(std::make_unique<InGameUIItem>("Save Globally"s, "Your changes will be saved as the new default for all tables."s,
         [this]()
         {
            m_page->SaveGlobally();
            m_player->m_liveUI->m_inGameUI.NavigateBack();
         }));
      if (m_canSaveTableOverrides)
         AddItem(std::make_unique<InGameUIItem>("Save as Table Override"s, "The settings adjusted for this specific table will be persisted for the next time you play this table."s,
            [this]()
            {
               m_page->SaveTableOverride();
               m_player->m_liveUI->m_inGameUI.NavigateBack();
            }));
      else
         AddItem(std::make_unique<InGameUIItem>("Cancel"s, "Get back"s,
            [this]()
            {
               m_player->m_liveUI->m_inGameUI.NavigateBack();
            }));
   };

   InGameUIPage* m_page;
   bool m_canSaveTableOverrides;
};

void InGameUIPage::Save()
{
   if (!IsModified())
      return;
   const bool canSaveTableOverrides = FileExists(m_player->m_ptable->m_filename);
   switch (m_saveMode)
   {
   case SaveMode::None: break;

   case SaveMode::Both:
      m_player->m_liveUI->m_inGameUI.AddPage("popup/save_select"s, [this, canSaveTableOverrides]() { return std::make_unique<SelectGlobalOrOverridePage>(this, canSaveTableOverrides); });
      m_player->m_liveUI->m_inGameUI.Navigate("popup/save_select"s);
      break;

   case SaveMode::Global: SaveGlobally(); break;

   case SaveMode::Table:
      if (!canSaveTableOverrides)
         m_player->m_liveUI->PushNotification("You need to save the table before saving table setting overrides"s, 5000);
      else
         SaveTableOverride();
      break;
   }
   if (!IsModified())
      m_selectedItem = 0;
}

void InGameUIPage::SaveGlobally()
{
   // First reset any table override
   Settings& tableSettings = m_player->m_ptable->m_settings;
   for (const auto& item : m_items)
      item->ResetSave(tableSettings);
   tableSettings.Save();
   // Then save to application settings
   Settings& appSettings = g_app->m_settings;
   for (const auto& item : m_items)
      item->Save(appSettings, false);
   appSettings.Save();
}

void InGameUIPage::SaveTableOverride()
{
   // First reset any table override (to start from a clear ground if saved items depends on user selection, note that some item may impact multiple settings so we save them afterward)
   Settings& tableSettings = m_player->m_ptable->m_settings;
   for (const auto& item : m_items)
      item->ResetSave(tableSettings);
   // Then save to table override
   for (const auto& item : m_items)
      item->Save(tableSettings, true);
   tableSettings.Save();
}

void InGameUIPage::SelectNextItem()
{
   m_pressedItemScroll = 0.f; // Start from top of item
   const int nItems = static_cast<int>(m_items.size());
   do
      m_selectedItem = (m_selectedItem + 1) % nItems;
   while (!m_items[m_selectedItem]->IsSelectable());
}

void InGameUIPage::SelectPrevItem()
{
   m_pressedItemScroll = 10000.f; // Start from bottom of item
   const int nItems = static_cast<int>(m_items.size());
   do
      m_selectedItem = (m_selectedItem + nItems - 1) % nItems;
   while (!m_items[m_selectedItem]->IsSelectable());
}

void InGameUIPage::AdjustItem(float direction, bool isInitialPress)
{
   if (m_selectedItem < 0 || m_selectedItem >= (int)m_items.size())
      return;
   const auto& item = m_items[m_selectedItem];
   const uint32_t now = msec();
   if (isInitialPress)
   {
      m_pressedItemLabel = item->m_label;
      m_pressStartMs = now;
      m_lastUpdateMs = now;
   }
   else if (m_pressedItemLabel != item->m_label)
   {
      // Different item, discard
      return;
   }
   const float elapsed = static_cast<float>(now - m_lastUpdateMs) / 1000.f;
   m_lastUpdateMs = now;
   const uint32_t elapsedSincePress = now - m_pressStartMs;
   float speedFactor;
   if (elapsedSincePress < 250)
      speedFactor = 1.0f;
   else if (elapsedSincePress < 500)
      speedFactor = 2.f;
   else if (elapsedSincePress < 1000)
      speedFactor = 4.f;
   else if (elapsedSincePress < 1500)
      speedFactor = 8.f;
   else
      speedFactor = 16.f;

   switch (item->m_type)
   {
   case InGameUIItem::Type::Label:
      if (item->m_labelType == InGameUIItem::LabelType::Markdown)
         m_pressedItemScroll += speedFactor * direction;
      break;

   case InGameUIItem::Type::Navigation: m_player->m_liveUI->m_inGameUI.Navigate(item->m_path); break;

   case InGameUIItem::Type::Runnable: item->m_runnable(); break;

   case InGameUIItem::Type::ResetToDefaults: // Defaults (allow continuously applying as defaults may be dynamic, for example for VR, headtracking, dynamic room exposure,...)
      ResetToDefaults();
      break;

   case InGameUIItem::Type::ResetToStoredValues: // Undo (allow continuously applying, if something else if modifying the values, as we do not forbid it)
      ResetToStoredValues();
      break;

   case InGameUIItem::Type::SaveChanges:
      if (isInitialPress)
         Save();
      break;

   case InGameUIItem::Type::Back:
      if (isInitialPress)
         m_player->m_liveUI->m_inGameUI.NavigateBack();
      break;

   case InGameUIItem::Type::ActionInputMapping:
      if (isInitialPress)
      {
         if (direction < 0.f)
         {
            item->m_inputAction->ClearMapping();
            // Base navigation items must always be mapped (otherwise navigation ends up broken), so immediately start a mapping definition
            if (item->m_inputAction->IsNavigationAction())
            {
               m_defineActionPopup = true;
               m_defineActionItem = item.get();
            }
         }
         else
         {
            m_defineActionPopup = true;
            m_defineActionItem = item.get();
         }
      }
      break;

   case InGameUIItem::Type::Property:
      switch (item->m_property->m_type)
      {
      case VPX::Properties::PropertyDef::Type::String:
         // Unsupported for now
         assert(false);
         break;

      case VPX::Properties::PropertyDef::Type::Enum:
         if (isInitialPress)
         {
            const int nValues = static_cast<int>(dynamic_cast<VPX::Properties::EnumPropertyDef*>(item->m_property.get())->m_values.size());
            if (direction < 0.f)
               item->SetValue((item->GetIntValue() + nValues - 1) % nValues);
            else
               item->SetValue((item->GetIntValue() + 1) % nValues);
         }
         break;

      case VPX::Properties::PropertyDef::Type::Float:
      {
         auto prop = dynamic_cast<const VPX::Properties::FloatPropertyDef*>(item->m_property.get());
         if (isInitialPress)
            m_adjustedValue = item->GetFloatValue();
         m_adjustedValue += direction * speedFactor * elapsed * (prop->m_max - prop->m_min) / 32.f;
         item->SetValue(prop->GetSteppedClamped(m_adjustedValue));
         break;
      }

      case VPX::Properties::PropertyDef::Type::Int:
      {
         auto prop = dynamic_cast<const VPX::Properties::IntPropertyDef*>(item->m_property.get());
         if (isInitialPress)
            m_adjustedValue = static_cast<float>(item->GetIntValue()) + direction;
         else if (elapsedSincePress > 100)
            m_adjustedValue += direction * speedFactor * elapsed * static_cast<float>(prop->m_max - prop->m_min) / 32.f;
         item->SetValue(prop->GetClamped(static_cast<int>(round(m_adjustedValue))));
         break;
      }

      case VPX::Properties::PropertyDef::Type::Bool:
         if (isInitialPress)
            item->SetValue(!item->GetBoolValue());
         break;
      }
      break;

   default: break;
   }
}

class EnumSelectPage final : public InGameUIPage
{
public:
   EnumSelectPage(InGameUIPage* page, const string& itemLabel, const string& info)
      : InGameUIPage(itemLabel, info, SaveMode::None)
      , m_page(page)
      , m_itemLabel(itemLabel)
   {
   }

   bool IsPlayerPauseAllowed() const override { return m_page->IsPlayerPauseAllowed(); }

private:
   void BuildPage() override
   {
      const InGameUIItem* item = m_page->GetItem(m_itemLabel);
      if (item == nullptr || item->m_type != InGameUIItem::Type::Property || item->m_property->m_type != VPX::Properties::PropertyDef::Type::Enum)
         return;
      const auto* prop = dynamic_cast<const VPX::Properties::EnumPropertyDef*>(item->m_property.get());
      const int current = item->GetIntValue();
      for (size_t i = 0; i < prop->m_values.size(); i++)
      {
         const int value = prop->m_min + static_cast<int>(i);
         if (value == current)
            SelectItem(static_cast<int>(i));
         AddItem(std::make_unique<InGameUIItem>((value == current ? ICON_FK_CHECK_CIRCLE "  "s : ICON_FK_CIRCLE_O "  "s) + prop->m_values[i], ""s,
            [this, value]()
            {
               if (InGameUIItem* target = m_page->GetItem(m_itemLabel))
                  target->SetValue(value);
               m_player->m_liveUI->m_inGameUI.NavigateBack();
            }));
      }
   }

   InGameUIPage* const m_page;
   const string m_itemLabel;
};

void InGameUIPage::OpenEnumPicker(int itemIndex)
{
   if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size()))
      return;
   const auto& item = m_items[itemIndex];
   if (item->m_type != InGameUIItem::Type::Property || item->m_property->m_type != VPX::Properties::PropertyDef::Type::Enum)
      return;
   const string label = item->m_label;
   const string info = item->m_tooltip;
   m_player->m_liveUI->m_inGameUI.AddPage("popup/enum_select"s, [this, label, info]() { return std::make_unique<EnumSelectPage>(this, label, info); });
   m_player->m_liveUI->m_inGameUI.Navigate("popup/enum_select"s);
}

void InGameUIPage::StepItem(int itemIndex, int direction)
{
   if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size()))
      return;
   const auto& item = m_items[itemIndex];
   if (item->m_type != InGameUIItem::Type::Property)
      return;
   switch (item->m_property->m_type)
   {
   case VPX::Properties::PropertyDef::Type::Float:
   {
      const auto* prop = dynamic_cast<const VPX::Properties::FloatPropertyDef*>(item->m_property.get());
      const float step = prop->m_step > 0.f ? prop->m_step : (prop->m_max - prop->m_min) / 100.f;
      item->SetValue(prop->GetSteppedClamped(item->GetFloatValue() + static_cast<float>(direction) * step));
      break;
   }
   case VPX::Properties::PropertyDef::Type::Int:
   {
      const auto* prop = dynamic_cast<const VPX::Properties::IntPropertyDef*>(item->m_property.get());
      item->SetValue(prop->GetClamped(item->GetIntValue() + direction));
      break;
   }
   case VPX::Properties::PropertyDef::Type::Enum:
   {
      const auto* prop = dynamic_cast<const VPX::Properties::EnumPropertyDef*>(item->m_property.get());
      const int nValues = static_cast<int>(prop->m_values.size());
      const int v = (item->GetIntValue() - prop->m_min + direction + nValues) % nValues;
      item->SetValue(prop->m_min + v);
      break;
   }
   case VPX::Properties::PropertyDef::Type::Bool: item->SetValue(direction > 0); break;
   default: break;
   }
}

void InGameUIPage::Render(float elapsedS)
{
   if (m_needsRebuild)
   {
      // Pages are always built here, before rendering and interaction handling, to avoid item lifecycle issues (for example when changing a setting may result in other settings being added/removed).
      assert(!m_isBuildingPage);
      m_needsRebuild = false;
      m_isBuildingPage = true;
      ClearItems();
      BuildPage();
      m_isBuildingPage = false;
   }

   ImGuiIO& io = ImGui::GetIO();
   const ImGuiStyle& style = ImGui::GetStyle();

   if (m_openAnimTarget != m_openAnimPos)
   {
      m_openAnimElapsed += elapsedS;
      m_openAnimPos = lerp(m_openAnimStart, m_openAnimTarget, smoothstep(0.f, 0.5f, m_openAnimElapsed));
      if (fabsf(m_openAnimTarget - m_openAnimPos) < 0.001f)
         m_openAnimPos = m_openAnimTarget;
   }
   const float animPos = m_openAnimPos;

   const bool touchUI = m_player->m_liveUI->IsTouchUI();
   const bool flipperNav = m_player->m_liveUI->m_inGameUI.IsFlipperNav();
   const bool useSelection = touchUI || flipperNav;
   const float dpi = m_player->m_liveUI->GetDPI();

   // EmulationStation like theme for touch UI
   constexpr ImU32 kSelector = IM_COL32(128, 128, 128, 180);
   constexpr ImU32 kLine = IM_COL32(255, 255, 255, 24);
   constexpr ImU32 kMuted = IM_COL32(150, 150, 150, 255);
   constexpr ImU32 kToggleOn = IM_COL32(190, 190, 190, 255);
   auto upper = [touchUI](const string& text)
   {
      if (!touchUI)
         return text;
      string result = text;
      std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      return result;
   };
   const float minDim = min(io.DisplaySize.x, io.DisplaySize.y);
   const bool smallScreen = minDim <= 480.f;
   int nPushedStyles = 0;
   int nPushedColors = 0;
   if (touchUI)
   {
      const float fontPx = clamp(0.045f * minDim * (smallScreen ? 1.31f : 1.f), 14.f, 32.f);
      ImGui::PushFont(nullptr, style.FontSizeBase * fontPx / ImGui::GetFontSize());
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 2.f, style.FramePadding.y * 2.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * 2.f));
      ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, style.ScrollbarSize * 1.2f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 2.f, style.WindowPadding.y * 1.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, smallScreen ? 0.f : 4.f * dpi);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * dpi);
      ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.f * dpi);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, smallScreen ? 0.f : 1.f);
      nPushedStyles = 8;
      ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(28, 28, 28, 245));
      ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 60));
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(236, 236, 236, 255));
      ImGui::PushStyleColor(ImGuiCol_TextDisabled, kMuted);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 18));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 30));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 42));
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 30));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 60));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(255, 255, 255, 60));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(255, 255, 255, 90));
      ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(255, 255, 255, 120));
      ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(255, 255, 255, 40));
      nPushedColors = 15;
      ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.f, 0.f), io.DisplaySize, IM_COL32(0, 0, 0, static_cast<int>(128.f * (1.f - fabsf(animPos)))));
   }

   ImGui::SetNextWindowBgAlpha(m_player->m_renderer->m_vrApplyColorKey ? 1.f : (touchUI ? 0.96f : 0.666f));
   // Size is selected to match pinball instruction cards format which have an aspect ratio of roughly 6" x 3.25" (WPC, other varies), except for touch UI where we favor size
   constexpr float pinballCardAR = 6.f / 3.5f;
   ImVec2 winSize;
   if (m_player->m_vrDevice)
   {
      winSize.x = min(0.25f * io.DisplaySize.x, 0.25f * io.DisplaySize.y);
      winSize.y = (1 / pinballCardAR) * winSize.x + 5.f * ImGui::GetTextLineHeightWithSpacing();
      ImGui::SetNextWindowPos(ImVec2((animPos + 0.5f) * io.DisplaySize.x, 0.5f * io.DisplaySize.y), 0, ImVec2(0.5f, 0.5f));
   }
   else if (touchUI)
   {
      if (smallScreen)
         winSize = io.DisplaySize;
      else if (io.DisplaySize.x > io.DisplaySize.y)
         winSize = ImVec2(min(1.25f * io.DisplaySize.y, 0.9f * io.DisplaySize.x), 0.88f * io.DisplaySize.y);
      else
         winSize = ImVec2(0.94f * io.DisplaySize.x, 0.88f * io.DisplaySize.y);
      ImGui::SetNextWindowPos(ImVec2((animPos + 0.5f) * io.DisplaySize.x, 0.5f * io.DisplaySize.y), 0, ImVec2(0.5f, 0.5f));
   }
   else if (io.DisplaySize.x > io.DisplaySize.y)
   { // Landscape mode, fit on height
      winSize.y = 0.5f * io.DisplaySize.y;
      winSize.x = pinballCardAR * (winSize.y - 5.f * ImGui::GetTextLineHeightWithSpacing());
      ImGui::SetNextWindowPos(ImVec2((animPos + 0.5f) * io.DisplaySize.x, 0.9f * io.DisplaySize.y), 0, ImVec2(0.5f, 1.f));
   }
   else
   { // Portrait mode, fit on width
      winSize.x = 0.8f * io.DisplaySize.x;
      winSize.y = (1.f / pinballCardAR) * winSize.x + 5.f * ImGui::GetTextLineHeightWithSpacing();
      ImGui::SetNextWindowPos(ImVec2((animPos + 0.5f) * io.DisplaySize.x, 0.8f * io.DisplaySize.y), 0, ImVec2(0.5f, 1.f));
   }
   ImGui::SetNextWindowSize(winSize);
   ImGui::Begin(std::to_string(reinterpret_cast<size_t>(this)).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

   if (touchUI)
      ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(255, 255, 255, 40));
   else
      ImGui::PushStyleColor(ImGuiCol_Separator, style.Colors[ImGuiCol_Text]);
   const float winMinX = ImGui::GetWindowPos().x;
   const float winMaxX = winMinX + ImGui::GetWindowSize().x;

   const bool isAdjustable = IsAdjustable();
   const ImVec2 itemPaddingBase = style.ItemSpacing;

   // Page actions (rendered in the header, or in a bottom action bar for touch UI)
   std::erase_if(m_items,
      [](auto& item)
      {
         using enum InGameUIItem::Type;
         return item->m_type == SaveChanges || item->m_type == ResetToDefaults || item->m_type == ResetToStoredValues || item->m_type == Back;
      });
   struct ActionDef
   {
      InGameUIItem::Type type;
      const char* icon;
      const char* label;
      bool enabled;
      int itemIndex = -1;
      bool hovered = false;
   };
   vector<ActionDef> actions;
   if (isAdjustable)
   {
      actions.push_back({ InGameUIItem::Type::ResetToDefaults, ICON_FK_HEART, "Defaults", !IsDefaults() });
      actions.push_back({ InGameUIItem::Type::ResetToStoredValues, ICON_FK_UNDO, "Undo", IsModified() });
      if (m_saveMode != SaveMode::None)
         actions.push_back({ InGameUIItem::Type::SaveChanges, ICON_FK_FLOPPY_O, "Save", IsModified() });
   }
   actions.push_back({ InGameUIItem::Type::Back, ICON_FK_REPLY, "Back", true });
   for (auto& action : actions)
   {
      if (action.enabled)
      {
         m_items.push_back(std::make_unique<InGameUIItem>(action.type));
         action.itemIndex = static_cast<int>(m_items.size()) - 1;
      }
   }
   auto renderAction = [&](ActionDef& action, const ImVec2& size)
   {
      const bool highlighted = flipperNav && action.enabled && (m_selectedItem == action.itemIndex);
      ImGui::BeginDisabled(!action.enabled);
      if (highlighted)
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
      const string label = touchUI ? upper(action.label) : action.icon;
      if (ImGui::Button(label.c_str(), size))
      {
         m_selectedItem = action.itemIndex;
         AdjustItem(1.f, true);
      }
      if (highlighted)
         ImGui::PopStyleColor();
      action.hovered = ImGui::IsItemHovered();
      ImGui::EndDisabled();
   };

   // Header
   if (touchUI)
   {
      ImGui::PushFont(nullptr, style.FontSizeBase * 1.3f);
      const string title = upper(m_title);
      ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(title.c_str()).x) * 0.5f);
      ImGui::Text("%s", title.c_str());
      ImGui::PopFont();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + itemPaddingBase.y * 0.5f);
      ImGui::Separator();
   }
   else
   {
      ImGui::SeparatorText(m_title.c_str());
      float buttonWidth = ImGui::CalcTextSize(ICON_FK_REPLY, nullptr, true).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
      if (isAdjustable)
      {
         buttonWidth += ImGui::CalcTextSize(ICON_FK_HEART, nullptr, true).x + style.FramePadding.x * 2.0f;
         buttonWidth += style.ItemSpacing.x;
         buttonWidth += ImGui::CalcTextSize(ICON_FK_UNDO, nullptr, true).x + style.FramePadding.x * 2.0f;
         buttonWidth += style.ItemSpacing.x;
         buttonWidth += ImGui::CalcTextSize(ICON_FK_FLOPPY_O, nullptr, true).x + style.FramePadding.x * 2.0f;
         buttonWidth += style.ItemSpacing.x;
      }
      ImGui::SameLine(ImGui::GetWindowSize().x - buttonWidth);
      for (auto& action : actions)
      {
         if (action.type == InGameUIItem::Type::Back)
            continue;
         renderAction(action, ImVec2(0.f, 0.f));
         ImGui::SameLine();
      }
      if (isAdjustable && m_saveMode == SaveMode::None)
      {
         ImGui::Dummy(ImGui::CalcTextSize(ICON_FK_FLOPPY_O, nullptr, true) + style.FramePadding * 2.0f);
         ImGui::SameLine();
      }
      renderAction(actions.back(), ImVec2(0.f, 0.f));
   }

   // As we may have changed the number of selectable items, ensure m_selectedItem is still valid and pointing to a selectable item
   {
      m_selectedItem = clamp(m_selectedItem, 0, static_cast<int>(m_items.size()) - 1);
      while (!m_items[m_selectedItem]->IsSelectable())
         m_selectedItem = (m_selectedItem + 1) % static_cast<int>(m_items.size());
   }

   // Page items
   // Note that items may trigger state change which in turn may trigger a rebuild of the page (changing m_items)
   const InGameUIItem* hoveredItem = nullptr;
   const ImVec2 itemPadding = style.ItemSpacing;
   const float actionBarHeight = touchUI ? ImGui::GetFrameHeight() * 1.15f + itemPadding.y * 3.f : 0.f;
   const float infoHeight = touchUI ? ImGui::GetTextLineHeight() * 0.85f * 2.6f + itemPadding.y : ImGui::GetTextLineHeight() * 3.f + itemPadding.y * 2.f;
   ImGui::BeginChild("PageItems", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - infoHeight - actionBarHeight), ImGuiChildFlags_None,
      ImGuiWindowFlags_NoBackground | (touchUI ? ImGuiWindowFlags_NoScrollbar : ImGuiWindowFlags_None));

   // Restore scroll position when navigating back to a page (needs one frame for ImGui to know the content size)
   if (m_pendingScrollY >= 0.f && m_pendingScrollFrames++ > 0)
   {
      ImGui::SetScrollY(m_pendingScrollY);
      m_pendingScrollY = -1.f;
   }

   // Touch gestures: tap to select/activate, vertical drag to scroll (with inertia), horizontal drag to adjust, long press to reset to default
   const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
   const uint32_t now = msec();
   if (touchUI)
   {
      const float dragThreshold = 8.f * dpi;
      if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         m_gesture = Gesture::Pending;
         m_gestureStartPos = ImGui::GetMousePos();
         m_gestureStartMs = now;
         m_gestureItem = -1;
         m_dragScrollVelocity = 0.f;
      }
      const bool gestureOnProperty = m_gestureItem >= 0 && m_gestureItem < static_cast<int>(m_items.size()) && m_items[m_gestureItem]->m_type == InGameUIItem::Type::Property;
      if (m_gesture == Gesture::Pending && mouseDown)
      {
         const ImVec2 delta = ImGui::GetMousePos() - m_gestureStartPos;
         if (fabsf(delta.y) > dragThreshold && fabsf(delta.y) >= fabsf(delta.x))
         {
            m_gesture = Gesture::Scroll;
            ImGui::ClearActiveID();
         }
         else if (fabsf(delta.x) > dragThreshold && gestureOnProperty)
         {
            m_gesture = Gesture::Adjust;
            m_selectedItem = m_gestureItem;
            ImGui::ClearActiveID();
            const auto& item = m_items[m_gestureItem];
            switch (item->m_property->m_type)
            {
            case VPX::Properties::PropertyDef::Type::Float: m_gestureStartValue = item->GetFloatValue(); break;
            case VPX::Properties::PropertyDef::Type::Int: m_gestureStartValue = static_cast<float>(item->GetIntValue()); break;
            default: StepItem(m_gestureItem, delta.x > 0.f ? 1 : -1); break;
            }
         }
         else if ((now - m_gestureStartMs) > 1000 && fabsf(delta.x) <= dragThreshold && fabsf(delta.y) <= dragThreshold && gestureOnProperty && !m_items[m_gestureItem]->m_excludeFromDefault
            && !m_items[m_gestureItem]->IsDefaultValue() && !ImGui::IsAnyItemActive())
         {
            m_gesture = Gesture::Done;
            m_selectedItem = m_gestureItem;
            ImGui::ClearActiveID();
            m_items[m_gestureItem]->ResetToDefault();
            m_resetNotifId = m_player->m_liveUI->PushNotification(std::format("'{}' reset to default", m_items[m_gestureItem]->m_label), 3000, m_resetNotifId);
         }
      }
      constexpr float kFriction = 8.f;
      constexpr float kVelocitySmooth = 0.2f;
      if (m_gesture == Gesture::Scroll)
      {
         if (mouseDown)
         {
            const float dy = io.MouseDelta.y;
            ImGui::SetScrollY(ImGui::GetScrollY() - dy);
            const float frameVel = (elapsedS > 0.f) ? (-dy / elapsedS) : 0.f;
            m_dragScrollVelocity = m_dragScrollVelocity * (1.f - kVelocitySmooth) + frameVel * kVelocitySmooth;
         }
         else
         {
            m_gesture = Gesture::None;
         }
      }
      else if (m_gesture == Gesture::None && fabsf(m_dragScrollVelocity) > 0.5f)
      {
         ImGui::SetScrollY(ImGui::GetScrollY() + m_dragScrollVelocity * elapsedS);
         m_dragScrollVelocity *= max(0.f, 1.f - kFriction * elapsedS);
         if (fabsf(m_dragScrollVelocity) < 0.5f)
            m_dragScrollVelocity = 0.f;
      }
   }

   const ImVec2 listClipMin = ImGui::GetWindowPos();
   const ImVec2 listClipMax = listClipMin + ImGui::GetWindowSize();
   const bool blockItems = touchUI && (m_gesture == Gesture::Scroll || m_gesture == Gesture::Adjust || m_gesture == Gesture::Done);
   if (blockItems)
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
   float maxLabelWidth = 0.f;
   for (const auto& item : m_items)
      if (item->IsAdjustable())
         maxLabelWidth = max(maxLabelWidth, ImGui::CalcTextSize(upper(item->m_label).c_str()).x);
   maxLabelWidth = min(maxLabelWidth, ImGui::CalcTextSize("Maximum label length before ellipsis").x);
   const float rowStartScreenX = ImGui::GetCursorScreenPos().x;
   const float labelEndScreenX = rowStartScreenX + maxLabelWidth + style.ItemSpacing.x * 2.0f + 30.f;
   const float itemEndScreenX = rowStartScreenX + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("*", nullptr, true).x - itemPadding.x;
   const bool stackFields = !touchUI && (itemEndScreenX - labelEndScreenX) < 12.f * ImGui::GetFontSize();
   const float rowWidth = itemEndScreenX - rowStartScreenX;
   const float touchLabelStartX = rowStartScreenX + itemPadding.x;
   const float sliderWidgetWidth = max(6.f * ImGui::GetFontSize(), (smallScreen ? 0.25f : 0.15f) * io.DisplaySize.x);
   const float labelMaxWidth = stackFields ? itemEndScreenX - rowStartScreenX : maxLabelWidth;
   const float closeButtonWidth = ImGui::CalcTextSize(ICON_FK_TIMES, nullptr, true).x + style.FramePadding.x * 2.0f;
   const float circleTextWidth = ImGui::CalcTextSize(ICON_FK_CIRCLE, nullptr, true).x + style.FramePadding.x * 2.0f;
   const float arrowPadding = 4.f * dpi;
   const float stepButtonWidth = ImGui::CalcTextSize(ICON_FK_ANGLE_LEFT, nullptr, true).x + arrowPadding * 2.0f;
   const float touchRowMinHeight = 1.9f * ImGui::GetFontSize();
   auto arrowButton = [&](const char* icon, const string& id)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(arrowPadding, style.FramePadding.y));
      ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
      const bool pressed = ImGui::Button((string(icon) + id).c_str(), ImVec2(stepButtonWidth, 0));
      ImGui::PopItemFlag();
      ImGui::PopStyleVar();
      return pressed;
   };
   auto placeTouchWidget = [&](float widgetWidth) { ImGui::SameLine(itemEndScreenX - rowStartScreenX - widgetWidth); };
   auto touchLabelWidth = [&](float widgetWidth) { return itemEndScreenX - touchLabelStartX - widgetWidth - style.ItemSpacing.x; };
   auto tapSide = [&]()
   {
      const ImVec2 rectMin = ImGui::GetItemRectMin();
      const ImVec2 rectMax = ImGui::GetItemRectMax();
      if (m_gestureStartPos.x < rectMin.x || m_gestureStartPos.x > rectMax.x)
         return 0;
      return m_gestureStartPos.x < (rectMin.x + rectMax.x) * 0.5f ? -1 : 1;
   };
   auto drawValueBar = [&](float t, float width)
   {
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float h = ImGui::GetFrameHeight();
      const float cy = pos.y + h * 0.5f;
      const float knobRadius = h * 0.28f;
      const float x0 = pos.x + knobRadius;
      const float x1 = pos.x + width - knobRadius;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), IM_COL32(255, 255, 255, 70), 2.f * dpi);
      dl->AddLine(ImVec2(x0, cy), ImVec2(lerp(x0, x1, t), cy), IM_COL32(236, 236, 236, 255), 2.f * dpi);
      dl->AddCircleFilled(ImVec2(lerp(x0, x1, t), cy), knobRadius, IM_COL32(236, 236, 236, 255));
      ImGui::Dummy(ImVec2(width, h));
   };
   for (int i = 0; i < (int)m_items.size(); i++)
   {
      // A value change handled while rendering a previous item may have requested a page rebuild (e.g.
      // switching a window output to embedded destroys the window that the following items query). The
      // rebuild is deferred to the next frame, so stop here to avoid evaluating the now-stale items
      // (their live getters would dereference freed state).
      if (m_needsRebuild)
         break;

      using enum InGameUIItem::Type;
      const auto& item = m_items[i];

      // Skip as these items are rendered in the header
      if (item->m_type == Back || item->m_type == SaveChanges || item->m_type == ResetToDefaults || item->m_type == ResetToStoredValues)
         continue;

      const bool isStackedItem = stackFields && item->IsAdjustable() && !(item->m_type == Property && item->m_property->m_type == VPX::Properties::PropertyDef::Type::Bool);
      const float contentHeight = isStackedItem ? ImGui::GetTextLineHeight() + itemPadding.y + ImGui::GetFrameHeight() : (touchUI && item->IsAdjustable()) ? ImGui::GetFrameHeight() : ImGui::GetTextLineHeight();
      const bool isTouchRow = touchUI && item->IsSelectable() && !(item->m_type == Label && item->m_labelType == InGameUIItem::LabelType::Markdown);
      const float rowHeight = isTouchRow ? max(contentHeight, touchRowMinHeight) : contentHeight;
      const float itemHeight = rowHeight + itemPadding.y * 2.f;
      const float rowTop = ImGui::GetCursorScreenPos().y;
      const bool isMouseOver = (ImGui::IsWindowHovered()) && (ImGui::GetMousePos().y >= ImGui::GetCursorScreenPos().y - itemPadding.y - 1.f)
         && (ImGui::GetMousePos().y <= ImGui::GetCursorScreenPos().y + itemHeight - itemPadding.y);
      if (touchUI && m_gesture == Gesture::Pending && m_gestureItem < 0 && isMouseOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         m_gestureItem = i;
      const bool tapped = touchUI && m_gesture == Gesture::Pending && m_gestureItem == i && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
      if (tapped && item->IsSelectable())
         m_selectedItem = i;
      const bool hovered = useSelection ? (i == m_selectedItem) : (isMouseOver && item->IsSelectable());
      const bool tintText = hovered && !touchUI;
      if (isTouchRow)
      {
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->PushClipRect(ImVec2(winMinX, listClipMin.y), ImVec2(winMaxX, listClipMax.y), false);
         if (hovered)
            dl->AddRectFilled(ImVec2(winMinX + 1.f, rowTop - itemPadding.y * 0.5f), ImVec2(winMaxX - 1.f, rowTop + rowHeight + itemPadding.y * 0.5f), kSelector);
         dl->AddLine(ImVec2(rowStartScreenX, rowTop + rowHeight + itemPadding.y * 0.5f), ImVec2(itemEndScreenX + itemPadding.x, rowTop + rowHeight + itemPadding.y * 0.5f), kLine, 1.f);
         dl->PopClipRect();
      }
      if (hovered)
      {
         hoveredItem = item.get();
         if (tintText)
         {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos() - itemPadding,
               ImGui::GetCursorScreenPos() + ImVec2(itemPadding.x, itemPadding.y * 2.f) + ImVec2(itemEndScreenX - ImGui::GetCursorScreenPos().x + itemPadding.x, rowHeight),
               IM_COL32(0, 255, 0, 50));
         }
         if (flipperNav)
         {
            if (ImGui::GetCursorPosY() - ImGui::GetScrollY() < 0.f)
               ImGui::SetScrollY(ImGui::GetCursorPosY());
            else if (ImGui::GetCursorPosY() - ImGui::GetScrollY() > ImGui::GetWindowHeight() - itemHeight)
               ImGui::SetScrollY(ImGui::GetCursorPosY() - ImGui::GetWindowHeight() + itemHeight);
         }
      }
      else if (!useSelection && isMouseOver)
      {
         hoveredItem = item.get();
      }
      if (isTouchRow)
         ImGui::SetCursorScreenPos(ImVec2(touchLabelStartX, rowTop + (rowHeight - contentHeight) * 0.5f));
      const bool alignLabel = touchUI && item->IsAdjustable() && !isStackedItem;

      switch (item->m_type)
      {
      case Label:
      {
         float infoHeight = ImGui::GetCursorPosY();
         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
         ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x - 60.f);
         switch (item->m_labelType)
         {
         case InGameUIItem::LabelType::Info:
            ImGui::TextWrapped("%s", item->m_label.c_str());
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
            break;
         case InGameUIItem::LabelType::Header:
         {
            if (touchUI)
            {
               ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
               ImGui::PushFont(nullptr, style.FontSizeBase * 0.85f);
               ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
               ImGui::Text("%s", upper(item->m_label).c_str());
               ImGui::PopStyleColor();
               ImGui::PopFont();
            }
            else
            {
               ImGui::Text("%s", item->m_label.c_str());
               ImVec2 min = ImGui::GetItemRectMin();
               ImVec2 max = ImGui::GetItemRectMax();
               min.y = max.y;
               ImGui::GetWindowDrawList()->AddLine(min, max, IM_COL32_WHITE, 1.0f);
            }
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
            break;
         }
         case InGameUIItem::LabelType::Markdown:
         {
            m_player->m_liveUI->SetMarkdownStartId(ImGui::GetItemID());
            ImGui::Markdown(item->m_label.c_str(), item->m_label.length(), m_player->m_liveUI->GetMarkdownConfig());
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
            infoHeight = ImGui::GetCursorPosY() - infoHeight;
            if (hovered && flipperNav && infoHeight > ImGui::GetWindowHeight())
            {
               const float scrollSpread = infoHeight - ImGui::GetWindowHeight();
               m_pressedItemScroll = clamp(m_pressedItemScroll, 0.f, scrollSpread);
               ImGui::SetScrollY(ImGui::GetCursorPosY() - infoHeight + m_pressedItemScroll);
            }
            break;
         }
         }
         break;
      }

      case Navigation:
      case Runnable:
         if (touchUI)
         {
            ImGui::Text("%s", upper(item->m_label).c_str());
            if (item->m_type == Navigation)
            {
               ImGui::SameLine(itemEndScreenX - rowStartScreenX - ImGui::CalcTextSize(ICON_FK_ANGLE_RIGHT).x);
               ImGui::TextDisabled(ICON_FK_ANGLE_RIGHT);
            }
         }
         else
         {
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
            ImGui::Text(ICON_FK_ANGLE_DOUBLE_RIGHT);
            ImGui::SameLine(0.f, 10.f);
            ImGui::Text("%s", item->m_label.c_str());
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
         }
         if (touchUI ? tapped : (isMouseOver && ImGui::IsMouseReleased(ImGuiMouseButton_::ImGuiMouseButton_Left)))
         {
            m_selectedItem = i;
            AdjustItem(1.f, true);
         }
         break;

      case CustomRender:
      {
         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
         if (item->m_customRender)
         {
            item->m_customRender(i, item.get());
         }
         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, itemPadding.y));
         break;
      }

      case ActionInputMapping:
      {
         if (alignLabel)
            ImGui::AlignTextToFramePadding();
         TextWithEllipsis(upper(item->m_label), touchUI ? touchLabelWidth(0.5f * rowWidth) : labelMaxWidth);
         if (touchUI)
            placeTouchWidget(0.5f * rowWidth);
         if (item->m_inputAction->IsMapped())
         {
            if (!stackFields && !touchUI)
               ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x);
            if (ImGui::Button(std::format("{}##Item{:d}", ICON_FK_TIMES, i).c_str(), ImVec2(closeButtonWidth, 0)))
            {
               item->m_inputAction->ClearMapping();
               if (item->m_inputAction->IsNavigationAction())
               {
                  m_defineActionPopup = true;
                  m_defineActionItem = item.get();
               }
            }
            ImGui::SameLine();
         }
         else if (!stackFields && !touchUI)
         {
            ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x + closeButtonWidth + style.ItemSpacing.x);
         }
         else if (touchUI)
         {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + closeButtonWidth + style.ItemSpacing.x);
         }
         else
         {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + closeButtonWidth + style.ItemSpacing.x);
         }
         const string mappingLabel = item->m_inputAction->GetMappingLabel();
         const float mapButtonWidth = itemEndScreenX - ImGui::GetCursorScreenPos().x - circleTextWidth - style.ItemSpacing.x;
         if (ImGui::Button(std::format("{}##Item{}", mappingLabel, i).c_str(), ImVec2(mapButtonWidth, 0)))
         {
            m_defineActionPopup = true;
            m_defineActionItem = item.get();
         }
         if (ImGui::CalcTextSize(mappingLabel.c_str()).x >= mapButtonWidth - style.ItemSpacing.x * 2.f)
            ImGui::SetItemTooltip("%s", mappingLabel.c_str());
         {
            string state = item->m_inputAction->IsPressed() ? ICON_FK_CIRCLE : ICON_FK_CIRCLE_O;
            ImGui::SameLine(itemEndScreenX - ImGui::GetCursorScreenPos().x - circleTextWidth);
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", state.c_str());
         }
         if (item->IsModified())
         {
            ImGui::SameLine(itemEndScreenX - ImGui::GetCursorScreenPos().x);
            ImGui::Text(ICON_FK_PENCIL);
         }
         break;
      }

      case Property:
      {
         auto renderStateIcon = [&]()
         {
            if (item->IsModified())
            {
               ImGui::SameLine(itemEndScreenX - ImGui::GetCursorScreenPos().x);
               if (alignLabel)
                  ImGui::AlignTextToFramePadding();
               ImGui::Text(ICON_FK_PENCIL);
            }
            else if (item->IsOverriden(g_app->m_settings, m_player->m_ptable->m_settings))
            {
               ImGui::SameLine(itemEndScreenX - ImGui::GetCursorScreenPos().x);
               if (alignLabel)
                  ImGui::AlignTextToFramePadding();
               ImGui::Text(ICON_FK_DOT_CIRCLE_O);
            }
         };
         switch (item->m_property->m_type)
         {
         case VPX::Properties::PropertyDef::Type::Float:
         {
            auto prop = dynamic_cast<VPX::Properties::FloatPropertyDef*>(item->m_property.get());
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            if (touchUI)
            {
               char maxText[64];
               snprintf(maxText, sizeof(maxText), item->m_format.c_str(), prop->m_max * item->m_floatValueDisplayScale);
               TextWithEllipsis(upper(prop->m_label), touchLabelWidth(sliderWidgetWidth + ImGui::CalcTextSize(maxText).x + style.ItemSpacing.x));
            }
            else
            {
               TextWithEllipsis(prop->m_label, labelMaxWidth);
               if (!stackFields)
                  ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x);
            }
            if (touchUI)
            {
               char maxText[64];
               snprintf(maxText, sizeof(maxText), item->m_format.c_str(), prop->m_max * item->m_floatValueDisplayScale);
               const float valueWidth = ImGui::CalcTextSize(maxText).x;
               placeTouchWidget(sliderWidgetWidth + valueWidth + style.ItemSpacing.x);
               if (m_gesture == Gesture::Adjust && m_gestureItem == i)
                  item->SetValue(prop->GetSteppedClamped(m_gestureStartValue + (ImGui::GetMousePos().x - m_gestureStartPos.x) / rowWidth * (prop->m_max - prop->m_min)));
               const float v = item->GetFloatValue();
               drawValueBar((v - prop->m_min) / max(0.0001f, prop->m_max - prop->m_min), sliderWidgetWidth);
               const int side = tapped ? tapSide() : 0;
               char valueText[64];
               snprintf(valueText, sizeof(valueText), item->m_format.c_str(), v * item->m_floatValueDisplayScale);
               ImGui::SameLine(itemEndScreenX - rowStartScreenX - ImGui::CalcTextSize(valueText).x);
               ImGui::AlignTextToFramePadding();
               ImGui::TextUnformatted(valueText);
               renderStateIcon();
               if (side != 0)
                  StepItem(i, side);
            }
            else
            {
               float v = item->GetFloatValue() * item->m_floatValueDisplayScale;
               ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x);
               ImGui::SliderFloat(std::format("##Item{}", i).c_str(), &v, prop->m_min * item->m_floatValueDisplayScale,
                  prop->m_max * item->m_floatValueDisplayScale, item->m_format.c_str(),
                  ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
               renderStateIcon();
               item->SetValue(v / item->m_floatValueDisplayScale);
            }
            break;
         }

         case VPX::Properties::PropertyDef::Type::Int:
         {
            auto prop = dynamic_cast<VPX::Properties::IntPropertyDef*>(item->m_property.get());
            const auto id = Settings::GetRegistry().GetPropertyId(item->m_property->m_groupId, item->m_property->m_propId);
            const bool isWindowSize = id.has_value() && (((Settings::m_propPlayer_PlayfieldWidth.index == id.value().index) || (Settings::m_propPlayer_PlayfieldHeight.index == id.value().index)) && !flipperNav);
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            if (touchUI && !isWindowSize)
            {
               char maxText[64];
               snprintf(maxText, sizeof(maxText), item->m_format.c_str(), prop->m_max);
               TextWithEllipsis(upper(prop->m_label), touchLabelWidth(sliderWidgetWidth + ImGui::CalcTextSize(maxText).x + style.ItemSpacing.x));
            }
            else
            {
               TextWithEllipsis(touchUI ? upper(prop->m_label) : prop->m_label, touchUI ? touchLabelWidth(0.5f * rowWidth) : labelMaxWidth);
               if (!stackFields)
                  ImGui::SameLine(touchUI ? itemEndScreenX - rowStartScreenX - 0.5f * rowWidth : labelEndScreenX - ImGui::GetCursorScreenPos().x);
            }
            int v = item->GetIntValue();
            if (touchUI && !isWindowSize)
            {
               char maxText[64];
               snprintf(maxText, sizeof(maxText), item->m_format.c_str(), prop->m_max);
               const float valueWidth = ImGui::CalcTextSize(maxText).x;
               placeTouchWidget(sliderWidgetWidth + valueWidth + style.ItemSpacing.x);
               if (m_gesture == Gesture::Adjust && m_gestureItem == i)
                  item->SetValue(prop->GetClamped(static_cast<int>(round(m_gestureStartValue + (ImGui::GetMousePos().x - m_gestureStartPos.x) / rowWidth * static_cast<float>(prop->m_max - prop->m_min)))));
               v = item->GetIntValue();
               drawValueBar(static_cast<float>(v - prop->m_min) / max(1.f, static_cast<float>(prop->m_max - prop->m_min)), sliderWidgetWidth);
               const int side = tapped ? tapSide() : 0;
               char valueText[64];
               snprintf(valueText, sizeof(valueText), item->m_format.c_str(), v);
               ImGui::SameLine(itemEndScreenX - rowStartScreenX - ImGui::CalcTextSize(valueText).x);
               ImGui::AlignTextToFramePadding();
               ImGui::TextUnformatted(valueText);
               renderStateIcon();
               if (side != 0)
                  StepItem(i, side);
               break;
            }
            if (isWindowSize)
            {
               // Special handling for editing main window size as mouse interaction would break (since the control is moved/resized while interacted)
               const float butWidth = ImGui::CalcTextSize(ICON_FK_ARROWS_H, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
               ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x - ImGui::GetStyle().ItemSpacing.x - butWidth);
               ImGui::InputInt(std::format("##Item{}", i).c_str(), &v, 1, 100, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
               ImGui::SameLine();
               if (ImGui::Button((Settings::m_propPlayer_PlayfieldWidth.index == id.value().index) ? ICON_FK_ARROWS_H : ICON_FK_ARROWS_V))
                  v = prop->m_max;
            }
            else
            {
               ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x);
               ImGui::SliderInt(std::format("##Item{}", i).c_str(), &v, prop->m_min, prop->m_max, item->m_format.c_str(), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
            }
            renderStateIcon();
            item->SetValue(v);
            break;
         }

         case VPX::Properties::PropertyDef::Type::Enum:
         {
            auto prop = dynamic_cast<VPX::Properties::EnumPropertyDef*>(item->m_property.get());
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            int v = item->GetIntValue() - prop->m_min;
            if (touchUI)
            {
               const string valueLabel = upper((v >= 0 && v < static_cast<int>(prop->m_values.size())) ? prop->m_values[v] : ""s);
               const float maxValueWidth = 0.55f * rowWidth - stepButtonWidth * 2.f - style.ItemSpacing.x * 2.f;
               const float valueWidth = min(ImGui::CalcTextSize(valueLabel.c_str()).x, maxValueWidth);
               const float widgetWidth = valueWidth + stepButtonWidth * 2.f + style.ItemSpacing.x * 2.f;
               TextWithEllipsis(upper(prop->m_label), touchLabelWidth(widgetWidth));
               placeTouchWidget(widgetWidth);
               bool arrowPressed = false;
               if (arrowButton(ICON_FK_ANGLE_LEFT, std::format("##Dec{}", i)))
               {
                  m_selectedItem = i;
                  StepItem(i, -1);
                  arrowPressed = true;
               }
               ImGui::SameLine();
               ImGui::AlignTextToFramePadding();
               TextWithEllipsis(valueLabel, maxValueWidth);
               ImGui::SameLine();
               if (arrowButton(ICON_FK_ANGLE_RIGHT, std::format("##Inc{}", i)))
               {
                  m_selectedItem = i;
                  StepItem(i, 1);
                  arrowPressed = true;
               }
               renderStateIcon();
               if (tapped && !arrowPressed && !ImGui::IsAnyItemActive())
                  OpenEnumPicker(i);
            }
            else
            {
               TextWithEllipsis(prop->m_label, labelMaxWidth);
               if (!stackFields)
                  ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x);
               ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x);
               ImGui::Combo(std::format("##Item{}", i).c_str(), &v,
                  [](void* data, int idx)
                  {
                     const auto* vec = static_cast<const vector<string>*>(data);
                     if (idx < 0 || idx >= (int)vec->size())
                        return "";
                     return vec->at(idx).c_str();
                  },
                  (void*)&prop->m_values, (int)prop->m_values.size());
               renderStateIcon();
               item->SetValue(prop->m_min + v);
            }
            break;
         }

         case VPX::Properties::PropertyDef::Type::Bool:
         {
            auto prop = dynamic_cast<VPX::Properties::BoolPropertyDef*>(item->m_property.get());
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            const float toggleWidth = ImGui::GetFrameHeight() * 1.75f;
            if (touchUI)
            {
               TextWithEllipsis(upper(prop->m_label), touchLabelWidth(toggleWidth));
               placeTouchWidget(toggleWidth);
            }
            else
            {
               TextWithEllipsis(prop->m_label, stackFields ? itemEndScreenX - rowStartScreenX - toggleWidth - style.ItemSpacing.x : maxLabelWidth);
               if (stackFields)
                  ImGui::SameLine();
               else
                  ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x);
            }
            bool v = item->GetBoolValue();
            RenderToggle(std::format("{}##Item{}", prop->m_label, i), ImVec2(itemEndScreenX - ImGui::GetCursorScreenPos().x, ImGui::GetFrameHeight()), v, !touchUI, hovered && !touchUI,
               touchUI ? kToggleOn : IM_COL32(64, 180, 32, 255));
            if (touchUI && tapped)
               v = !v;
            renderStateIcon();
            item->SetValue(v);
            break;
         }

         case VPX::Properties::PropertyDef::Type::String:
         {
            auto prop = dynamic_cast<VPX::Properties::StringPropertyDef*>(item->m_property.get());
            if (alignLabel)
               ImGui::AlignTextToFramePadding();
            if (touchUI)
            {
               TextWithEllipsis(upper(prop->m_label), touchLabelWidth(0.5f * rowWidth));
               placeTouchWidget(0.5f * rowWidth);
            }
            else
            {
               TextWithEllipsis(prop->m_label, labelMaxWidth);
               if (!stackFields)
                  ImGui::SameLine(labelEndScreenX - ImGui::GetCursorScreenPos().x);
            }
            string v = item->GetStringValue();
            ImGui::SetNextItemWidth(itemEndScreenX - ImGui::GetCursorScreenPos().x);
            ImGui::InputText(std::format("##Item{}", i).c_str(), &v);
            renderStateIcon();
            item->SetValue(v);
            break;
         }

         default: assert(false); break;
         }
         break;
      }

      default: assert(false); break;
      }

      if (isTouchRow)
         ImGui::SetCursorScreenPos(ImVec2(rowStartScreenX, rowTop + rowHeight + itemPadding.y));

      if (tintText)
         ImGui::PopStyleColor();
   }
   if (blockItems)
      ImGui::PopItemFlag();
   if (!mouseDown && m_gesture != Gesture::Scroll)
      m_gesture = Gesture::None;
   ImGui::Dummy(ImVec2(0, 0));
   m_scrollY = ImGui::GetScrollY();
   if (touchUI && ImGui::GetScrollMaxY() > 0.f)
   {
      const float trackTop = listClipMin.y + itemPadding.y;
      const float trackBottom = listClipMax.y - itemPadding.y;
      const float trackLength = trackBottom - trackTop;
      const float visible = ImGui::GetWindowSize().y;
      const float content = visible + ImGui::GetScrollMaxY();
      const float thumbLength = max(trackLength * visible / content, 4.f * itemPadding.y);
      const float thumbTop = trackTop + (trackLength - thumbLength) * (m_scrollY / ImGui::GetScrollMaxY());
      const float x = listClipMax.x - 3.f * dpi;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->PushClipRect(listClipMin, listClipMax, false);
      dl->AddRectFilled(ImVec2(x - 1.5f * dpi, trackTop), ImVec2(x + 1.5f * dpi, trackBottom), kLine, 2.f * dpi);
      dl->AddRectFilled(ImVec2(x - 1.5f * dpi, thumbTop), ImVec2(x + 1.5f * dpi, thumbTop + thumbLength), IM_COL32(255, 255, 255, 110), 2.f * dpi);
      dl->PopClipRect();
   }
   ImGui::EndChild();

   ImGui::Separator();

   ImGui::BeginChild("Info", ImVec2(0.f, touchUI ? ImGui::GetContentRegionAvail().y - actionBarHeight : 0.f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
   if (touchUI)
   {
      ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
      ImGui::PushFont(nullptr, style.FontSizeBase * 0.85f);
   }
   const ActionDef* hoveredAction = nullptr;
   for (const auto& action : actions)
      if (action.hovered)
         hoveredAction = &action;
   if (hoveredItem && std::ranges::find_if(m_items, [hoveredItem](auto& item) { return item.get() == hoveredItem; }) != m_items.end() && !hoveredItem->m_tooltip.empty())
      ImGui::TextWrapped("%s", hoveredItem->m_tooltip.c_str());
   else if (hoveredAction && hoveredAction->type == InGameUIItem::Type::ResetToStoredValues)
      ImGui::TextWrapped("Undo changes\n[Input shortcut: Credit Button]");
   else if (hoveredAction && hoveredAction->type == InGameUIItem::Type::ResetToDefaults)
      ImGui::TextWrapped("Reset page to defaults\n[Input shortcut: Launch Button]");
   else if (hoveredAction && hoveredAction->type == InGameUIItem::Type::SaveChanges)
      ImGui::TextWrapped("Save changes\n[Input shortcut: Start Button]");
   else if (hoveredAction && hoveredAction->type == InGameUIItem::Type::Back)
      ImGui::TextWrapped("Get back\n[Input shortcut: Quit Button]");
   else if (!m_info.empty())
      ImGui::TextWrapped("%s", m_info.c_str());
   if (touchUI)
   {
      ImGui::PopFont();
      ImGui::PopStyleColor();
   }
   ImGui::EndChild();

   // Bottom button row for touch UI (EmulationStation like outlined buttons, centered)
   if (touchUI)
   {
      ImGui::Separator();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + itemPadding.y * 0.5f);
      const float buttonHeight = ImGui::GetFrameHeight() * 1.15f;
      const float availWidth = ImGui::GetContentRegionAvail().x;
      bool smallFont = false;
      float totalWidth = 0.f;
      vector<float> widths;
      auto measureButtons = [&]()
      {
         totalWidth = 0.f;
         widths.clear();
         for (const auto& action : actions)
         {
            widths.push_back(ImGui::CalcTextSize(upper(action.label).c_str()).x + style.FramePadding.x * 4.f);
            totalWidth += widths.back() + itemPadding.x;
         }
         totalWidth -= itemPadding.x;
      };
      measureButtons();
      if (totalWidth > availWidth)
      {
         smallFont = true;
         ImGui::PushFont(nullptr, style.FontSizeBase * 0.8f);
         measureButtons();
         if (totalWidth > availWidth)
         {
            for (auto& w : widths)
               w = (availWidth - itemPadding.x * static_cast<float>(actions.size() - 1)) / static_cast<float>(actions.size());
            totalWidth = availWidth;
         }
      }
      ImGui::SetCursorPosX(max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + (availWidth - totalWidth) * 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
      ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 110));
      renderAction(actions.back(), ImVec2(widths.back(), buttonHeight));
      for (size_t a = 0; a + 1 < actions.size(); a++)
      {
         ImGui::SameLine();
         const bool primary = actions[a].type == InGameUIItem::Type::SaveChanges && actions[a].enabled;
         if (primary)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(236, 236, 236, 230));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
         }
         renderAction(actions[a], ImVec2(widths[a], buttonHeight));
         if (primary)
            ImGui::PopStyleColor(3);
      }
      ImGui::PopStyleColor();
      ImGui::PopStyleVar();
      if (smallFont)
         ImGui::PopFont();
   }

   ImGui::PopStyleColor();

   m_windowPos = ImGui::GetWindowPos();
   m_windowSize = ImGui::GetWindowSize();
   m_windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

   ImGui::End();

   if (touchUI)
   {
      ImGui::PopStyleColor(nPushedColors);
      ImGui::PopStyleVar(nPushedStyles);
      ImGui::PopFont();

      // Tap outside of the page to get back
      const bool insideWindow = ImGui::IsMouseHoveringRect(m_windowPos, m_windowPos + m_windowSize, false);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         m_pressedOutside = !insideWindow && IsActive() && animPos == 0.f && m_defineActionItem == nullptr;
      if (m_pressedOutside && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         m_pressedOutside = false;
         if (!insideWindow && IsActive())
            m_player->m_liveUI->m_inGameUI.NavigateBack();
      }
   }

   RenderInputActionPopup();
}

void InGameUIPage::RenderInputActionPopup()
{
   if (!m_defineActionPopup && m_defineActionItem)
   {
      assert(false); // Not supposed to happen as the only way to close the popup is to actually define a mapping which deselects the item
      if (m_defineActionItem->m_inputAction->IsNavigationAction() && !m_defineActionItem->m_inputAction->IsMapped())
         m_defineActionPopup = true;
      else
         m_defineActionItem = nullptr;
   }
   if (m_defineActionItem && m_defineActionPopup && !ImGui::IsPopupOpen((m_defineActionItem->m_label + " input binding").c_str()))
   {
      ImGui::OpenPopup((m_defineActionItem->m_label + " input binding").c_str());
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
      m_player->m_pininput.StartButtonCapture();
   }
   if (m_defineActionItem && ImGui::BeginPopupModal((m_defineActionItem->m_label + " input binding").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::Text("Press the key/button or combination of keys/buttons\nyou want to use for the action '%s'", m_defineActionItem->m_label.c_str());
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Text("New mapping: %s", m_player->m_pininput.GetMappingLabel(m_player->m_pininput.GetButtonCapture()).c_str());
      ImGui::Spacing();
      ImGui::Separator();
      if (m_player->m_pininput.IsButtonCaptureDone())
      {
         ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
         ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
         m_defineActionItem->m_inputAction->AddMapping(m_player->m_pininput.GetButtonCapture());
         if (!(m_defineActionItem->m_inputAction->IsNavigationAction() && !m_defineActionItem->m_inputAction->IsMapped()))
         {
            ImGui::CloseCurrentPopup();
            m_defineActionItem = nullptr;
         }
      }
      ImGui::EndPopup();
   }
}

// Basic Toggle button, based on https://github.com/ocornut/imgui/issues/1537#issuecomment-355562097
void InGameUIPage::RenderToggle(const string& label, const ImVec2& size, bool& v, bool interactive, bool highlighted, ImU32 onColor)
{
   ImVec2 p = ImGui::GetCursorScreenPos();
   ImDrawList* draw_list = ImGui::GetWindowDrawList();

   const float height = size.y;
   const float width = height * 1.75f;
   const float radius = height * 0.50f;
   float itemEndScreenX = ImGui::GetCursorScreenPos().x + size.x;
   p.x = itemEndScreenX - width;

   ImGui::SetNextItemWidth(size.x);
   if (interactive)
   {
      if (ImGui::InvisibleButton(label.c_str(), size))
         v = !v;
      highlighted |= ImGui::IsItemHovered();
   }
   else
   {
      ImGui::Dummy(size);
   }
   ImU32 col_bg;
   const ImU32 onHighlightColor = IM_COL32(min(255, static_cast<int>((onColor >> IM_COL32_R_SHIFT) & 0xFF) + 20), min(255, static_cast<int>((onColor >> IM_COL32_G_SHIFT) & 0xFF) + 20),
      min(255, static_cast<int>((onColor >> IM_COL32_B_SHIFT) & 0xFF) + 20), 255);
   if (highlighted)
      col_bg = v ? onHighlightColor : IM_COL32(64, 64, 64, 255);
   else
      col_bg = v ? onColor : IM_COL32(37, 37, 37, 255);

   draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
   draw_list->AddCircleFilled(ImVec2(v ? (p.x + width - radius) : (p.x + radius), p.y + radius), radius - 3.5f, IM_COL32(255, 255, 255, 255));
}

void InGameUIPage::TextWithEllipsis(const string& textStr, float maxWidth)
{
   float textWidth = ImGui::CalcTextSize(textStr.c_str()).x;
   if (textWidth > maxWidth)
   {
      float ellipsisWidth = ImGui::CalcTextSize("...").x;
      if (float availableWidth = maxWidth - ellipsisWidth; availableWidth <= 0)
      {
         ImGui::Text("...");
         return;
      }

      // Binary search to find the right length
      int left = 0;
      int right = static_cast<int>(textStr.length());
      while (left < right)
      {
         int mid = (left + right + 1) / 2;
         std::string truncated = textStr.substr(0, mid) + "...";
         float truncatedWidth = ImGui::CalcTextSize(truncated.c_str()).x;
         if (truncatedWidth <= maxWidth)
         {
            left = mid;
         }
         else
         {
            right = mid - 1;
         }
      }

      std::string truncated = textStr.substr(0, left) + "...";
      ImGui::Text("%s", truncated.c_str());
   }
   else
   {
      ImGui::Text("%s", textStr.c_str());
   }
}

}
