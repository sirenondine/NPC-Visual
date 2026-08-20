#include "logger.h"
#include "Settings.h"
#include "Manager.h"
#include "Hooks.h"

#include <chrono>
#include <string>

namespace {
    bool hasDFG = false;
    bool preparedCurrentLoad = false;

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton()
        {
            static DynamicFormsGeneratorListener singleton;
            return &singleton;
        }

        void Register()
        {
            if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
                dispatcher->AddEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            std::string_view eventName = a_event->eventName.c_str();
            if (eventName == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                return RE::BSEventNotifyControl::kContinue;
            }
            if (eventName == "DynamicFormsGeneratorUpdated") {
                Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void LoadSavedData(const char* reason)
    {
        const auto startedAt = std::chrono::steady_clock::now();
        logger::info("[SavedData] BEGIN reason={}", reason);
        NSettings::Load();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();
        logger::info("[SavedData] END reason={} elapsedMs={}", reason, elapsedMs);
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::info("DynamicFormsGenerator.dll found!");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        logger::debug("[MainMenuBoot] DataLoaded BEGIN");
        logger::debug("[MainMenuBoot] AllocTrampoline BEGIN");
        SKSE::AllocTrampoline(64);
        logger::debug("[MainMenuBoot] AllocTrampoline END");
        logger::debug("[MainMenuBoot] MmRegister BEGIN");
        NSettings::MmRegister();
        logger::debug("[MainMenuBoot] MmRegister END");
        logger::debug("[MainMenuBoot] InitializeFaceGenCache BEGIN");
        NSettings::InitializeFaceGenCache();
        logger::debug("[MainMenuBoot] InitializeFaceGenCache END");
        logger::debug("[MainMenuBoot] Load3DHook::Install BEGIN");
        Load3DHook::Install();
        logger::debug("[MainMenuBoot] Load3DHook::Install END");
        if (!hasDFG) {
            Manager::GetSingleton()->PopulateAllLists();
        }
        logger::debug("[MainMenuBoot] Saved JSON load deferred until PreLoadGame / NewGame");
        logger::debug("[MainMenuBoot] DataLoaded END");
    }
    if (message->type == SKSE::MessagingInterface::kPreLoadGame) {
        LoadSavedData("PreLoadGame");
        preparedCurrentLoad = true;
    }
    if (message->type == SKSE::MessagingInterface::kNewGame) {
        LoadSavedData("NewGame");
        preparedCurrentLoad = true;
    }
    if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
        if (!preparedCurrentLoad) {
            logger::warn("[SavedData] PreLoadGame was not observed; applying JSON fallback after PostLoadGame.");
            LoadSavedData("PostLoadGameFallback");
        }
        else {
            logger::debug("[SavedData] PostLoadGame requires no refresh; JSON was applied before actor 3D loading.");
        }
        preparedCurrentLoad = false;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
