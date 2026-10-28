// license:GPLv3+

#include "core/stdafx.h"
#include "ui/live/editor/ProposalStore.h"
#include "core/pinundo.h"
#include "parts/pintable.h"
#include "ui/live/editor/SketchStore.h"

#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace VPX::Proposals
{

using json = nlohmann::json;

namespace
{

std::string NowString()
{
   const std::time_t t = std::time(nullptr);
   std::tm tm {};
#ifdef _WIN32
   localtime_s(&tm, &t);
#else
   localtime_r(&t, &tm);
#endif
   char buf[32];
   std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
   return buf;
}

bool StatusFromName(const std::string& name, Status& status)
{
   for (const Status s : { Status::Pending, Status::Accepted, Status::Rejected, Status::Withdrawn })
      if (name == StatusName(s))
      {
         status = s;
         return true;
      }
   return false;
}

}

const char* StatusName(Status status)
{
   switch (status)
   {
   case Status::Pending: return "pending";
   case Status::Accepted: return "accepted";
   case Status::Rejected: return "rejected";
   case Status::Withdrawn: return "withdrawn";
   }
   return "pending";
}

json ProposalToJson(const Proposal& p, bool full)
{
   json j { { "id", p.id }, { "name", p.name }, { "status", StatusName(p.status) }, { "created", p.created }, { "seq", p.seq }, { "operations", p.ops.size() } };
   if (!p.description.empty())
      j["description"] = p.description;
   if (!p.group.empty())
      j["group"] = p.group;
   if (!p.decided.empty())
      j["decided"] = p.decided;
   if (!p.decidedBy.empty())
      j["decidedBy"] = p.decidedBy;
   if (!p.reason.empty())
      j["reason"] = p.reason;
   if (!p.lastError.empty())
      j["lastError"] = p.lastError;
   if (full)
   {
      json ops = json::array();
      for (const Op& op : p.ops)
      {
         json o { { "tool", op.tool }, { "args", op.args } };
         if (!op.note.empty())
            o["note"] = op.note;
         ops.push_back(std::move(o));
      }
      j["ops"] = ops;
   }
   return j;
}

std::string ProposalFromJson(const json& j, Proposal& p)
{
   if (!j.is_object())
      return "A proposal must be an object";
   if (j.contains("name") && j["name"].is_string())
      p.name = j["name"].get<std::string>();
   if (j.contains("description") && j["description"].is_string())
      p.description = j["description"].get<std::string>();
   if (j.contains("group") && j["group"].is_string())
      p.group = j["group"].get<std::string>();
   if (j.contains("status") && j["status"].is_string())
      StatusFromName(j["status"].get<std::string>(), p.status);
   if (j.contains("decided") && j["decided"].is_string())
      p.decided = j["decided"].get<std::string>();
   if (j.contains("decidedBy") && j["decidedBy"].is_string())
      p.decidedBy = j["decidedBy"].get<std::string>();
   if (j.contains("reason") && j["reason"].is_string())
      p.reason = j["reason"].get<std::string>();
   const char* key = j.contains("ops") ? "ops" : (j.contains("operations") && j["operations"].is_array() ? "operations" : nullptr);
   if (key != nullptr)
   {
      if (!j[key].is_array())
         return "ops must be an array of {tool, args, note}";
      p.ops.clear();
      for (const json& e : j[key])
      {
         if (!e.is_object() || !e.contains("tool") || !e["tool"].is_string() || !e.contains("args") || !e["args"].is_object())
            return "Each op must be {tool: string, args: object, note?: string}";
         Op op;
         op.tool = e["tool"].get<std::string>();
         op.args = e["args"];
         if (e.contains("note") && e["note"].is_string())
            op.note = e["note"].get<std::string>();
         p.ops.push_back(std::move(op));
      }
   }
   return {};
}

///////////////////////////////////////////////////////////////////////////////

Store& Store::For(const PinTable* table)
{
   static std::map<std::string, std::unique_ptr<Store>> stores;
   std::filesystem::path path;
   const std::string key = Sketch::SidecarKey(table, ".proposals.json", path);
   auto it = stores.find(key);
   if (it == stores.end())
   {
      it = stores.emplace(key, std::make_unique<Store>()).first;
      it->second->m_path = path;
      it->second->Load();
   }
   return *it->second;
}

void Store::Load()
{
   m_proposals.clear();
   m_seq = 0;
   m_nextId = 1;
   if (m_path.empty() || !std::filesystem::exists(m_path))
      return;
   try
   {
      std::ifstream in(m_path, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      const json doc = json::parse(ss.str(), nullptr, false);
      if (!doc.is_object())
      {
         PLOGE << "Proposals file is not valid JSON: " << m_path.string();
         return;
      }
      if (doc.contains("seq") && doc["seq"].is_number_unsigned())
         m_seq = doc["seq"].get<uint64_t>();
      if (doc.contains("proposals") && doc["proposals"].is_array())
         for (const json& e : doc["proposals"])
         {
            Proposal p;
            const std::string err = ProposalFromJson(e, p);
            if (!err.empty())
            {
               PLOGW << "Proposals: skipped one (" << err << ")";
               continue;
            }
            p.id = e.value("id", m_nextId);
            p.created = e.value("created", std::string());
            p.seq = e.value("seq", static_cast<uint64_t>(0));
            m_seq = std::max(m_seq, p.seq);
            m_nextId = std::max(m_nextId, p.id + 1);
            m_proposals.push_back(std::move(p));
         }
   }
   catch (const std::exception& e)
   {
      PLOGE << "Proposals could not be read: " << e.what();
   }
}

void Store::Save()
{
   if (m_path.empty())
      return;
   try
   {
      json proposals = json::array();
      for (const Proposal& p : m_proposals)
         proposals.push_back(ProposalToJson(p, true));
      const json doc { { "version", 1 }, { "seq", m_seq }, { "proposals", proposals } };
      const std::filesystem::path tmp = m_path.string() + ".tmp";
      {
         std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
         out << doc.dump(1, '\t');
         if (!out)
         {
            PLOGE << "Proposals could not be written: " << tmp.string();
            return;
         }
      }
      std::filesystem::rename(tmp, m_path);
   }
   catch (const std::exception& e)
   {
      PLOGE << "Proposals could not be saved: " << e.what();
   }
}

Proposal* Store::Find(int id)
{
   for (Proposal& p : m_proposals)
      if (p.id == id)
         return &p;
   return nullptr;
}

const Proposal* Store::Find(int id) const
{
   for (const Proposal& p : m_proposals)
      if (p.id == id)
         return &p;
   return nullptr;
}

int Store::Add(Proposal proposal)
{
   proposal.id = m_nextId++;
   proposal.status = Status::Pending;
   if (proposal.created.empty())
      proposal.created = NowString();
   proposal.seq = ++m_seq;
   const int id = proposal.id;
   m_proposals.push_back(std::move(proposal));
   // The decided ones are history: keep the last 30
   size_t decided = 0;
   for (const Proposal& p : m_proposals)
      decided += p.status != Status::Pending ? 1 : 0;
   for (size_t i = 0; decided > 30 && i < m_proposals.size();)
      if (m_proposals[i].status != Status::Pending)
      {
         m_proposals.erase(m_proposals.begin() + static_cast<std::ptrdiff_t>(i));
         decided--;
      }
      else
         i++;
   Save();
   return id;
}

bool Store::Revise(int id, const Proposal& revised)
{
   Proposal* p = Find(id);
   if (p == nullptr || p->status != Status::Pending)
      return false;
   p->name = revised.name.empty() ? p->name : revised.name;
   p->description = revised.description.empty() ? p->description : revised.description;
   p->group = revised.group.empty() ? p->group : revised.group;
   if (!revised.ops.empty())
      p->ops = revised.ops;
   p->lastError.clear();
   p->seq = ++m_seq;
   Save();
   return true;
}

bool Store::Decide(int id, Status status, const std::string& by, const std::string& reason)
{
   Proposal* p = Find(id);
   if (p == nullptr || p->status != Status::Pending || status == Status::Pending)
      return false;
   const std::string now = NowString();
   p->status = status;
   p->decided = now;
   p->decidedBy = by;
   p->reason = reason;
   p->lastError.clear();
   p->seq = ++m_seq;
   if (status == Status::Accepted && !p->group.empty())
      for (Proposal& other : m_proposals)
         if (other.id != id && other.status == Status::Pending && other.group == p->group)
         {
            other.status = Status::Rejected;
            other.decided = now;
            other.decidedBy = by;
            other.reason = "Another option of the group '" + p->group + "' was accepted: " + p->name;
            other.supersededBy = id;
            other.seq = ++m_seq;
         }
   p->wasUndone = false;
   Save();
   return true;
}

void Store::SetChangeId(int id, uint64_t changeId)
{
   if (Proposal* p = Find(id))
      p->changeId = changeId;
}

void Store::Sync(const PinUndo& undo)
{
   // Only proposals linked to a change log entry are followed (the link is not saved)
   bool any = false;
   for (const Proposal& p : m_proposals)
      any = any || p.changeId != 0;
   if (!any)
      return;
   const auto findEntry = [&undo](uint64_t changeId) -> const PinUndo::ChangeEntry*
   {
      for (const PinUndo::ChangeEntry& e : undo.ChangeLog())
         if (e.changeId == changeId)
            return &e;
      return nullptr;
   };
   bool changed = false;
   for (Proposal& p : m_proposals)
   {
      if (p.changeId == 0)
         continue;
      const PinUndo::ChangeEntry* entry = findEntry(p.changeId);
      if (entry == nullptr)
         continue;
      if (p.status == Status::Accepted && !entry->applied)
      {
         // The accept was undone: the proposal can be accepted again, and the options it had rejected are back
         p.status = Status::Pending;
         p.decided.clear();
         p.decidedBy.clear();
         p.reason.clear();
         p.wasUndone = true;
         p.seq = ++m_seq;
         for (Proposal& other : m_proposals)
            if (other.id != p.id && other.status == Status::Rejected && other.supersededBy == p.id)
            {
               other.status = Status::Pending;
               other.decided.clear();
               other.decidedBy.clear();
               other.reason.clear();
               other.supersededBy = 0;
               other.seq = ++m_seq;
            }
         changed = true;
      }
      else if (p.status == Status::Pending && p.wasUndone && entry->applied)
      {
         // The accept was redone
         const std::string now = NowString();
         p.status = Status::Accepted;
         p.decided = now;
         p.decidedBy = "user";
         p.wasUndone = false;
         p.seq = ++m_seq;
         if (!p.group.empty())
            for (Proposal& other : m_proposals)
               if (other.id != p.id && other.status == Status::Pending && other.group == p.group)
               {
                  other.status = Status::Rejected;
                  other.decided = now;
                  other.decidedBy = "user";
                  other.reason = "Another option of the group '" + p.group + "' was accepted: " + p.name;
                  other.supersededBy = p.id;
                  other.seq = ++m_seq;
               }
         changed = true;
      }
   }
   if (changed)
      Save();
}

void Store::Changed(Proposal& proposal)
{
   proposal.seq = ++m_seq;
   Save();
}

}
