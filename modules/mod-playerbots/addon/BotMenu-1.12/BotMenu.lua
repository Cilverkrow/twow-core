-- BotMenu (twow-repo#290): a "Bots" entry in the chat bubble menu.
--
-- A click only writes the command into the chat line; the player presses
-- Enter. The channel that is active there decides who hears it, exactly as
-- when typing: /w <bot> = one bot, /p = the party, /raid = the raid, /s = own
-- bots nearby. No new protocol, no addon messages, nothing the server does
-- not already accept from chat - every command is checked server side.
--
-- Lua 5.0 / 1.12 client: no '#', no '%', handlers read `this`, no SetSize.

BOTMENU_VERSION = "1.3";

-- Categories in menu order (owner 2026-09-27, twow-repo#290). `menu` is the
-- frame from BotMenu.xml; each entry is { label, command }. Commands are the
-- playerbot chat commands of docs/bots/command-inventory.md, checked against
-- the chat triggers by t/botmenu_addon_contract_tests.cmake. Entries ending
-- in a space wait for a shift-clicked link. "Vorsicht" commands (destroy,
-- drop, sendmail, ah, cast, guild ranks, reset) are deliberately left out.
BOTMENU_CATEGORIES = {
	{ label = "Kampf", menu = "BotMenuCombat", entries = {
		{ "Folgen",              "follow" },
		{ "Bleiben",             "stay" },
		{ "Angreifen (Ziel)",    "attack" },
		{ "Tank greift an",      "tank attack" },
		{ "Ziehen (Ziel)",       "pull" },
		{ "Markiertes Ziel",     "attack rti" },
		{ "Volle Kraft",         "max dps" },
		{ "Zurückziehen",        "flee" },
		{ "Bewachen",            "guard" },
		{ "Frei bewegen",        "free" },
		{ "Umherstreifen",       "wander" },
		{ "Passiv an",           "co +passive" },
		{ "Passiv aus",          "co -passive" },
	} },
	{ label = "Formation", menu = "BotMenuFormation", entries = {
		{ "Nah (Standard)",      "formation near" },
		{ "Nahkampf",            "formation melee" },
		{ "Linie",               "formation line" },
		{ "Kreis",               "formation circle" },
		{ "Pfeil",               "formation arrow" },
		{ "Speer",               "formation spear" },
		{ "Reihe",               "formation queue" },
		{ "Chaos",               "formation chaos" },
		{ "Weit",                "formation far" },
		{ "Schild",              "formation shield" },
		{ "Schutzring (voll)",   "formation ring" },
		{ "Vorhut (Halbring vorn)",   "formation vanguard" },
		{ "Nachhut (Halbring hinten)", "formation rearguard" },
		{ "Dreieck",             "formation triangle" },
		{ "Block",               "formation block" },
		{ "Kolonne",             "formation column" },
		{ "Welche Formation?",   "formation ?" },
	} },
	-- Role filter: a click remembers the prefix, the next command click puts
	-- it in front ("/p @tank attack") - only the matching bots react.
	{ label = "Rolle", menu = "BotMenuRole", entries = {
		{ "Nur Tanks ...",       prefix = "@tank " },
		{ "Nur Heiler ...",      prefix = "@heal " },
		{ "Nur Fernkampf ...",   prefix = "@ranged " },
		{ "Nur Nahkampf ...",    prefix = "@melee " },
		{ "Alle (Filter aus)",   prefix = "" },
	} },
	{ label = "Gruppe", menu = "BotMenuGroup", entries = {
		{ "Herbeirufen",         "summon" },
		{ "Wegschicken",         "leave" },
		{ "Anführer geben",      "give leader" },
		{ "Bereitschaftscheck",  "ready" },
		-- #419: all online characters (paced /who) with an invite button.
		{ "Bot-Liste (alle online)...", open = "BotList" },
	} },
	{ label = "Quests", menu = "BotMenuQuest", entries = {
		{ "Questliste",          "quests" },
		{ "Quests annehmen",     "accept *" },
		{ "Mit NPC sprechen",    "talk" },
		{ "Quest nachholen...",  "catchup quest " },
		{ "Belohnung wählen...", "r " },
		{ "Questziel prüfen...", "q " },
	} },
	-- Select the NPC first; the bot must stand next to it.
	{ label = "Händler & NPC", menu = "BotMenuNpc", entries = {
		{ "Mit NPC sprechen",    "talk" },
		{ "Option 1",            "talk 1" },
		{ "Option 2",            "talk 2" },
		{ "Option 3",            "talk 3" },
		{ "Hier wohnen",         "home" },
		{ "Reparieren",          "repair" },
		{ "Graues verkaufen",    "s" },
		{ "Verkaufen...",        "s " },
		{ "Kaufen...",           "b " },
		{ "Zurückkaufen",        "bb all" },
		{ "Bank zeigen",         "bank ?" },
	} },
	{ label = "Beute", menu = "BotMenuLoot", entries = {
		{ "Aufsammeln",          "loot" },
		{ "Nur Nützliches",      "ll normal" },
		{ "Auch Graues",         "ll gray" },
		{ "Alles",               "ll all" },
		{ "Looten an",           "nc +loot" },
		{ "Looten aus",          "nc -loot" },
		{ "Würfeln: Bedarf",     "roll need" },
		{ "Würfeln: Gier",       "roll greed" },
		{ "Würfeln: Passen",     "roll pass" },
		{ "Würfeln: Automatisch", "roll auto" },
	} },
	{ label = "Berufe", menu = "BotMenuProfession", entries = {
		{ "Beim Lehrer lernen",  "train" },
		{ "Lehrer zeigen",       "trainer" },
		{ "Fertigkeiten",        "skill" },
		{ "Sammeln an",          "nc +gather" },
		{ "Sammeln aus",         "nc -gather" },
	} },
	-- Item commands: shift-click the item into the line before Enter.
	{ label = "Inventar", menu = "BotMenuInventory", entries = {
		{ "Inventar zeigen",     "c" },
		{ "Anzahl von...",       "c " },
		{ "Ausrüsten...",        "e " },
		{ "Ablegen...",          "ue " },
		{ "Benutzen...",         "u " },
		{ "Handeln (Fenster offen)...", "t " },
	} },
	{ label = "Tod", menu = "BotMenuDeath", entries = {
		{ "Geist freilassen",    "release" },
		{ "Beim Geistheiler",    "revive" },
		{ "Selbst wiederbeleben", "self res" },
	} },
	{ label = "Info", menu = "BotMenuInfo", entries = {
		{ "Status",              "stats" },
		{ "Wo bist du?",         "where" },
		{ "Talente",             "talents" },
		{ "Zauber",              "spells" },
		{ "Ruf",                 "reputation" },
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
		BotMenu_Print("Rollenfilter aus - der nächste Befehl gilt für alle Bots.");
	else
		BotMenu_Print("Der nächste Befehl gilt nur für "..botMenuPrefix.."- jetzt den Befehl wählen.");
	end
	ChatMenu:Hide();
end

-- Opens a window instead of writing a command.
function BotMenu_OpenClick()
	ChatMenu:Hide();
	BotList_Show();
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
		BotMenu_Print("Ein anderes Addon hat danach Einträge angelegt - bitte /reload.");
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
		BotMenu_Print("an - Eintrag \"Bots\" im Chat-Menü.");
	elseif ( msg == "off" ) then
		BotMenuDB.enabled = 0;
		BotMenu_ShowEntry(false);
		BotMenu_Print("aus.");
	elseif ( msg == "liste" or msg == "list" ) then
		BotList_Show();
	elseif ( string.sub(msg, 1, 4) == "takt" ) then
		-- Pace of the bot list scan: seconds between two /who, or auto.
		local value = tonumber(string.sub(msg, 6));
		BotMenuDB.whoInterval = value;
		BotMenu_Print("Takt der Bot-Liste: "..(value and (value.." s") or "automatisch")..".");
	else
		BotMenu_Print("Version "..BOTMENU_VERSION..", "..((BotMenuDB and BotMenuDB.enabled == 1) and "an" or "aus")..
			". /botmenu on | off | liste | takt <s>|auto. Der aktive Chat-Kanal bestimmt, welche Bots den Befehl bekommen: "..
			"/w Name = ein Bot, /p = Gruppe, /raid = Schlachtzug.");
	end
end
