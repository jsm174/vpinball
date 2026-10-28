// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/LockStore.h"

namespace VPX::MCP
{

bool g_ignoreLocks = false;

namespace
{

// Every part of a part group, at any depth
void PartsUnder(Player& p, IEditable* group, vector<IEditable*>& out)
{
   for (IEditable* part : p.m_ptable->GetParts())
      for (PartGroup* g = part->GetPartGroup(); g; g = g->GetPartGroup())
         if (g == group)
         {
            out.push_back(part);
            break;
         }
}

}

string LockedPartsError(Player& p, const string& tool, const json& args)
{
   if (g_ignoreLocks)
      return {};
   Locks::Store& locks = Locks::Store::For(p.m_ptable);
   locks.Reconcile(p.m_ptable);
   const string action = args.value("action", ""s);
   vector<string> names;
   const auto addNames = [&]()
   {
      if (args.contains("name") && args["name"].is_string())
         names.push_back(args["name"].get<string>());
      if (args.contains("names") && args["names"].is_array())
         for (const json& n : args["names"])
            if (n.is_string())
               names.push_back(n.get<string>());
   };
   if (tool == "vpx_part")
   {
      if (action != "set" && action != "mesh" && action != "points" && action != "transform" && action != "order" && action != "delete")
         return {};
      addNames();
   }
   else if (tool == "vpx_wireform")
   {
      if (args.value("preview", false))
         return {};
      if (args.contains("name") && args["name"].is_string())
         names.push_back(args["name"].get<string>());
   }
   else if (tool == "vpx_ramp")
   {
      if (action != "apply" && action != "sheet")
         return {};
      if (args.contains("name") && args["name"].is_string())
         names.push_back(args["name"].get<string>());
   }
   else if (tool == "vpx_editor")
   {
      if (action != "set_origin")
         return {};
      addNames();
      if (names.empty() && p.m_liveUI)
         for (IEditable* part : p.m_liveUI->m_editorUI.GetSelectedParts())
            names.push_back(part->GetName());
   }
   else if (tool == "vpx_reference")
   {
      if (action != "set" && action != "remove")
         return {};
      addNames();
   }
   else if (tool == "vpx_collection")
   {
      if (action == "list")
         return {};
      const string name = args.value("name", ""s);
      if (!name.empty() && locks.IsCollectionNameLocked(name))
         return "The collection '" + name + "' is locked for the assistant: the user locked it so it stays as it is. Ask the user to unlock it (right click a part of it > Lock For Assistant), or propose the change with vpx_proposal.";
      return {};
   }
   else
      return {};

   // Deleting a group with its children also changes the children
   if (tool == "vpx_part" && action == "delete" && args.value("withChildren", false))
      for (const string& n : vector<string>(names))
         if (IEditable* part = FindPart(p, n); part && part->GetItemType() == eItemPartGroup)
         {
            vector<IEditable*> children;
            PartsUnder(p, part, children);
            for (IEditable* c : children)
               names.push_back(c->GetName());
         }

   json blocked = json::array();
   string firstReason;
   for (const string& n : names)
      if (IEditable* part = FindPart(p, n))
         if (const string reason = locks.Reason(part, p.m_ptable); !reason.empty())
         {
            blocked.push_back(n + " (" + reason + ")");
            if (firstReason.empty())
               firstReason = reason;
         }
   if (blocked.empty())
   {
      // The editor's own part lock (the lock toggle of the properties header) stops every editor path: the same holds here, instead of
      // editor commands silently skipping the part. Unlocking is an edit of its own (ui_locked alone); a reference image is driven by its tool
      if (tool != "vpx_reference")
      {
         bool unlockOnly = false;
         if (tool == "vpx_part" && action == "set" && args.contains("data") && args["data"].is_object() && args["data"].size() == 1)
         {
            string key;
            for (const char c : args["data"].begin().key())
               if (c != '_' && c != ' ')
                  key += static_cast<char>(tolower(static_cast<unsigned char>(c)));
            unlockOnly = key == "uilocked";
         }
         json editorLocked = json::array();
         if (!unlockOnly)
            for (const string& n : names)
               if (IEditable* part = FindPart(p, n); part && part->IsUILocked())
                  editorLocked.push_back(n);
         if (!editorLocked.empty())
            return "Locked in the editor, nothing was changed: " + editorLocked.dump() + ". These parts carry the editor lock (the lock toggle of the properties header, also set on parts "
                   "cloned from a table that locked them): the editor does not modify a locked part. Unlock one first with vpx_part set data:{ui_locked: false}, when it is yours to change.";
      }
      return {};
   }
   return "Locked for the assistant, nothing was changed: " + blocked.dump() + ". The user locks parts so they stay as they are: leave them alone, ask the user to unlock them (right click the part > Lock For Assistant), "
          "or propose the change with vpx_proposal (the user can accept a proposal that touches a locked part).";
}

void AddLockTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_locks",
         "The parts locked for the assistant: the user locks parts, part groups and collections in the editor (right click > Lock For Assistant) so they stay exactly as they are. Edits of a locked part through the other tools (vpx_part set / transform / delete / points / mesh / order, vpx_wireform, vpx_ramp apply / sheet, vpx_editor set_origin, vpx_collection) are refused with an error naming the part. "
         "'list' returns the locks (part and group names, collections) and every part that is locked as a result with the reason. Check it before planning an edit; you can not lock or unlock anything yourself. A proposal (vpx_proposal) may include steps on locked parts: the panel warns the user, and accepting it is their decision. Separate from these, the editor has its own part lock (ui_locked): a part with it is refused too, until unlocked with vpx_part set data:{ui_locked: false}.",
         Schema({ { "action", Prop("string", "list (default)") } }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               Locks::Store& locks = Locks::Store::For(p.m_ptable);
               if (args.value("action", "list"s) == "set" && TestHooksEnabled())
               {
                  IEditable* const part = FindPart(p, args.value("name", ""s));
                  if (part == nullptr)
                     return ErrorOutput("No part named '"s + args.value("name", ""s) + '\'');
                  locks.SetPart(part->GetName(), args.value("locked", true));
               }
               locks.Reconcile(p.m_ptable);
               json parts = json::array(), collections = json::array(), effective = json::array();
               for (const string& n : locks.Parts())
               {
                  IEditable* part = FindPart(p, n);
                  json j { { "name", n }, { "exists", part != nullptr } };
                  if (part)
                     j["type"] = ItemTypeName(part->GetItemType());
                  parts.push_back(std::move(j));
               }
               for (const string& n : locks.Collections())
               {
                  bool exists = false;
                  for (CComObject<Collection>* c : p.m_ptable->GetCollections())
                     exists = exists || StrCompareNoCase(c->m_name, n);
                  collections.push_back(json { { "name", n }, { "exists", exists } });
               }
               for (IEditable* part : p.m_ptable->GetParts())
                  if (part->GetItemType() != eItemPartGroup)
                     if (const string reason = locks.Reason(part, p.m_ptable); !reason.empty())
                        effective.push_back(json { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) }, { "reason", reason } });
               json out { { "parts", parts }, { "collections", collections }, { "lockedParts", effective }, { "count", effective.size() } };
               out["saved"] = !locks.Path().empty() ? json(PathToUTF8(locks.Path())) : json("not saved: the table has no file yet, locks only live in this session");
               return TextOutput(out);
            });
         } });
}

}
