#include "Rules.h"
#include "Settings.h"
#include "Manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ClibUtil/editorID.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"

namespace {
    const char* RulesPath = "Data/Viny Mods/NPC Visual/Rules";

    std::string Lower(std::string_view a_str)
    {
        std::string out(a_str);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool StartsWith(std::string_view a_str, std::string_view a_prefix)
    {
        return a_str.size() >= a_prefix.size() && Lower(a_str.substr(0, a_prefix.size())) == Lower(a_prefix);
    }

    std::vector<std::string> ReadStringArray(const rapidjson::Value& a_obj, const char* a_key)
    {
        std::vector<std::string> out;
        if (!a_obj.HasMember(a_key) || !a_obj[a_key].IsArray()) return out;
        for (const auto& v : a_obj[a_key].GetArray()) {
            if (v.IsString() && v.GetStringLength() > 0) out.emplace_back(v.GetString());
        }
        return out;
    }

    std::string ReadString(const rapidjson::Value& a_obj, const char* a_key)
    {
        if (a_obj.HasMember(a_key) && a_obj[a_key].IsString()) return a_obj[a_key].GetString();
        return {};
    }

    bool ListHas(const std::vector<std::string>& a_list, std::string_view a_value)
    {
        const auto v = Lower(a_value);
        return std::any_of(a_list.begin(), a_list.end(), [&](const std::string& s) { return Lower(s) == v; });
    }

    bool ListHasPrefixOf(const std::vector<std::string>& a_list, std::string_view a_value)
    {
        return std::any_of(a_list.begin(), a_list.end(), [&](const std::string& s) { return StartsWith(a_value, s); });
    }

    struct Rule
    {
        std::string name;
        std::string headPrefix;
        std::vector<std::string> headVariants;
        std::string headPlugin;
        std::vector<std::string> partPrefixes;
        std::map<std::string, std::string> raceAliases;
        std::vector<std::string> plugins, excludePlugins, races, excludeRaces, editorIdPrefixes, excludeEditorIdPrefixes;

        bool SwapsHeads() const { return !headPrefix.empty() || !headPlugin.empty(); }
    };

    // Every HDPT in the load order, keyed by lower-cased EditorID. Built once per Apply().
    class HeadPartIndex
    {
    public:
        HeadPartIndex()
        {
            auto* handler = RE::TESDataHandler::GetSingleton();
            if (!handler) return;
            for (auto* hp : handler->GetFormArray<RE::BGSHeadPart>()) {
                if (!hp) continue;
                const auto edid = clib_util::editorID::get_editorID(hp);
                if (edid.empty()) continue;
                _byEditorID.emplace(Lower(edid), hp);
                for (auto* extra : hp->extraParts) {
                    if (extra) _extras.insert(extra);
                }
            }
        }

        RE::BGSHeadPart* Find(std::string_view a_editorID) const
        {
            auto it = _byEditorID.find(Lower(a_editorID));
            return it == _byEditorID.end() ? nullptr : it->second;
        }

        bool IsExtra(RE::BGSHeadPart* a_hp) const { return _extras.contains(a_hp); }
        std::size_t Size() const { return _byEditorID.size(); }

    private:
        std::unordered_map<std::string, RE::BGSHeadPart*> _byEditorID;
        std::set<RE::BGSHeadPart*> _extras;
    };

    std::string EditorIDOf(RE::TESForm* a_form)
    {
        return a_form ? clib_util::editorID::get_editorID(a_form) : std::string{};
    }

    std::string PluginOf(RE::TESForm* a_form)
    {
        if (!a_form) return {};
        auto* file = a_form->GetFile(0);
        return file ? std::string(file->GetFilename()) : std::string{};
    }

    // NordRaceVampire -> nordvampire, then the rule's aliases
    std::string RaceToken(RE::TESRace* a_race, const Rule& a_rule)
    {
        std::string token = Lower(EditorIDOf(a_race));
        for (std::size_t pos = token.find("race"); pos != std::string::npos; pos = token.find("race")) {
            token.erase(pos, 4);
        }
        auto it = a_rule.raceAliases.find(token);
        return it == a_rule.raceAliases.end() ? token : it->second;
    }

    bool HeadFromAllowedPlugin(RE::BGSHeadPart* a_hp, const Rule& a_rule)
    {
        if (a_rule.headPlugin.empty()) return true;
        return Lower(PluginOf(a_hp)) == Lower(a_rule.headPlugin);
    }

    RE::BGSHeadPart* SwapTargetHead(RE::TESNPC* a_npc, RE::TESRace* a_race, const Rule& a_rule, const HeadPartIndex& a_index)
    {
        if (!a_rule.SwapsHeads() || !a_race) return nullptr;
        const std::string sex = a_npc->IsFemale() ? "female" : "male";
        const std::string token = RaceToken(a_race, a_rule);
        const std::string stem = Lower(a_rule.headPrefix) + sex + "head" + token;

        auto accept = [&](RE::BGSHeadPart* hp) -> RE::BGSHeadPart* {
            if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kFace) return nullptr;
            if (!HeadFromAllowedPlugin(hp, a_rule)) return nullptr;
            return hp;
        };

        // Variant sets first: start at a FormID-derived slot, first variant that exists wins
        if (!a_rule.headVariants.empty()) {
            const auto n = a_rule.headVariants.size();
            const auto start = static_cast<std::size_t>(a_npc->GetFormID() % n);
            for (std::size_t i = 0; i < n; ++i) {
                if (auto* hp = accept(a_index.Find(stem + "_" + Lower(a_rule.headVariants[(start + i) % n])))) return hp;
            }
        }
        return accept(a_index.Find(stem));
    }

    std::string BasePartName(const std::string& a_edid, const Rule& a_rule)
    {
        for (const auto& p : a_rule.partPrefixes) {
            if (!p.empty() && StartsWith(a_edid, p)) return a_edid.substr(p.size());
        }
        return a_edid;
    }

    // The preferred twin for a part, or nullptr when it is already the best one
    RE::BGSHeadPart* SwapTargetPart(RE::BGSHeadPart* a_hp, const Rule& a_rule, const HeadPartIndex& a_index)
    {
        const std::string cur = EditorIDOf(a_hp);
        const std::string base = BasePartName(cur, a_rule);
        if (base.empty()) return nullptr;
        for (const auto& p : a_rule.partPrefixes) {
            if (p.empty()) continue;
            auto* cand = a_index.Find(p + base);
            if (!cand || cand->type.get() != a_hp->type.get()) continue;
            if (cand == a_hp) return nullptr;
            return cand;
        }
        return nullptr;
    }

    bool RuleMatches(RE::TESNPC* a_npc, RE::TESRace* a_race, const Rule& a_rule)
    {
        const auto plugin = PluginOf(a_npc);
        if (!a_rule.plugins.empty() && !ListHas(a_rule.plugins, plugin)) return false;
        if (ListHas(a_rule.excludePlugins, plugin)) return false;

        const auto race = EditorIDOf(a_race);
        if (!a_rule.races.empty() && !ListHas(a_rule.races, race)) return false;
        if (ListHas(a_rule.excludeRaces, race)) return false;

        const auto edid = EditorIDOf(a_npc);
        if (!a_rule.editorIdPrefixes.empty() && !ListHasPrefixOf(a_rule.editorIdPrefixes, edid)) return false;
        if (ListHasPrefixOf(a_rule.excludeEditorIdPrefixes, edid)) return false;
        return true;
    }

    bool HasType(const std::vector<RE::BGSHeadPart*>& a_parts, RE::BGSHeadPart::HeadPartType a_type)
    {
        return std::any_of(a_parts.begin(), a_parts.end(), [&](auto* hp) { return hp && hp->type.get() == a_type; });
    }

    // Record parts (extras dropped, the engine re-adds them), then the race
    // defaults for every slot still empty.
    std::vector<RE::BGSHeadPart*> CollectParts(RE::TESNPC* a_npc, RE::TESRace* a_race, const HeadPartIndex& a_index)
    {
        std::vector<RE::BGSHeadPart*> parts;
        if (a_npc->headParts) {
            for (std::int8_t i = 0; i < a_npc->numHeadParts; ++i) {
                auto* hp = a_npc->headParts[i];
                if (!hp || a_index.IsExtra(hp)) continue;
                if (std::find(parts.begin(), parts.end(), hp) == parts.end()) parts.push_back(hp);
            }
        }
        if (a_race) {
            const auto sex = a_npc->IsFemale() ? RE::SEXES::kFemale : RE::SEXES::kMale;
            auto* face = a_race->faceRelatedData[sex];
            if (face && face->headParts) {
                for (auto* hp : *face->headParts) {
                    if (hp && !HasType(parts, hp->type.get())) parts.push_back(hp);
                }
            }
        }
        return parts;
    }

    std::vector<Rule> LoadRules()
    {
        std::vector<Rule> rules;
        std::error_code ec;
        if (!std::filesystem::is_directory(RulesPath, ec)) return rules;

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(RulesPath, ec)) {
            if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (const auto& path : files) {
            FILE* fp = nullptr;
            fopen_s(&fp, path.string().c_str(), "rb");
            if (!fp) {
                logger::warn("[Rules] cannot open {}", path.string());
                continue;
            }
            char buffer[65536];
            rapidjson::FileReadStream stream(fp, buffer, sizeof(buffer));
            rapidjson::Document doc;
            doc.ParseStream(stream);
            std::fclose(fp);
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("rules") || !doc["rules"].IsArray()) {
                logger::warn("[Rules] {} is not a JSON object with a \"rules\" array, ignored", path.string());
                continue;
            }
            for (const auto& r : doc["rules"].GetArray()) {
                if (!r.IsObject()) continue;
                Rule rule;
                rule.name = ReadString(r, "name");
                if (rule.name.empty()) rule.name = path.stem().string();
                rule.headPrefix = ReadString(r, "headPrefix");
                rule.headVariants = ReadStringArray(r, "headVariants");
                rule.headPlugin = ReadString(r, "headPlugin");
                rule.partPrefixes = ReadStringArray(r, "partPrefixes");
                rule.raceAliases = { { "snowelf", "highelfsnow" }, { "da13afflicted", "bretonafflicted" } };
                if (r.HasMember("raceAliases") && r["raceAliases"].IsObject()) {
                    for (auto it = r["raceAliases"].MemberBegin(); it != r["raceAliases"].MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsString()) rule.raceAliases[Lower(it->name.GetString())] = Lower(it->value.GetString());
                    }
                }
                rule.plugins = ReadStringArray(r, "plugins");
                rule.excludePlugins = ReadStringArray(r, "excludePlugins");
                rule.races = ReadStringArray(r, "races");
                rule.excludeRaces = ReadStringArray(r, "excludeRaces");
                rule.editorIdPrefixes = ReadStringArray(r, "editorIdPrefixes");
                rule.excludeEditorIdPrefixes = ReadStringArray(r, "excludeEditorIdPrefixes");

                if (!rule.SwapsHeads() && rule.partPrefixes.empty()) {
                    logger::warn("[Rules] rule '{}' in {} does nothing (no headPrefix/headPlugin and no partPrefixes), ignored", rule.name, path.filename().string());
                    continue;
                }
                if (rule.headPrefix.empty() && !rule.headPlugin.empty()) {
                    logger::info("[Rules] rule '{}': empty headPrefix, heads restricted to plugin '{}'", rule.name, rule.headPlugin);
                }
                rules.push_back(std::move(rule));
            }
            logger::info("[Rules] loaded {}", path.filename().string());
        }
        return rules;
    }
}

int NRules::Apply(const std::set<RE::FormID>& a_npcsWithJson)
{
    const auto rules = LoadRules();
    if (rules.empty()) {
        logger::info("[Rules] no rule files in {}", RulesPath);
        return 0;
    }

    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!handler) return 0;

    const HeadPartIndex index;
    logger::info("[Rules] {} rule(s), {} head parts indexed", rules.size(), index.Size());

    int changedNPCs = 0, headSwaps = 0, partSwaps = 0, skippedJson = 0, skippedTemplate = 0, noHead = 0;

    for (auto* npc : handler->GetFormArray<RE::TESNPC>()) {
        if (!npc || npc->IsDeleted()) continue;
        if (a_npcsWithJson.contains(npc->GetFormID())) {
            ++skippedJson;
            continue;
        }
        // Traits come from the template NPC; the template gets its own pass
        if (npc->baseTemplateForm && npc->actorData.templateUseFlags.any(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kTraits)) {
            ++skippedTemplate;
            continue;
        }
        auto* race = npc->race;
        if (!race) continue;

        std::vector<RE::BGSHeadPart*> parts;
        bool collected = false;
        bool changed = false;

        for (const auto& rule : rules) {
            if (!RuleMatches(npc, race, rule)) continue;
            if (!collected) {
                parts = CollectParts(npc, race, index);
                collected = true;
            }

            // Same-name twins on everything but the head
            for (auto& hp : parts) {
                if (!hp || hp->type.get() == RE::BGSHeadPart::HeadPartType::kFace) continue;
                if (auto* twin = SwapTargetPart(hp, rule, index)) {
                    logger::debug("[Rules] {} [{:08X}]: part {} -> {} ({})", EditorIDOf(npc), npc->GetFormID(), EditorIDOf(hp), EditorIDOf(twin), rule.name);
                    hp = twin;
                    ++partSwaps;
                    changed = true;
                }
            }

            // The head for this race and sex
            if (rule.SwapsHeads()) {
                auto* target = SwapTargetHead(npc, race, rule, index);
                if (!target) {
                    ++noHead;
                    logger::debug("[Rules] {} [{:08X}]: no head for {} in rule '{}'", EditorIDOf(npc), npc->GetFormID(), RaceToken(race, rule), rule.name);
                } else {
                    auto it = std::find_if(parts.begin(), parts.end(), [](auto* hp) { return hp && hp->type.get() == RE::BGSHeadPart::HeadPartType::kFace; });
                    if (it == parts.end()) {
                        parts.push_back(target);
                        ++headSwaps;
                        changed = true;
                        logger::debug("[Rules] {} [{:08X}]: head added {} ({})", EditorIDOf(npc), npc->GetFormID(), EditorIDOf(target), rule.name);
                    } else if (*it != target) {
                        logger::debug("[Rules] {} [{:08X}]: head {} -> {} ({})", EditorIDOf(npc), npc->GetFormID(), EditorIDOf(*it), EditorIDOf(target), rule.name);
                        *it = target;
                        ++headSwaps;
                        changed = true;
                    }
                }
            }
        }

        if (!changed) continue;

        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        rapidjson::Value hpArray(rapidjson::kArrayType);
        for (auto* hp : parts) {
            if (hp) hpArray.PushBack(FormUtil::MakeFormRef(hp, allocator), allocator);
        }
        doc.AddMember("headParts", hpArray, allocator);
        NSettings::ApplyDocumentToNPC(npc, doc);
        ++changedNPCs;
    }

    logger::info("[Rules] applied: {} NPCs changed, {} head swaps, {} part swaps; skipped {} with JSON, {} templated; {} without a head for their race/sex",
        changedNPCs, headSwaps, partSwaps, skippedJson, skippedTemplate, noHead);
    return changedNPCs;
}
