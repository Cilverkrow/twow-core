/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "GuildMgr.h"
#include "Guild.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Database/DatabaseEnv.h"
#include "Policies/SingletonImp.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"

#define GUILD_BANK_SAVE_INTERVAL 1 * MINUTE * IN_MILLISECONDS

GuildMgr sGuildMgr;

GuildMgr::GuildMgr() : m_guildBankSaveTimer(GUILD_BANK_SAVE_INTERVAL)
{
}

GuildMgr::~GuildMgr()
{
    for (const auto& itr : m_GuildMap)
        delete itr.second;

    CleanUpPetitions();
}

void GuildMgr::CleanUpPetitions()
{
    for (const auto& iter : m_petitionMap)
        delete iter.second; // will clean up signatures too

    m_petitionMap.clear();
}


//have to run this once on maint to cleanup remnant items from gbank bug
void GuildMgr::FixupInfernoBanks()
{
    auto result = std::unique_ptr<QueryResult>(CharacterDatabase.Query("SELECT guildid, guid, isInferno, tab, item_template, count FROM guild_bank WHERE `isInferno` > 1"));

    if (result)
    {
        do {
            auto fields = result->Fetch();
            uint32 guildId = fields[0].GetUInt32();
            uint32 guid = fields[1].GetUInt32();
            uint8 isInferno = fields[2].GetUInt8();
            uint32 tab = fields[3].GetUInt32();
            uint32 itemEntry = fields[4].GetUInt32();
            uint32 count = fields[5].GetUInt32();


            if (auto guild = GetGuildById(guildId))
            {
                if (guild->_Bank)
                {
                    Item* item = Item::CreateItem(itemEntry, count);
                    if (item)
                    {
                        guild->_Bank->DepositInternal(tab, item);
                        CharacterDatabase.PExecute("DELETE FROM `guild_bank` WHERE `guildId` = %u AND `guid` = %u AND `isInferno` = %u", guildId, guid, isInferno);
                    }
                }
            }

        } while (result->NextRow());
    }

    SaveGuildBanks();
}

void GuildMgr::AddGuild(Guild* guild)
{
    std::lock_guard<std::shared_mutex> guard(m_guildMutex);
    m_GuildMap[guild->GetId()] = guild;

    guild->_Bank = new GuildBank{ false };
	guild->_Bank->SetGuild(guild);

    guild->_InfernoBank = new GuildBank{ true };
    guild->_InfernoBank->SetGuild(guild);
}

void GuildMgr::RemoveGuild(uint32 guildId)
{
    std::lock_guard<std::shared_mutex> guard(m_guildMutex);
    m_GuildMap.erase(guildId);
}

Guild* GuildMgr::GetGuildById(uint32 guildId) const
{
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    GuildMap::const_iterator itr = m_GuildMap.find(guildId);
    if (itr != m_GuildMap.end())
        return itr->second;

    return nullptr;
}

Guild* GuildMgr::GetGuildByName(std::string const& name) const
{
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    for (const auto& itr : m_GuildMap)
        if (itr.second->GetName() == name)
            return itr.second;

    return nullptr;
}

Guild* GuildMgr::GetGuildByLeader(ObjectGuid const& guid) const
{
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    for (const auto& itr : m_GuildMap)
        if (itr.second->GetLeaderGuid() == guid)
            return itr.second;

    return nullptr;
}

std::string GuildMgr::GetGuildNameById(uint32 guildId) const
{
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    GuildMap::const_iterator itr = m_GuildMap.find(guildId);
    if (itr != m_GuildMap.end())
        return itr->second->GetName();

    return "";
}

void GuildMgr::LoadGuilds()
{
    //                                                    0             1          2          3           4           5           6
    QueryResult *result = CharacterDatabase.Query("SELECT guild.guildid,guild.name,leaderguid,EmblemStyle,EmblemColor,BorderStyle,BorderColor,"
                          //   7               8    9    10
                          "BackgroundColor,info,motd,createdate FROM guild ORDER BY guildid ASC");

    if (!result)
    {
        return;
    }

    // load guild ranks
    //                                                                0       1   2     3
    QueryResult *guildRanksResult   = CharacterDatabase.Query("SELECT guildid,rid,rname,rights FROM guild_rank ORDER BY guildid ASC, rid ASC");

    // load guild members
    //                                                                0       1                 2    3     4
    QueryResult *guildMembersResult = CharacterDatabase.Query("SELECT guildid,guild_member.guid,`rank`,pnote,offnote,"
                                      //   5                6                 7                 8                9                       10
                                      "characters.name, characters.level, characters.class, characters.zone, characters.logout_time, characters.account "
                                      "FROM guild_member LEFT JOIN characters ON characters.guid = guild_member.guid ORDER BY guildid ASC");


    do
    {
        Guild *newGuild = new Guild;
        if (!newGuild->LoadGuildFromDB(result) ||
                !newGuild->LoadRanksFromDB(guildRanksResult) ||
                !newGuild->LoadMembersFromDB(guildMembersResult) ||
                !newGuild->CheckGuildStructure()
           )
        {
            newGuild->Disband();
            delete newGuild;
            continue;
        }
        newGuild->LoadGuildEventLogFromDB();

        AddGuild(newGuild);
    }
    while (result->NextRow());

    delete result;
    delete guildRanksResult;
    delete guildMembersResult;

    //delete unused LogGuid records in guild_eventlog table
    //you can comment these lines if you don't plan to change CONFIG_UINT32_GUILD_EVENT_LOG_COUNT
    CharacterDatabase.PExecute("DELETE FROM guild_eventlog WHERE LogGuid > '%u'", sWorld.getConfig(CONFIG_UINT32_GUILD_EVENT_LOG_COUNT));

    
}

void GuildMgr::LoadPetitions()
{
    CleanUpPetitions(); // for reload
    //                                                    0          1             2            3
    QueryResult* result = CharacterDatabase.Query("SELECT ownerguid, petitionguid, charterguid, name FROM petition");

    if (!result)
    {
        return;
    }

    // load signatures
    //                                                                0          1             2           3
    QueryResult* petitionSignatures = CharacterDatabase.Query("SELECT ownerguid, petitionguid, playerguid, player_account FROM petition_sign");

    do
    {
        Petition *petition = new Petition;
        if (!petition->LoadFromDB(result))
        {
            petition->Delete();
            delete petition;
            continue;
        }

        m_petitionMap[petition->GetId()] = petition;
    } while (result->NextRow());
    delete result;

    if (petitionSignatures)
    {
        do
        {
            Field *fields = petitionSignatures->Fetch();

            ObjectGuid ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
            uint32 petitionId = fields[1].GetUInt32();
            ObjectGuid playerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[2].GetUInt32());
            uint32 accountId = fields[3].GetUInt32();

            Petition* petition = GetPetitionById(petitionId);

            if (!petition)
            {
                // Signatures for a petition that does not exist. Delete it
                sLog.outErrorDb("Signatures exist for petition %u that does not exist", petitionId);
                CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid = '%u'", petitionId);
                continue;
            }

            if (ownerGuid != petition->GetOwnerGuid())
            {
                sLog.outErrorDb("Signatures exist for petition %u with a different owner, updating", petitionId);
                CharacterDatabase.PExecute("UPDATE petition_sign SET ownerguid = '%u' WHERE petitionguid = '%u'",
                    petition->GetOwnerGuid().GetCounter(), petition->GetId());

                ownerGuid = petition->GetOwnerGuid();
            }

            PetitionSignature* signature = new PetitionSignature(petition, playerGuid, accountId);
            petition->AddSignature(signature);

        } while (petitionSignatures->NextRow());
        delete petitionSignatures;
    }
}

Petition::~Petition()
{
    for (const auto& itr : m_signatures)
        delete itr;

    m_signatures.clear();
}

void GuildMgr::CreatePetition(uint32 id, Player* player, const ObjectGuid& charterGuid, std::string& name)
{
    Petition* petition = new Petition(id, ObjectGuid(charterGuid), ObjectGuid(player->GetObjectGuid()), name);
    petition->SetTeam(player->GetTeam());
    petition->SaveToDB();

    std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);
    m_petitionMap[petition->GetId()] = petition;
}

void GuildMgr::DeletePetition(Petition* petition)
{
    std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);
    m_petitionMap.erase(petition->GetId());

    petition->Delete();
    delete petition;
}

void GuildMgr::Update(uint32 diff)
{
	if (m_guildBankSaveTimer < diff)
		SaveGuildBanks();
	else
		m_guildBankSaveTimer -= diff;

    for (const auto& [key, guild] : m_GuildMap)
    {
        guild->UpdateCaches(diff);
    }

    // twow-repo#485: public notes queued by bots on map threads (SetMemberPublicNote).
    ApplyPendingPublicNotes();
    // twow-repo#485: guild switches queued by bots on map threads (RequestGuildSwitch).
    ApplyPendingGuildSwitches();
}

void GuildMgr::RequestGuildSwitch(ObjectGuid const& member, uint32 fromGuildId, uint32 toGuildId, uint32 petitionId)
{
    // Exactly one target: a guild invitation or a charter.
    if (member.IsEmpty() || !fromGuildId || (toGuildId == 0) == (petitionId == 0))
        return;

    PendingGuildSwitch pending;
    pending.member = member;
    pending.fromGuildId = fromGuildId;
    pending.toGuildId = toGuildId;
    pending.petitionId = petitionId;

    std::lock_guard<std::mutex> guard(m_guildSwitchMutex);
    m_pendingGuildSwitches[member.GetCounter()] = pending;
}

time_t GuildMgr::GetLastGuildSwitch(uint32 memberLowGuid)
{
    std::lock_guard<std::mutex> guard(m_guildSwitchMutex);
    auto it = m_lastGuildSwitch.find(memberLowGuid);
    return it != m_lastGuildSwitch.end() ? it->second : 0;
}

void GuildMgr::ApplyPendingGuildSwitches()
{
    std::unordered_map<uint32, PendingGuildSwitch> pending;
    {
        std::lock_guard<std::mutex> guard(m_guildSwitchMutex);
        if (m_pendingGuildSwitches.empty())
            return;
        pending.swap(m_pendingGuildSwitches);
    }

    // World thread, after the map update (no map thread runs), like the guild opcodes; no lock is
    // held while the guilds change. At most one entry per member and tick.
    for (auto const& item : pending)
    {
        PendingGuildSwitch const& entry = item.second;
        char const* reason = "";
        bool const switched = ApplyGuildSwitch(entry.member, entry.fromGuildId, entry.toGuildId, entry.petitionId, reason);
        if (switched)
        {
            std::lock_guard<std::mutex> guard(m_guildSwitchMutex);
            m_lastGuildSwitch[entry.member.GetCounter()] = time(nullptr);
        }

        // One line per request; requests follow real players' invitations (cooldown per bot).
        sLog.outBasic("[RosterGuild] event=%s bot=%u from=%u to=%u via=%s%s%s", switched ? "poached" : "poach_failed",
            entry.member.GetCounter(), entry.fromGuildId, entry.petitionId ? entry.petitionId : entry.toGuildId,
            entry.petitionId ? "charter" : "invite", switched ? "" : " reason=", reason);
    }
}

bool GuildMgr::ApplyGuildSwitch(ObjectGuid const& member, uint32 fromGuildId, uint32 toGuildId, uint32 petitionId, char const*& reason)
{
    Player* player = sObjectMgr.GetPlayer(member);
    if (!player || !player->IsInWorld())
    {
        reason = "offline";
        return false;
    }

    // An open invitation to toGuildId is dropped when the switch does not happen (as CMSG_GUILD_DECLINE).
    auto dropInvite = [&]()
    {
        if (toGuildId && player->GetGuildIdInvited() == toGuildId)
            player->SetGuildIdInvited(0);
    };

    Guild* from = GetGuildById(fromGuildId);
    if (!from || player->GetGuildId() != fromGuildId)
    {
        dropInvite();
        reason = "not_in_guild";
        return false;
    }

    // CMSG_GUILD_LEAVE: the guild master cannot leave a guild with members.
    if (from->GetLeaderGuid() == member)
    {
        dropInvite();
        reason = "guild_master";
        return false;
    }

    Guild* to = nullptr;
    PetitionSummary petition;
    if (petitionId)
    {
        // CMSG_PETITION_SIGN, checked before leaving: the charter exists, is not full, is not the
        // member's own, neither the member nor its account signed it, no open guild invitation, same
        // team unless two-side guilds. AddPetitionSignature repeats the petition checks under the lock.
        if (!GetPetitionSummaryById(petitionId, petition, player->GetSession()->GetAccountId(), member))
        {
            reason = "no_petition";
            return false;
        }
        if (uint32(petition.signatureCount) >= GetPetitionSignsRequired() || petition.ownerGuid == member ||
            petition.signedByAccount || petition.signedByPlayer || player->GetGuildIdInvited())
        {
            reason = "petition_closed";
            return false;
        }
        if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != petition.team)
        {
            reason = "faction";
            return false;
        }
    }
    else
    {
        // CMSG_GUILD_ACCEPT: still invited to that guild, which still exists.
        to = GetGuildById(toGuildId);
        if (!to || to == from || player->GetGuildIdInvited() != toGuildId)
        {
            dropInvite();
            reason = "invite_gone";
            return false;
        }
        if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != sObjectMgr.GetPlayerTeamByGUID(to->GetLeaderGuid()))
        {
            dropInvite();
            reason = "faction";
            return false;
        }
    }

    // Leave, as CMSG_GUILD_LEAVE (not the guild master, so DelMember keeps the guild).
    std::string const name = player->GetName();
    player->GetSession()->SendGuildCommandResult(GUILD_QUIT_S, from->GetName(), ERR_PLAYER_NO_MORE_IN_GUILD);
    if (from->DelMember(member))
    {
        from->Disband();
        delete from;
    }
    else
    {
        from->LogGuildEvent(GUILD_EVENT_LOG_LEAVE_GUILD, member);
        from->BroadcastEvent(GE_LEFT, member, name.c_str());
    }

    if (to)
    {
        // Join, as CMSG_GUILD_ACCEPT.
        if (to->AddMember(member, to->GetLowestRank()) != GuildAddStatus::OK)
        {
            dropInvite();
            reason = "join_failed";
            return false;
        }
        to->LogGuildEvent(GUILD_EVENT_LOG_JOIN_GUILD, member);
        to->BroadcastEvent(GE_JOINED, member, name.c_str());
        return true;
    }

    // Sign, as CMSG_PETITION_SIGN (exclusive petition lock inside).
    if (!AddPetitionSignature(petitionId, player))
    {
        reason = "sign_failed";
        return false;
    }

    WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8 + 8 + 4));
    data << petition.charterGuid;
    data << member;
    data << uint32(PETITION_SIGN_OK);
    player->GetSession()->SendPacket(&data);
    if (Player* owner = sObjectMgr.GetPlayer(petition.ownerGuid))
        owner->GetSession()->SendPacket(&data);
    return true;
}

bool GuildMgr::GetGuildSummary(uint32 guildId, GuildSummary& out) const
{
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    GuildMap::const_iterator itr = m_GuildMap.find(guildId);
    if (itr == m_GuildMap.end())
        return false;

    out.id = itr->second->GetId();
    out.leaderGuid = itr->second->GetLeaderGuid();
    out.name = itr->second->GetName();
    return true;
}

void GuildMgr::SetMemberPublicNote(uint32 guildId, ObjectGuid const& member, std::string const& note)
{
    if (!guildId || member.IsEmpty())
        return;

    PendingPublicNote pending;
    pending.guildId = guildId;
    pending.member = member;
    pending.note = note.substr(0, GUILD_NOTE_MAX_LENGTH);

    std::lock_guard<std::mutex> guard(m_pendingNotesMutex);
    m_pendingPublicNotes[member.GetCounter()] = std::move(pending);
}

void GuildMgr::ApplyPendingPublicNotes()
{
    std::unordered_map<uint32, PendingPublicNote> pending;
    {
        std::lock_guard<std::mutex> guard(m_pendingNotesMutex);
        if (m_pendingPublicNotes.empty())
            return;
        pending.swap(m_pendingPublicNotes);
    }

    // World thread, like HandleGuildSetPublicNoteOpcode; no lock held while writing.
    for (auto const& item : pending)
    {
        PendingPublicNote const& entry = item.second;
        Guild* guild = GetGuildById(entry.guildId);
        if (!guild)
            continue;

        // The member may have left the guild since the note was queued.
        if (MemberSlot* slot = guild->GetMemberSlot(entry.member))
            slot->SetPublicNote(entry.note);   // UPDATE guild_member SET pnote, only on a change
    }
}

uint32 GuildMgr::GetPetitionSignsRequired() const
{
    // The client shows at most 9 signatures (PetitionsHandler: "Client hard limit at 9 signatures").
    return std::min<uint32>(9, sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS));
}

void GuildMgr::SaveGuildBanks()
{
	uint32 uSaveStartTime = WorldTimer::getMSTime();

	m_guildBankSaveTimer = GUILD_BANK_SAVE_INTERVAL;
    for (const auto& itr : m_GuildMap)
    {
        itr.second->_Bank->SaveToDB();
        itr.second->_InfernoBank->SaveToDB();
    }

	uint32 uSaveDuration = WorldTimer::getMSTimeDiff(uSaveStartTime, WorldTimer::getMSTime());

	//sLog.outInfo("[GuildBank] Save finished in %i minutes %i seconds (%u ms).",
		//uSaveDuration / 60000, (uSaveDuration % 60000) / 1000, uSaveDuration);

}

Petition* GuildMgr::GetPetitionById(uint32 id)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    PetitionMap::iterator iter = m_petitionMap.find(id);
    if (iter != m_petitionMap.end())
        return iter->second;

    return nullptr;
}

Petition* GuildMgr::GetPetitionByCharterGuid(const ObjectGuid& charterGuid)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (petition->GetCharterGuid() == charterGuid)
            return petition;
    }

    return nullptr;
}

Petition* GuildMgr::GetPetitionByOwnerGuid(const ObjectGuid& ownerGuid)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (petition->GetOwnerGuid() == ownerGuid)
            return petition;
    }

    return nullptr;
}

PetitionSignature* GuildMgr::GetSignatureForPlayerGuid(const ObjectGuid& guid)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (PetitionSignature* petitionSignature = petition->GetSignatureForPlayerGuid(guid))
            return petitionSignature;
    }

    return nullptr;
}

// twow-repo#485: callers hold m_petitionsMutex (shared or exclusive).
static void CopyPetitionSummary(Petition* petition, PetitionSummary& out, uint32 accountId, ObjectGuid const& player)
{
    out.id = petition->GetId();
    out.ownerGuid = petition->GetOwnerGuid();
    out.charterGuid = petition->GetCharterGuid();
    out.name = petition->GetName();
    out.team = petition->GetTeam();
    out.signatureCount = petition->GetSignatureCount();
    out.signedByAccount = accountId && petition->GetSignatureForAccount(accountId);
    out.signedByPlayer = !player.IsEmpty() && petition->GetSignatureForPlayerGuid(player);
}

bool GuildMgr::GetPetitionSummaryByCharterGuid(ObjectGuid const& charterGuid, PetitionSummary& out, uint32 accountId, ObjectGuid const& player)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (petition->GetCharterGuid() == charterGuid)
        {
            CopyPetitionSummary(petition, out, accountId, player);
            return true;
        }
    }

    return false;
}

bool GuildMgr::GetPetitionSummaryBySigner(ObjectGuid const& signerGuid, PetitionSummary& out)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (petition->GetSignatureForPlayerGuid(signerGuid))
        {
            CopyPetitionSummary(petition, out, 0, signerGuid);
            return true;
        }
    }

    return false;
}

void GuildMgr::CollectPetitionSummaries(std::vector<PetitionSummary>& out)
{
    out.clear();
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    out.reserve(m_petitionMap.size());
    for (const auto& iter : m_petitionMap)
    {
        PetitionSummary summary;
        CopyPetitionSummary(iter.second, summary, 0, ObjectGuid());
        out.push_back(std::move(summary));
    }
}

void GuildMgr::CollectGuildSummaries(std::vector<GuildSummary>& out) const
{
    out.clear();
    std::shared_lock<std::shared_mutex> guard(m_guildMutex);
    out.reserve(m_GuildMap.size());
    for (const auto& itr : m_GuildMap)
    {
        GuildSummary summary;
        summary.id = itr.second->GetId();
        summary.leaderGuid = itr.second->GetLeaderGuid();
        summary.name = itr.second->GetName();
        out.push_back(std::move(summary));
    }
}

bool GuildMgr::RenamePetition(ObjectGuid const& charterGuid, ObjectGuid const& ownerGuid, std::string const& newName)
{
    // Same checks as HandlePetitionRenameOpcode, before the petition lock (GetGuildByName takes the guild lock).
    if (GetGuildByName(newName) || sObjectMgr.IsReservedName(newName) || !ObjectMgr::IsValidCharterName(newName))
        return false;

    std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (petition->GetCharterGuid() != charterGuid)
            continue;

        if (petition->GetOwnerGuid() != ownerGuid)
            return false;

        std::string name = newName;
        return petition->Rename(name);  // UPDATE petition SET name
    }

    return false;
}

bool GuildMgr::GetPetitionSummaryById(uint32 petitionId, PetitionSummary& out, uint32 accountId, ObjectGuid const& player)
{
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    PetitionMap::iterator iter = m_petitionMap.find(petitionId);
    if (iter == m_petitionMap.end())
        return false;

    CopyPetitionSummary(iter->second, out, accountId, player);
    return true;
}

bool GuildMgr::GetPetitionSignerGuids(uint32 petitionId, std::vector<ObjectGuid>& out)
{
    out.clear();
    std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);
    PetitionMap::iterator iter = m_petitionMap.find(petitionId);
    if (iter == m_petitionMap.end())
        return false;

    PetitionSignatureList const& signatures = iter->second->GetSignatureList();
    out.reserve(signatures.size());
    for (PetitionSignature* signature : signatures)
        out.push_back(signature->GetSignatureGuid());

    return true;
}

bool GuildMgr::AddPetitionSignature(uint32 petitionId, Player* signer)
{
    std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);
    PetitionMap::iterator target = m_petitionMap.find(petitionId);
    if (target == m_petitionMap.end())
        return false;

    Petition* petition = target->second;
    // Same checks as HandlePetitionSignOpcode, again under the lock (another map thread may have
    // signed in between). One limit: MinPetitionSigns, at most the client's 9 (OB-10 review core#281:
    // no second hard-coded 9 next to the config).
    if (petition->GetSignatureCount() >= GetPetitionSignsRequired() || petition->GetSignatureForPlayer(signer))
        return false;

    // Move: before signing, delete any previous signature of this player (same lock).
    for (const auto& iter : m_petitionMap)
    {
        Petition* previous = iter.second;
        if (PetitionSignature* signature = previous->GetSignatureForPlayerGuid(signer->GetObjectGuid()))
        {
            signature->DeleteFromDB();
            previous->DeleteSignature(signature);
            break;
        }
    }

    return petition->AddNewSignature(signer);   // INSERT petition_sign
}

void GuildMgr::RemovePetitionSignature(ObjectGuid const& signerGuid)
{
    std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);
    for (const auto& iter : m_petitionMap)
    {
        Petition* petition = iter.second;
        if (PetitionSignature* signature = petition->GetSignatureForPlayerGuid(signerGuid))
        {
            signature->DeleteFromDB();
            petition->DeleteSignature(signature);
            return;
        }
    }
}

bool Petition::LoadFromDB(QueryResult* result)
{
    if (!result)
    {
        sLog.outErrorDb("[Petitions] Unable to load petitions from DB");
        return false;
    }

    // SELECT ownerguid, petitionguid, name FROM petition
    Field* fields = result->Fetch();

    m_ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
    m_id = fields[1].GetUInt32();
    m_charterGuid = ObjectGuid(HIGHGUID_ITEM, fields[2].GetUInt32());
    m_name = fields[3].GetString();

    m_team = sObjectMgr.GetPlayerTeamByGUID(m_ownerGuid);

    return true;
}

void Petition::Delete()
{
    // Only delete if initialized
    if (m_id)
    {
        CharacterDatabase.BeginTransaction();
        CharacterDatabase.PExecute("DELETE FROM petition WHERE petitionguid = '%u'", m_id);
        CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid = '%u'", m_id);
        CharacterDatabase.CommitTransaction();
    }
}

void Petition::BuildSignatureData(WorldPacket& data)
{
    for (const auto signature : m_signatures)
    {
        data << signature->GetSignatureGuid();
        data << 0;
    }
}

bool Petition::Rename(std::string& newname)
{
    std::string db_newname = newname;
    CharacterDatabase.escape_string(db_newname);
    CharacterDatabase.PExecute("UPDATE petition SET name = '%s' WHERE petitionguid = '%u'",
        db_newname.c_str(), m_id);

    DEBUG_LOG("Petition %u renamed to '%s'", m_id, newname.c_str());

    m_name = newname;

    return true;
}

void Petition::SaveToDB()
{
    std::string escaped_name = m_name;
    CharacterDatabase.escape_string(escaped_name);
    CharacterDatabase.PExecute("INSERT INTO petition (ownerguid, petitionguid, charterguid, name) VALUES ('%u', '%u', '%u', '%s')",
        m_ownerGuid.GetCounter(), m_id, m_charterGuid.GetCounter(), escaped_name.c_str());
}

PetitionSignature* Petition::GetSignatureForPlayer(Player* player)
{
    PetitionSignature* signature = nullptr;
    // Note that in pretty much any case if the player has a signature on
    // this petition, then the account has a signature. Therefore, it will
    // return here
    if (signature = GetSignatureForAccount(player->GetSession()->GetAccountId()))
        return signature;

    if (signature = GetSignatureForPlayerGuid(player->GetObjectGuid()))
        return signature;

    return nullptr;
}

PetitionSignature* Petition::GetSignatureForAccount(uint32 accountId)
{
    for (const auto signature : m_signatures)
    {
        if (signature->GetSignatureAccountId() == accountId)
            return signature;
    }

    return nullptr;
}

PetitionSignature* Petition::GetSignatureForPlayerGuid(const ObjectGuid& guid)
{
    for (const auto signature : m_signatures)
    {
        if (signature->GetSignatureGuid() == guid)
            return signature;
    }

    return nullptr;
}

void Petition::AddSignature(PetitionSignature* signature)
{
    m_signatures.push_back(signature);
}

void Petition::DeleteSignature(PetitionSignature* signature)
{
    m_signatures.remove(signature);
    delete signature;
}

bool Petition::AddNewSignature(Player* player)
{
    if (IsComplete())
        return false;

    PetitionSignature* signature = new PetitionSignature(this, player);
    signature->SaveToDB();
    AddSignature(signature);

    return true;
}

PetitionSignature::PetitionSignature(Petition* petition, Player* player)
    : m_petition(petition), m_playerGuid(player->GetObjectGuid()),
    m_playerAccount(player->GetSession()->GetAccountId())
{

}

void PetitionSignature::SaveToDB()
{
    CharacterDatabase.PExecute("INSERT INTO petition_sign (ownerguid, petitionguid, playerguid, player_account) VALUES ('%u', '%u', '%u','%u')",
        m_petition->GetOwnerGuid().GetCounter(), m_petition->GetId(), m_playerGuid.GetCounter(), m_playerAccount);
}

void PetitionSignature::DeleteFromDB()
{
    CharacterDatabase.BeginTransaction();
    // Only this signature (petition + signer). "ownerguid = signer" kept the row - a later
    // re-sign hit a duplicate key - and wiped the signatures on the signer's own charter.
    CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid = '%u' AND playerguid = '%u'",
        m_petition->GetId(), m_playerGuid.GetCounter());
    CharacterDatabase.CommitTransaction();
}
