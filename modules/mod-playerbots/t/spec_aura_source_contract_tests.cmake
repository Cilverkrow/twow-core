function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AutoLearnSpellAction.cpp" learn)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChangeTalentsAction.cpp" talents)
file(READ "${PB_SOURCE_DIR}/Talentspec.cpp" talentspec)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${PB_MODULE_DIR}/tools/build_premade_specs.py" generator)

# #357 O-12: talent auras for 7.1 / 7.3 (route B), idempotent, visible.
require_text("${learn}" "GrantSpecAuras();" "aura grant on login / level-up / respec")
require_text("${learn}" "ai::spec_aura::ManagedWantedAuras(bot->getClass(), path, bot->GetLevel(), auraTalentsAreReal)" "highest due rank per talent")
require_text("${learn}" "bot->removeSpell(spellId, false, false);" "outgrown ranks removed, no lower rank taught back")
require_text("${learn}" "sSpellTemplate.LookupEntry<SpellEntry>(spellId)" "missing spell_template rows skipped")
require_text("${learn}" "[SpecAura] state=grant" "visible grant")
require_text("${learn}" "[SpecAura] state=missing_spell" "visible missing spell")

# The premade path pays for its auras: selection and crop use the budget.
require_text("${talents}" "uint32 ChangeTalentsAction::PremadeBudget(Player* bot, int specId)" "premade budget")
require_text("${talents}" "if (spec.points >= budget)" "premade entry chosen by budget")
require_text("${talents}" "newSpec.CropTalents(bot, PremadeBudget(bot, specId));" "cropped to the budget")
require_text("${talentspec}" "void TalentSpec::CropTalents(Player* bot, uint32 maxPoints)" "crop to a budget")

# The known-path hold that keeps these points free comes with core#189.

# Keys and generator.
require_text("${config_source}" "\"AiPlayerbot.SpecAura.Enabled\", false" "grant off by default")
require_text("${config_template}" "AiPlayerbot.SpecAura.Enabled = 0" "documented key")
require_text("${generator}" "def reserved_points(cls, name, level, talent_classes=frozenset()):" "per-level reserve in the generator")
require_text("${generator}" "(7, 'shaman tank'): {257}," "no Calming Winds for the tank")
require_text("${generator}" "(7, 'shaman tank'): {259: 3}," "Ancestral Guardian for the tank")

# #367 kit (core#188): Spit and Shadow Dance for 4.3, free, and Shadow Dance kept up.
file(READ "${PB_SOURCE_DIR}/strategy/rogue/TankRogueStrategy.cpp" tank_rogue)
require_text("${learn}" "ai::spec_aura::WantedKit(bot->getClass(), path, bot->GetLevel())" "kit granted with the auras")
require_text("${tank_rogue}" "new NextAction(\"shadow dance\", ACTION_HIGH + 3)" "Shadow Dance kept up in combat")

# twow-repo#409 stage 2: real talents (same IDs) are the talent system's. A listed class
# gets no aura grant, no aura removal (a bought talent stays) and no reserve.
require_text("${learn}" "ai::spec_aura::AuraTalentsAreReal(bot->getClass(), sPlayerbotAIConfig.specAuraTalentClasses)" "talent classes read in the grant")
require_text("${learn}" "ai::spec_aura::ManagedAllAuras(bot->getClass(), auraTalentsAreReal)" "no removal of real talents")
require_text("${talents}" "ai::spec_aura::ManagedReservedPoints(bot->getClass(), ai::spec_aura::PathFor(bot->getClass(), path->name), bot->GetLevel(), auraTalentsAreReal)" "no reserve for real talents")
require_text("${config_source}" "\"AiPlayerbot.SpecAura.TalentClasses\", \"\"" "talent classes empty by default")
require_text("${config_template}" "AiPlayerbot.SpecAura.TalentClasses =" "documented key")
require_text("${generator}" "--talent-classes" "generator reads the new talents from the DBC")
