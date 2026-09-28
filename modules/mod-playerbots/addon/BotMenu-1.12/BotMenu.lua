-- BotMenu (twow-repo#290): a "Bots" entry in the chat bubble menu.
--
-- A click only writes the command into the chat line; the player presses
-- Enter. The channel that is active there decides who hears it, exactly as
-- when typing: /w <bot> = one bot, /p = the party, /raid = the raid, /s = own
-- bots nearby. No new protocol, no addon messages, nothing the server does
-- not already accept from chat - every command is checked server side.
--
-- Lua 5.0 / 1.12 client: no '#', no '%', handlers read `this`, no SetSize.

BOTMENU_VERSION = "1.6";

-- Categories in menu order (owner 2026-09-27, twow-repo#290). `menu` is the
-- frame from BotMenu.xml; each entry is { label, command }. Commands are the
-- playerbot chat commands of docs/bots/command-inventory.md, checked against
-- the chat triggers by t/botmenu_addon_contract_tests.cmake. Entries ending
-- in a space wait for a shift-clicked link. "Vorsicht" commands (destroy,
-- drop, sendmail, ah, cast, guild ranks, reset) are deliberately left out.
BOTMENU_CATEGORIES = {
	{ label = "Combat", menu = "BotMenuCombat", entries = {
		{ "Follow",              "follow" },
		{ "Stay",             "stay" },
		{ "Attack (target)",    "attack" },
		{ "Tank attack",      "tank attack" },
		{ "Pull (target)",       "pull" },
		{ "Attack marked target",     "attack rti" },
		{ "Max DPS",         "max dps" },
		{ "Flee",        "flee" },
		{ "Guard",            "guard" },
		{ "Free",        "free" },
		{ "Wander",       "wander" },
		{ "Passive on",           "co +passive" },
		{ "Passive off",          "co -passive" },
	} },
	{ label = "Formation", menu = "BotMenuFormation", entries = {
		{ "Near (default)",      "formation near" },
		{ "Melee",            "formation melee" },
		{ "Line",               "formation line" },
		{ "Circle",               "formation circle" },
		{ "Arrow",               "formation arrow" },
		{ "Spear",               "formation spear" },
		{ "Queue",               "formation queue" },
		{ "Chaos",               "formation chaos" },
		{ "Far",                "formation far" },
		{ "Shield",              "formation shield" },
		{ "Ring",   "formation ring" },
		{ "Vanguard",   "formation vanguard" },
		{ "Rearguard", "formation rearguard" },
		{ "Triangle",             "formation triangle" },
		{ "Block",               "formation block" },
		{ "Column",             "formation column" },
		{ "Dragonslayer (raid)", "formation dragonslayer" },
		{ "Giant Killer (raid)", "formation giantkiller" },
		{ "Which formation?",   "formation ?" },
	} },
	-- Role filter: a click remembers the prefix, the next command click puts
	-- it in front ("/p @tank attack") - only the matching bots react.
	{ label = "Role", menu = "BotMenuRole", entries = {
		{ "Tanks only ...",       prefix = "@tank " },
		{ "Healers only ...",      prefix = "@heal " },
		{ "Ranged only ...",   prefix = "@ranged " },
		{ "Melee only ...",    prefix = "@melee " },
		{ "All (filter off)",   prefix = "" },
	} },
	{ label = "Group", menu = "BotMenuGroup", entries = {
		{ "Summon",         "summon" },
		{ "Leave group",         "leave" },
		{ "Give leader",      "give leader" },
		{ "Ready check",  "ready" },
		-- #419: all online characters (paced /who) with an invite button.
		{ "Bot list (all online)...", open = "BotList" },
	} },
	{ label = "Quests", menu = "BotMenuQuest", entries = {
		{ "Quest list",          "quests" },
		{ "Accept quests",     "accept *" },
		{ "Talk to NPC",    "talk" },
		{ "Catch up quest...",  "catchup quest " },
		{ "Choose reward...", "r " },
		{ "Quest objective...", "q " },
	} },
	-- Select the NPC first; the bot must stand next to it.
	{ label = "Vendor & NPC", menu = "BotMenuNpc", entries = {
		{ "Talk to NPC",    "talk" },
		{ "Gossip option 1",            "talk 1" },
		{ "Gossip option 2",            "talk 2" },
		{ "Gossip option 3",            "talk 3" },
		{ "Set hearthstone",         "home" },
		{ "Repair",          "repair" },
		{ "Sell grey items",    "s" },
		{ "Sell...",        "s " },
		{ "Buy...",           "b " },
		{ "Buy back all",        "bb all" },
		{ "Show bank",         "bank ?" },
	} },
	{ label = "Loot", menu = "BotMenuLoot", entries = {
		{ "Loot",          "loot" },
		{ "Useful items only",      "ll normal" },
		{ "Include grey",         "ll gray" },
		{ "Everything",               "ll all" },
		{ "Looting on",           "nc +loot" },
		{ "Looting off",          "nc -loot" },
		{ "Roll: need",     "roll need" },
		{ "Roll: greed",       "roll greed" },
		{ "Roll: pass",     "roll pass" },
		{ "Roll: auto", "roll auto" },
	} },
	{ label = "Professions", menu = "BotMenuProfession", entries = {
		{ "Learn at trainer",  "train" },
		{ "Show trainer",       "trainer" },
		{ "Skills",        "skill" },
		{ "Gathering on",          "nc +gather" },
		{ "Gathering off",         "nc -gather" },
	} },
	-- Item commands: shift-click the item into the line before Enter.
	{ label = "Inventory", menu = "BotMenuInventory", entries = {
		{ "Show inventory",     "c" },
		{ "Count of...",       "c " },
		{ "Equip...",        "e " },
		{ "Unequip...",          "ue " },
		{ "Use...",         "u " },
		{ "Trade (window open)...", "t " },
	} },
	{ label = "Death", menu = "BotMenuDeath", entries = {
		{ "Release spirit",    "release" },
		{ "Spirit healer",    "revive" },
		{ "Self resurrect", "self res" },
	} },
	{ label = "Info", menu = "BotMenuInfo", entries = {
		{ "Stats",              "stats" },
		{ "Where are you?",         "where" },
		{ "Talents",             "talents" },
		{ "Spells",              "spells" },
		{ "Reputation",                 "reputation" },
	} },
};

-- Role prefix chosen in "Rolle", used by the next command click.
local botMenuPrefix = "";

-- The "Bots" button in ChatMenu, remembered so the switch can hide it.
local botMenuButton = nil;

local function BotMenu_Print(text)
	DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffBotMenu:|r "..text);
end

-- UIMenu_AddButton works on the global `this`; call it for another frame.
local function BotMenu_AddButton(menu, text, func, nested)
	local saved = this;
	this = menu;
	UIMenu_AddButton(text, nil, func, nested);
	local button = getglobal(menu:GetName().."Button"..menu.numButtons);
	this = saved;
	return button;
end

-- Called by the menu frames' OnLoad: set up the empty menu and its parent
-- chain, so hovering keeps the whole path open (as EmoteMenu does).
function BotMenu_OnLoadMenu(parentName)
	UIMenu_Initialize();
	this.parentMenu = parentName;
end

-- Button click: write the command into the chat line in the active channel,
-- behind a role prefix chosen before (used once).
function BotMenu_CommandClick()
	local command = this.botCommand;
	if ( command ) then
		ChatFrame_OpenChat(botMenuPrefix..command, DEFAULT_CHAT_FRAME);
		botMenuPrefix = "";
	end
	ChatMenu:Hide();
end

-- Role click: remember the filter for the next command.
function BotMenu_PrefixClick()
	botMenuPrefix = this.botPrefix or "";
	if ( botMenuPrefix == "" ) then
		BotMenu_Print("Role filter off - the next command goes to all bots.");
	else
		BotMenu_Print("The next command goes to "..botMenuPrefix.."only - now pick the command.");
	end
	ChatMenu:Hide();
end

-- Opens the bot list window instead of writing a command. BotList.lua may
-- be missing after an update until the game is restarted (see BotMenu.xml).
local function BotMenu_ShowList()
	if ( BotList_Show ) then
		BotList_Show();
	else
		BotMenu_Print("bot list not loaded - please close and restart the game (a /reload does not load new addon files).");
	end
end

function BotMenu_OpenClick()
	ChatMenu:Hide();
	BotMenu_ShowList();
end

local function BotMenu_Fill()
	for i = 1, table.getn(BOTMENU_CATEGORIES) do
		local category = BOTMENU_CATEGORIES[i];
		local menu = getglobal(category.menu);
		BotMenu_AddButton(BotMenu, category.label, nil, category.menu);
		for j = 1, table.getn(category.entries) do
			local entry = category.entries[j];
			if ( entry.open ) then
				BotMenu_AddButton(menu, entry[1], BotMenu_OpenClick, nil);
			elseif ( entry.prefix ) then
				local button = BotMenu_AddButton(menu, entry[1], BotMenu_PrefixClick, nil);
				if ( button ) then
					button.botPrefix = entry.prefix;
				end
			else
				local button = BotMenu_AddButton(menu, entry[1], BotMenu_CommandClick, nil);
				if ( button ) then
					button.botCommand = entry[2];
				end
			end
		end
	end
end

-- The switch. The button is the last one in ChatMenu (added after all of
-- Blizzard's), so it can be taken out and put back without a gap.
local function BotMenu_ShowEntry(show)
	if ( not botMenuButton ) then
		return;
	end
	local isLast = (ChatMenu.numButtons == botMenuButton:GetID()) or
		(ChatMenu.numButtons == botMenuButton:GetID() - 1);
	if ( not isLast ) then
		BotMenu_Print("Another addon added entries after this one - please /reload.");
		return;
	end
	if ( show ) then
		ChatMenu.numButtons = botMenuButton:GetID();
		botMenuButton:Show();
	else
		ChatMenu.numButtons = botMenuButton:GetID() - 1;
		botMenuButton:Hide();
	end
	ChatMenu:SetHeight((ChatMenu.numButtons * UIMENU_BUTTON_HEIGHT) + (UIMENU_BORDER_HEIGHT * 2));
end

function BotMenu_OnEvent()
	if ( event ~= "VARIABLES_LOADED" ) then
		return;
	end
	if ( not BotMenuDB ) then
		BotMenuDB = { enabled = 1 };
	end

	BotMenu_Fill();
	botMenuButton = BotMenu_AddButton(ChatMenu, "Bots", nil, "BotMenu");
	BotMenu_ShowEntry(BotMenuDB.enabled == 1);

	-- ChatMenu only hides EmoteMenu when it opens; hide ours too, or an
	-- old submenu would reappear with it.
	local original = ChatMenu_OnShow;
	ChatMenu_OnShow = function()
		original();
		BotMenu:Hide();
		for i = 1, table.getn(BOTMENU_CATEGORIES) do
			getglobal(BOTMENU_CATEGORIES[i].menu):Hide();
		end
	end
end

SLASH_BOTMENU1 = "/botmenu";
SlashCmdList["BOTMENU"] = function(msg)
	msg = string.lower(msg or "");
	if ( msg == "on" ) then
		BotMenuDB.enabled = 1;
		BotMenu_ShowEntry(true);
		BotMenu_Print("on - \"Bots\" entry in the chat menu.");
	elseif ( msg == "off" ) then
		BotMenuDB.enabled = 0;
		BotMenu_ShowEntry(false);
		BotMenu_Print("off.");
	elseif ( msg == "list" or msg == "liste" ) then
		BotMenu_ShowList();
	elseif ( string.sub(msg, 1, 4) == "pace" or string.sub(msg, 1, 4) == "takt" ) then
		-- Pace of the bot list scan: seconds between two /who, or auto.
		local value = tonumber(string.sub(msg, 6));
		BotMenuDB.whoInterval = value;
		BotMenu_Print("Bot list pace: "..(value and (value.." s") or "automatic")..".");
	else
		BotMenu_Print("Version "..BOTMENU_VERSION..", "..((BotMenuDB and BotMenuDB.enabled == 1) and "on" or "off")..
			". /botmenu on | off | list | pace <s>|auto. The active chat channel decides which bots get the command: "..
			"/w name = one bot, /p = party, /raid = raid.");
	end
end
