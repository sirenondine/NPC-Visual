#include "Rules.h"
#include "Settings.h"
#include "Manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <format>
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
        // Part EditorID (prefix-stripped base or exact, lower-cased) -> replacement EditorID.
        // For replacers whose parts don't share the vanilla name, e.g. DollFace HPH scars.
        std::map<std::string, std::string> partMap;
        std::string sex; // "female", "male" or empty for both
        std::map<std::string, std::string> raceAliases;
        std::vector<std::string> plugins, excludePlugins, races, excludeRaces, editorIdPrefixes, excludeEditorIdPrefixes;
        // Only swap a head that currently comes from one of these plugins.
        // Protects followers / replacers that ship their own head part and a
        // sculpted FaceGen NVE cannot rebuild. "*" = any plugin.
        std::vector<std::string> headSourcePlugins;

        bool SwapsHeads() const { return !headPrefix.empty() || !headPlugin.empty(); }
        bool HeadSourceAllowed(std::string_view a_plugin) const
        {
            return ListHas(headSourcePlugins, "*") || ListHas(headSourcePlugins, a_plugin);
        }
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

    RE::BGSHeadPart* MappedPart(RE::BGSHeadPart* a_hp, const Rule& a_rule, const HeadPartIndex& a_index)
    {
        if (a_rule.partMap.empty()) return nullptr;
        const std::string cur = EditorIDOf(a_hp);
        auto it = a_rule.partMap.find(Lower(cur));
        if (it == a_rule.partMap.end()) it = a_rule.partMap.find(Lower(BasePartName(cur, a_rule)));
        if (it == a_rule.partMap.end()) return nullptr;
        auto* cand = a_index.Find(it->second);
        if (!cand || cand == a_hp || cand->type.get() != a_hp->type.get()) return nullptr;
        return cand;
    }

    bool RuleMatches(RE::TESNPC* a_npc, RE::TESRace* a_race, const Rule& a_rule)
    {
        if (!a_rule.sex.empty() && a_rule.sex != (a_npc->IsFemale() ? "female" : "male")) return false;

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

    bool IsTraitTemplated(RE::TESNPC* a_npc)
    {
        return a_npc->baseTemplateForm && a_npc->actorData.templateUseFlags.any(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kTraits);
    }

    RE::BGSHeadPart* RaceDefaultPart(RE::TESNPC* a_npc, RE::TESRace* a_race, RE::BGSHeadPart::HeadPartType a_type)
    {
        if (!a_race) return nullptr;
        auto* face = a_race->faceRelatedData[a_npc->IsFemale() ? RE::SEXES::kFemale : RE::SEXES::kMale];
        if (!face || !face->headParts) return nullptr;
        for (auto* hp : *face->headParts) {
            if (hp && hp->type.get() == a_type) return hp;
        }
        return nullptr;
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
                if (r.HasMember("partMap") && r["partMap"].IsObject()) {
                    for (auto it = r["partMap"].MemberBegin(); it != r["partMap"].MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsString()) rule.partMap[Lower(it->name.GetString())] = it->value.GetString();
                    }
                }
                rule.sex = Lower(ReadString(r, "sex"));
                if (!rule.sex.empty() && rule.sex != "female" && rule.sex != "male") {
                    logger::warn("[Rules] rule '{}' in {}: sex '{}' is not \"female\" or \"male\", ignored", rule.name, path.filename().string(), rule.sex);
                    continue;
                }
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
                rule.headSourcePlugins = ReadStringArray(r, "headSourcePlugins");
                if (rule.headSourcePlugins.empty()) {
                    rule.headSourcePlugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
                }

                if (!rule.SwapsHeads() && rule.partPrefixes.empty() && rule.partMap.empty()) {
                    logger::warn("[Rules] rule '{}' in {} does nothing (no headPrefix/headPlugin, partPrefixes or partMap), ignored", rule.name, path.filename().string());
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

struct NRules::Session::Impl
{
    std::vector<Rule> rules = LoadRules();
    HeadPartIndex index;
    int jsonHeadSwaps = 0;
};

NRules::Session::Session() :
    _impl(std::make_unique<Impl>())
{
    if (_impl->rules.empty()) {
        logger::info("[Rules] no rule files in {}", RulesPath);
    } else {
        logger::info("[Rules] {} rule(s), {} head parts indexed", _impl->rules.size(), _impl->index.Size());
    }
}

NRules::Session::~Session() = default;

bool NRules::Session::SwapDocumentHead(RE::TESNPC* a_npc, rapidjson::Document& a_doc)
{
    if (!a_npc || !a_npc->race || !a_doc.IsObject() || IsTraitTemplated(a_npc)) return false;
    if (a_doc.HasMember("customFaceNif") && a_doc["customFaceNif"].IsString() && a_doc["customFaceNif"].GetStringLength() > 0) return false;
    if (!a_doc.HasMember("headParts") || !a_doc["headParts"].IsArray()) return false;

    auto& parts = a_doc["headParts"];
    rapidjson::Value* faceRef = nullptr;
    RE::BGSHeadPart* face = nullptr;
    for (auto& ref : parts.GetArray()) {
        auto* hp = FormUtil::ResolveForm<RE::BGSHeadPart>(ref);
        if (hp && hp->type.get() == RE::BGSHeadPart::HeadPartType::kFace) {
            faceRef = &ref;
            face = hp;
            break;
        }
    }
    if (!face) face = RaceDefaultPart(a_npc, a_npc->race, RE::BGSHeadPart::HeadPartType::kFace);

    // Same order as Apply(): once a rule has placed its head, later rules see
    // a head outside their headSourcePlugins and keep it.
    RE::BGSHeadPart* head = face;
    const Rule* by = nullptr;
    for (const auto& rule : _impl->rules) {
        if (!rule.SwapsHeads() || !RuleMatches(a_npc, a_npc->race, rule)) continue;
        auto* target = SwapTargetHead(a_npc, a_npc->race, rule, _impl->index);
        if (!target || target == head) continue;
        if (head && !rule.HeadSourceAllowed(PluginOf(head))) continue;
        head = target;
        by = &rule;
    }
    if (!by) return false;

    auto& allocator = a_doc.GetAllocator();
    if (faceRef) {
        *faceRef = FormUtil::MakeFormRef(head, allocator);
    } else {
        parts.PushBack(FormUtil::MakeFormRef(head, allocator), allocator);
    }
    logger::debug("[Rules] {} [{:08X}]: JSON head {} -> {} ({})", EditorIDOf(a_npc), a_npc->GetFormID(), EditorIDOf(face), EditorIDOf(head), by->name);
    ++_impl->jsonHeadSwaps;
    return true;
}

int NRules::Session::Apply(const std::set<RE::FormID>& a_npcsWithJson)
{
    const auto& rules = _impl->rules;
    if (rules.empty()) return 0;

    auto* handler = RE::TESDataHandler::GetSingleton();
    if (!handler) return 0;

    const auto& index = _impl->index;

    int changedNPCs = 0, headSwaps = 0, partSwaps = 0, skippedJson = 0, skippedTemplate = 0, noHead = 0, keptCustomHead = 0;
    std::map<std::string, int> noHeadByRace;      // "NordRaceVampire (F)" -> count
    std::map<std::string, int> customHeadByPlugin; // defining plugin of the head we left alone -> count

    for (auto* npc : handler->GetFormArray<RE::TESNPC>()) {
        if (!npc || npc->IsDeleted()) continue;
        if (a_npcsWithJson.contains(npc->GetFormID())) {
            ++skippedJson;
            continue;
        }
        // Traits come from the template NPC; the template gets its own pass
        if (IsTraitTemplated(npc)) {
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
                if (auto* mapped = MappedPart(hp, rule, index)) {
                    logger::debug("[Rules] {} [{:08X}]: part {} -> {} (mapped, {})", EditorIDOf(npc), npc->GetFormID(), EditorIDOf(hp), EditorIDOf(mapped), rule.name);
                    hp = mapped;
                    ++partSwaps;
                    changed = true;
                }
            }
            // Two parts mapped onto the same replacement
            for (auto it = parts.begin(); it != parts.end(); ++it) {
                parts.erase(std::remove(std::next(it), parts.end(), *it), parts.end());
            }

            // The head for this race and sex
            if (rule.SwapsHeads()) {
                auto* target = SwapTargetHead(npc, race, rule, index);
                auto it = std::find_if(parts.begin(), parts.end(), [](auto* hp) { return hp && hp->type.get() == RE::BGSHeadPart::HeadPartType::kFace; });
                if (!target) {
                    ++noHead;
                    ++noHeadByRace[EditorIDOf(race) + (npc->IsFemale() ? " (F)" : " (M)")];
                    logger::debug("[Rules] {} [{:08X}]: no head for {} in rule '{}'", EditorIDOf(npc), npc->GetFormID(), RaceToken(race, rule), rule.name);
                } else if (it != parts.end() && *it != target && !rule.HeadSourceAllowed(PluginOf(*it))) {
                    ++keptCustomHead;
                    ++customHeadByPlugin[PluginOf(*it)];
                    logger::debug("[Rules] {} [{:08X}]: head {} from {} kept (not in headSourcePlugins)", EditorIDOf(npc), npc->GetFormID(), EditorIDOf(*it), PluginOf(*it));
                } else {
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

    logger::info("[Rules] applied: {} NPCs changed, {} head swaps, {} part swaps; skipped {} with JSON ({} of them got the rule head), {} templated; {} kept a custom head; {} without a head for their race/sex",
        changedNPCs, headSwaps, partSwaps, skippedJson, _impl->jsonHeadSwaps, skippedTemplate, keptCustomHead, noHead);

    auto logTally = [](const char* a_label, const std::map<std::string, int>& a_tally) {
        if (a_tally.empty()) return;
        std::vector<std::pair<std::string, int>> rows(a_tally.begin(), a_tally.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string line;
        std::size_t shown = 0;
        for (const auto& [key, count] : rows) {
            if (shown++ == 30) {
                line += std::format(", ... {} more", rows.size() - 30);
                break;
            }
            if (!line.empty()) line += ", ";
            line += std::format("{} {}", key, count);
        }
        logger::info("[Rules] {}: {}", a_label, line);
    };
    logTally("no head by race", noHeadByRace);
    logTally("custom heads kept by plugin", customHeadByPlugin);
    return changedNPCs;
}
