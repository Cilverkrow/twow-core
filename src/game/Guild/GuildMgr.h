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

#ifndef _GUILDMGR_H
#define _GUILDMGR_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "World.h"
#include "GuildBank/GuildBank.h"
#include "Utilities/robin_hood.h"
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

class Guild;
class ObjectGuid;
class Petition;
class PetitionSignature;

typedef robin_hood::unordered_map<uint32, Petition*> PetitionMap;
typedef std::list<PetitionSignature*> PetitionSignatureList;
typedef robin_hood::unordered_map<uint32, Guild*> GuildMap;

// twow-repo#485: copy of a petition, taken under m_petitionsMutex. Code on map threads (bots) keeps
// no Petition*: a turn-in on another map thread deletes the petition (HandleTurnInPetitionOpcode ->
// DeletePetition) as soon as the lock of GetPetitionBy*() is released.
struct PetitionSummary
{
    uint32 id = 0;
    ObjectGuid ownerGuid;
    ObjectGuid charterGuid;
    std::string name;
    Team team = TEAM_NONE;
    uint8 signatureCount = 0;
    bool signedByAccount = false;       // the account passed to the getter has signed
    bool signedByPlayer = false;        // the player passed to the getter has signed
};

// twow-repo#485: copy of a guild's identity, taken under m_guildMutex.
struct GuildSummary
{
    uint32 id = 0;
    ObjectGuid leaderGuid;
    std::string name;
};

class GuildMgr
{
    public:
        GuildMgr();
        ~GuildMgr();

        void FixupInfernoBanks();

        void AddGuild(Guild* guild);
        void RemoveGuild(uint32 guildId);

        Guild* GetGuildById(uint32 guildId) const;
        Guild* GetGuildByName(std::string const& name) const;
        Guild* GetGuildByLeader(ObjectGuid const& guid) const;
        std::string GetGuildNameById(uint32 guildId) const;

        void GuildMemberAdded(uint32 guildId, uint32 memberGuid)
        {
            std::lock_guard<std::shared_mutex> guard(m_guid2GuildMutex);
            m_guid2guild[memberGuid] = guildId;
        }
        void GuildMemberRemoved(uint32 memberGuid)
        {
            std::lock_guard<std::shared_mutex> guard(m_guid2GuildMutex);
            m_guid2guild.erase(memberGuid);
        }
        Guild* GetPlayerGuild(uint32 lowguid)
        {
            std::shared_lock<std::shared_mutex> guard(m_guid2GuildMutex);
            std::map<uint32, uint32>::iterator it = m_guid2guild.find(lowguid);
            if (it != m_guid2guild.end())
                return GetGuildById(it->second);
            return nullptr;
        }
        // twow-repo#485 (role fill of roster guilds): the guild id of a character, 0 = none. A copy
        // under the shared lock of m_guid2guild, no Guild* - for bot code on map threads.
        uint32 GetPlayerGuildId(uint32 lowguid)
        {
            std::shared_lock<std::shared_mutex> guard(m_guid2GuildMutex);
            std::map<uint32, uint32>::const_iterator it = m_guid2guild.find(lowguid);
            return it != m_guid2guild.end() ? it->second : 0;
        }

        void CreatePetition(uint32 id, Player* player, const ObjectGuid& charterGuid, std::string& name);
        void DeletePetition(Petition* petition);
        void Update(uint32 diff);
        void SaveGuildBanks();
        Petition* GetPetitionByCharterGuid(const ObjectGuid& charterGuid);
        Petition* GetPetitionById(uint32 id);
        Petition* GetPetitionByOwnerGuid(const ObjectGuid& ownerGuid);
        PetitionSignature* GetSignatureForPlayerGuid(const ObjectGuid& guid);

        // twow-repo#485: copies for map threads, taken under the manager's lock (see PetitionSummary).
        bool GetPetitionSummaryByCharterGuid(ObjectGuid const& charterGuid, PetitionSummary& out, uint32 accountId = 0, ObjectGuid const& player = ObjectGuid());
        bool GetPetitionSummaryBySigner(ObjectGuid const& signerGuid, PetitionSummary& out);
        void CollectPetitionSummaries(std::vector<PetitionSummary>& out);
        void CollectGuildSummaries(std::vector<GuildSummary>& out) const;
        // Renames the petition of this charter and owner under the exclusive lock. Writes the
        // character DB (UPDATE petition SET name, Petition::Rename). Same name checks as
        // MSG_PETITION_RENAME; false when the name or the charter does not qualify.
        bool RenamePetition(ObjectGuid const& charterGuid, ObjectGuid const& ownerGuid, std::string const& newName);
        bool GetPetitionSummaryById(uint32 petitionId, PetitionSummary& out, uint32 accountId = 0, ObjectGuid const& player = ObjectGuid());
        // Signer guids of a petition, copied under the shared lock (signature list packets, guild founding).
        bool GetPetitionSignerGuids(uint32 petitionId, std::vector<ObjectGuid>& out);

        // twow-repo#485 (OB-30 review core#281): every change of a Petition in m_petitionMap runs
        // under the exclusive m_petitionsMutex, so the shared readers never see a signature list
        // while a signature is removed and deleted on another map thread.
        // Signs petitionId for this player. Moves an earlier signature of the player: the old one
        // is removed (DELETE petition_sign, delete) and the new one added (INSERT petition_sign)
        // under ONE lock, so no reader sees the player signed nowhere. The sign checks (full, see
        // GetPetitionSignsRequired; account or player already signed) are repeated under the lock;
        // false when the petition is gone or a check fails, and then nothing changed.
        bool AddPetitionSignature(uint32 petitionId, Player* signer);
        // Removes the player's signature from any petition (DELETE petition_sign, delete).
        void RemovePetitionSignature(ObjectGuid const& signerGuid);

        // twow-repo#485 (OB-10 review core#281): signatures that make a petition full, the one
        // limit for signing: MinPetitionSigns, at most the client's 9 (SMSG_PETITION_SHOW_SIGNATURES
        // shows no more; World.cpp clamps the config to 0-9 as well).
        uint32 GetPetitionSignsRequired() const;

        // twow-repo#485 (owner 04.10.: roster bots keep their class, role and item level in their
        // public guild note). Bots run on map threads, while every guild opcode, the roster packets
        // and the note handlers run on the world thread without a lock on MemberSlot. So the note
        // is only queued here, under the exclusive m_pendingNotesMutex (one entry per member, the
        // latest wins), and GuildMgr::Update writes it on the world thread
        // (MemberSlot::SetPublicNote: no-op when unchanged, else UPDATE guild_member SET pnote,
        // asynchronous). Cut to GUILD_NOTE_MAX_LENGTH. No roster broadcast: clients ask for the
        // roster when the guild window is open.
        void SetMemberPublicNote(uint32 guildId, ObjectGuid const& member, std::string const& note);

        void LoadGuilds();
        void LoadPetitions();

    private:
        void CleanUpPetitions();
        // World thread (Update): writes the queued notes; the queue is swapped out under the lock.
        void ApplyPendingPublicNotes();

        struct PendingPublicNote
        {
            uint32 guildId = 0;
            ObjectGuid member;
            std::string note;
        };
        std::mutex m_pendingNotesMutex;
        std::unordered_map<uint32, PendingPublicNote> m_pendingPublicNotes;     // member guid low -> note

        mutable std::shared_mutex m_guildMutex;
        GuildMap m_GuildMap;
        std::shared_mutex m_guid2GuildMutex;
        std::map<uint32, uint32> m_guid2guild;

        std::shared_mutex m_petitionsMutex;
        PetitionMap m_petitionMap;

		uint32 m_guildBankSaveTimer;

};

class Petition
{
public:
    Petition() : m_id(0) {};
    Petition(uint32 id, ObjectGuid charterGuid, ObjectGuid ownerGuid, std::string& name)
        : m_id(id), m_charterGuid(charterGuid), m_ownerGuid(ownerGuid), m_name(name)
    {
    }

    ~Petition();

    bool LoadFromDB(QueryResult* result);
    void Delete();
    void SaveToDB();

    uint32 GetId() const { return m_id; }
    const ObjectGuid& GetCharterGuid() { return m_charterGuid; }
    const ObjectGuid& GetOwnerGuid() { return m_ownerGuid; }
    std::string const& GetName() { return m_name; }
    Team GetTeam() const { return m_team; }
    void SetTeam(Team team) { m_team = team; }

    uint8 GetSignatureCount() const { return static_cast<uint8>(m_signatures.size()); }
    const PetitionSignatureList& GetSignatureList() { return m_signatures; }

    void BuildSignatureData(WorldPacket &data);

    bool Rename(std::string& newname);

    PetitionSignature* GetSignatureForPlayerGuid(const ObjectGuid& player);
    PetitionSignature* GetSignatureForPlayer(Player* player);
    PetitionSignature* GetSignatureForAccount(uint32 accountId);
    void AddSignature(PetitionSignature* signature);
    void DeleteSignature(PetitionSignature* signature);
    bool AddNewSignature(Player* player);

    bool IsComplete() const { return m_signatures.size() == sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS); }

private:
    uint32 m_id;
    ObjectGuid m_charterGuid;           // item guid for charter the petition belongs to
    ObjectGuid m_ownerGuid;             // guid of the player who owns the charter
    std::string m_name;                 // name of the guild (or team) the petition is for

    Team m_team;                        // Team of the player who created this petition

    PetitionSignatureList m_signatures; // a list of all signatures for this petition
};

class PetitionSignature
{
public:
    PetitionSignature(Petition* petition, ObjectGuid signer, uint32 signerAccount)
        : m_petition(petition), m_playerGuid(signer), m_playerAccount(signerAccount)
    {
    }

    PetitionSignature(Petition* petition, Player* player);

    void SaveToDB();
    void DeleteFromDB();

    const ObjectGuid& GetSignatureGuid() { return m_playerGuid; }
    Petition* GetSignaturePetition() { return m_petition; }
    uint32 GetSignatureAccountId() const { return m_playerAccount; }

private:
    Petition* m_petition;
    ObjectGuid m_playerGuid;
    uint32 m_playerAccount;
};

extern GuildMgr sGuildMgr;

#endif // _GUILDMGR_H
