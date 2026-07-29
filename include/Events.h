#pragma once

#include "RE/Skyrim.h"

namespace NPCVisualEvents {
    inline constexpr auto kUpdateEventName = "NPCVisualUpdate";

    // strArg carries the exact 8-digit hexadecimal FormID. numArg is also
    // populated for conventional ModEvent consumers, and sender is the TESNPC.
    void QueueUpdate(RE::FormID a_npcFormID);
}
