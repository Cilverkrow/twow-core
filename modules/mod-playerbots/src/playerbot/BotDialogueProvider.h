#ifndef BOT_DIALOGUE_PROVIDER_H
#define BOT_DIALOGUE_PROVIDER_H

// Modules answer chat on a bot's behalf through here.
//
// PlayerbotLLMInterface::Generate returns "" in this build: the LLM gateway
// client was removed, so every part of the chat path above it -- the gating in
// ChatReplyAction::ChatReplyDo, the prompt assembly, the channel mirroring, the
// async worker, the delayed-packet delivery -- runs and produces nothing. This
// is the seam through which a module can put something there again.
//
// It sits ABOVE Generate rather than inside it, and that is the whole point.
// Generate receives a fully rendered provider request body, built from
// AiPlayerbot.LLMApiJson and shaped for one particular completion API. A
// provider intercepting there would be handed a blob built for somebody else's
// endpoint and would have to reverse-engineer the facts back out of it. Here
// the facts are still facts: who spoke, where, what they said, and to which
// bot.
//
// ---------------------------------------------------------------------------
// Threading -- read this before implementing one
// ---------------------------------------------------------------------------
// RunBotDialogueProvider is called from the std::async worker that the chat
// path already spawns, NEVER from the world or map thread. A provider may
// therefore block: a network round trip here costs a worker thread, not a tick.
//
// What it may NOT do is touch the world. BotDialogueRequest deliberately
// carries nothing but scalars and strings -- no Player*, no PlayerbotAI*, no
// WorldSession*, no ObjectGuid -- so an implementation cannot express the
// use-after-free that a detached thread holding a session pointer already cost
// this tree once. The bot may have logged out by the time the provider is
// called; `botGuidLow` is a key to look up, not a handle to dereference.
//
// The reply is delivered by the existing machinery: the future is handed to
// PlayerbotAI::SendDelayedPacket, which polls it with wait_for(0) on the bot's
// own tick. Nothing added here waits on the world thread.
//
// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
// Same properties AiContextAugment.h documents, for the same reasons:
// registered for the process lifetime, no unregister (a module cannot be
// unloaded from a static build), and registering is safe at any point after
// startup.
//
// One difference from the augmenter list: there is exactly ONE provider, not a
// vector of them. A reply is a single line of text, so two registered providers
// would be two bots' worth of speech from one bot, or a silent race over which
// one wins. A second registration replaces the first and is logged.
//
// NO PROVIDER IS THE NORMAL STATE. With none registered the chat path behaves
// exactly as it does today -- it takes the old Generate route, which answers ""
// and the bot says nothing. Nothing here changes what a build without the
// module does.

#include <cstdint>
#include <string>

// One bot being spoken to, as plain data.
struct BotDialogueRequest
{
    // The bot that was spoken TO, as a low GUID. A provider that needs the bot
    // itself looks it up on its own terms; it is not given a pointer, because
    // by the time this runs the bot may be gone.
    uint32_t botGuidLow = 0;

    // Where it was said, as this tree's own vocabulary rather than any
    // module's: "guild", "world", "general", "trade", "lfg", "local_defense",
    // "world_defense", "guild_recruitment", "say", "whisper", "emote",
    // "text_emote", "yell", "party", "raid".
    //
    // Every ChatChannelSource this tree has is reported, including the ones a
    // given provider will want nothing to do with. Deciding which channels a
    // bot may answer in is policy, and policy belongs to whoever implements the
    // provider -- returning "" for a channel it does not serve is a normal
    // answer. Filtering here would silently make that decision for every
    // future provider.
    std::string channel;

    // True when the speaker is another bot rather than a human. The distinction
    // matters to a provider (a bot answers a person differently than it answers
    // a bot, and bot-to-bot chatter needs a budget), and it is not recoverable
    // from the name.
    bool speakerIsBot = false;

    // The speaker's character name. Public in the channel the message was sent
    // in, and the only identity in this struct.
    std::string speakerName;

    // What was said. Player-typed and therefore hostile input: a provider is
    // responsible for whatever it does with this.
    std::string message;

    // How long the caller is willing to wait, in milliseconds, derived from
    // AiPlayerbot.LLMGenerationTimeout. Advisory: a provider that ignores it
    // costs a worker thread, not a tick. Zero means "no opinion, use your own".
    uint32_t timeoutMs = 0;

    // The speaker's low GUID, and the chat type and language the message
    // arrived with. A provider needs none of them to write a sentence; they are
    // here so that a provider which answers with a COMMAND can have it executed
    // as the speaker, through the same call and with the same arguments a typed
    // command would have carried. See BotDialogueCommand below.
    //
    // A key to look up, again -- never a handle. Zero when the speaker could
    // not be resolved on the world thread, and a command is then impossible:
    // there is nobody to execute it as.
    uint32_t speakerGuidLow = 0;
    uint32_t chatType = 0;   // the ChatMsg the message arrived as
    uint32_t lang = 0;       // the Language it was spoken in
};

// ---------------------------------------------------------------------------
// Commands: the one thing a provider may ask the world to DO
// ---------------------------------------------------------------------------
//
// A provider answers with a line of text and, at most, one value from this
// closed enum. Every value names a chat command this tree ALREADY accepts from
// a player who types it, and the worldserver executes it through
// PlayerbotAI::HandleCommand with the SPEAKER as the commanding player.
//
// That is the whole safety property, and it is meant to be checkable by
// reading:
//
//     A provider may only cause what the speaker could already have caused by
//     typing the command themselves.
//
// Nothing is bypassed to make it work. Both PlayerbotSecurity gates inside
// HandleCommand run unchanged, so a stranger's command is refused exactly where
// a stranger's typed command is refused; and the reachability filters the chat
// managers apply BEFORE calling HandleCommand -- say within 25 yards, yell
// within 300, party and raid only inside the group, guild only inside the guild
// -- are re-applied at the point of execution rather than assumed, because this
// call does not come through those managers. A provider that has been talked
// into anything by a hostile chat message therefore buys an attacker nothing
// they did not already have: the worst it can do is make a bot obey a player
// who was already allowed to command it, using words that player could already
// have typed.
//
// The enum is deliberately small, and it carries no target, no item, no
// destination and no free text: a value here selects a fixed string from
// BotDialogueCommandText and nothing else, so there is no field through which
// an argument could be smuggled. `Attack` is in it because "attack" resolves
// its victim from the SPEAKER'S OWN client selection --
// AttackMyTargetAction::Execute reads requester->GetSelectionGuid() and parses
// no name -- so the target is chosen by the player, not by whoever wrote the
// answer.
enum class BotDialogueCommand : uint32_t
{
    None = 0,       // say something, or nothing, but do nothing
    Follow,         // "follow"
    Stay,           // "stay"
    Flee,           // "flee"
    Attack,         // "attack" -- the SPEAKER's current selection
    EquipUpgrades,  // "do equip upgrades"
};

// The exact text the command is executed as: the words a player types. Returns
// "" for None and for any value this build does not know, which is what makes
// an unmapped value silence rather than a guess.
char const* BotDialogueCommandText(BotDialogueCommand command);

// What a provider answers with.
//
// The reply and the command are INDEPENDENT. A bot may speak without acting,
// act without speaking, or both, and an empty reply with
// BotDialogueCommand::None -- silence, doing nothing -- remains the normal
// outcome it has always been.
struct BotDialogueAnswer
{
    // The line the bot should say, or "" for silence.
    std::string reply;

    // What the speaker asked the bot to do, or None.
    BotDialogueCommand command = BotDialogueCommand::None;
};

// Returns what the bot should say, and at most one thing it should do.
//
// Silence is a normal, expected answer -- not every line of chat deserves a
// reply, and every failure path (no service, timeout, refused, budget spent)
// must also arrive here as an empty reply and BotDialogueCommand::None. A
// provider must not throw; one that does is caught and treated as silence,
// because this runs on a worker thread where an escaping exception is
// std::terminate for the whole worldserver.
typedef BotDialogueAnswer (*BotDialogueProvider)(BotDialogueRequest const& request);

// Install the provider. Call once, from module load. Passing nullptr is a
// no-op rather than an unregister.
void RegisterBotDialogueProvider(BotDialogueProvider fn);

// True when a provider is installed. Callable from any thread. Checked on the
// world thread before the structured request is built, so a build with no
// provider does not pay for one.
bool HasBotDialogueProvider();

// Call the provider, or answer with silence when none is installed. WORKER
// THREAD ONLY -- this is allowed to block. Never throws.
BotDialogueAnswer RunBotDialogueProvider(BotDialogueRequest const& request);

// ---------------------------------------------------------------------------
// Getting a command from the worker to the world thread
// ---------------------------------------------------------------------------
//
// The provider runs on a worker and may not touch a Player; HandleCommand needs
// two of them. So a command crosses as scalars through this queue and is
// executed on the bot's own tick, alongside the delayed packets its reply
// already travels with: the same round trip, one thread later.

struct BotDialogueCommandRequest
{
    uint32_t botGuidLow = 0;
    uint32_t speakerGuidLow = 0;
    uint32_t chatType = 0;
    uint32_t lang = 0;
    BotDialogueCommand command = BotDialogueCommand::None;
};

// The most commands that may be waiting at once, across every bot. Small on
// purpose: a backlog here means bots are being commanded faster than they tick,
// and the useful behaviour then is to drop the oldest rather than to obey a
// minute-old instruction. Over the bound the OLDEST is discarded, and loudly.
uint32_t constexpr kMaxPendingBotDialogueCommands = 64;

// How long a queued command stays worth executing, in seconds. One that waited
// longer is dropped: the player has moved on, and a bot that suddenly follows
// because of something said half a minute ago reads as a bug rather than as
// obedience.
uint32_t constexpr kBotDialogueCommandTtlSeconds = 30;

// Queue one command. ANY THREAD; called from the chat worker. Never throws.
// A request carrying BotDialogueCommand::None, no bot or no speaker is dropped
// here rather than queued.
void QueueBotDialogueCommand(BotDialogueCommandRequest const& request);

// Take the next queued command for one bot, oldest first. WORLD THREAD ONLY.
// Returns false when there is nothing for it; expired entries are discarded
// here rather than returned.
bool TakeBotDialogueCommand(uint32_t botGuidLow, BotDialogueCommandRequest& out);

#endif
