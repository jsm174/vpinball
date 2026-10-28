// license:GPLv3+

#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

class PinTable;
class PinUndo;

// Proposals: changes the assistant suggests, shown as ghosts on the table and applied only when the user accepts them (all as one undo step).
// A proposal is a list of operations: calls of the MCP edit tools (mostly vpx_part add / set / transform / delete / points) that are run in order on accept.
// Saved next to the table (<table>.proposals.json), in table units.
namespace VPX::Proposals
{

enum class Status { Pending, Accepted, Rejected, Withdrawn };

const char* StatusName(Status status);

struct Op
{
   std::string tool; // vpx_part, vpx_script, vpx_material, vpx_collection or vpx_library
   nlohmann::json args; // the arguments of the tool call
   std::string note; // what this step does, for the panel
};

struct Proposal
{
   int id = 0;
   std::string name;
   std::string description;
   std::string group; // proposals of a group are alternatives: accepting one rejects the others
   std::vector<Op> ops;
   Status status = Status::Pending;
   std::string created;
   std::string decided; // time of the accept / reject
   std::string reason; // why it was rejected or withdrawn
   std::string decidedBy; // user or assistant
   uint64_t seq = 0; // sequence number of the last change
   // Display state, not saved
   bool visible = true;
   std::string lastError; // the accept that failed
   // Link with the undo history (not saved): the change log entry of the accept, so that undoing it in the editor puts the proposal back to pending
   uint64_t changeId = 0;
   int supersededBy = 0; // set on a proposal rejected because another option of its group was accepted
   bool wasUndone = false; // shown in the panel: the accepted change was undone in the editor
};

class Store
{
public:
   // The store of a table (the live copy of a playtest shares the store of its base table). Loaded from the sidecar file on first use.
   static Store& For(const PinTable* table);

   const std::vector<Proposal>& All() const { return m_proposals; }
   Proposal* Find(int id);
   const Proposal* Find(int id) const;
   uint64_t Seq() const { return m_seq; }
   // Where the proposals are saved; empty when the table is not saved to disk yet
   const std::filesystem::path& Path() const { return m_path; }

   // Adds a pending proposal (id, seq and time are assigned); returns its id
   int Add(Proposal proposal);
   // Replaces the operations and texts of a pending proposal
   bool Revise(int id, const Proposal& revised);
   // Sets the status (accepted, rejected, withdrawn); other pending proposals of the same group are rejected when one is accepted
   bool Decide(int id, Status status, const std::string& by, const std::string& reason = {});
   // Follows the undo history: an accepted proposal whose change was undone goes back to pending (with the options it had rejected), and comes back
   // as accepted when the change is redone. Call before reading the proposals.
   void Sync(const PinUndo& undo);
   // Remembers the change log entry of an accept
   void SetChangeId(int id, uint64_t changeId);
   // Saves after display only changes are not needed; call after changing a proposal through Find
   void Changed(Proposal& proposal);

private:
   void Load();
   void Save();

   std::filesystem::path m_path;
   std::vector<Proposal> m_proposals;
   uint64_t m_seq = 0;
   int m_nextId = 1;
};

nlohmann::json ProposalToJson(const Proposal& proposal, bool full);
// Returns an error text, empty when OK
std::string ProposalFromJson(const nlohmann::json& j, Proposal& proposal);

}
