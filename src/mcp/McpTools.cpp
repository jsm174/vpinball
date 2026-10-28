// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

const std::vector<ToolDef>& Tools()
{
   static const std::vector<ToolDef> tools = []()
   {
      std::vector<ToolDef> t;
      AddSessionTools(t);
      AddSimTools(t);
      AddScriptTools(t);
      AddPartTools(t);
      AddAssetsTools(t);
      AddLibraryTools(t);
      AddTableTools(t);
      AddScreenshotTools(t);
      return t;
   }();
   return tools;
}

// Hints for clients that decide which calls need the user's approval: read only tools only look; destructive ones can replace or delete
// table content or files (undo covers most edits, not file writes); none of them reach beyond this computer
json ToolAnnotations(const string& name)
{
   struct Hints { const char* title; bool readOnly; bool destructive; bool idempotent; };
   static const std::map<string, Hints> hints = {
      { "vpx_status", { "Session status", true, false, true } },
      { "vpx_job", { "Background jobs", false, false, false } },
      { "vpx_play", { "Play or pause", false, false, false } },
      { "vpx_ball", { "Balls", false, true, false } },
      { "vpx_input", { "Input actions", false, false, false } },
      { "vpx_sim", { "Physics simulation", false, false, false } },
      { "vpx_script", { "Table script", false, true, false } },
      { "vpx_part", { "Table parts", false, true, false } },
      { "vpx_geometry", { "Collider geometry", true, false, true } },
      { "vpx_material", { "Materials", false, true, false } },
      { "vpx_image", { "Images", false, true, false } },
      { "vpx_sound", { "Sounds", false, true, false } },
      { "vpx_collection", { "Collections", false, true, false } },
      { "vpx_library", { "Part library", false, true, false } },
      { "vpx_editor", { "Live editor", false, true, false } },
      { "vpx_table", { "Table", false, true, false } },
      { "vpx_export", { "Export", false, true, false } },
      { "vpx_screenshot", { "Screenshot", true, false, true } },
   };
   const auto it = hints.find(name);
   if (it == hints.end())
      return json::object();
   const Hints& h = it->second;
   json annotations { { "title", h.title }, { "readOnlyHint", h.readOnly }, { "openWorldHint", false } };
   if (!h.readOnly)
   {
      annotations["destructiveHint"] = h.destructive;
      annotations["idempotentHint"] = h.idempotent;
   }
   return annotations;
}

const string& ToolsFingerprint()
{
   static const string fingerprint = []()
   {
      uint64_t hash = 14695981039346656037ull;
      const auto mix = [&hash](const string& text)
      {
         for (const unsigned char c : text)
            hash = (hash ^ c) * 1099511628211ull;
      };
      for (const ToolDef& tool : Tools())
      {
         mix(tool.name);
         mix(tool.description);
         mix(tool.inputSchema.dump());
         mix(ToolAnnotations(tool.name).dump());
      }
      mix(GUIDE_TEXT);
      for (const auto& [topic, text] : GuideTopics())
      {
         mix(topic);
         mix(text);
      }
      return std::format("{:08x}", static_cast<uint32_t>(hash ^ (hash >> 32)));
   }();
   return fingerprint;
}

}
