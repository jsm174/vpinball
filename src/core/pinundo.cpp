// license:GPLv3+

#include "core/stdafx.h"

#include "pinundo.h"

#include <ctime>

#include "vpversion.h"
#include "parts/ball.h"
#include "parts/PartGroup.h"
#include "parts/pintable.h"
#include "physics/PhysicsEngine.h"
#include "ui/live/LiveUI.h"
#include "utils/fileio.h"
#include "utils/BiffReader.h"
#include "utils/BiffWriter.h"

#define MAXUNDO 16 // Kept records, plus the one being recorded


class UndoRecord final
{
public:
   UndoRecord();
   ~UndoRecord();

   void MarkForUndo(IEditable *const pie, const bool saveForUndo);
   void MarkForCreate(IEditable *const pie);
   void MarkForDelete(IEditable *const pie);

   struct Action
   {
      std::function<void()> undo;
      std::function<std::function<void()>()> snapshotForRedo; // Returns the function restoring the state as it is when called (empty if not redoable)
   };
   vector<Action> m_actions;
   // Redo records set aside when the record was begun (a new edit invalidates them), restored if the record is discarded or canceled. Kept per record so
   // that the records begun and discarded by other panels while this one is open do not clobber them
   vector<std::unique_ptr<UndoRecord>> m_stashedRedo;
   size_t m_cleanpointAtBegin = 0;
   SaveDirtyState m_dirtyStateAtBegin = SaveDirtyState::Clean;
   vector<std::unique_ptr<InMemStream>> m_vstm;
   vector<IEditable *> m_vieCreate;
   vector<IEditable *> m_vieDelete;
   vector<IEditable *> m_vieMark;
   uint64_t m_id = 0; // Unique id of the record (see PinUndo::TopRecordId)
   uint64_t m_changeId = 0; // Id of the edit in the change log: shared by a record and the records that undo and redo it
   string m_source; // who made the edit (user or mcp)
   string m_time; // local time of the edit
   bool m_ownsCreateRefs = false; // The record holds the creation reference of its created parts (revert of an undelete), released if it is dropped without being applied
   std::any m_editorState; // Editor provided state (e.g. selection) captured when the record was begun

   // Description shown by the undo history
   string m_label; // Complete label set by the editor (e.g. "Move 3 parts")
   string m_field; // Edited field/operation, composed with the subject part name (e.g. "Rim Width: PBR_DOME_f125")
   string m_subject; // Name of the first part involved in the record
   size_t m_subjectCount = 0; // Number of parts involved in the record
   void NoteSubject(IEditable *const pie);
   string Label() const;
};


UndoRecord::UndoRecord() { }

UndoRecord::~UndoRecord()
{
   for (IEditable *editable : m_vieDelete)
      editable->Release();
   if (m_ownsCreateRefs)
      for (IEditable *editable : m_vieCreate)
         editable->Release();
}

void UndoRecord::NoteSubject(IEditable *const pie)
{
   m_subjectCount++;
   if (m_subject.empty())
      if (const IScriptable *const scriptable = pie->GetIScriptable())
         m_subject = scriptable->m_name;
}

string UndoRecord::Label() const
{
   if (!m_label.empty())
      return m_label;
   if (!m_field.empty())
   {
      if (m_subjectCount > 1)
         return m_field + ": " + std::to_string(m_subjectCount) + " parts";
      return m_subject.empty() ? m_field : m_field + ": " + m_subject;
   }
   if (m_subjectCount > 1)
      return std::to_string(m_subjectCount) + " parts";
   if (m_subjectCount == 1 && !m_subject.empty())
      return m_subject;
   return "Edit";
}

void UndoRecord::MarkForUndo(IEditable *const pie, const bool saveForUndo)
{
   if (FindIndexOf(m_vieMark, pie) != -1) // Been marked already
      return;

   if (FindIndexOf(m_vieCreate, pie) != -1) // Just created, so undo will delete it anyway
      return;

   m_vieMark.push_back(pie);
   NoteSubject(pie);

   auto pstm = std::make_unique<InMemStream>();

   pstm->Write(&pie, sizeof(IEditable *));

   BiffWriter writer(pstm.get(), nullptr);
   pie->Save(writer, saveForUndo);

   m_vstm.push_back(std::move(pstm));
}

void UndoRecord::MarkForCreate(IEditable *const pie)
{
   assert(FindIndexOf(m_vieCreate, pie) == -1); // Created twice?
   m_vieCreate.push_back(pie);
   NoteSubject(pie);
}

void UndoRecord::MarkForDelete(IEditable *const pie)
{
   assert(FindIndexOf(m_vieDelete, pie) == -1); // Already deleted - bad thing

   const int pie_pos = FindIndexOf(m_vieCreate, pie);
   if (pie_pos != -1) // Created and deleted in the same step
   {
      // Just forget about it
      m_vieCreate.erase(m_vieCreate.begin() + pie_pos);
      RemoveFromVectorSingle(m_vieMark, pie);
      return;
   }

   m_vieDelete.push_back(pie);
   pie->AddRef();
   NoteSubject(pie);
}



PinUndo::PinUndo(PinTable *table)
   : m_table(table)
{
}

PinUndo::~PinUndo() { }

bool PinUndo::HasUndo() const { return !m_undoRecords.empty(); }

uint64_t PinUndo::TopRecordId() const { return m_undoRecords.empty() ? 0 : m_undoRecords.back()->m_id; }

bool PinUndo::MergeLastIntoPrevious()
{
   if (m_nUndoLayer != 0 || m_undoRecords.size() < 2)
      return false;
   UndoRecord &last = *m_undoRecords.back();
   UndoRecord &prev = *m_undoRecords[m_undoRecords.size() - 2];
   // Only part edits can be folded in (the previous record keeps the older snapshot of the parts it already holds)
   if (!last.m_vieCreate.empty() || !last.m_vieDelete.empty() || !last.m_actions.empty())
      return false;
   for (size_t i = 0; i < last.m_vieMark.size(); i++)
   {
      IEditable *const pie = last.m_vieMark[i];
      if (FindIndexOf(prev.m_vieCreate, pie) != -1 || FindIndexOf(prev.m_vieMark, pie) != -1) // Undoing the previous record already handles the part
         continue;
      prev.m_vieMark.push_back(pie);
      prev.m_vstm.push_back(std::move(last.m_vstm[i]));
   }
   // The log shows the two edits as one: the parts of the folded record join the previous entry
   if (ChangeEntry *const folded = FindChange(last.m_changeId))
   {
      if (ChangeEntry *const into = FindChange(prev.m_changeId))
      {
         for (const auto &part : folded->parts)
            if (std::ranges::find(into->parts, part) == into->parts.end())
               into->parts.push_back(part);
         into->seq = 0;
      }
      LogRemove(last.m_changeId);
   }
   m_undoRecords.pop_back();
   if (m_cleanpoint != NoCleanPoint && m_cleanpoint > m_undoRecords.size())
      m_cleanpoint = NoCleanPoint;
   return true;
}

bool PinUndo::IsInvertible(const UndoRecord &record)
{
   return std::ranges::all_of(record.m_actions, [](const UndoRecord::Action &action) { return action.snapshotForRedo != nullptr; });
}

bool PinUndo::NextUndoClearsRedo() const { return !m_undoRecords.empty() && !IsInvertible(*m_undoRecords.back()); }

bool PinUndo::HasRedo() const { return !m_redoRecords.empty(); }

size_t PinUndo::UndoCount() const { return m_undoRecords.size(); }

size_t PinUndo::RedoCount() const { return m_redoRecords.size(); }

string PinUndo::UndoLabel(const size_t index) const { return index < m_undoRecords.size() ? m_undoRecords[m_undoRecords.size() - 1 - index]->Label() : string(); }

string PinUndo::RedoLabel(const size_t index) const { return index < m_redoRecords.size() ? m_redoRecords[m_redoRecords.size() - 1 - index]->Label() : string(); }

void PinUndo::SetLabel(const string &label)
{
   if (m_undoRecords.empty())
      return;
   m_undoRecords.back()->m_label = label;
   LogLabel(*m_undoRecords.back());
}

void PinUndo::SetFieldLabel(const string &field)
{
   if (m_undoRecords.empty())
      return;
   m_undoRecords.back()->m_field = field;
   LogLabel(*m_undoRecords.back());
}

///////////////////////////////////////////////////////////////////////////////
// Change log

void PinUndo::PublishChanges() const
{
   for (ChangeEntry &entry : m_changeLog)
      if (entry.seq == 0)
         entry.seq = ++m_changeSeq;
}

PinUndo::ChangeEntry *PinUndo::FindChange(const uint64_t changeId)
{
   for (auto it = m_changeLog.rbegin(); it != m_changeLog.rend(); ++it)
      if (it->changeId == changeId)
         return &*it;
   return nullptr;
}

// A record has just ended: the edit goes to the log (records that hold nothing, begun by the editor just in case, are skipped)
void PinUndo::LogEnded(const UndoRecord &record)
{
   if (record.m_changeId == 0 || FindChange(record.m_changeId) != nullptr)
      return;
   ChangeEntry entry;
   entry.changeId = record.m_changeId;
   entry.label = record.Label();
   entry.field = record.m_field;
   entry.source = record.m_source;
   entry.time = record.m_time;
   entry.actions = record.m_actions.size();
   const auto addParts = [&entry](const vector<IEditable *> &parts, const char *op)
   {
      for (IEditable *const pie : parts)
         if (pie != nullptr)
            entry.parts.emplace_back(pie->GetName(), op);
   };
   addParts(record.m_vieCreate, "add");
   addParts(record.m_vieDelete, "delete");
   addParts(record.m_vieMark, "edit");
   if (entry.parts.empty() && entry.actions == 0)
      return;
   entry.seq = 0;
   m_changeLog.push_back(std::move(entry));
   constexpr size_t MAX_LOG = 400;
   if (m_changeLog.size() > MAX_LOG)
      m_changeLog.erase(m_changeLog.begin());
}

// The history label of a record changed after it ended
void PinUndo::LogLabel(const UndoRecord &record)
{
   if (ChangeEntry *const entry = FindChange(record.m_changeId))
   {
      const string label = record.Label();
      if (entry->label != label || entry->field != record.m_field)
      {
         entry->label = label;
         entry->field = record.m_field;
         entry->seq = 0;
      }
   }
}

// An edit was undone or redone
void PinUndo::LogStatus(const uint64_t changeId, const bool applied)
{
   if (ChangeEntry *const entry = FindChange(changeId))
   {
      entry->applied = applied;
      entry->by = s_editSource;
      entry->seq = 0;
   }
}

// The record is gone without an edit (discarded, or canceled right after it began)
void PinUndo::LogRemove(const uint64_t changeId)
{
   const auto it = std::ranges::find_if(m_changeLog, [changeId](const ChangeEntry &entry) { return entry.changeId == changeId; });
   if (it != m_changeLog.end())
   {
      m_changeLog.erase(it);
   }
}

void PinUndo::ClearRedo()
{
   m_redoRecords.clear();
   if (m_cleanpoint != NoCleanPoint && m_cleanpoint > m_undoRecords.size()) // The clean state was in the dropped redo records
      m_cleanpoint = NoCleanPoint;
}

void PinUndo::UpdateDirtyAfterMove()
{
   if ((m_undoRecords.size() == m_cleanpoint) && (m_dirtyState > SaveDirtyState::Clean))
   {
      m_dirtyState = SaveDirtyState::Clean;
      m_table->SetDirty(SaveDirtyState::Clean);
   }
   else if ((m_undoRecords.size() != m_cleanpoint) && (m_dirtyState < SaveDirtyState::Dirty))
   {
      // If we're not clean, we must be dirty (always process this case for autosave, even though to the user it wouldn't appear to matter)
      m_dirtyState = SaveDirtyState::Dirty;
      m_table->SetDirty(SaveDirtyState::Dirty);
   }
}

bool PinUndo::IsUndoPastCleanPoint() const { return !m_undoRecords.empty() && m_undoRecords.size() == m_cleanpoint; }

void PinUndo::SetCleanPoint(const SaveDirtyState sds)
{
   if (sds == SaveDirtyState::Clean)
      m_cleanpoint = m_undoRecords.size();
   m_dirtyState = sds;
   m_table->SetDirty(sds);
}

void PinUndo::BeginUndo()
{
   m_nUndoLayer++;
   if (m_nUndoLayer == 1)
   {
      // Only drop the oldest record once the history exceeds its size, so that a record that ends up being discarded (see Discard) does not cost a real one
      if (m_undoRecords.size() > MAXUNDO)
      {
         m_undoRecords.erase(m_undoRecords.begin());
         const auto shift = [](const size_t point) { return (point == 0 || point == NoCleanPoint) ? NoCleanPoint : point - 1; };
         m_cleanpoint = shift(m_cleanpoint);
         for (const auto &remaining : m_undoRecords) // The clean points kept by the records to restore on cancel / discard shift too
            remaining->m_cleanpointAtBegin = shift(remaining->m_cleanpointAtBegin);
      }
      m_undoRecords.push_back(std::make_unique<UndoRecord>());
      UndoRecord &record = *m_undoRecords.back();
      record.m_id = ++m_lastRecordId;
      record.m_changeId = ++m_lastChangeId;
      record.m_source = s_editSource;
      {
         const std::time_t now = std::time(nullptr);
         std::tm tm {};
#ifdef _WIN32
         localtime_s(&tm, &now);
#else
         localtime_r(&now, &tm);
#endif
         char buf[32];
         std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
         record.m_time = buf;
      }
      record.m_dirtyStateAtBegin = m_dirtyState;
      // A new edit invalidates the redo records. They are set aside in the new record, which restores them if it ends up discarded or canceled (see
      // Discard, and Undo(false) for a canceled modal transform: the editor also begins and discards a record every frame while the mouse is over a
      // property pane, so the stash of a record can not be released while other records come and go on top of it). A stash is released with its record
      // (undone, truncated or dropped)
      record.m_cleanpointAtBegin = m_cleanpoint;
      if (!m_redoRecords.empty())
      {
         record.m_stashedRedo = std::move(m_redoRecords);
         m_redoRecords.clear();
         if (m_cleanpoint != NoCleanPoint && m_cleanpoint + 1 > m_undoRecords.size()) // The clean state was in the redo records: no longer reachable (the new record counts)
            m_cleanpoint = NoCleanPoint;
      }
      if (m_editorStateCapture)
         m_undoRecords.back()->m_editorState = m_editorStateCapture();
   }
}

void PinUndo::MarkForUndo(IEditable *const pie, const bool saveForUndo)
{
   assert(!m_undoRecords.empty());
   assert(m_nUndoLayer > 0);
   if (m_undoRecords.empty())
      return;
   if (m_nUndoLayer <= 0)
      BeginUndo();

   m_undoRecords.back()->MarkForUndo(pie, saveForUndo);
}

void PinUndo::MarkForCreate(IEditable *const pie)
{
   assert(!m_undoRecords.empty());
   assert(m_nUndoLayer > 0);
   if (m_undoRecords.empty())
      return;
   if (m_nUndoLayer <= 0)
      BeginUndo();

   m_undoRecords.back()->MarkForCreate(pie);
}

void PinUndo::MarkForDelete(IEditable *const pie)
{
   assert(!m_undoRecords.empty());
   assert(m_nUndoLayer > 0);
   if (m_undoRecords.empty())
      return;
   if (m_nUndoLayer <= 0)
      BeginUndo();

   m_undoRecords.back()->MarkForDelete(pie);
}

void PinUndo::MarkForAction(std::function<void()> undo, std::function<std::function<void()>()> snapshotForRedo)
{
   assert(!m_undoRecords.empty());
   assert(m_nUndoLayer > 0);
   if (m_undoRecords.empty())
      return;
   if (m_nUndoLayer <= 0)
      BeginUndo();

   m_undoRecords.back()->m_actions.push_back({ std::move(undo), std::move(snapshotForRedo) });
}

std::any PinUndo::Undo(const bool keepForRedo)
{
   assert(m_nUndoLayer == 0);
   while (m_nUndoLayer > 0)
      EndUndo();

   if (!HasUndo())
      return {};

   std::unique_ptr<UndoRecord> record = std::move(m_undoRecords.back());
   m_undoRecords.pop_back();

   std::unique_ptr<UndoRecord> inverse;
   std::any editorState = ApplyRecord(*record, keepForRedo, inverse);
   if (inverse)
      m_redoRecords.push_back(std::move(inverse)); // The record's stashed redo records die with it: they were invalidated by its edit
   else if (!keepForRedo && !record->m_stashedRedo.empty())
   {
      // Cancels the record that has just been begun (e.g. a canceled modal transform): the redo records it set aside are back
      m_redoRecords = std::move(record->m_stashedRedo);
      record->m_stashedRedo.clear();
      m_cleanpoint = record->m_cleanpointAtBegin;
   }
   else
      ClearRedo(); // Not redoable (non part change): the redo records may depend on the state before it
   if (keepForRedo)
      LogStatus(record->m_changeId, false);
   else
      LogRemove(record->m_changeId); // canceled right after it began: no edit
   record.reset();

   UpdateDirtyAfterMove();
   return editorState;
}

std::any PinUndo::Redo()
{
   assert(m_nUndoLayer == 0);
   while (m_nUndoLayer > 0)
      EndUndo();

   if (!HasRedo())
      return {};

   std::unique_ptr<UndoRecord> record = std::move(m_redoRecords.back());
   m_redoRecords.pop_back();

   std::unique_ptr<UndoRecord> inverse;
   std::any editorState = ApplyRecord(*record, true, inverse);
   assert(inverse); // Redo records never hold actions, they are always invertible
   if (inverse)
      m_undoRecords.push_back(std::move(inverse));
   LogStatus(record->m_changeId, true);
   record.reset();

   UpdateDirtyAfterMove();
   return editorState;
}

// Applies a record (restores the state it was started from) and builds the record that reverts this application (the redo record when undoing, the undo record when redoing)
std::any PinUndo::ApplyRecord(UndoRecord &record, const bool buildInverse, std::unique_ptr<UndoRecord> &inverseOut)
{
   // Non part changes (actions) can be reverted if all of them can snapshot their current state
   const bool invertible = buildInverse && IsInvertible(record);
   std::unique_ptr<UndoRecord> inverse;
   if (invertible)
   {
      inverse = std::make_unique<UndoRecord>();
      inverse->m_id = ++m_lastRecordId;
      inverse->m_changeId = record.m_changeId;
      inverse->m_source = record.m_source;
      inverse->m_time = record.m_time;
      inverse->m_ownsCreateRefs = true;
      if (m_editorStateCapture)
         inverse->m_editorState = m_editorStateCapture(); // Editor state before the revert, given back when the inverse is applied
      inverse->m_label = record.m_label;
      inverse->m_field = record.m_field;
      inverse->m_subject = record.m_subject;
      inverse->m_subjectCount = record.m_subjectCount;
   }

   // Non part changes first, most recent first: a part restored below may refer to an asset they bring back
   for (auto it = record.m_actions.rbegin(); it != record.m_actions.rend(); ++it)
   {
      if (inverse)
         inverse->m_actions.push_back({ it->snapshotForRedo(), it->snapshotForRedo }); // Restores the state before this undo; symmetric when it is applied
      it->undo();
   }

   for (IEditable *editable : record.m_vieDelete)
   {
      m_table->Undelete(editable); // Adds the table reference; the record's one is released with the record below
      if (inverse)
      {
         // Reverting this undelete is a creation undo: Uncreate drops two references (the creation one and the table one)
         editable->AddRef();
         inverse->m_vieCreate.push_back(editable);
      }
      if (g_pplayer && (g_pplayer->m_ptable == m_table))
      {
         // Deleted through the LiveUI: restore the player side resources it released (same setup as part creation). The render release is
         // deferred to the end of the frame and runs on the render thread: when it is still pending, the render setup is deferred to the
         // next frame preparation, which runs after the frame (and its deferred commands) has been fully processed
         g_pplayer->TimerSetup(editable);
         if (IRenderable *const renderable = editable->GetIRenderable())
         {
            if (editable->m_renderReleasePending)
               g_pplayer->m_renderer->SetupRenderable(renderable);
            else
            {
               renderable->RenderSetup(g_pplayer->m_renderer.get());
               g_pplayer->m_renderer->MarkStaticsDirty();
            }
         }
         if (editable->GetIHitable())
            g_pplayer->m_physics->Add(editable);
      }
   }

   for (const auto &pstm : record.m_vstm)
   {
      IEditable *const pie = *reinterpret_cast<IEditable *const *>(pstm->Data());
      if (inverse && FindIndexOf(inverse->m_vieCreate, pie) == -1) // Parts that are deleted again by the inverse need no snapshot
      {
         // Snapshot of the part before it is overwritten: restored when the inverse is applied
         auto inverseStm = std::make_unique<InMemStream>();
         inverseStm->Write(&pie, sizeof(IEditable *));
         BiffWriter inverseWriter(inverseStm.get(), nullptr);
         pie->Save(inverseWriter, true);
         inverse->m_vieMark.push_back(pie);
         inverse->m_vstm.push_back(std::move(inverseStm));
      }
      IScriptable *const scriptable = pie->GetIScriptable();
      const string nameBefore = scriptable ? scriptable->m_name : string();
      pie->ClearForOverwrite();

      // Process the loaded PartGroup parenting to support undoing reparenting
      pie->m_onLoadExpectedPartGroup.clear();
      BiffReader reader(pstm->Data() + sizeof(IEditable *), static_cast<uint32_t>(pstm->Size() - sizeof(IEditable *)), CURRENT_FILE_FORMAT_VERSION, nullptr, 0);
      pie->Load(reader);

      // Load writes the name directly: apply a restored name through the table to keep its name registry (and code view) in sync
      if (scriptable && scriptable->m_name != nameBefore && m_table->HasRegisteredName(pie))
      {
         const string restoredName = scriptable->m_name;
         scriptable->m_name = nameBefore;
         if (lowerCase(restoredName) == lowerCase(nameBefore) || m_table->IsNameUnique(restoredName))
            m_table->RenamePart(pie, restoredName);
         else
            PLOGW << "Undo could not restore the name '" << restoredName << "' of '" << nameBefore << "' as it is now used by another part";
      }
      // The record holds the name of the part's group (empty when it had none, which is only valid for part groups)
      if (!pie->m_onLoadExpectedPartGroup.empty())
      {
         const string groupName = pie->m_onLoadExpectedPartGroup;
         for (IEditable *const edit : m_table->GetParts())
            if (edit->GetItemType() == eItemPartGroup && StrCompareNoCase(edit->GetIScriptable()->m_name, groupName))
            {
               pie->SetPartGroup(static_cast<PartGroup *>(edit));
               break;
            }
      }
      else if (pie->GetItemType() == eItemPartGroup)
         pie->SetPartGroup(nullptr);
      if (g_pplayer)
      {
         if (pie->GetIRenderable())
            g_pplayer->m_renderer->ReinitRenderable(pie->GetIRenderable());
         if (pie->GetIHitable())
            g_pplayer->m_physics->Update(pie);
      }
   }

   record.m_ownsCreateRefs = false; // Uncreate drops the creation references
   for (size_t i = 0; i < record.m_vieCreate.size(); i++)
   {
      IEditable *const pie = record.m_vieCreate[i];
      if (inverse)
      {
         // Reverting this creation undo is a delete: the inverse keeps the part alive, to put it back in the table (Undelete) when applied
         inverse->m_vieDelete.push_back(pie);
         pie->AddRef();
      }
      if (g_pplayer && (g_pplayer->m_ptable == m_table))
      {
         // The part was created through the LiveUI and owns player side resources: release them before removing it
         if (pie->GetItemType() == eItemBall)
            RemoveFromVectorSingle(g_pplayer->m_vball, static_cast<Ball *>(pie));
         if (pie->GetIHitable())
            g_pplayer->m_physics->Remove(pie);
         if (pie->m_phittimer)
            g_pplayer->TimerRelease(pie);
         g_pplayer->m_renderer->DropRenderable(pie->GetIRenderable());
         pie->AddRef(); // Keep the part alive until its deferred render release has been processed
         pie->m_renderReleasePending = true;
         g_pplayer->m_renderer->m_renderDevice->AddEndOfFrameCmd(
            [pie]()
            {
               if (pie->GetIRenderable())
                  pie->GetIRenderable()->RenderRelease();
               pie->m_renderReleasePending = false;
               pie->Release();
            });
      }
      m_table->Uncreate(pie);
   }

   inverseOut = std::move(inverse);
   return std::move(record.m_editorState);
}

void PinUndo::Discard()
{
   assert(m_nUndoLayer == 0);
   while (m_nUndoLayer > 0)
      EndUndo();

   if (!HasUndo())
      return;

   std::unique_ptr<UndoRecord> record = std::move(m_undoRecords.back());
   m_undoRecords.pop_back();

   // The redo records set aside when the record was begun are back
   if (!record->m_stashedRedo.empty())
   {
      m_redoRecords = std::move(record->m_stashedRedo);
      record->m_stashedRedo.clear();
      m_cleanpoint = record->m_cleanpointAtBegin;
   }

   LogRemove(record->m_changeId);

   // A discarded record leaves no trace: restore the dirty state it changed when ended
   if (m_dirtyState != record->m_dirtyStateAtBegin)
   {
      m_dirtyState = record->m_dirtyStateAtBegin;
      m_table->SetDirty(m_dirtyState);
   }
}


void PinUndo::EndUndo()
{
   assert(m_nUndoLayer > 0);
   if (m_nUndoLayer > 0)
      m_nUndoLayer--;
   if (m_nUndoLayer == 0 && !m_undoRecords.empty())
      LogEnded(*m_undoRecords.back());
   if (m_nUndoLayer == 0 && (m_dirtyState < SaveDirtyState::Dirty))
   {
      m_dirtyState = SaveDirtyState::Dirty;
      m_table->SetDirty(SaveDirtyState::Dirty);
   }
}

