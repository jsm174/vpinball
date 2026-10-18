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
