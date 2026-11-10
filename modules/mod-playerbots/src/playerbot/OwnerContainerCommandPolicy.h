#pragma once

#include <cctype>
#include <string>

// twow-repo#563 part 4: a chat command runs on the bot's thread with the commanding Player* as owner. When the owner stands on another Map object
// (another region thread), actions that READ or WRITE the owner's containers (group, spell book, inventory) race with that thread. These commands are
// answered with a short reply instead of being run for such an owner; every other command (follow, stay, attack, cast, loot, strategies, travel, ...)
// only reads scalars of the owner and runs as before. Pure string logic, no game types.
//
// The words are the chat command words (ChatActionContext "supported" actions) that reach a container read of the owner; audit note
// Y:\tw\evidence\ws-00\563p4\audit-note.md, table B, verified against the actions:
//   rtsc    RTSCAction           HasSpell / learnSpell / removeSpell on the owner (spell book, read and write)
//   summon  SummonAction         SummonUsingNpcs(requester, bot, requester): requester->HasItemCount(6948) (inventory)
//   invite  InviteToGroupAction  group_ops invite: inviter->GetGroup(), ConvertToRaid() (group, read and write)
//   join    JoinGroupAction      owner->GetGroup()
//   lfg     LfgAction            requester->GetGroup()
// (accept / quest: Player::AddQuest takes the owner as quest giver and writes into its quest map - fixed at the call site by passing no giver.)
namespace ai::owner_cmd
{
inline bool IsContainerReadingCommand(std::string const& text)
{
    static char const* const words[] = { "rtsc", "summon", "invite", "join", "lfg" };

    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    size_t end = begin;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])))
        ++end;
    if (end == begin)
        return false;

    std::string first = text.substr(begin, end - begin);
    for (char& c : first)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (char const* word : words)
    {
        if (first == word)
            return true;
    }
    return false;
}
}
