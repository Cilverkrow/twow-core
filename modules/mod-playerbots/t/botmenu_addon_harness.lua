-- twow-repo#290: runs BotMenu.lua against stand-ins for the 1.12 FrameXML it
-- uses (UIMenu.lua, ChatMenu, ChatFrame_OpenChat). The stand-ins follow the
-- 1.12.1 source: UIMenu_AddButton(text, shortcut, func, nested) works on the
-- global `this`, buttons are <menu>Button<id>, a click calls func() with
-- `this` = the button. Usage: lua botmenu_addon_harness.lua <addon dir>
local dir = arg[1] or "."

local frames = {}
local function fail(message) io.stderr:write("FAILED: "..message.."\n") os.exit(1) end
local function require_true(condition, message) if not condition then fail(message) end end

local function newButton(menu, id)
    local b = { id = id, shown = false, text = nil }
    function b:GetID() return self.id end
    function b:SetText(t) self.text = t end
    function b:Show() self.shown = true end
    function b:Hide() self.shown = false end
    function b:SetWidth() end
    function b:SetHeight() end
    function b:GetName() return menu:GetName().."Button"..self.id end
    frames[b:GetName()] = b
    frames[b:GetName().."ShortcutText"] = { SetText = function() end, Show = function() end, Hide = function() end }
    return b
end

local function newMenu(name)
    local m = { name = name, shown = false, height = 0 }
    function m:GetName() return self.name end
    function m:Show() self.shown = true end
    function m:Hide() self.shown = false end
    function m:IsVisible() return self.shown end
    function m:SetWidth() end
    function m:SetHeight(h) self.height = h end
    for id = 1, 32 do newButton(m, id) end
    frames[name] = m
    _G[name] = m
    return m
end

function getglobal(name) return frames[name] or _G[name] end
UIMENU_NUMBUTTONS = 32; UIMENU_BUTTON_HEIGHT = 16; UIMENU_BUTTON_WIDTH = 104; UIMENU_BORDER_HEIGHT = 12; UIMENU_BORDER_WIDTH = 12
function UIMenu_Initialize() this.numButtons = 0; this.subMenu = "" end
function UIMenu_AddButton(text, shortcut, func, nested)
    local id = this.numButtons + 1
    if id > UIMENU_NUMBUTTONS then return end
    this.numButtons = id
    local button = getglobal(this:GetName().."Button"..id)
    if text then button:SetText(text) end
    button.func = func; button.nested = nested; button:Show()
    this:SetHeight(id * UIMENU_BUTTON_HEIGHT + UIMENU_BORDER_HEIGHT * 2)
end

local written = {}
DEFAULT_CHAT_FRAME = { AddMessage = function(_, text) end, editBox = {} }
function ChatFrame_OpenChat(text, chatFrame) table.insert(written, text) end

-- Stock ChatMenu with Blizzard's ten entries, as ChatMenu_OnLoad builds it.
local chatMenu = newMenu("ChatMenu")
this = chatMenu; UIMenu_Initialize()
for i = 1, 10 do UIMenu_AddButton("stock"..i, nil, function() end, nil) end
local chatMenuOnShowCalls = 0
function ChatMenu_OnShow() chatMenuOnShowCalls = chatMenuOnShowCalls + 1 end

-- #419: a stand-in server for /who, as MiscHandler.cpp answers it: one open
-- request, a per-account cooldown (dropped silently), at most 49 entries in
-- a fixed order; the answer arrives as WHO_LIST_UPDATE.
local now = 0
function GetTime() return now end
function GetLocale() return "enUS" end
WHO_TAG_CLASS = "c-"; WHO_TAG_RACE = "r-"
local server = { players = {}, cooldown = 30, lastAccepted = -1000, answerAt = nil, pending = nil, results = {}, queries = 0 }
local whoToUI = 0
function SetWhoToUI(value) whoToUI = value end
local friendsPopups = 0
FriendsFrame = { events = { WHO_LIST_UPDATE = true } }
function FriendsFrame:RegisterEvent(name) self.events[name] = true end
function FriendsFrame:UnregisterEvent(name) self.events[name] = nil end
local invited = {}
function InviteByName(name) table.insert(invited, name) end
local converted = 0
function ConvertToRaid() converted = converted + 1 end
function SendWho(text)
    server.queries = server.queries + 1
    if server.answerAt or now - server.lastAccepted < server.cooldown then
        return
    end
    server.lastAccepted = now
    local lo, hi = string.match(text, "^(%d+)%-(%d+)")
    local class = string.match(text, 'c%-"([^"]+)"')
    local race = string.match(text, 'r%-"([^"]+)"')
    local found = {}
    for _, p in ipairs(server.players) do
        if p.level >= tonumber(lo) and p.level <= tonumber(hi)
                and (not class or p.class == class) and (not race or p.race == race) then
            table.insert(found, p)
            if table.getn(found) == 49 then break end
        end
    end
    server.pending = found
    server.answerAt = now + 0.3
end
function GetNumWhoResults() return table.getn(server.results), table.getn(server.players) end
function GetWhoInfo(i)
    local p = server.results[i]
    return p.name, p.guild, p.level, p.race, p.class, p.zone
end
local function deliverWho()
    if server.answerAt and now >= server.answerAt then
        server.results = server.pending
        server.answerAt = nil
        if FriendsFrame.events.WHO_LIST_UPDATE then friendsPopups = friendsPopups + 1 end
        event = "WHO_LIST_UPDATE"
        BotList_OnWhoUpdate()
    end
end

-- Load the addon the way the client does: Lua files, then the XML OnLoads.
SlashCmdList = {}
local chunk, err = loadfile(dir.."/BotMenu.lua")
require_true(chunk, "BotMenu.lua does not parse: "..tostring(err))
chunk()
chunk, err = loadfile(dir.."/BotList.lua")
require_true(chunk, "BotList.lua does not parse: "..tostring(err))
chunk()
BotListFrame = { shown = false }
function BotListFrame:Show() self.shown = true end
function BotListFrame:Hide() self.shown = false end

local xml = io.open(dir.."/BotMenu.xml"):read("*a")
for name, parent in string.gmatch(xml, '<Frame name="(BotMenu%w*)" inherits="UIMenuTemplate" hidden="true" parent="(%w+)"') do
    local m = newMenu(name)
    this = m
    BotMenu_OnLoadMenu(parent == "ChatMenu" and "ChatMenu" or "BotMenu")
    require_true(m.parentMenu ~= nil, name.." has no parentMenu")
end

-- VARIABLES_LOADED with no saved variables yet.
event = "VARIABLES_LOADED"
BotMenu_OnEvent()
require_true(BotMenuDB and BotMenuDB.enabled == 1, "the menu is on by default")
require_true(ChatMenu.numButtons == 11, "exactly one entry is added to ChatMenu")
local bots = getglobal("ChatMenuButton11")
require_true(bots.text == "Bots" and bots.nested == "BotMenu" and bots.shown, "the entry is 'Bots' and opens BotMenu")
require_true(BotMenu.numButtons == table.getn(BOTMENU_CATEGORIES), "one BotMenu entry per category")

-- Every leaf writes exactly its command into the chat line and closes the menu.
local total = 0
for i = 1, table.getn(BOTMENU_CATEGORIES) do
    local category = BOTMENU_CATEGORIES[i]
    local categoryButton = getglobal("BotMenuButton"..i)
    require_true(categoryButton.nested == category.menu, category.label.." opens its submenu")
    local menu = getglobal(category.menu)
    require_true(menu.numButtons == table.getn(category.entries), category.label.." has all entries")
    for j = 1, menu.numButtons do
        local button = getglobal(category.menu.."Button"..j)
        local entry = category.entries[j]
        ChatMenu:Show()
        this = button
        if entry.open then
            -- Opens the bot list window instead of writing a command.
            local before = table.getn(written)
            button.func()
            require_true(table.getn(written) == before and BotListFrame.shown, entry[1].." opens the bot list")
            require_true(not ChatMenu.shown, "opening the list closes the chat menu")
            BotListFrame:Hide()
        elseif entry.prefix then
            -- A role click writes nothing; the next command gets the prefix once.
            local before = table.getn(written)
            button.func()
            require_true(table.getn(written) == before, entry[1].." writes nothing by itself")
            require_true(not ChatMenu.shown, "a role click closes the chat menu")
            local follow = getglobal("BotMenuCombatButton1")
            this = follow; follow.func()
            require_true(written[table.getn(written)] == entry.prefix.."follow", "role prefix '"..entry.prefix.."' goes in front of the next command")
            this = follow; follow.func()
            require_true(written[table.getn(written)] == "follow", "the role prefix applies to one command only")
        else
            button.func()
            require_true(written[table.getn(written)] == entry[2], "click writes '"..entry[2].."'")
            require_true(not ChatMenu.shown, "a click closes the chat menu")
        end
        total = total + 1
    end
end
require_true(total >= 30, "the full menu is present")

-- Reopening ChatMenu closes old submenus.
BotMenu:Show(); BotMenuCombat:Show()
ChatMenu_OnShow()
require_true(chatMenuOnShowCalls == 1, "the stock ChatMenu_OnShow still runs")
require_true(not BotMenu.shown and not BotMenuCombat.shown, "old bot submenus are closed on reopen")

-- The switch.
SlashCmdList["BOTMENU"]("off")
require_true(BotMenuDB.enabled == 0 and not bots.shown and ChatMenu.numButtons == 10, "/botmenu off removes the entry")
SlashCmdList["BOTMENU"]("on")
require_true(BotMenuDB.enabled == 1 and bots.shown and ChatMenu.numButtons == 11, "/botmenu on restores it")
SlashCmdList["BOTMENU"]("")

-- #419 bot list. "live": levels 3-17 and mixed classes, as in the train 7
-- video (timing is reported for this one). "extreme": 40 % on level 1 and
-- 70 level-1 warriors, so the scan must split by level, class and race.
local classes = BOTLIST_LOCALES.enUS.classes
local alliance, horde = BOTLIST_LOCALES.enUS.alliance, BOTLIST_LOCALES.enUS.horde
local zones = { "Ironforge", "Orgrimmar", "Elwynn Forest", "Durotar", "Stranglethorn Vale" }
local function makePlayers(n, extreme)
    local players = {}
    for i = 1, n do
        local races = (((i) % (2)) == 0) and alliance or horde
        local level = 3 + ((i * 7) % (15))
        local class = classes[1 + ((i) % (table.getn(classes)))]
        if extreme then
            level = 1
            if ((i) % (5)) >= 2 then level = 2 + ((i * 7) % (59)) end
            if i <= 70 then class = "Warrior"; level = 1 end
        end
        table.insert(players, { name = "Char"..i, guild = (((i) % (3)) == 0) and "Roster" or "",
            level = level, race = races[1 + ((i) % (table.getn(races)))], class = class,
            zone = zones[1 + ((i) % (table.getn(zones)))] })
    end
    return players
end

local function runScan(n, cooldown, extreme)
    server.players = makePlayers(n, extreme); server.cooldown = cooldown
    server.lastAccepted = -1000; server.answerAt = nil; server.queries = 0
    BotListData.chars = {}
    friendsPopups = 0
    BotList_StartScan()
    require_true(BotList_IsScanning() and whoToUI == 1 and not FriendsFrame.events.WHO_LIST_UPDATE,
        "a scan detaches the stock who window")
    local start = now
    while BotList_IsScanning() and now - start < 3600 do
        now = now + 0.1
        deliverWho()
        BotList_OnUpdate()
    end
    require_true(not BotList_IsScanning(), "the scan ends ("..n.." characters, cooldown "..cooldown.." s)")
    require_true(BotList_Count() == n, "all "..n.." characters found, got "..BotList_Count())
    require_true(friendsPopups == 0, "the stock who window never pops up during a scan")
    require_true(whoToUI == 0 and FriendsFrame.events.WHO_LIST_UPDATE, "the stock who window is attached again")
    now = now + 60
    return now - 60 - start, server.queries
end

local report = {}
for _, n in ipairs({ 180, 360 }) do
    for _, cooldown in ipairs({ 0, 5, 30 }) do
        local seconds, queries = runScan(n, cooldown)
        table.insert(report, string.format("n=%d cooldown=%ds: %.0f s, %d /who", n, cooldown, seconds, queries))
        if cooldown == 30 then
            require_true(seconds < 360, "a rank-0 scan of "..n.." stays under 6 min")
        end
    end
    local seconds, queries = runScan(n, 0, true)
    table.insert(report, string.format("n=%d extreme (GM): %.0f s, %d /who", n, seconds, queries))
end

-- Filters on the last (360) list.
local function countWhere(check)
    local c = 0
    for _, p in ipairs(server.players) do if check(p) then c = c + 1 end end
    return c
end
local function isAlliance(p) for _, r in ipairs(alliance) do if r == p.race then return true end end return false end
BotListFilter.faction = "Alliance"
require_true(table.getn(BotList_Filtered()) == countWhere(isAlliance), "faction filter")
BotListFilter.faction = nil; BotListFilter.class = "Mage"
require_true(table.getn(BotList_Filtered()) == countWhere(function(p) return p.class == "Mage" end), "class filter")
BotListFilter.class = nil; BotListFilter.minLevel = 10; BotListFilter.maxLevel = 20
require_true(table.getn(BotList_Filtered()) == countWhere(function(p) return p.level >= 10 and p.level <= 20 end), "level filter")
BotListFilter.minLevel = nil; BotListFilter.maxLevel = nil; BotList_SetZone("forge")
require_true(table.getn(BotList_Filtered()) == countWhere(function(p) return p.zone == "Ironforge" end), "zone filter (part of the name)")
BotList_SetZone("")
local sorted = BotList_Filtered()
require_true(sorted[1].name <= sorted[2].name, "the list is sorted by name")

-- Invite and raid.
BotList_Invite(nil)
require_true(table.getn(invited) == 0, "no invite without a selection")
BotList_Select("Char7"); BotList_Invite(nil)
require_true(invited[1] == "Char7", "invite the selected character")
BotList_ConvertToRaid()
require_true(converted == 1, "convert to raid")

-- The player's own /who cancels a running scan and gets the stock window.
server.players = makePlayers(180); server.cooldown = 30; server.lastAccepted = -1000
BotList_StartScan()
now = now + 1; deliverWho(); BotList_OnUpdate()
SendWho("Char1")
require_true(not BotList_IsScanning() and FriendsFrame.events.WHO_LIST_UPDATE and whoToUI == 0,
    "the player's own /who cancels the scan and restores the who window")

-- Manual pace.
SlashCmdList["BOTMENU"]("pace 10")
require_true(BotMenuDB.whoInterval == 10, "/botmenu pace 10")
SlashCmdList["BOTMENU"]("pace auto")
require_true(BotMenuDB.whoInterval == nil, "/botmenu pace auto")

-- BotList.lua missing (the 1.12 client only sees files that existed when-- the game started): opening the list must print a hint, not raise.local savedShow = BotList_ShowBotList_Show = nillocal okList = pcall(SlashCmdList["BOTMENU"], "list")require_true(okList, "/botmenu list without BotList.lua prints a hint instead of an error")BotList_Show = savedShow
for _, line in ipairs(report) do print("BOTLIST_SCAN "..line) end
print("BOTMENU_ADDON_HARNESS=PASS entries="..total)
