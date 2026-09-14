#pragma once

#include <algorithm>
#include <cstdint>

namespace ai { namespace roster { namespace talents
{
// specNo is stored as the configured premade-path id plus one.  It is not a
// DBC TalentTab index.  Keep the decision independent of game/database types
// so the only runtime authority remains RandomPlayerbotMgr's roster gate.
template <class TalentPaths>
inline bool KeepStoredSpecNo(bool persistentRosterMember, std::uint32_t storedSpecNo,
    TalentPaths const& classPaths)
{
    if (!persistentRosterMember || !storedSpecNo)
        return false;

    return std::find_if(classPaths.begin(), classPaths.end(), [storedSpecNo](auto const& path)
    {
        return path.id >= 0 && static_cast<std::uint32_t>(path.id) + 1 == storedSpecNo;
    }) != classPaths.end();
}
}}} // namespace ai::roster::talents
