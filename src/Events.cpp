#include "Events.h"

#include <mutex>
#include <unordered_set>

namespace {
    std::mutex g_pendingUpdateLock;
    std::unordered_set<RE::FormID> g_pendingUpdates;

    void DispatchNPCVisualUpdate(RE::FormID a_npcFormID) {
        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            logger::warn("[NPCVisualUpdate] Event source unavailable for NPC {:08X}.", a_npcFormID);
            return;
        }

        const auto formIDString = std::format("{:08X}", a_npcFormID);
        auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(a_npcFormID);
        SKSE::ModCallbackEvent event{RE::BSFixedString(NPCVisualEvents::kUpdateEventName),
                                     RE::BSFixedString(formIDString.c_str()), static_cast<float>(a_npcFormID), npc};
        eventSource->SendEvent(&event);
        logger::info("[NPCVisualUpdate] Dispatched event='{}' npc={:08X} strArg='{}' numArg={} sender={:X}.",
                     NPCVisualEvents::kUpdateEventName, a_npcFormID, formIDString, event.numArg,
                     reinterpret_cast<std::uintptr_t>(npc));
    }

    void CompletePendingUpdate(RE::FormID a_npcFormID) {
        {
            std::scoped_lock lock(g_pendingUpdateLock);
            g_pendingUpdates.erase(a_npcFormID);
        }
        DispatchNPCVisualUpdate(a_npcFormID);
    }
}

void NPCVisualEvents::QueueUpdate(RE::FormID a_npcFormID) {
    if (a_npcFormID == 0) {
        return;
    }

    {
        std::scoped_lock lock(g_pendingUpdateLock);
        if (!g_pendingUpdates.insert(a_npcFormID).second) {
            logger::debug("[NPCVisualUpdate] Coalesced duplicate update for NPC {:08X}.", a_npcFormID);
            return;
        }
    }

    if (auto* taskInterface = SKSE::GetTaskInterface()) {
        taskInterface->AddTask([a_npcFormID]() { CompletePendingUpdate(a_npcFormID); });
        return;
    }

    logger::warn("[NPCVisualUpdate] TaskInterface unavailable; dispatching NPC {:08X} immediately.", a_npcFormID);
    CompletePendingUpdate(a_npcFormID);
}
