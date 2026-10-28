// license:GPLv3+

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

class PinTable;
class IEditable;

// Parts locked for the assistant: the MCP server refuses to change them (set, transform, delete, points, mesh, order, wireform, sheet...). Only the user locks and unlocks, in
// the editor. A part is locked when its own name is locked, when a part group it is in is locked, or when it is a member of a locked collection.
// Saved next to the table (<table>.locks.json), by name. A part renamed while its table is open keeps its lock (see Reconcile).
namespace VPX::Locks
{

class Store
{
public:
   // The store of a table (the live copy of a playtest shares the store of its base table). Loaded from the sidecar file on first use.
   static Store& For(const PinTable* table);

   bool Empty() const { return m_parts.empty() && m_collections.empty(); }
   const std::vector<std::string>& Parts() const { return m_parts; } // part and part group names
   const std::vector<std::string>& Collections() const { return m_collections; }
   const std::filesystem::path& Path() const { return m_path; }

   bool IsPartNameLocked(const std::string& name) const;
   bool IsCollectionNameLocked(const std::string& name) const;
   // Locks or unlocks a part (or part group) / a collection by name
   void SetPart(const std::string& name, bool locked);
   void SetCollection(const std::string& name, bool locked);

   // Follows the renames: a locked name that no longer names a part of the table is replaced by the new name of the part it named. Call it
   // before a pass of Reason calls (it only looks at the table when there are part locks)
   void Reconcile(const PinTable* table);
   // Why a part is locked, empty when it is not: "the part itself", "its part group 'G'", "its collection 'C'"
   std::string Reason(const IEditable* part, const PinTable* table) const;

private:
   void Load();
   void Save();

   std::filesystem::path m_path;
   std::vector<std::string> m_parts;
   std::vector<std::string> m_collections;
   std::map<std::string, const IEditable*> m_named; // The part each locked name was last seen on (base table)
};

}
