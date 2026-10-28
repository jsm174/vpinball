// license:GPLv3+

#include "core/stdafx.h"
#include "McpSettingsPage.h"

#include "mcp/McpServer.h"
#include "ui/live/LiveUI.h"

namespace VPX::InGameUI
{

McpSettingsPage::McpSettingsPage()
   : InGameUIPage("MCP Server Settings"s, "Expose this VPX instance to AI assistants (Claude Code, Codex CLI, ...) through the Model Context Protocol. The server only listens on this computer."s, SaveMode::Global)
{
}

void McpSettingsPage::ApplyAndRebuild()
{
   VPX::MCP::McpServer::ApplySettings();
   RequestRebuild();
}

void McpSettingsPage::BuildPage()
{
   Settings& settings = g_settingsService.GetAppSettings();
   const VPX::MCP::McpServer& server = VPX::MCP::McpServer::Get();

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propMCP_Enable, //
      [&settings]() { return settings.GetMCP_Enable(); }, //
      [this, &settings](bool v)
      {
         settings.SetMCP_Enable(v, false);
         ApplyAndRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propMCP_Port, "%d"s, //
      [&settings]() { return settings.GetMCP_Port(); }, //
      [this, &settings](int, int v)
      {
         settings.SetMCP_Port(v, false);
         ApplyAndRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>( //
      Settings::m_propMCP_Token, //
      [&settings]() { return settings.GetMCP_Token(); }, //
      [this, &settings](const string&, const string& v)
      {
         settings.SetMCP_Token(v, false);
         ApplyAndRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>("Generate New Token"s, "Create a random access token. Clients set up with the previous token must be set up again."s,
      [this, &settings]()
      {
         settings.SetMCP_Token(VPX::MCP::McpServer::RandomHex(32), false);
         ApplyAndRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>("Clear Token"s, "Accept any client from this computer without a token."s,
      [this, &settings]()
      {
         settings.SetMCP_Token(""s, false);
         ApplyAndRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Status"s));
   if (server.IsRunning())
   {
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Running at "s + server.GetEndpoint()));
      const string token = server.GetToken();
      const string header = token.empty() ? ""s : (" --header \"Authorization: Bearer "s + token + '"');
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Claude Code setup"s));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "claude mcp add --transport http vpx "s + server.GetEndpoint() + header));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Codex CLI setup (~/.codex/config.toml)"s));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "[mcp_servers.vpx]"s));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "url = \""s + server.GetEndpoint() + '"'));
      if (!token.empty())
         AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "http_headers = { Authorization = \"Bearer "s + token + "\" }"));
   }
   else if (!server.GetLastError().empty())
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Not running: "s + server.GetLastError()));
   else
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Not running"s));
}

}
