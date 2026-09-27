#include "Hooks.h"
#include "Manager.h"
#include "DelayedDispatcher.h"
#include "BSFaceGenBaseMorphExtraData.h"

#include <array>
#include <cstdint>
#include <cstring>


// ==============================================================================
// FUNÇÃO AUXILIAR PARA RASTREIO DE FMD
// ==============================================================================
void LogAllFMDs(RE::NiAVObject* root, RE::FormID actorID, const char* callerContext) {
    if (!root) return;

    auto netObj = static_cast<RE::NiObjectNET*>(root);
    if (auto extra = netObj->GetExtraData("FMD")) {
        if (auto fmdExtra = static_cast<RE::BSFaceGenModelExtraData*>(extra)) {
            std::string nodeName = root->name.c_str() ? root->name.c_str() : "SemNome";
            logger::info("[{}] [Ator {:08X}] FMD encontrado na malha/node '{}' | m_model: {}",
                callerContext, actorID, nodeName, (void*)fmdExtra->m_model);
        }
    }

    // Busca recursivamente nos filhos
    if (auto node = root->AsNode()) {
        for (auto& child : node->GetChildren()) {
            if (child) {
                LogAllFMDs(child.get(), actorID, callerContext);
            }
        }
    }
}

// ==============================================================================
// SISTEMA DE AGENDAMENTO (POLLING PARA 3D CARREGADO)
// ==============================================================================
namespace TaskScheduler {

    static void WaitFor3DAndApplyLoad(RE::FormID refID, int retries) {
        logger::debug("[TaskScheduler] WaitFor3DAndApplyLoad request ref={:08X} retries={}", refID, retries);
        if (retries <= 0) {
            logger::warn("[TaskScheduler] Esgotaram as tentativas para carregar o 3D do FormID {:08X}.", refID);
            return;
        }

        Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(60), [refID, retries]() {
            logger::debug("[TaskScheduler] Delayed wake ref={:08X} retries={}", refID, retries);
            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                logger::error("[TaskScheduler] TaskInterface ausente ref={:08X}; WaitFor3D cancelado.", refID);
                return;
            }

            taskInterface->AddTask([refID, retries]() {
                logger::debug("[TaskScheduler] Task begin ref={:08X} retries={}", refID, retries);
                auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(refID);
                if (!ref) {
                    logger::warn("[TaskScheduler] Referencia {:08X} deixou de existir durante a espera.", refID);
                    return;
                }

                auto node3D = ref->Get3D();
                if (node3D) {
                    logger::info("[TaskScheduler] 3D carregado para ref={:08X} refPtr={:X} node3D={:X}.",
                        refID,
                        reinterpret_cast<std::uintptr_t>(ref),
                        reinterpret_cast<std::uintptr_t>(node3D));


                   // LogAllFMDs(node3D, refID, "TaskScheduler_Wait");

                    if (ref->GetFormType() == RE::FormType::ActorCharacter || ref->GetFormType() == RE::FormType::NPC) {
                        if (auto actor = ref->As<RE::Actor>()) {
                            if (auto base = actor->GetActorBase()) {
                                std::string nifPath;
                                if (Manager::GetSingleton()->IsNPCAffected(base->GetFormID(), nifPath)) {
                                    logger::info("[TaskScheduler] Actor afetado ref={:08X} base={:08X}; ScheduleFaceDeform nif='{}'",
                                        refID,
                                        base->GetFormID(),
                                        nifPath);
                                    Manager::ScheduleFaceDeform(actor->GetFormID(), nifPath, 20);
                                } else {
                                    logger::debug("[TaskScheduler] Actor ref={:08X} base={:08X} nao esta em AffectedNPC.", refID, base->GetFormID());
                                }
                            } else {
                                logger::warn("[TaskScheduler] Actor ref={:08X} sem ActorBase.", refID);
                            }
                        } else {
                            logger::warn("[TaskScheduler] Ref {:08X} com FormType actor mas As<Actor>() falhou.", refID);
                        }
                    }
                }
                else {
                    logger::debug("[TaskScheduler] 3D ainda ausente para {:08X}. Tentativas restantes: {}", refID, retries - 1);
                    WaitFor3DAndApplyLoad(refID, retries - 1);
                }
                logger::debug("[TaskScheduler] Task end ref={:08X} retries={}", refID, retries);
                });
            });
    }

}

void FaceMorphLockFix::Install()
{
    if (REL::Module::GetRuntime() != REL::Module::Runtime::AE) {
        logger::warn("[FaceMorphLockFix] Skipped: offsets are only known for AE.");
        return;
    }

    constexpr std::uintptr_t kBranchOffset = 0x80;  // jb <exit> right after the vertex-count check
    constexpr std::uintptr_t kExitOffset = 0x4A5;   // shared epilogue that skips the unlock
    REL::Relocation<std::uintptr_t> func{ REL::VariantID(0, 26917, 0x0) };
    REL::Relocation<std::uintptr_t> unlock{ REL::VariantID(0, 70952, 0x0) };

    const auto branch = func.address() + kBranchOffset;
    const auto exit = func.address() + kExitOffset;
    const auto* code = reinterpret_cast<const std::uint8_t*>(branch);
    std::int32_t rel = 0;
    std::memcpy(&rel, code + 2, sizeof(rel));
    if (code[0] != 0x0F || code[1] != 0x82 || branch + 6 + rel != exit) {
        logger::warn("[FaceMorphLockFix] Unexpected code at {:X}; not patching.", branch);
        return;
    }

    // mov rcx, [rsp+0xA8]   ; the locked geometry, spilled right after the lock
    // call [rip+2]          ; unlock
    // jmp +8
    // dq unlock
    // jmp [rip+0]
    // dq exit
    std::array<std::uint8_t, 38> stub{
        0x48, 0x8B, 0x8C, 0x24, 0xA8, 0x00, 0x00, 0x00,
        0xFF, 0x15, 0x02, 0x00, 0x00, 0x00,
        0xEB, 0x08,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    const auto unlockAddr = unlock.address();
    std::memcpy(stub.data() + 16, &unlockAddr, sizeof(unlockAddr));
    std::memcpy(stub.data() + 30, &exit, sizeof(exit));

    auto* mem = static_cast<std::uint8_t*>(SKSE::GetTrampoline().allocate(stub.size()));
    std::memcpy(mem, stub.data(), stub.size());

    const auto disp = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(mem) - (branch + 6));
    if (disp < INT32_MIN || disp > INT32_MAX) {
        logger::error("[FaceMorphLockFix] Stub at {:X} out of rel32 range; not patching.", reinterpret_cast<std::uintptr_t>(mem));
        return;
    }
    const auto newRel = static_cast<std::int32_t>(disp);
    REL::safe_write(branch + 2, &newRel, sizeof(newRel));
    logger::info("[FaceMorphLockFix] Installed at {:X} -> stub {:X}.", branch, reinterpret_cast<std::uintptr_t>(mem));
}

RE::NiAVObject* Load3DHook::Hook_Load3D_REFR(RE::TESObjectREFR* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_REFR(a_this, a_backgroundLoading);
    ProcessPrismaLoad3D(a_this, result3D);
    return result3D;
}

RE::NiAVObject* Load3DHook::Hook_Load3D_Char(RE::Character* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_Char(a_this, a_backgroundLoading);
    ProcessPrismaLoad3D(a_this, result3D);
    return result3D;
}

RE::NiAVObject* Load3DHook::Hook_Load3D_Player(RE::PlayerCharacter* a_this, bool a_backgroundLoading) {
    auto result3D = _Load3D_Player(a_this, a_backgroundLoading);
    ProcessPrismaLoad3D(a_this, result3D);
    return result3D;
}

// Lógica unificada para qualquer objeto que termine de carregar o 3D
void Load3DHook::ProcessPrismaLoad3D(RE::TESObjectREFR* a_this, RE::NiAVObject* result3D) {
    if (result3D && a_this) {
        bool isActor = (a_this->GetFormType() == RE::FormType::ActorCharacter || a_this->GetFormType() == RE::FormType::NPC);
        const char* typeStr = isActor ? "ATOR " : "";

        if (isActor) {
            logger::debug("[LOAD3D] ProcessPrismaLoad3D {}ref={:08X} result3D={:X} is3DLoaded={}",
                typeStr,
                a_this->GetFormID(),
                reinterpret_cast<std::uintptr_t>(result3D),
                a_this->Is3DLoaded());
            if (a_this->Is3DLoaded()) {
                //logger::info("[LOAD3D] Malha 3D carregada para {}RefID: {:08X}. Inspecionando FMDs...", typeStr, a_this->GetFormID());

                // --- INJEÇÃO DO LOG DE RASTREIO DIRETO DO RESULT3D ---
                //LogAllFMDs(result3D, a_this->GetFormID(), "ProcessPrismaLoad3D_Imediato");

                if (auto actor = a_this->As<RE::Actor>()) {
                    // VERIFICA O BANCO DE DADOS EM MEMÓRIA
                    if (auto base = actor->GetActorBase()) {
                        std::string nifPath;
                        if (Manager::GetSingleton()->IsNPCAffected(base->GetFormID(), nifPath)) {
                            logger::info("[LOAD3D] {}RefID={:08X} base={:08X} afetado; agendando deformacao nif='{}'",
                                typeStr,
                                a_this->GetFormID(),
                                base->GetFormID(),
                                nifPath);
                            Manager::ScheduleFaceDeform(actor->GetFormID(), nifPath, 20);
                        } else {
                            logger::debug("[LOAD3D] {}RefID={:08X} base={:08X} nao afetado.", typeStr, a_this->GetFormID(), base->GetFormID());
                        }
                    } else {
                        logger::warn("[LOAD3D] {}RefID={:08X} sem ActorBase.", typeStr, a_this->GetFormID());
                    }
                } else {
                    logger::warn("[LOAD3D] {}RefID={:08X} As<Actor>() falhou.", typeStr, a_this->GetFormID());
                }
            }
            else {
                logger::debug("[LOAD3D] Load3D foi chamado para {}RefID: {:08X}, mas o 3D ainda nao esta marcado como carregado. Agendando...", typeStr, a_this->GetFormID());
                TaskScheduler::WaitFor3DAndApplyLoad(a_this->GetFormID(), 20);
            }
        }
    }
}
