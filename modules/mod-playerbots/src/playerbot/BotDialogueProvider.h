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
};

// Returns the line the bot should say, or "" for silence.
//
// Silence is a normal, expected answer -- not every line of chat deserves a
// reply, and every failure path (no service, timeout, refused, budget spent)
// must also arrive here as "". A provider must not throw; one that does is
// caught and treated as silence, because this runs on a worker thread where an
// escaping exception is std::terminate for the whole worldserver.
typedef std::string (*BotDialogueProvider)(BotDialogueRequest const& request);

// Install the provider. Call once, from module load. Passing nullptr is a
// no-op rather than an unregister.
void RegisterBotDialogueProvider(BotDialogueProvider fn);

// True when a provider is installed. Callable from any thread. Checked on the
// world thread before the structured request is built, so a build with no
// provider does not pay for one.
bool HasBotDialogueProvider();

// Call the provider, or return "" when none is installed. WORKER THREAD ONLY --
// this is allowed to block. Never throws.
std::string RunBotDialogueProvider(BotDialogueRequest const& request);

#endif
