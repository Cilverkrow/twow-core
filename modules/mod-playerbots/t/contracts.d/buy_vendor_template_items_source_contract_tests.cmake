# twow-repo#333 Berufe P1-z (OB-00 31.10.2026, bug fix: code default 1, 0 = rollback): AiPlayerbot.Buy.VendorTemplateItems lets
# BuyAction "vendor" read the vendor template items (creature_template.vendor_id) in the MANGOSBOT_ZERO build too. The upstream branch
# read only npc_vendor, while the vendor map sends the travel targets to template vendors and the player's buy takes their items
# (M1r run 7: both tool vendors, 1690 and 3367, sell the pick / knife only through their template - three tool trips, no purchase,
# no line). This contract pins the switch (dist key, member, read with unknown = 1, [ConfigEcho] entry sorted), the ZERO branch
# behind the cohort rule, the two ExtendedCost guards kept, the core's template accessor and the tool visit line.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-z VendorTemplateItems: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/strategy/actions/BuyAction.cpp" buy)
read_source("${CORE_SOURCE_DIR}/src/game/Objects/Creature.cpp" creature_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho].
require_text("${dist}" "\nAiPlayerbot.Buy.VendorTemplateItems = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 buyVendorTemplateItems = 1;" "config member, default 1")
require_text("${config_cpp}" "buyVendorTemplateItems = config.GetIntDefault(\"AiPlayerbot.Buy.VendorTemplateItems\", 1);   // twow-repo#333 P1-z vendor templates\n    if (buyVendorTemplateItems > 2)\n        buyVendorTemplateItems = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"BotUpdateTrace\", Value(botUpdateTrace)},\n            {\"Buy.VendorTemplateItems\", Value(buyVendorTemplateItems)},\n            {\"CastWait.Preparing\"" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. BuyAction "vendor": the ZERO branch reads the template list behind the cohort rule; the non-ZERO branch is unchanged.
require_text("${buy}" "\n            vItems = pCreature->GetVendorTemplateItems();\n#else\n" "upstream branch kept, ZERO branch added")
require_text("${buy}" "            if (ai::cohort::Applies(sPlayerbotAIConfig.buyVendorTemplateItems, bot->GetGUIDLow()))\n                vItems = pCreature->GetVendorTemplateItems();\n#endif" "template items in the ZERO build behind the switch")
require_text("${buy}" "#include \"playerbot/CohortPolicy.h\"" "cohort rule include")
# The template items are bought through the same BuyItem call as before (npc_vendor first, then the template).
require_text("${buy}" "didBuy = BuyItem(requester, tItems, vendorguid, proto, bought, usage, units);\n                    if (!didBuy)\n                        didBuy = BuyItem(requester, vItems, vendorguid, proto, bought, usage, units);" "purchase through both lists")

# 3. The ExtendedCost guards stay (no such cost in this client): the template branch and the two ExtendedCost blocks of BuyAction.
string(REGEX MATCHALL "#ifndef MANGOSBOT_ZERO" zero_guards "${buy}")
list(LENGTH zero_guards zero_guard_count)
if(NOT zero_guard_count EQUAL 3)
  message(FATAL_ERROR "#333 P1-z VendorTemplateItems: BuyAction.cpp must keep exactly 3 '#ifndef MANGOSBOT_ZERO' (template branch + 2 ExtendedCost guards), found ${zero_guard_count}")
endif()
require_text("${buy}" "if (tItem->ExtendedCost)" "ExtendedCost lookup guard")
require_text("${buy}" "// ExtendedCost check" "ExtendedCost price guard")

# 4. The core accessor the branch relies on (creature_template.vendor_id).
require_text("${creature_cpp}" "VendorItemData const* Creature::GetVendorTemplateItems() const\n{\n    uint32 vendorId = GetCreatureInfo()->vendor_id;" "core template accessor")

# 5. The tool visit line (ProfessionUse.Trace): one per vendor visit of a bot missing its pick / knife at a vendor that stocks it.
require_text("${buy}" "uint32 const missingTool = sPlayerbotAIConfig.professionUseTrace ? ai::MissingGatherTool(ai) : 0;" "tool visit line only with ProfessionUse.Trace")
require_text("${buy}" "\"[ProfessionUse] stage=tool_buy state=%s bot=%u level=%u vendor=%u item=%u source=%s template_items=%u money=%u\"" "tool visit line format")
require_text("${buy}" "    if (!tool || !source)\n        return;" "no line without a missing tool or at a vendor without it")
require_text("${dist}" "\"[ProfessionUse] stage=tool_buy state=bought|not_bought bot= level= vendor= item= source=npc|template" "dist names the line")
