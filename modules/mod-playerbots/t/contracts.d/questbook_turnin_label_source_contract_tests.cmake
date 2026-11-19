# twow-repo#724 (CLI-Deep 18.11.2026, logging only, no switch): the quest book labels a quest that left the log turned_in
# when Player::GetQuestRewardStatus says rewarded OR a reward note for it came in since the last snapshot. A repeatable quest is
# never "rewarded" (RewardQuest sets it to status NONE), so its turn-ins were logged "dropped" (BOT-16 unit 1, 18.11.: Blood
# Shard turn-ins). Two note sources: the bot's own SMSG_QUESTGIVER_QUEST_COMPLETE (RewardQuest with announce = true; the opcode
# passes the inbox filter, the handler feeds the book on the bot's thread, the inbox is drained before Observe in UpdateAI), and
# PlayerbotAI::NoteQuestRewarded right after the bot's RewardQuest calls with announce = false (no packet): RewardNoItem (every
# quest without a reward choice, the Blood Shard quests) and RewardAction. The book classifies through ClassifyLeft with the
# note and clears the notes after the left loop.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "questbook turn-in label: ${description}: missing '${needle}'")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "questbook turn-in label: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAI.cpp" ai_cpp)
read_source("QuestBook.cpp" qb_cpp)
read_source("QuestBook.h" qb_h)
read_source("QuestBookPolicy.h" qb_policy)
read_source("PlayerbotAI.h" ai_h)
read_source("strategy/actions/TalkToQuestGiverAction.cpp" talk_cpp)
read_source("strategy/actions/RewardAction.cpp" reward_cpp)

# The inbox filter lets the reward packet through (otherwise it is never copied to the bot's thread).
require_order("${ai_cpp}" "bool PlayerbotAI::WantsBotOutgoingPacket(uint16 opcode) const"
  "    case SMSG_QUESTGIVER_QUEST_COMPLETE:   // twow-repo#" "reward packet passes the inbox filter")
require_order("${ai_cpp}" "    case SMSG_QUESTGIVER_QUEST_COMPLETE:   // twow-repo#"
  "        return botOutgoingPacketHandlers.HasHandler(opcode);" "reward packet case before the default")

# The handler (bot thread) feeds the book and returns; the quest id is the packet's first field.
require_order("${ai_cpp}" "void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)"
  "	case SMSG_QUESTGIVER_QUEST_COMPLETE:" "handler case inside HandleBotOutgoingPacket")
require_text("${ai_cpp}" "		if (questBook_ && packet.size() >= 4)" "handler guards the book and the packet size")
require_text("${ai_cpp}" "			questBook_->OnRewarded(questId);" "handler feeds the book")

# The packet is read before the snapshot of the same pass.
require_order("${ai_cpp}" "    HandleQueuedBotPackets();" "EnsureQuestBook(!active)->Observe(WorldTimer::getMSTime());"
  "packet inbox drained before the quest book snapshot")

# RewardQuest with announce = false sends no packet: the bot's own calls note the quest directly (book active or shadow).
require_text("${ai_h}" "    void NoteQuestRewarded(uint32 questId);" "NoteQuestRewarded declared")
require_text("${ai_cpp}" "void PlayerbotAI::NoteQuestRewarded(uint32 questId)
{
    if (questBook_)
        questBook_->OnRewarded(questId);
}" "NoteQuestRewarded feeds the book, active or shadow")
require_text("${talk_cpp}" "        bot->RewardQuest(quest, 0, questGiver, false);
        ai->NoteQuestRewarded(quest->GetQuestId());" "RewardNoItem notes the quest right after RewardQuest")
require_text("${reward_cpp}" "                    bot->RewardQuest(pQuest, rewardIdx, questGiver ? ToWorldObject(questGiver) : nullptr, false);
                    ai->NoteQuestRewarded(questID);" "RewardAction notes the quest right after RewardQuest")

# The book: a note only notes (no dirty_: the snapshot timing, and with it the active book's state, stays as before); the
# classification takes the note; the notes are cleared after the left loop. The whole body is pinned, so an added dirty_ fails.
require_text("${qb_h}" "    void OnRewarded(std::uint32_t questId);" "OnRewarded declared")
require_text("${qb_cpp}" "void QuestBook::OnRewarded(std::uint32_t questId)
{
    // twow-repo#724: no dirty_ here (logging only: the snapshot keeps its old timing; the note waits for the next one).
    nRewarded_ = NoteQuestId(rewarded_, nRewarded_, kLog, questId);
}" "OnRewarded only notes the quest (no forced snapshot)")
string(FIND "${qb_cpp}" "void QuestBook::OnRewarded(std::uint32_t questId)" on_rewarded_at)
string(SUBSTRING "${qb_cpp}" ${on_rewarded_at} 300 on_rewarded_body)
string(FIND "${on_rewarded_body}" "}" on_rewarded_end)
string(SUBSTRING "${on_rewarded_body}" 0 ${on_rewarded_end} on_rewarded_body)
string(FIND "${on_rewarded_body}" "dirty_ =" on_rewarded_dirty)
if(NOT on_rewarded_dirty EQUAL -1)
  message(FATAL_ERROR "questbook turn-in label: OnRewarded must not force a snapshot (dirty_ =): logging only")
endif()
require_text("${qb_cpp}" "        LeftReason const reason = ClassifyLeft(bot->GetQuestRewardStatus(left[i]), HasQuestId(rewarded_, nRewarded_, left[i]),
            bot->GetQuestStatus(left[i]) == QUEST_STATUS_FAILED);" "left_log reason via ClassifyLeft with the reward note")
# The clear sits right after the left loop's closing brace (a clear inside the loop would lose a second turn-in of the snapshot).
require_text("${qb_cpp}" "            *w = Watch{};
    }
    nRewarded_ = 0;   // twow-repo#" "notes cleared right after the left loop")
require_order("${qb_cpp}" "LeftReason const reason = ClassifyLeft(" "    }
    nRewarded_ = 0;   // twow-repo#" "notes cleared after the classification")
require_order("${qb_cpp}" "    }
    nRewarded_ = 0;   // twow-repo#" "    // QB4 KPI: complete but not turned in" "notes cleared before the stale loop")

# The rule itself (unit tests in t/questbook_policy_tests.cpp).
require_text("${qb_policy}" "inline LeftReason ClassifyLeft(bool rewardStatus, bool rewardNote, bool failed)" "ClassifyLeft")
require_text("${qb_policy}" "    if (rewardStatus || rewardNote)
        return LeftReason::TurnedIn;" "a reward note labels turned_in")
