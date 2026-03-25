// ============================================================================
// Narrative System Implementation
// Dialogue, Quests, Choice Tracking, Ending Determination
// ============================================================================

#include "game/narrative/Narrative.h"
#include "core/Logger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Unsuffered {

// ============================================================================
// Dialogue System
// ============================================================================

void DialogueSystem::LoadDialogue(const std::string& json_path) {
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            Logger::Error("Failed to open dialogue file: {}", json_path);
            return;
        }

        nlohmann::json j;
        file >> j;

        for (const auto& node_json : j["nodes"]) {
            DialogueNode node;
            node.id = node_json["id"].get<std::string>();
            node.speaker = node_json["speaker"].get<std::string>();
            node.text = node_json["text"].get<std::string>();
            node.is_philosophical = node_json.value("is_philosophical", false);
            node.audio_cue = node_json.value("audio_cue", "");
            node.condition = node_json.value("condition", "");

            if (node_json.contains("choices")) {
                for (const auto& choice : node_json["choices"]) {
                    node.choices.push_back(choice.get<std::string>());
                }
            }
            if (node_json.contains("choice_targets")) {
                for (const auto& target : node_json["choice_targets"]) {
                    node.choice_targets.push_back(target.get<std::string>());
                }
            }

            m_nodes[node.id] = std::move(node);
        }

        Logger::Info("Loaded {} dialogue nodes from {}", m_nodes.size(), json_path);
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse dialogue: {}", e.what());
    }
}

void DialogueSystem::StartConversation(const std::string& conversation_id) {
    auto it = m_nodes.find(conversation_id);
    if (it == m_nodes.end()) {
        Logger::Error("Conversation not found: {}", conversation_id);
        return;
    }

    m_current_node_id = conversation_id;
    m_active = true;

    // [AUDIO HOOK] Fire DialogueOpen event
    AudioEventData event{AudioEvent::DialogueOpen};
    EventBus::Instance().Publish(event);

    // Play node-specific audio cue if present
    const auto& node = it->second;
    if (!node.audio_cue.empty()) {
        AudioEventData cue_event{AudioEvent::AmbientTrigger};
        cue_event.asset_override = node.audio_cue;
        EventBus::Instance().Publish(cue_event);
    }

    Logger::Info("Conversation started: {} (Speaker: {})",
                 conversation_id, node.speaker);
}

void DialogueSystem::EndConversation() {
    m_active = false;
    m_current_node_id.clear();

    // [AUDIO HOOK] Fire DialogueClose event
    AudioEventData event{AudioEvent::DialogueClose};
    EventBus::Instance().Publish(event);

    Logger::Info("Conversation ended");
}

const DialogueNode& DialogueSystem::GetCurrentNode() const {
    auto it = m_nodes.find(m_current_node_id);
    if (it != m_nodes.end()) return it->second;
    static DialogueNode empty{};
    return empty;
}

void DialogueSystem::SelectChoice(int index) {
    const auto& node = GetCurrentNode();
    if (index < 0 || index >= static_cast<int>(node.choice_targets.size())) {
        Logger::Error("Invalid choice index: {} (max: {})",
                      index, node.choice_targets.size() - 1);
        return;
    }

    const std::string& target = node.choice_targets[index];

    // If philosophical choice, fire ChoiceMade event
    if (node.is_philosophical) {
        AudioEventData event{AudioEvent::ChoiceMade};
        EventBus::Instance().Publish(event);
    }

    m_current_node_id = target;

    // Check if new node has audio cue
    const auto& next_node = GetCurrentNode();
    if (!next_node.audio_cue.empty()) {
        AudioEventData cue_event{AudioEvent::AmbientTrigger};
        cue_event.asset_override = next_node.audio_cue;
        EventBus::Instance().Publish(cue_event);
    }

    Logger::Debug("Choice selected: {} -> {}", index, target);
}

// ============================================================================
// Quest Manager
// ============================================================================

void QuestManager::LoadQuests(const std::string& json_path) {
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            Logger::Error("Failed to open quests file: {}", json_path);
            return;
        }

        nlohmann::json j;
        file >> j;

        for (const auto& quest_json : j["quests"]) {
            Quest quest;
            quest.id = quest_json["id"].get<QuestID>();
            quest.name = quest_json["name"].get<std::string>();
            quest.description = quest_json["description"].get<std::string>();
            quest.chapter = quest_json["chapter"].get<int>();
            quest.is_main_quest = quest_json.value("is_main_quest", false);
            quest.related_fallen = static_cast<FallenID>(quest_json.value("related_fallen", -1));

            if (quest_json.contains("objectives")) {
                for (const auto& obj : quest_json["objectives"]) {
                    quest.objectives.push_back(obj.get<std::string>());
                    quest.objective_complete.push_back(false);
                }
            }

            m_quests[quest.id] = std::move(quest);
        }

        Logger::Info("Loaded {} quests from {}", m_quests.size(), json_path);
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse quests: {}", e.what());
    }
}

void QuestManager::StartQuest(QuestID id) {
    auto it = m_quests.find(id);
    if (it == m_quests.end()) {
        Logger::Error("Quest {} not found", id);
        return;
    }
    m_active_quests.push_back(id);
    Logger::Info("Quest started: {}", it->second.name);
}

void QuestManager::CompleteObjective(QuestID quest, int objective_index) {
    auto it = m_quests.find(quest);
    if (it == m_quests.end()) return;
    if (objective_index < static_cast<int>(it->second.objective_complete.size())) {
        it->second.objective_complete[objective_index] = true;
        Logger::Info("Objective completed: {} [{}]",
                     it->second.objectives[objective_index], it->second.name);
    }
}

void QuestManager::CompleteQuest(QuestID id) {
    auto it = m_quests.find(id);
    if (it == m_quests.end()) return;
    it->second.completed = true;
    std::erase(m_active_quests, id);
    Logger::Info("Quest completed: {}", it->second.name);
}

const Quest& QuestManager::GetQuest(QuestID id) const {
    auto it = m_quests.find(id);
    if (it != m_quests.end()) return it->second;
    static Quest empty{};
    return empty;
}

std::vector<const Quest*> QuestManager::GetActiveQuests() const {
    std::vector<const Quest*> active;
    for (QuestID id : m_active_quests) {
        auto it = m_quests.find(id);
        if (it != m_quests.end()) active.push_back(&it->second);
    }
    return active;
}

bool QuestManager::IsQuestComplete(QuestID id) const {
    auto it = m_quests.find(id);
    return it != m_quests.end() && it->second.completed;
}

void QuestManager::AdvanceChapter() {
    m_current_chapter++;
    Logger::Info("Chapter advanced to: {}", m_current_chapter);
}

// ============================================================================
// Choice Tracker - Ending Determination
// ============================================================================

void ChoiceTracker::RecordChoice(const ChoiceRecord& choice) {
    m_history.push_back(choice);

    if (choice.is_philosophical) {
        // [AUDIO HOOK] Philosophical choice event
        AudioEventData event{AudioEvent::ChoiceMade};
        EventBus::Instance().Publish(event);

        Logger::Info("Philosophical choice recorded: {} -> {} (weight: {})",
                     choice.choice_id, choice.chosen_option,
                     static_cast<int>(choice.weight));
    }
}

int ChoiceTracker::CountByWeight(ChoiceRecord::Philosophy weight) const {
    return static_cast<int>(std::count_if(m_history.begin(), m_history.end(),
        [weight](const ChoiceRecord& r) {
            return r.is_philosophical && r.weight == weight;
        }));
}

float ChoiceTracker::GetCreationScore() const {
    return static_cast<float>(CountByWeight(ChoiceRecord::Philosophy::Creation));
}

float ChoiceTracker::GetDestructionScore() const {
    return static_cast<float>(CountByWeight(ChoiceRecord::Philosophy::Destruction));
}

float ChoiceTracker::GetSubmissionScore() const {
    return static_cast<float>(CountByWeight(ChoiceRecord::Philosophy::Submission));
}

float ChoiceTracker::GetAffirmationScore() const {
    return static_cast<float>(CountByWeight(ChoiceRecord::Philosophy::Affirmation));
}

Ending ChoiceTracker::DetermineEnding(float avg_bond_depth, bool is_ng_plus) const {
    // Eternal Recurrence: NG+ with high bonds and high affirmation
    if (is_ng_plus && avg_bond_depth >= 1500.0f &&
        GetAffirmationScore() >= GetCreationScore()) {
        Logger::Info("Ending determined: Eternal Recurrence");

        AudioEventData event{AudioEvent::EndingTriggered};
        event.asset_override = "ending_eternal_recurrence";
        EventBus::Instance().Publish(event);

        return Ending::EternalRecurrence;
    }

    // Ubermensch: Deep bonds + creation-dominant choices
    if (avg_bond_depth >= 800.0f && GetCreationScore() > GetDestructionScore() &&
        GetCreationScore() > GetSubmissionScore()) {
        Logger::Info("Ending determined: The Ubermensch");

        AudioEventData event{AudioEvent::EndingTriggered};
        event.asset_override = "ending_ubermensch";
        EventBus::Instance().Publish(event);

        return Ending::TheUbermensch;
    }

    // Higher Man: High bonds + destruction-dominant
    if (avg_bond_depth >= 400.0f && GetDestructionScore() > GetCreationScore()) {
        Logger::Info("Ending determined: The Higher Man");

        AudioEventData event{AudioEvent::EndingTriggered};
        event.asset_override = "ending_higher_man";
        EventBus::Instance().Publish(event);

        return Ending::TheHigherMan;
    }

    // Last Man: Low bonds, submission-dominant or passive
    Logger::Info("Ending determined: The Last Man");

    AudioEventData event{AudioEvent::EndingTriggered};
    event.asset_override = "ending_last_man";
    EventBus::Instance().Publish(event);

    return Ending::TheLastMan;
}

} // namespace Unsuffered
