#pragma once
#include <set>

// Runtime head-part rules: Data/Viny Mods/NPC Visual/Rules/*.json.
//
// A rule swaps head parts on every loaded NPC it matches, at PreLoadGame /
// NewGame, without a per-NPC JSON:
//   * the Face part -> "<headPrefix><Male|Female>Head<RaceToken>" for the NPC's
//     race and sex (00BoP_FemaleHeadNord), or a variant set
//     (MaleHeadKhajiit_tiger, _cheetah, ...) picked per NPC, stable on FormID;
//   * every other part "X" -> the first "<prefix>X" that exists, for the
//     prefixes listed in partPrefixes (00_BrowsMaleHumanoid06, CVEO_MaleEyesHumanBrown).
//     A part that already carries one of the prefixes is re-based first.
// Slots the record leaves empty are filled from the race defaults before the
// swap, as the engine would. NPCs that have their own JSON are left to it.
//
// {
//   "rules": [
//     {
//       "name": "IMMORTAN",
//       "headPrefix": "00BoP_",
//       "headVariants": [],                 // optional
//       "headPlugin": "",                   // optional: only heads from this plugin
//       "partPrefixes": ["CVEO_", "00_"],   // preference order
//       "raceAliases": { "snowelf": "highelfsnow" },   // optional, merged over the defaults
//       "plugins": [], "excludePlugins": [],           // NPC's defining plugin
//       "races": [],   "excludeRaces": [],             // race EditorIDs
//       "editorIdPrefixes": [], "excludeEditorIdPrefixes": []
//     }
//   ]
// }
namespace NRules {
    // Applies every rule file to every NPC not in a_npcsWithJson.
    // Returns the number of NPCs changed.
    int Apply(const std::set<RE::FormID>& a_npcsWithJson);
}
