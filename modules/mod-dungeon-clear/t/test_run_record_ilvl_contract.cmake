# G1 dry-r3 25.10.2026 (CLI-GRP, Refs twow-repo#629; owner: "is it the gear?"): the test-run record carried only the factory's
# gear ceiling (gearIlvl/gearQuality). Schema 12 adds what each member actually wears at the run start: comp[].avgIlvl (mean
# item level over the filled equipment slots without shirt and tabard) and comp[].gearSlots. TestTestRunRecord is not in the
# CI fast suite (core-coupled, see tests.cmake), so the record layout is pinned here as source text.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "test-run record ilvl: missing ${description}: ${needle}")
  endif()
endfunction()

read_dc("src/TestRun/DcTestRunRecord.h" header)
read_dc("src/TestRun/DcTestRunRecord.cpp" writer)
read_dc("src/TestRun/DcTestRunJob.cpp" job)

require_text("${header}" "// 12: added the realised gear per member (comp[].avgIlvl, comp[].gearSlots;" "schema 12 (still listed; the current schema may be higher)")
require_text("${header}" "        float avgIlvl = 0.0f;
        std::uint32_t gearSlots = 0;" "per-member fields")
require_text("${writer}" "              << \",\\\"avgIlvl\\\":\" << ilvl
              << \",\\\"gearSlots\\\":\" << c.gearSlots" "written next to the level")
require_text("${writer}" "std::snprintf(ilvl, sizeof(ilvl), \"%.1f\", c.avgIlvl);" "one decimal without the stream's float format")
require_text("${job}" "            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                continue;" "shirt and tabard left out")
require_text("${job}" "    EquippedIlvl(bot, entry.avgIlvl, entry.gearSlots);   // after the gear, ammo and food above" "pool member: after its gear")
require_text("${job}" "        EquippedIlvl(bot, entry.avgIlvl, entry.gearSlots);   // a roster member wears its own gear" "roster member")
