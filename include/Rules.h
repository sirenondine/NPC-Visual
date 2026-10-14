#pragma once
#include <memory>
#include <set>

#include "rapidjson/document.h"

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
//       "headSourcePlugins": ["Skyrim.esm"],// only swap a head that currently comes from
//                                           // these plugins (default: the vanilla masters);
//                                           // "*" = any. Keeps followers' custom heads.
//       "partPrefixes": ["CVEO_", "00_"],   // preference order
//       "raceAliases": { "snowelf": "highelfsnow" },   // optional, merged over the defaults
//       "plugins": [], "excludePlugins": [],           // NPC's defining plugin
//       "races": [],   "excludeRaces": [],             // race EditorIDs
//       "editorIdPrefixes": [], "excludeEditorIdPrefixes": []
//     }
//   ]
// }
//
// NPCs with their own JSON keep it, except for the head: when the JSON's Face
// part still comes from headSourcePlugins (an export of a record no replacer
// covers), SwapDocumentHead puts the rule head into the document before it is
// applied. Documents with a customFaceNif are left alone.
namespace NRules {
    class Session
    {
    public:
        Session(); // reads the rule files and indexes every head part
        ~Session();

        // Rewrites the Face entry of a per-NPC document's "headParts".
        // Returns true when the document changed.
        bool SwapDocumentHead(RE::TESNPC* a_npc, rapidjson::Document& a_doc);

        // Applies every rule to every NPC not in a_npcsWithJson.
        // Returns the number of NPCs changed.
        int Apply(const std::set<RE::FormID>& a_npcsWithJson);

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
