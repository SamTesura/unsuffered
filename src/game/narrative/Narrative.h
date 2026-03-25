#pragma once
// ============================================================================
// Narrative System - Dialogue, Quests, Choices, Endings
// The ending is determined by cumulative choices, bond depth, and
// philosophical decision points throughout the story.
// ============================================================================

#include "core/Types.h"
#include "core/EventBus.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Unsuffered {

// --- Dialogue Node ---
struct DialogueNode {
    std::string id;
    std::string speaker;      // Character name or "NARRATOR"
    std::string text;
    std::vector<std::string> choices;       // Player options
    std::vector<std::string> choice_targets; // Node IDs to jump to
    std::string condition;     // Prerequisite (quest flag, bond level, etc.)
    bool is_philosophical;     // Marks Nietzschean decision points

    // [AUDIO HOOK] Dialogue nodes can trigger:
    // - DialogueOpen/Close events
    // - Character voice lines
    // - Ambient mood shifts
    std::string audio_cue;     // Optional audio asset to play on entry
};

// --- Quest ---
struct Quest {
    QuestID id;
    std::string name;
    std::string description;
    int chapter;               // Which story chapter (0=Prologue, 1-5)
    bool is_main_quest;
    std::vector<std::string> objectives;
    std::vector<bool> objective_complete;
    FallenID related_fallen;   // Which Fallen this quest confronts
    bool completed = false;
};

// --- Choice Record (for ending determination) ---
struct ChoiceRecord {
    std::string choice_id;
    std::string chosen_option;
    int chapter;
    bool is_philosophical;     // Nietzschean decision point
    float timestamp;           // Game-time when made

    // Philosophical weight categories:
    enum class Philosophy { Creation, Destruction, Submission, Affirmation, Neutral };
    Philosophy weight;
};

// --- Dialogue System ---
class DialogueSystem {
public:
    void LoadDialogue(const std::string& json_path);

    // [AUDIO HOOK] Fires DialogueOpen event -> NarrativeAudioManager
    void StartConversation(const std::string& conversation_id);

    // [AUDIO HOOK] Fires DialogueClose event
    void EndConversation();

    // Advance to next node or present choices
    const DialogueNode& GetCurrentNode() const;
    void SelectChoice(int index);
    bool IsActive() const { return m_active; }

private:
    std::unordered_map<std::string, DialogueNode> m_nodes;
    std::string m_current_node_id;
    bool m_active = false;
};

// --- Quest Manager ---
class QuestManager {
public:
    void LoadQuests(const std::string& json_path);
    void StartQuest(QuestID id);
    void CompleteObjective(QuestID quest, int objective_index);
    void CompleteQuest(QuestID id);

    const Quest& GetQuest(QuestID id) const;
    std::vector<const Quest*> GetActiveQuests() const;
    bool IsQuestComplete(QuestID id) const;
    int GetCurrentChapter() const { return m_current_chapter; }
    void AdvanceChapter();

private:
    std::unordered_map<QuestID, Quest> m_quests;
    std::vector<QuestID> m_active_quests;
    int m_current_chapter = 0; // Prologue
};

// --- Choice Tracker ---
// Tracks all player decisions to determine ending.
// Ending criteria:
//   LastMan:        Low bonds, avoided confrontation, Submission weight dominant
//   HigherMan:      High bonds, Destruction weight dominant
//   Ubermensch:     Deep bonds, Creation weight dominant
//   EternalRecurrence: NG+ with all bonds maxed, Affirmation weight dominant
class ChoiceTracker {
public:
    // [AUDIO HOOK] ChoiceMade event fires when philosophical choices are made
    void RecordChoice(const ChoiceRecord& choice);

    // Aggregate scores for ending determination
    float GetCreationScore() const;
    float GetDestructionScore() const;
    float GetSubmissionScore() const;
    float GetAffirmationScore() const;

    // Determine ending based on accumulated choices + bond state
    // [AUDIO HOOK] EndingTriggered event -> NarrativeAudioManager::PlayEnding()
    Ending DetermineEnding(float avg_bond_depth, bool is_ng_plus) const;

    const std::vector<ChoiceRecord>& GetHistory() const { return m_history; }

private:
    std::vector<ChoiceRecord> m_history;

    int CountByWeight(ChoiceRecord::Philosophy weight) const;
};

} // namespace Unsuffered
