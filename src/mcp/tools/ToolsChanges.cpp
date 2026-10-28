// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddChangesTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_changes",
         "The change log of the live editor: every edit made in it since the table was opened, by the user's hand (source user) or through this server (source mcp): parts moved, added, deleted or edited, and non part changes (assets, script, table properties, sketch marks). "
         "Each entry has a seq (it goes up whenever an entry is added or changes), the history label (e.g. 'Move 3 parts', 'Rim Width: Wall7'), the parts it touched with what happened to each (edit, add, delete), the time, and a status: applied, or undone (by is who undid it). "
         "Check it before building on a part the user may have touched, and after the user says they changed something: 'list' with since = the seq of the last call returns only what is new (vpx_status also reports changeSeq, the current one). "
         "A part listed as edit changed: read it again with vpx_part get. Edits that go through no undo step (a few direct panel changes) are not logged; the log keeps the last 400 edits. Needs the editor open.",
         Schema({ { "action", Prop("string", "list (default)") }, { "since", Prop("integer", "Only the entries added or changed after this seq (default 0: all)") },
                  { "source", Prop("string", "user | mcp: only the edits made by the user, or through this server") },
                  { "part", Prop("string", "Only the entries that touch this part") }, { "limit", Prop("integer", "Most recent entries to return, default 50") },
                  { "applied", Prop("boolean", "true: only the edits still in effect; false: only the undone ones") } }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               if (!p.m_liveUI)
                  return ErrorOutput("The editor is not available"s);
               const PinUndo& undo = p.m_liveUI->m_editorUI.GetUndoLog();
               const uint64_t since = args.value("since", static_cast<uint64_t>(0));
               const string source = args.value("source", ""s), part = args.value("part", ""s);
               const size_t limit = std::clamp<size_t>(args.value("limit", 50), 1, 400);
               const bool filterApplied = args.contains("applied") && args["applied"].is_boolean();
               const bool wantApplied = filterApplied && args["applied"].get<bool>();

               vector<const PinUndo::ChangeEntry*> picked;
               for (const PinUndo::ChangeEntry& entry : undo.ChangeLog())
               {
                  if (entry.seq <= since)
                     continue;
                  if (!source.empty() && entry.source != source)
                     continue;
                  if (filterApplied && entry.applied != wantApplied)
                     continue;
                  if (!part.empty() && std::ranges::none_of(entry.parts, [&part](const auto& pr) { return StrCompareNoCase(pr.first, part); }))
                     continue;
                  picked.push_back(&entry);
               }
               std::ranges::sort(picked, [](const auto* a, const auto* b) { return a->seq < b->seq; });
               const size_t total = picked.size();
               if (picked.size() > limit)
                  picked.erase(picked.begin(), picked.end() - limit);

               json entries = json::array();
               for (const PinUndo::ChangeEntry* e : picked)
               {
                  json parts = json::array();
                  for (const auto& [name, op] : e->parts)
                     parts.push_back(json { { "name", name }, { "op", op } });
                  json j { { "seq", e->seq }, { "id", e->changeId }, { "time", e->time }, { "source", e->source }, { "label", e->label },
                     { "status", e->applied ? "applied" : "undone" }, { "parts", parts } };
                  if (!e->field.empty())
                     j["field"] = e->field;
                  if (!e->by.empty())
                     j["by"] = e->by;
                  if (e->actions > 0)
                     j["otherChanges"] = e->actions;
                  entries.push_back(std::move(j));
               }
               json out { { "seq", undo.ChangeSeq() }, { "count", entries.size() }, { "entries", entries } };
               if (total > entries.size())
                  out["older"] = total - entries.size();
               return TextOutput(out);
            });
         } });
}

}
