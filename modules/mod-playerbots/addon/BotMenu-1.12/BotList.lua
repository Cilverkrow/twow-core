-- BotList (twow-repo#419): every online character in one list - name, class,
-- level, guild, zone - with filters and an invite button.
--
-- The 1.12 /who answer holds at most 49 entries (the server stops there,
-- MiscHandler.cpp) and a rank-0 account may send one /who per cooldown
-- (WhoList.RequestCooldownSeconds, 30 s by default); earlier ones are
-- dropped without an answer. So the list is collected with several /who
-- queries: 1-60 first, and every answer that is full (49) is split into
-- level halves, then per class, then per race. The pace adapts by itself:
-- queries go out as fast as answers come back (GM accounts); after a query
-- without answer one per 6 s (a 5 s cooldown), after a second one per 31 s.
-- /botmenu pace <s> sets it by hand.
--
-- While a scan runs, the stock who window is detached (FriendsFrame would pop
-- up on every answer) and the player's own /who cancels the scan first.
-- Nothing is sent but /who and the normal invite; the server checks both.
--
-- Lua 5.0 / 1.12 client: no '#', no '%', handlers read `this`/`event`.

BOTLIST_MAX_PER_QUERY = 49;   -- the server stops a /who answer here
BOTLIST_TIMEOUT = 5;          -- no answer after this: the server dropped it
BOTLIST_SHORT_COOLDOWN = 6;   -- first step after a dropped query (server cooldown 5 s)
BOTLIST_COOLDOWN = 31;        -- rank 0 default: one /who per 30 s
BOTLIST_MAX_LEVEL = 60;
BOTLIST_MAX_DROPS = 3;        -- per query, then the scan stops
BOTLIST_ROWS = 15;

-- Names the client uses in /who answers and filters (enUS/enGB, deDE).
BOTLIST_LOCALES = {
	enUS = {
		classes = { "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Shaman", "Mage", "Warlock", "Druid" },
		alliance = { "Human", "Dwarf", "Night Elf", "Gnome", "High Elf" },
		horde = { "Orc", "Undead", "Tauren", "Troll", "Goblin" },
	},
	deDE = {
		classes = { "Krieger", "Paladin", "Jäger", "Schurke", "Priester", "Schamane", "Magier", "Hexenmeister", "Druide" },
		alliance = { "Mensch", "Zwerg", "Nachtelf", "Gnom", "Hochelf" },
		horde = { "Orc", "Untoter", "Tauren", "Troll", "Goblin" },
	},
};
BOTLIST_LOCALES.enGB = BOTLIST_LOCALES.enUS;

-- Collected characters (this session only) and the list filters.
BotListData = { chars = {}, scanId = 0, lastScan = nil };
BotListFilter = { faction = nil, class = nil, minLevel = nil, maxLevel = nil, zone = "" };

local scan = nil;
local offset = 0;
local selected = nil;
local originalSendWho = SendWho;

local function Print(text)
	DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffBotMenu:|r "..text);
end

local function Locale()
	local code = "enUS";
	if ( GetLocale ) then
		code = GetLocale();
	end
	return BOTLIST_LOCALES[code];
end

function BotList_Faction(race)
	for _, locale in pairs(BOTLIST_LOCALES) do
		for i = 1, table.getn(locale.alliance) do
			if ( locale.alliance[i] == race ) then
				return "Alliance";
			end
		end
		for i = 1, table.getn(locale.horde) do
			if ( locale.horde[i] == race ) then
				return "Horde";
			end
		end
	end
	return nil;
end

local function Spec(minLevel, maxLevel, class, race)
	return { minLevel = minLevel, maxLevel = maxLevel, class = class, race = race };
end

function BotList_QueryText(spec)
	local text = spec.minLevel.."-"..spec.maxLevel;
	if ( spec.class ) then
		text = text.." "..(WHO_TAG_CLASS or "c-").."\""..spec.class.."\"";
	end
	if ( spec.race ) then
		text = text.." "..(WHO_TAG_RACE or "r-").."\""..spec.race.."\"";
	end
	return text;
end

-- The first queries: one per class over all levels (a known client
-- language), otherwise one over all levels.
local function FirstQueries()
	local locale = Locale();
	local queries = {};
	if ( not locale ) then
		table.insert(queries, Spec(1, BOTLIST_MAX_LEVEL, nil, nil));
		return queries;
	end
	for i = 1, table.getn(locale.classes) do
		table.insert(queries, Spec(1, BOTLIST_MAX_LEVEL, locale.classes[i], nil));
	end
	return queries;
end

-- A full answer means there may be more: split the query further. Levels
-- are split at the median of the answer (its 49 entries are a sample in the
-- server's own order), so both halves hold about the same number.
local function Split(spec, levels)
	local parts = {};
	local locale = Locale();
	if ( spec.minLevel < spec.maxLevel ) then
		table.sort(levels);
		local middle = levels[math.floor((table.getn(levels) + 1) / 2)] or spec.minLevel;
		if ( middle < spec.minLevel ) then
			middle = spec.minLevel;
		end
		if ( middle >= spec.maxLevel ) then
			middle = spec.maxLevel - 1;
		end
		table.insert(parts, Spec(spec.minLevel, middle, spec.class, spec.race));
		table.insert(parts, Spec(middle + 1, spec.maxLevel, spec.class, spec.race));
	elseif ( locale and not spec.class ) then
		for i = 1, table.getn(locale.classes) do
			table.insert(parts, Spec(spec.minLevel, spec.maxLevel, locale.classes[i], nil));
		end
	elseif ( locale and not spec.race ) then
		for i = 1, table.getn(locale.alliance) do
			table.insert(parts, Spec(spec.minLevel, spec.maxLevel, spec.class, locale.alliance[i]));
		end
		for i = 1, table.getn(locale.horde) do
			table.insert(parts, Spec(spec.minLevel, spec.maxLevel, spec.class, locale.horde[i]));
		end
	end
	return parts;
end

local function Detach()
	if ( FriendsFrame ) then
		FriendsFrame:UnregisterEvent("WHO_LIST_UPDATE");
	end
	if ( SetWhoToUI ) then
		SetWhoToUI(1);
	end
end

local function Attach()
	if ( FriendsFrame ) then
		FriendsFrame:RegisterEvent("WHO_LIST_UPDATE");
	end
	if ( SetWhoToUI ) then
		SetWhoToUI(0);
	end
end

local function Finish(message)
	if ( not scan ) then
		return;
	end
	-- A complete scan forgets characters that were not seen any more.
	if ( scan.complete ) then
		for name, char in pairs(BotListData.chars) do
			if ( char.seen ~= BotListData.scanId ) then
				BotListData.chars[name] = nil;
			end
		end
		BotListData.lastScan = GetTime();
	end
	scan = nil;
	Attach();
	if ( message ) then
		Print(message);
	end
	BotList_Refresh();
end

function BotList_IsScanning()
	return scan ~= nil;
end

function BotList_Progress()
	if ( not scan ) then
		return nil;
	end
	return scan.done, scan.planned, scan.interval;
end

function BotList_StartScan()
	if ( scan ) then
		return;
	end
	BotListData.scanId = BotListData.scanId + 1;
	local interval = 0;
	if ( BotMenuDB and BotMenuDB.whoInterval ) then
		interval = BotMenuDB.whoInterval;
	end
	scan = {
		queue = FirstQueries(),
		planned = 0, done = 0, drops = 0,
		interval = interval, nextAt = 0,
		pending = nil, sentAt = nil, acceptedAt = nil,
		complete = true,
	};
	scan.planned = table.getn(scan.queue);
	Detach();
	BotList_Refresh();
end

function BotList_CancelScan(reason)
	if ( not scan ) then
		return;
	end
	scan.complete = false;
	Finish("Scan cancelled"..(reason and (" ("..reason..")") or "")..".");
end

function BotList_Store(name, guild, level, race, class, zone)
	if ( not name or name == "" ) then
		return;
	end
	local char = BotListData.chars[name];
	if ( not char ) then
		char = {};
		BotListData.chars[name] = char;
	end
	char.name = name;
	char.guild = guild or "";
	char.level = level or 0;
	char.race = race or "";
	char.class = class or "";
	char.zone = zone or "";
	char.faction = BotList_Faction(race);
	char.seen = BotListData.scanId;
end

-- WHO_LIST_UPDATE while a scan runs: take the answer, split full ones.
function BotList_OnWhoUpdate()
	if ( not scan or not scan.pending ) then
		return;
	end
	local count = GetNumWhoResults();
	local levels = {};
	for i = 1, count do
		local name, guild, level, race, class, zone = GetWhoInfo(i);
		BotList_Store(name, guild, level, race, class, zone);
		table.insert(levels, level or 1);
	end
	local spec = scan.pending;
	scan.pending = nil;
	scan.done = scan.done + 1;
	scan.drops = 0;
	scan.acceptedAt = scan.sentAt;
	if ( count >= BOTLIST_MAX_PER_QUERY ) then
		local parts = Split(spec, levels);
		if ( table.getn(parts) == 0 ) then
			scan.complete = false;
		end
		for i = 1, table.getn(parts) do
			table.insert(scan.queue, parts[i]);
		end
		scan.planned = scan.planned + table.getn(parts);
	end
	scan.nextAt = scan.acceptedAt + scan.interval;
	if ( table.getn(scan.queue) == 0 ) then
		if ( scan.complete ) then
			Finish(nil);
		else
			Finish("List incomplete: more than 49 characters with the same level, class and race.");
		end
		return;
	end
	BotList_Refresh();
end

-- OnUpdate: send the next query when the pace allows, detect dropped ones.
function BotList_OnUpdate()
	if ( not scan ) then
		return;
	end
	local now = GetTime();
	if ( scan.pending ) then
		if ( now - scan.sentAt < BOTLIST_TIMEOUT ) then
			return;
		end
		-- No answer: the server's /who cooldown dropped it. Slow down and
		-- send the same query again once the cooldown is over.
		scan.drops = scan.drops + 1;
		if ( scan.drops > BOTLIST_MAX_DROPS ) then
			scan.complete = false;
			Finish("The server does not answer /who right now - scan stopped.");
			return;
		end
		if ( not (BotMenuDB and BotMenuDB.whoInterval) ) then
			-- Step up: a server with a short cooldown (e.g. 5 s) keeps a fast
			-- pace, the default 30 s one ends at 31 s after a second drop.
			if ( scan.interval < BOTLIST_SHORT_COOLDOWN ) then
				scan.interval = BOTLIST_SHORT_COOLDOWN;
			else
				scan.interval = BOTLIST_COOLDOWN;
			end
		end
		table.insert(scan.queue, 1, scan.pending);
		scan.pending = nil;
		scan.nextAt = (scan.acceptedAt or scan.sentAt) + scan.interval;
		BotList_Refresh();
		return;
	end
	if ( now < scan.nextAt or table.getn(scan.queue) == 0 ) then
		return;
	end
	scan.pending = table.remove(scan.queue, 1);
	scan.sentAt = now;
	originalSendWho(BotList_QueryText(scan.pending));
end

-- The player's own /who (slash command or who window) cancels a scan, so
-- the stock window gets its answer as usual.
SendWho = function(text)
	if ( scan ) then
		BotList_CancelScan("own /who");
	end
	originalSendWho(text);
end

local function Matches(char)
	local f = BotListFilter;
	if ( f.faction and char.faction ~= f.faction ) then
		return nil;
	end
	if ( f.class and char.class ~= f.class ) then
		return nil;
	end
	if ( f.minLevel and char.level < f.minLevel ) then
		return nil;
	end
	if ( f.maxLevel and char.level > f.maxLevel ) then
		return nil;
	end
	if ( f.zone and f.zone ~= "" and not string.find(string.lower(char.zone), string.lower(f.zone), 1, true) ) then
		return nil;
	end
	return 1;
end

-- The filtered list, sorted by name.
function BotList_Filtered()
	local list = {};
	for _, char in pairs(BotListData.chars) do
		if ( Matches(char) ) then
			table.insert(list, char);
		end
	end
	table.sort(list, function(a, b) return a.name < b.name; end);
	return list;
end

function BotList_Count()
	local count = 0;
	for _ in pairs(BotListData.chars) do
		count = count + 1;
	end
	return count;
end

function BotList_Invite(name)
	name = name or selected;
	if ( not name ) then
		Print("Click a character in the list first.");
		return;
	end
	InviteByName(name);
end

function BotList_ConvertToRaid()
	ConvertToRaid();
end

function BotList_Select(name)
	selected = name;
	BotList_Refresh();
end

function BotList_Selected()
	return selected;
end

-- Cycle buttons: Fraktion (Alle, Allianz, Horde) and Klasse (Alle, ...).
function BotList_CycleFaction()
	local f = BotListFilter;
	if ( not f.faction ) then
		f.faction = "Alliance";
	elseif ( f.faction == "Alliance" ) then
		f.faction = "Horde";
	else
		f.faction = nil;
	end
	offset = 0;
	BotList_Refresh();
end

function BotList_CycleClass()
	local locale = Locale() or BOTLIST_LOCALES.enUS;
	local classes = locale.classes;
	local f = BotListFilter;
	local nextClass = classes[1];
	for i = 1, table.getn(classes) do
		if ( classes[i] == f.class ) then
			nextClass = classes[i + 1];
		end
	end
	f.class = nextClass;
	offset = 0;
	BotList_Refresh();
end

function BotList_SetLevel(which, text)
	BotListFilter[which] = tonumber(text);
	offset = 0;
	BotList_Refresh();
end

function BotList_SetZone(text)
	BotListFilter.zone = text or "";
	offset = 0;
	BotList_Refresh();
end

function BotList_Scroll(delta)
	offset = math.max(0, offset - delta * 3);
	BotList_Refresh();
end

function BotList_RowClick()
	if ( this.charName ) then
		BotList_Select(this.charName);
	end
end

function BotList_Show()
	if ( BotListFrame ) then
		BotListFrame:Show();
	end
	BotList_Refresh();
end

-- UI update; the list logic above works without the frame (tests).
function BotList_Refresh()
	if ( not getglobal("BotListRow1") ) then
		return;
	end
	local list = BotList_Filtered();
	local total = table.getn(list);
	if ( offset > math.max(0, total - BOTLIST_ROWS) ) then
		offset = math.max(0, total - BOTLIST_ROWS);
	end
	for i = 1, BOTLIST_ROWS do
		local row = getglobal("BotListRow"..i);
		local char = list[offset + i];
		if ( char ) then
			row.charName = char.name;
			getglobal("BotListRow"..i.."Name"):SetText(char.name);
			getglobal("BotListRow"..i.."Class"):SetText(char.class);
			getglobal("BotListRow"..i.."Level"):SetText(char.level);
			getglobal("BotListRow"..i.."Guild"):SetText(char.guild);
			getglobal("BotListRow"..i.."Zone"):SetText(char.zone);
			if ( char.name == selected ) then
				row:LockHighlight();
			else
				row:UnlockHighlight();
			end
			row:Show();
		else
			row.charName = nil;
			row:Hide();
		end
	end

	local status;
	if ( scan ) then
		status = "Queries "..scan.done.."/"..scan.planned;
		if ( scan.interval > 0 ) then
			status = status.." - pace "..scan.interval.." s";
		end
	elseif ( BotListData.lastScan ) then
		status = "Scan done";
	else
		status = "No scan yet";
	end
	BotListFrameStatus:SetText(status.." - "..total.." of "..BotList_Count().." characters");
	BotListFrameFaction:SetText(BotListFilter.faction or "Faction: all");
	BotListFrameClass:SetText(BotListFilter.class or "Class: all");
	if ( scan ) then
		BotListFrameScan:Disable();
		BotListFrameCancel:Enable();
	else
		BotListFrameScan:Enable();
		BotListFrameCancel:Disable();
	end
end
