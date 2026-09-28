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
require_text("${learn}" "ai::spec_aura::WantedAuras(bot->getClass(), path, bot->GetLevel())" "highest due rank per talent")
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

# #357 stage 2: classes whose auras are real talents (patched Talent.dbc) are skipped
# by the grant and pay nothing; the links come from the generator's stage-2 mode.
require_text("${config_source}" "\"AiPlayerbot.SpecAura.TalentClasses\", \"\"), specAuraTalentClasses" "talent-backed classes, none by default")
require_text("${config_template}" "AiPlayerbot.SpecAura.TalentClasses =" "documented talent-backed key")
require_text("${learn}" "bool const talentBacked = ai::spec_aura::TalentBacked(bot->getClass(), sPlayerbotAIConfig.specAuraTalentClasses);" "grant checks talent-backed classes")
require_text("${learn}" "path && !talentBacked ? ai::spec_aura::WantedAuras(" "no aura granted for a talent-backed class")
require_text("${learn}" "talentBacked ? std::vector<uint32>() : ai::spec_aura::AllAuras(bot->getClass())" "no talent rank removed for a talent-backed class")
require_text("${talents}" "ai::spec_aura::TalentBacked(bot->getClass(), sPlayerbotAIConfig.specAuraTalentClasses)" "no reserve for a talent-backed class")
require_text("${generator}" "STAGE2_TALENTS = {7: frozenset(range(9001, 9011))}" "stage-2 Enhancement talent ids")
require_text("${generator}" "def read_target(cls, name, link, entries, talent_classes=frozenset()):" "stage-2 link reading")
require_text("${generator}" "def reserved_points(cls, name, level, talent_classes=frozenset()):" "per-level reserve in the generator")
require_text("${generator}" "(7, 'shaman tank'): {257}," "no Calming Winds for the tank")
require_text("${generator}" "(7, 'shaman tank'): {259: 3}," "Ancestral Guardian for the tank")

# #367 kit (core#188): Spit and Shadow Dance for 4.3, free, and Shadow Dance kept up.
file(READ "${PB_SOURCE_DIR}/strategy/rogue/TankRogueStrategy.cpp" tank_rogue)
require_text("${learn}" "ai::spec_aura::WantedKit(bot->getClass(), path, bot->GetLevel())" "kit granted with the auras")
require_text("${tank_rogue}" "new NextAction(\"shadow dance\", ACTION_HIGH + 3)" "Shadow Dance kept up in combat")
