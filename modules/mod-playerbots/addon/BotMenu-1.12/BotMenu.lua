-- BotMenu (twow-repo#290): a "Bots" entry in the chat bubble menu.
--
-- A click only writes the command into the chat line; the player presses
-- Enter. The channel that is active there decides who hears it, exactly as
-- when typing: /w <bot> = one bot, /p = the party, /raid = the raid, /s = own
-- bots nearby. No new protocol, no addon messages, nothing the server does
-- not already accept from chat - every command is checked server side.
--
-- Lua 5.0 / 1.12 client: no '#', no '%', handlers read `this`, no SetSize.

BOTMENU_VERSION = "1.0";

-- Categories in menu order. `menu` is the frame from BotMenu.xml; each entry
-- is { label, command }. Commands are the playerbot chat commands verified in
-- the inventory of twow-repo#290 (and t/botmenu_addon_contract_tests.cmake).
BOTMENU_CATEGORIES = {
	{ label = "Kampf", menu = "BotMenuCombat", entries = {
		{ "Folgen",              "follow" },
		{ "Bleiben",             "stay" },
		{ "Angreifen (Ziel)",    "attack" },
		{ "Zurückziehen",        "flee" },
		{ "Bewachen",            "guard" },
		{ "Frei bewegen",        "free" },
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
		{ "Welche Formation?",   "formation ?" },
	} },
	{ label = "Beute", menu = "BotMenuLoot", entries = {
		{ "Aufsammeln",          "loot" },
		{ "Nur Nützliches",      "ll normal" },
		{ "Auch Graues",         "ll gray" },
		{ "Alles",               "ll all" },
		{ "Looten an",           "nc +loot" },
		{ "Looten aus",          "nc -loot" },
	} },
	{ label = "Berufe", menu = "BotMenuProfession", entries = {
		{ "Beim Lehrer lernen",  "train" },
		{ "Lehrer zeigen",       "trainer" },
		{ "Fertigkeiten",        "skill" },
		{ "Sammeln an",          "nc +gather" },
		{ "Sammeln aus",         "nc -gather" },
	} },
	{ label = "Quests", menu = "BotMenuQuest", entries = {
		{ "Questliste",          "quests" },
		{ "Quests annehmen",     "accept *" },
		{ "Mit NPC sprechen",    "talk" },
		-- Ends with a space: shift-click the quest link into the line.
		{ "Quest nachholen...",  "catchup quest " },
	} },
	{ label = "Gruppe", menu = "BotMenuGroup", entries = {
		{ "Herbeirufen",         "summon" },
		{ "Wegschicken",         "leave" },
	} },
};

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

-- Button click: write the command into the chat line in the active channel.
function BotMenu_CommandClick()
	local command = this.botCommand;
	if ( command ) then
		ChatFrame_OpenChat(command, DEFAULT_CHAT_FRAME);
	end
	ChatMenu:Hide();
end

local function BotMenu_Fill()
	for i = 1, table.getn(BOTMENU_CATEGORIES) do
		local category = BOTMENU_CATEGORIES[i];
		local menu = getglobal(category.menu);
		BotMenu_AddButton(BotMenu, category.label, nil, category.menu);
		for j = 1, table.getn(category.entries) do
			local entry = category.entries[j];
			local button = BotMenu_AddButton(menu, entry[1], BotMenu_CommandClick, nil);
			if ( button ) then
				button.botCommand = entry[2];
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
	else
		BotMenu_Print("Version "..BOTMENU_VERSION..", "..((BotMenuDB and BotMenuDB.enabled == 1) and "an" or "aus")..
			". /botmenu on | off. Der aktive Chat-Kanal bestimmt, welche Bots den Befehl bekommen: "..
			"/w Name = ein Bot, /p = Gruppe, /raid = Schlachtzug.");
	end
end
