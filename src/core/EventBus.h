#pragma once
// ============================================================================
// EventBus - Publish/Subscribe system
// [AUDIO HOOK] AudioManager subscribes to all AudioEvent types here.
// ============================================================================

#include "Types.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <any>

namespace Unsuffered {

class EventBus {
public:
    using HandlerID = uint64_t;

    template<typename EventType>
    HandlerID Subscribe(std::function<void(const EventType&)> handler) {
        auto id = m_next_id++;
        auto wrapper = [handler](const std::any& data) {
            handler(std::any_cast<const EventType&>(data));
        };
        std::lock_guard lock(m_mutex);
        m_handlers[typeid(EventType).hash_code()].push_back({id, wrapper});
        return id;
    }

    template<typename EventType>
    void Publish(const EventType& event) {
        std::lock_guard lock(m_mutex);
        auto key = typeid(EventType).hash_code();
        if (auto it = m_handlers.find(key); it != m_handlers.end()) {
            for (auto& [id, handler] : it->second) {
                handler(std::any(event));
            }
        }
    }

    void Unsubscribe(HandlerID id) {
        std::lock_guard lock(m_mutex);
        for (auto& [key, handlers] : m_handlers) {
            std::erase_if(handlers, [id](const auto& entry) {
                return entry.first == id;
            });
        }
    }

    static EventBus& Instance() {
        static EventBus instance;
        return instance;
    }

private:
    using Handler = std::pair<HandlerID, std::function<void(const std::any&)>>;
    std::unordered_map<size_t, std::vector<Handler>> m_handlers;
    std::mutex m_mutex;
    HandlerID m_next_id = 1;
};

} // namespace Unsuffered
