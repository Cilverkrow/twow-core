
#include "playerbot/playerbot.h"
#include "PartyMemberWithoutAuraValue.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/GroupBuffPolicy.h"
#include "playerbot/BuffTargetPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
using namespace ai;

extern std::vector<std::string> split(const std::string &s, char delim);

class PlayerWithoutAuraPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PlayerWithoutAuraPredicate(PlayerbotAI* ai, std::string aura) :
        PlayerbotAIAware(ai), FindPlayerPredicate(), auras(split(aura, ',')) {}

public:
    virtual bool Check(Unit* unit) override
    {
        Pet* pet = dynamic_cast<Pet*>(unit);
        if (pet && (pet->getPetType() == MINI_PET || pet->getPetType() == SUMMON_PET))
            return false;

        if (!sServerFacade.IsAlive(unit)) return false;

        for (std::vector<std::string>::iterator i = auras.begin(); i != auras.end(); ++i)
        {
#ifdef MANGOSBOT_ZERO
            // Ignore mana buff spells for non mana users
            if (*i == "arcane intellect" || *i == "arcane brilliance" || *i == "divine spirit" || *i == "prayer of spirit")
            {
                if (unit->GetPowerType() != POWER_MANA)
                {
                    return false;
                }
            }
#endif

            if (ai->HasAura(*i, unit))
                return false;
        }

        return true;
    }

private:
    std::vector<std::string> auras;
};

Unit* FriendlyUnitWithoutAuraValue::Calculate()
{
    bool ignoreTank = false;
    std::string auras = qualifier;
    
    size_t paramPos = qualifier.find("-");
    if (paramPos != std::string::npos)
    {
        auras = qualifier.substr(0, paramPos);
        ignoreTank = std::stoi(qualifier.substr(paramPos + 1, paramPos + 2));
    }

    // #420: bots grouped with a real player buff their group only.
    bool const ignoreOutOfGroup = !ai::group_buff::MayBuffOutOfGroup(ai->IsInGroupWithRealPlayer());

    PlayerWithoutAuraPredicate predicate(ai, auras);

    // twow-repo#588 (AiPlayerbot.Buff.RealPlayerStyle): like a real player - self and group first, then the own guild, and a
    // stranger (bot or player) only once per StrangerCooldownSeconds per bot. Off: every friendly player in sight, as before.
    if (sPlayerbotAIConfig.buffRealPlayerStyle && !ignoreOutOfGroup)
    {
        if (Unit* member = FindPartyMember(predicate, true, ignoreTank))
        {
            ai::buff_target::Count(ai::buff_target::Category::Group);
            return member;
        }
        // The guards of the old out-of-group path: no outside buffs while that activity is off or while mounted.
        if (!ai->AllowActivity(OUT_OF_PARTY_ACTIVITY) || AI_VALUE2(uint32, "current mount speed", "self target"))
            return nullptr;

        Player* stranger = nullptr;
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest friendly players"))
        {
            Player* player = dynamic_cast<Player*>(ai->GetUnit(guid));
            if (!player || player == bot || !predicate.Check(player))
                continue;
            if (ai::buff_target::SameGuild(bot->GetGuildId(), player->GetGuildId()))
            {
                ai::buff_target::Count(ai::buff_target::Category::Guild);
                return player;
            }
            if (!stranger)
                stranger = player;
        }
        if (!stranger)
            return nullptr;

        uint32 const now = uint32(time(nullptr));
        if (!ai::buff_target::StrangerAllowed(now, uint32(std::max(0, AI_VALUE2(int, "manual int", "buff stranger next")))))
        {
            ai::buff_target::Count(ai::buff_target::Category::StrangerOnCooldown);
            return nullptr;
        }
        SET_AI_VALUE2(int, "manual int", "buff stranger next",
            int(ai::buff_target::NextStrangerAllowed(now, sPlayerbotAIConfig.buffStrangerCooldownSeconds)));
        ai::buff_target::Count(GetBotAI(stranger) ? ai::buff_target::Category::StrangerBot : ai::buff_target::Category::StrangerPlayer);
        return stranger;
    }

    return FindPartyMember(predicate, ignoreOutOfGroup, ignoreTank);
}

Unit* PartyMemberWithoutAuraValue::Calculate()
{
    bool ignoreTank = false;
    std::string auras = qualifier;

    size_t paramPos = qualifier.find("-");
    if (paramPos != std::string::npos)
    {
        auras = qualifier.substr(0, paramPos);
        ignoreTank = std::stoi(qualifier.substr(paramPos + 1, paramPos + 2));
    }

	PlayerWithoutAuraPredicate predicate(ai, auras);
    return FindPartyMember(predicate, true, ignoreTank);
}

class PlayerWithoutMyAuraPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PlayerWithoutMyAuraPredicate(PlayerbotAI* ai, std::string aura) :
        PlayerbotAIAware(ai), FindPlayerPredicate(), auras(split(aura, ',')) {}

public:
    virtual bool Check(Unit* unit) override
    {
        Pet* pet = dynamic_cast<Pet*>(unit);
        if (pet && (pet->getPetType() == MINI_PET || pet->getPetType() == SUMMON_PET))
            return false;

        if (!sServerFacade.IsAlive(unit)) return false;
        if (sServerFacade.GetDistance2d(unit, ai->GetBot()) > 30.0f) return false;

        for (std::vector<std::string>::iterator i = auras.begin(); i != auras.end(); ++i)
        {
            if (ai->HasMyAura(*i, unit))
                return false;
        }

        return true;
    }

private:
    std::vector<std::string> auras;
};

Unit* PartyMemberWithoutMyAuraValue::Calculate()
{
    bool ignoreTank = false;
    std::string auras = qualifier;

    size_t paramPos = qualifier.find("-");
    if (paramPos != std::string::npos)
    {
        auras = qualifier.substr(0, paramPos);
        ignoreTank = std::stoi(qualifier.substr(paramPos + 1, paramPos + 2));
    }

    PlayerWithoutMyAuraPredicate predicate(ai, auras);
    return FindPartyMember(predicate, false, ignoreTank);
}

class TankWithoutAuraPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    TankWithoutAuraPredicate(PlayerbotAI* ai, std::string aura) :
        PlayerbotAIAware(ai), FindPlayerPredicate(), auras(split(aura, ',')) {}

public:
    virtual bool Check(Unit* unit) override
    {
        if (unit && unit->IsPlayer() && sServerFacade.IsAlive(unit))
        {
            if (ai->IsTank((Player*)unit))
            {
                bool missingAura = false;
                for (std::vector<std::string>::iterator i = auras.begin(); i != auras.end(); ++i)
                {
                    if (!ai->HasAura(*i, unit))
                    {
                        missingAura = true;
                        break;
                    }
                }

                return missingAura;
            }
        }

        return false;
    }

private:
    std::vector<std::string> auras;
};

Unit* PartyTankWithoutAuraValue::Calculate()
{
    TankWithoutAuraPredicate predicate(ai, qualifier);
    return FindPartyMember(predicate);
}
