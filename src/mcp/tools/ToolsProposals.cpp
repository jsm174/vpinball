// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "mcp/McpProposals.h"
#include "ui/live/editor/ProposalDraw.h"
#include "ui/live/editor/ProposalStore.h"

namespace VPX::MCP
{

namespace
{

using namespace VPX::Proposals;

// Checks the operations of a proposal before it is stored: allowed tools, and the parts they name exist
string ValidateOps(Player& p, const vector<Op>& ops)
{
   if (ops.empty())
      return "ops is required: the steps of the proposal, each {tool, args, note}";
   for (size_t i = 0; i < ops.size(); i++)
   {
      const Op& op = ops[i];
      const string at = std::format("Step {}: ", i + 1);
      if (!IsProposalTool(op.tool))
         return at + "the tool '" + op.tool + "' can not be used in a proposal (vpx_part, vpx_script, vpx_material, vpx_collection, vpx_library)";
      if (op.tool != "vpx_part")
         continue;
      const string action = op.args.value("action", ""s);
      static const std::set<string> allowed = { "add", "set", "transform", "delete", "points", "mesh", "order" };
      if (!allowed.contains(action))
         return at + "vpx_part action must be add | set | transform | delete | points | mesh | order";
      if (action == "add")
      {
         if (op.args.value("type", ""s).empty())
            return at + "add needs a type";
         continue;
      }
      vector<string> names = op.args.value("names", vector<string>());
      if (op.args.contains("name") && op.args["name"].is_string())
         names.push_back(op.args["name"].get<string>());
      if (names.empty())
         return at + action + " needs name or names";
      for (const string& n : names)
         if (FindPart(p, n) == nullptr)
         {
            // A step may name a part an earlier step of the proposal adds
            const bool added = std::ranges::any_of(ops, [&n](const Op& o) { return o.tool == "vpx_part" && o.args.value("action", ""s) == "add" && StrCompareNoCase(o.args.value("name", ""s), n); });
            if (!added)
               return at + "no part named '" + n + "'";
         }
   }
   return {};
}

json OpsSummary(const Proposal& pr, const PinTable* table)
{
   json arr = json::array();
   for (const Op& op : pr.ops)
   {
      json o { { "tool", op.tool }, { "action", op.args.value("action", ""s) } };
      if (!op.note.empty())
         o["note"] = op.note;
      o["ghosts"] = GhostsOf(op, table).size();
      arr.push_back(std::move(o));
   }
   return arr;
}

}

void AddProposalTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_proposal",
         "Proposals: changes you suggest instead of making them. A proposal is a named list of steps (calls of vpx_part add / set / transform / delete / points / mesh, or vpx_script, vpx_material, vpx_collection, vpx_library), shown to the user as a coloured ghost on the table in the editor (adds filled, changes outlined with an arrow where a part moves, removals crossed out) and listed in a panel with Accept and Reject. "
         "Nothing is built until the user accepts: then all the steps run as one undo step ('Proposal: <name>'), and if a step fails everything is reverted. Use it when the user wants to compare options or to see a change before it happens; make several proposals with the same group to offer alternatives (accepting one rejects the others). "
         "'propose' creates one (name, ops: [{tool, args, note}], optional description and group; args are exactly the arguments of that tool call; the parts the steps name must exist or be added by an earlier step). 'list' shows the proposals with their status (pending, accepted, rejected, withdrawn), who decided and why; poll it to learn the user's decision (since = the seq of the last call). "
         "'get' returns the steps of one. 'revise' replaces the steps, description, name or group of a pending one (answering feedback). 'withdraw' removes a pending one. You can not accept a proposal: only the user can. vpx_screenshot shows the pending ghosts (proposals:false hides them). The ghosts are drawn from the steps: a part add with centerline + thickness, points or circle, set (with data.center), transform (dx, dy) and delete have a place on the playfield; other steps appear in the panel only.",
         Schema({ { "action", Prop("string", "propose | list | get | revise | withdraw") }, { "id", Prop("integer", "Proposal id (get, revise, withdraw, list)") },
                  { "name", Prop("string", "Short name of the proposal, shown on the ghost and in the panel (propose, revise)") },
                  { "description", Prop("string", "What the proposal does and why (propose, revise)") },
                  { "group", Prop("string", "Proposals with the same group are alternatives: accepting one rejects the others (propose, revise)") },
                  { "ops", json { { "type", "array" }, { "items", json { { "type", "object" } } }, { "description", "The steps: [{tool: 'vpx_part', args: {action: 'add', type: 'Wall', centerline: [...], thickness: 8, data: {...}}, note: 'guide wall'}] (propose; revise replaces them)" } } },
                  { "status", Prop("string", "list filter: pending | accepted | rejected | withdrawn") }, { "since", Prop("integer", "list: only proposals changed after this seq") },
                  { "reason", Prop("string", "withdraw: why") } },
            { "action" }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               const string action = args.value("action", ""s);
               PinTable* table = p.m_ptable;
               Store& store = Store::For(table);
               if (p.m_liveUI)
                  store.Sync(p.m_liveUI->m_editorUI.GetUndoLog());
               const auto result = [&](json j)
               {
                  j["seq"] = store.Seq();
                  j["saved"] = !store.Path().empty() ? json(PathToUTF8(store.Path())) : json("not saved: the table has no file yet, proposals only live in this session");
                  return TextOutput(j);
               };
               const int id = args.value("id", 0);

               if (action == "propose" || action == "revise")
               {
                  Proposal draft;
                  // The same reader as the file: name, description, group, ops
                  json fields = json::object();
                  for (const char* key : { "name", "description", "group", "ops" })
                     if (args.contains(key))
                        fields[key] = args[key];
                  const string parseError = ProposalFromJson(fields, draft);
                  if (!parseError.empty())
                     return ErrorOutput(parseError);
                  if (action == "propose")
                  {
                     if (draft.name.empty())
                        return ErrorOutput("name is required"s);
                     for (const Proposal& other : store.All())
                        if (other.status == Status::Pending && StrCompareNoCase(other.name, draft.name))
                           return ErrorOutput("A pending proposal is already named '"s + draft.name + "': revise it or pick another name");
                  }
                  if (action == "propose" || args.contains("ops"))
                     if (const string err = ValidateOps(p, draft.ops); !err.empty())
                        return ErrorOutput(err);
                  if (action == "propose")
                  {
                     const int newId = store.Add(draft);
                     size_t ghosts = 0;
                     for (const Op& op : draft.ops)
                        ghosts += GhostsOf(op, table).size();
                     return result(json { { "id", newId }, { "status", "pending" }, { "ghosts", ghosts },
                        { "note", "The user sees it as a ghost in the editor and accepts or rejects it in the Assistant proposals panel. Poll vpx_proposal list to see the decision." } });
                  }
                  if (id == 0)
                     return ErrorOutput("id is required"s);
                  if (!store.Revise(id, draft))
                     return ErrorOutput("No pending proposal with that id"s);
                  return result(json { { "id", id }, { "status", "pending" }, { "revised", true } });
               }
               if (action == "withdraw")
               {
                  if (id == 0)
                     return ErrorOutput("id is required"s);
                  if (!store.Decide(id, Status::Withdrawn, "assistant", args.value("reason", "Withdrawn"s)))
                     return ErrorOutput("No pending proposal with that id"s);
                  return result(json { { "id", id }, { "status", "withdrawn" } });
               }
               if (action == "get")
               {
                  const Proposal* pr = store.Find(id);
                  if (pr == nullptr)
                     return ErrorOutput("Unknown proposal id"s);
                  json j = ProposalToJson(*pr, true);
                  j["steps"] = OpsSummary(*pr, table);
                  return result(j);
               }
               if (action == "list")
               {
                  const string status = args.value("status", ""s);
                  const uint64_t since = args.value("since", static_cast<uint64_t>(0));
                  json arr = json::array();
                  for (const Proposal& pr : store.All())
                  {
                     if (id != 0 && pr.id != id)
                        continue;
                     if (!status.empty() && status != StatusName(pr.status))
                        continue;
                     if (pr.seq <= since)
                        continue;
                     json j = ProposalToJson(pr, false);
                     j["steps"] = OpsSummary(pr, table);
                     arr.push_back(std::move(j));
                  }
                  return result(json { { "count", arr.size() }, { "proposals", arr } });
               }
               if ((action == "accept" || action == "reject") && TestHooksEnabled())
               {
                  if (action == "accept")
                  {
                     if (const string error = ApplyProposal(p, id); !error.empty())
                        return ErrorOutput(error);
                  }
                  else
                     RejectProposal(p, id, args.value("reason", ""s));
                  const Proposal* const decided = store.Find(id);
                  return decided != nullptr ? result(json { { "id", id }, { "status", StatusName(decided->status) } }) : ErrorOutput("Unknown proposal"s);
               }
               return ErrorOutput("Unknown action"s);
            });
         } });
}

}
