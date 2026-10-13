/* This file is part of the TrinityCore Project. See AUTHORS for copyright information. */
#ifndef TRINITYCORE_MODULESPELLADAPTERS_H
#define TRINITYCORE_MODULESPELLADAPTERS_H
#include "TrinityModule.h"
#include <string>
namespace Trinity::Modules
{
bool RegisterSpellAuraAdapters(trinity_host const*, trinity_spell_callbacks const*, trinity_aura_callbacks const*, std::string&);
void InvalidateSpellAuraAdapters(trinity_host const*);
}
#endif
