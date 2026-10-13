/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#include "ModuleAdapters.h"
#include "ScriptMgr.h"
#include "SpellScript.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Trinity::Modules
{
namespace
{

struct State
{
    trinity_host const* host;
    trinity_spell_callbacks spell{};
    trinity_aura_callbacks aura{};
    bool hasSpell = false;
    bool hasAura = false;

    int32_t Dispatch(trinity_spell_callbacks const& table, uint32_t id, void* object,
        trinity_object_kind kind, trinity_spell_hook_result& result) const
    {
        trinity_callback_scope scope = BeginAbiCallbackScope(host, TRINITY_FAMILY_SPELL_LOADER);
        trinity_handle handle = object ? RegisterAbiObject(host, scope, kind, object) : 0;
        int32_t status = table.dispatch_hook(table.context, host, id, &scope, handle, &result);
        EndAbiCallbackScope(host, scope);
        return status;
    }

    int32_t Spell(uint32_t id, void* object = nullptr, trinity_object_kind kind = TRINITY_OBJECT_UNKNOWN,
        trinity_spell_hook_result* result = nullptr) const
    {
        if (!hasSpell || !spell.dispatch_hook)
            return -1;
        trinity_spell_hook_result local{sizeof(local), 0, 0, 0, 1.0F, 0, 0};
        int32_t status = Dispatch(spell, id, object, kind, result ? *result : local);
        if (result) result->status = status;
        return status;
    }

    int32_t Aura(uint32_t id, void* object = nullptr, trinity_object_kind kind = TRINITY_OBJECT_UNKNOWN,
        trinity_spell_hook_result* result = nullptr) const
    {
        if (!hasAura || !aura.dispatch_hook)
            return -1;
        trinity_spell_hook_result local{sizeof(local), 0, 0, 0, 1.0F, 0, 0};
        int32_t status = Dispatch(aura, id, object, kind, result ? *result : local);
        if (result) result->status = status;
        return status;
    }
    void Value(uint32_t id, int64_t value) const { trinity_spell_hook_result r{sizeof(r), 0, 0, value, 1.0F, 0, 0}; Spell(id, nullptr, TRINITY_OBJECT_UNKNOWN, &r); }
    void Float(uint32_t id, float& value, void* object, trinity_object_kind kind) const { trinity_spell_hook_result r{sizeof(r), 0, 0, 0, value, 0, 0}; Spell(id, object, kind, &r); value = r.multiplier; }
    void Damage(uint32_t id, Unit* object, int32& value, int32& flat, float& pct) const { trinity_spell_hook_result r{sizeof(r), 0, 0, value, pct, 0, 0}; Spell(id, object, TRINITY_OBJECT_UNIT, &r); value = static_cast<int32>(r.value); flat = static_cast<int32>(r.flags); pct = r.multiplier; }
    void Resist(uint32_t id, uint32& resist, int32& absorb) const { trinity_spell_hook_result r{sizeof(r), 0, 0, static_cast<int64>(absorb), 1.0F, resist, 0}; Spell(id, nullptr, TRINITY_OBJECT_UNKNOWN, &r); resist = r.effect_index; absorb = static_cast<int32>(r.value); }
    int32 CastTime(int32 value) const { trinity_spell_hook_result r{sizeof(r), 0, 0, value, 1.0F, 0, 0}; Spell(SPELL_SCRIPT_HOOK_CALC_CAST_TIME, nullptr, TRINITY_OBJECT_UNKNOWN, &r); return static_cast<int32>(r.value); }
    bool Bool(uint32_t id) const { trinity_spell_hook_result r{sizeof(r), 0, 0, 1, 1.0F, 0, 0}; Aura(id, nullptr, TRINITY_OBJECT_UNKNOWN, &r); return r.status != 0 || r.value != 0; }
    void Effect(uint32_t id, SpellEffIndex index) const { trinity_spell_hook_result r{sizeof(r), 0, 0, 0, 1.0F, static_cast<uint32_t>(index), 0}; Spell(id, nullptr, TRINITY_OBJECT_UNKNOWN, &r); }
    void AuraCall(uint32_t id, void* object = nullptr, trinity_object_kind kind = TRINITY_OBJECT_UNKNOWN) const { Aura(id, object, kind); }
};

std::mutex Mutex;
std::unordered_map<trinity_host const*, std::shared_ptr<State>> States;
class ModuleSpellScript final : public SpellScript
{
public:
    explicit ModuleSpellScript(std::shared_ptr<State> state) : _state(std::move(state)) { }
    void Register() override
    {
        BeforeCast += SpellCastFn(ModuleSpellScript::HandleBeforeCast);
        OnCast += SpellCastFn(ModuleSpellScript::HandleOnCast);
        AfterCast += SpellCastFn(ModuleSpellScript::HandleAfterCast);
        OnCheckCast += SpellCheckCastFn(ModuleSpellScript::HandleCheckCast);
        OnEffectLaunch += SpellEffectFn(ModuleSpellScript::HandleEffectLaunch, uint8(-1), uint16(-1));
        OnEffectLaunchTarget += SpellEffectFn(ModuleSpellScript::HandleEffectLaunchTarget, uint8(-1), uint16(-1));
        OnEffectHit += SpellEffectFn(ModuleSpellScript::HandleEffectHit, uint8(-1), uint16(-1));
        OnEffectHitTarget += SpellEffectFn(ModuleSpellScript::HandleEffectHitTarget, uint8(-1), uint16(-1));
        OnEffectSuccessfulDispel += SpellEffectFn(ModuleSpellScript::HandleEffectSuccessfulDispel, uint8(-1), uint16(-1));
        BeforeHit += BeforeSpellHitFn(ModuleSpellScript::HandleBeforeHit);
        OnHit += SpellHitFn(ModuleSpellScript::HandleHit);
        AfterHit += SpellHitFn(ModuleSpellScript::HandleAfterHit);
        OnCalcCritChance += SpellOnCalcCritChanceFn(ModuleSpellScript::HandleCalcCritChance);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(ModuleSpellScript::HandleAreaTargetSelect, uint8(-1), uint16(-1));
        OnObjectTargetSelect += SpellObjectTargetSelectFn(ModuleSpellScript::HandleTargetSelect, uint8(-1), uint16(-1));
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(ModuleSpellScript::HandleDestinationSelect, uint8(-1), uint16(-1));
        CalcDamage += SpellCalcDamageFn(ModuleSpellScript::HandleDamage);
        CalcHealing += SpellCalcHealingFn(ModuleSpellScript::HandleHealing);
        OnCalculateResistAbsorb += SpellOnResistAbsorbCalculateFn(ModuleSpellScript::HandleResistAbsorb);
        OnEmpowerStageCompleted += SpellOnEmpowerStageCompletedFn(ModuleSpellScript::HandleEmpowerStageCompleted);
        OnEmpowerCompleted += SpellOnEmpowerCompletedFn(ModuleSpellScript::HandleEmpowerCompleted);
    }
    void OnPrecast() override { _state->Spell(SPELL_SCRIPT_HOOK_ON_PRECAST); }
    int32 CalcCastTime(int32 value) override { return _state->CastTime(value); }
private:
    void HandleBeforeCast() { _state->Spell(SPELL_SCRIPT_HOOK_BEFORE_CAST); }
    void HandleOnCast() { _state->Spell(SPELL_SCRIPT_HOOK_ON_CAST); }
    void HandleAfterCast() { _state->Spell(SPELL_SCRIPT_HOOK_AFTER_CAST); }
    SpellCastResult HandleCheckCast() { trinity_spell_hook_result r{sizeof(r),0,0,0,1.0F,0,0}; _state->Spell(SPELL_SCRIPT_HOOK_CHECK_CAST,nullptr,TRINITY_OBJECT_UNKNOWN,&r); return static_cast<SpellCastResult>(r.status); }
    void HandleEffectLaunch(SpellEffIndex i) { _state->Effect(SPELL_SCRIPT_HOOK_EFFECT_LAUNCH, i); }
    void HandleEffectLaunchTarget(SpellEffIndex i) { _state->Effect(SPELL_SCRIPT_HOOK_EFFECT_LAUNCH_TARGET, i); }
    void HandleEffectHit(SpellEffIndex i) { _state->Effect(SPELL_SCRIPT_HOOK_EFFECT_HIT, i); }
    void HandleEffectHitTarget(SpellEffIndex i) { _state->Effect(SPELL_SCRIPT_HOOK_EFFECT_HIT_TARGET, i); }
    void HandleEffectSuccessfulDispel(SpellEffIndex i) { _state->Effect(SPELL_SCRIPT_HOOK_EFFECT_SUCCESSFUL_DISPEL, i); }
    void HandleBeforeHit(SpellMissInfo value) { _state->Value(SPELL_SCRIPT_HOOK_BEFORE_HIT, static_cast<int64>(value)); }
    void HandleHit() { _state->Spell(SPELL_SCRIPT_HOOK_HIT); }
    void HandleAfterHit() { _state->Spell(SPELL_SCRIPT_HOOK_AFTER_HIT); }
    void HandleCalcCritChance(Unit const* victim, float& chance) { _state->Float(SPELL_SCRIPT_HOOK_CALC_CRIT_CHANCE, chance, const_cast<Unit*>(victim), TRINITY_OBJECT_UNIT); }
    void HandleAreaTargetSelect(std::list<WorldObject*>& targets) { (void)targets; _state->Spell(SPELL_SCRIPT_HOOK_OBJECT_AREA_TARGET_SELECT); }
    void HandleTargetSelect(WorldObject*& target) { _state->Spell(SPELL_SCRIPT_HOOK_OBJECT_TARGET_SELECT, target, TRINITY_OBJECT_UNKNOWN); }
    void HandleDestinationSelect(SpellDestination&) { _state->Spell(SPELL_SCRIPT_HOOK_DESTINATION_TARGET_SELECT); }
    void HandleDamage(SpellEffectInfo const&, Unit* victim, int32& value, int32& flat, float& pct) { _state->Damage(SPELL_SCRIPT_HOOK_CALC_DAMAGE, victim, value, flat, pct); }
    void HandleHealing(SpellEffectInfo const&, Unit* victim, int32& value, int32& flat, float& pct) { _state->Damage(SPELL_SCRIPT_HOOK_CALC_HEALING, victim, value, flat, pct); }
    void HandleResistAbsorb(DamageInfo const&, uint32& resist, int32& absorb) { _state->Resist(SPELL_SCRIPT_HOOK_ON_RESIST_ABSORB_CALCULATION, resist, absorb); }
    void HandleEmpowerStageCompleted(int32 value) { _state->Value(SPELL_SCRIPT_HOOK_EMPOWER_STAGE_COMPLETED, value); }
    void HandleEmpowerCompleted(int32 value) { _state->Value(SPELL_SCRIPT_HOOK_EMPOWER_COMPLETED, value); }
    std::shared_ptr<State> _state;
};

class ModuleAuraScript final : public AuraScript
{
public:
    explicit ModuleAuraScript(std::shared_ptr<State> state) : _state(std::move(state)) { }
    void Register() override
    {
        DoCheckAreaTarget += AuraCheckAreaTargetFn(ModuleAuraScript::HandleCheckAreaTarget);
        OnDispel += AuraDispelFn(ModuleAuraScript::HandleDispel);
        AfterDispel += AuraDispelFn(ModuleAuraScript::HandleAfterDispel);
        OnHeartbeat += AuraHeartbeatFn(ModuleAuraScript::HandleHeartbeat);
        OnEffectApply += AuraEffectApplyFn(ModuleAuraScript::HandleApply, uint8(-1), uint16(-1), AuraEffectHandleModes(0xFF));
        AfterEffectApply += AuraEffectApplyFn(ModuleAuraScript::HandleAfterApply, uint8(-1), uint16(-1), AuraEffectHandleModes(0xFF));
        OnEffectRemove += AuraEffectRemoveFn(ModuleAuraScript::HandleRemove, uint8(-1), uint16(-1), AuraEffectHandleModes(0xFF));
        AfterEffectRemove += AuraEffectRemoveFn(ModuleAuraScript::HandleAfterRemove, uint8(-1), uint16(-1), AuraEffectHandleModes(0xFF));
        OnEffectPeriodic += AuraEffectPeriodicFn(ModuleAuraScript::HandlePeriodic, uint8(-1), uint16(-1));
        OnEffectUpdatePeriodic += AuraEffectUpdatePeriodicFn(ModuleAuraScript::HandleUpdatePeriodic, uint8(-1), uint16(-1));
        DoEffectCalcAmount += AuraEffectCalcAmountFn(ModuleAuraScript::HandleCalcAmount, uint8(-1), uint16(-1));
        DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(ModuleAuraScript::HandleCalcPeriodic, uint8(-1), uint16(-1));
        DoEffectCalcSpellMod += AuraEffectCalcSpellModFn(ModuleAuraScript::HandleCalcSpellMod, uint8(-1), uint16(-1));
        DoEffectCalcCritChance += AuraEffectCalcCritChanceFn(ModuleAuraScript::HandleCalcCritChance, uint8(-1), uint16(-1));
        DoEffectCalcDamageAndHealing += AuraEffectCalcDamageFn(ModuleAuraScript::HandleCalcDamageAndHealing, uint8(-1), uint16(-1));
        OnEffectAbsorb += AuraEffectAbsorbFn(ModuleAuraScript::HandleAbsorb, uint8(-1));
        AfterEffectAbsorb += AuraEffectAbsorbFn(ModuleAuraScript::HandleAfterAbsorb, uint8(-1));
        OnEffectAbsorbHeal += AuraEffectAbsorbHealFn(ModuleAuraScript::HandleAbsorbHeal, uint8(-1));
        AfterEffectAbsorbHeal += AuraEffectAbsorbHealFn(ModuleAuraScript::HandleAfterAbsorbHeal, uint8(-1));
        OnEffectManaShield += AuraEffectManaShieldFn(ModuleAuraScript::HandleManaShield, uint8(-1));
        AfterEffectManaShield += AuraEffectManaShieldFn(ModuleAuraScript::HandleAfterManaShield, uint8(-1));
        OnEffectSplit += AuraEffectAbsorbFn(ModuleAuraScript::HandleSplit, uint8(-1));
        DoCheckProc += AuraCheckProcFn(ModuleAuraScript::HandleCheckProc);
        DoCheckEffectProc += AuraCheckEffectProcFn(ModuleAuraScript::HandleCheckEffectProc, uint8(-1), uint16(-1));
        DoPrepareProc += AuraProcFn(ModuleAuraScript::HandlePrepareProc);
        OnProc += AuraProcFn(ModuleAuraScript::HandleProc);
        AfterProc += AuraProcFn(ModuleAuraScript::HandleAfterProc);
        OnEffectProc += AuraEffectProcFn(ModuleAuraScript::HandleEffectProc, uint8(-1), uint16(-1));
        AfterEffectProc += AuraEffectProcFn(ModuleAuraScript::HandleAfterEffectProc, uint8(-1), uint16(-1));
        OnEnterLeaveCombat += AuraEnterLeaveCombatFn(ModuleAuraScript::HandleEnterLeaveCombat);
    }
private:
    bool HandleCheckAreaTarget(Unit*) { return _state->Bool(AURA_SCRIPT_HOOK_CHECK_AREA_TARGET); }
    void HandleDispel(DispelInfo*) { _state->AuraCall(AURA_SCRIPT_HOOK_DISPEL); }
    void HandleAfterDispel(DispelInfo*) { _state->AuraCall(AURA_SCRIPT_HOOK_AFTER_DISPEL); }
    void HandleHeartbeat() { _state->AuraCall(AURA_SCRIPT_HOOK_ON_HEARTBEAT); }
    void HandleApply(AuraEffect const*, AuraEffectHandleModes mode) { _state->Value(AURA_SCRIPT_HOOK_EFFECT_APPLY, mode); }
    void HandleAfterApply(AuraEffect const*, AuraEffectHandleModes mode) { _state->Value(AURA_SCRIPT_HOOK_EFFECT_AFTER_APPLY, mode); }
    void HandleRemove(AuraEffect const*, AuraEffectHandleModes mode) { _state->Value(AURA_SCRIPT_HOOK_EFFECT_REMOVE, mode); }
    void HandleAfterRemove(AuraEffect const*, AuraEffectHandleModes mode) { _state->Value(AURA_SCRIPT_HOOK_EFFECT_AFTER_REMOVE, mode); }
    void HandlePeriodic(AuraEffect const* effect) { _state->AuraCall(AURA_SCRIPT_HOOK_EFFECT_PERIODIC, const_cast<AuraEffect*>(effect), TRINITY_OBJECT_AURA); }
    void HandleUpdatePeriodic(AuraEffect* effect) { _state->AuraCall(AURA_SCRIPT_HOOK_EFFECT_UPDATE_PERIODIC, effect, TRINITY_OBJECT_AURA); }
    void HandleCalcAmount(AuraEffect const*, SpellEffectValue& amount, bool&) { _state->Float(AURA_SCRIPT_HOOK_EFFECT_CALC_AMOUNT, reinterpret_cast<float&>(amount), nullptr, TRINITY_OBJECT_UNKNOWN); }
    void HandleCalcPeriodic(AuraEffect const*, bool& periodic, int32& timer) { _state->Damage(AURA_SCRIPT_HOOK_EFFECT_CALC_PERIODIC, nullptr, timer, timer, reinterpret_cast<float&>(periodic)); }
    void HandleCalcSpellMod(AuraEffect const*, SpellModifier*&) { _state->AuraCall(AURA_SCRIPT_HOOK_EFFECT_CALC_SPELLMOD); }
    void HandleCalcCritChance(AuraEffect const*, Unit const* unit, float& chance) { _state->Float(AURA_SCRIPT_HOOK_EFFECT_CALC_CRIT_CHANCE, chance, const_cast<Unit*>(unit), TRINITY_OBJECT_UNIT); }
    void HandleCalcDamageAndHealing(AuraEffect const*, Unit* unit, int32& value, int32& flat, float& pct) { _state->Damage(AURA_SCRIPT_HOOK_EFFECT_CALC_DAMAGE_AND_HEALING, unit, value, flat, pct); }
    void HandleAbsorb(AuraEffect*, DamageInfo&, uint32& amount) { int32 value = amount; _state->Damage(AURA_SCRIPT_HOOK_EFFECT_ABSORB, nullptr, value, value, *reinterpret_cast<float*>(&amount)); amount = value; }
    void HandleAfterAbsorb(AuraEffect*, DamageInfo&, uint32& amount) { int32 value = amount; _state->Damage(AURA_SCRIPT_HOOK_EFFECT_AFTER_ABSORB, nullptr, value, value, *reinterpret_cast<float*>(&amount)); amount = value; }
    void HandleAbsorbHeal(AuraEffect*, HealInfo&, uint32& amount) { int32 value = amount; _state->Damage(AURA_SCRIPT_HOOK_EFFECT_ABSORB, nullptr, value, value, *reinterpret_cast<float*>(&amount)); amount = value; }
    void HandleAfterAbsorbHeal(AuraEffect*, HealInfo&, uint32& amount) { int32 value = amount; _state->Damage(AURA_SCRIPT_HOOK_EFFECT_AFTER_ABSORB, nullptr, value, value, *reinterpret_cast<float*>(&amount)); amount = value; }
    bool HandleCheckProc(ProcEventInfo&) { return _state->Bool(AURA_SCRIPT_HOOK_CHECK_PROC); }
    bool HandleCheckEffectProc(AuraEffect const*, ProcEventInfo&) { return _state->Bool(AURA_SCRIPT_HOOK_CHECK_EFFECT_PROC); }
    void HandlePrepareProc(ProcEventInfo&) { _state->AuraCall(AURA_SCRIPT_HOOK_PREPARE_PROC); }
    void HandleProc(ProcEventInfo&) { _state->AuraCall(AURA_SCRIPT_HOOK_PROC); }
    void HandleAfterProc(ProcEventInfo&) { _state->AuraCall(AURA_SCRIPT_HOOK_AFTER_PROC); }
    void HandleEffectProc(AuraEffect*, ProcEventInfo&) { _state->AuraCall(AURA_SCRIPT_HOOK_EFFECT_PROC); }
    void HandleAfterEffectProc(AuraEffect*, ProcEventInfo&) { _state->AuraCall(AURA_SCRIPT_HOOK_EFFECT_AFTER_PROC); }
    void HandleEnterLeaveCombat(bool inCombat) { _state->Value(AURA_SCRIPT_HOOK_ENTER_LEAVE_COMBAT, inCombat); }
    void HandleManaShield(AuraEffect* effect, DamageInfo& info, uint32& amount) { HandleAbsorb(effect, info, amount); }
    void HandleAfterManaShield(AuraEffect* effect, DamageInfo& info, uint32& amount) { HandleAfterAbsorb(effect, info, amount); }
    void HandleSplit(AuraEffect* effect, DamageInfo& info, uint32& amount) { HandleAbsorb(effect, info, amount); }
    std::shared_ptr<State> _state;
};

class ModuleSpellLoader final : public SpellScriptLoader
{
public:
    explicit ModuleSpellLoader(std::shared_ptr<State> state) : SpellScriptLoader("__trinity_module_spell_loader"), _state(std::move(state)) { }
    SpellScript* GetSpellScript() const override { return new ModuleSpellScript(_state); }
    AuraScript* GetAuraScript() const override { return new ModuleAuraScript(_state); }
private:
    std::shared_ptr<State> _state;
};
std::unordered_map<trinity_host const*, std::unique_ptr<ModuleSpellLoader>> Loaders;
}

bool RegisterSpellAuraAdapters(trinity_host const* host, trinity_spell_callbacks const* spell,
    trinity_aura_callbacks const* aura, std::string& error)
{
    if (!host || (!spell && !aura)) { error = "spell/aura callback table missing"; return false; }
    auto state = std::make_shared<State>(); state->host = host;
    if (spell) { if (spell->size < offsetof(trinity_spell_callbacks, dispatch_hook) + sizeof(spell->dispatch_hook) || spell->abi_version != TRINITY_ABI_V1 || spell->host != host || !spell->dispatch_hook) { error = "invalid spell callback table"; return false; } state->spell = *spell; state->hasSpell = true; }
    if (aura) { if (aura->size < offsetof(trinity_aura_callbacks, dispatch_hook) + sizeof(aura->dispatch_hook) || aura->abi_version != TRINITY_ABI_V1 || aura->host != host || !aura->dispatch_hook) { error = "invalid aura callback table"; return false; } state->aura = *aura; state->hasAura = true; }
    std::lock_guard lock(Mutex); if (States.contains(host)) { error = "spell/aura adapter already registered"; return false; }
    Loaders.emplace(host, std::make_unique<ModuleSpellLoader>(state)); States.emplace(host, std::move(state)); return true;
}
void InvalidateSpellAuraAdapters(trinity_host const* host)
{
    std::lock_guard lock(Mutex); auto state = States.find(host); if (state != States.end()) { if (state->second->hasSpell && state->second->spell.clear_hooks) state->second->spell.clear_hooks(state->second->spell.context, host); if (state->second->hasAura && state->second->aura.clear_hooks) state->second->aura.clear_hooks(state->second->aura.context, host); States.erase(state); } Loaders.erase(host);
}
}
