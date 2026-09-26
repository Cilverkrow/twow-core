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

-- Load the addon the way the client does: Lua file, then the XML OnLoads.
SlashCmdList = {}
local chunk, err = loadfile(dir.."/BotMenu.lua")
require_true(chunk, "BotMenu.lua does not parse: "..tostring(err))
chunk()

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
        ChatMenu:Show()
        this = button
        button.func()
        require_true(written[table.getn(written)] == category.entries[j][2], "click writes '"..category.entries[j][2].."'")
        require_true(not ChatMenu.shown, "a click closes the chat menu")
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

print("BOTMENU_ADDON_HARNESS=PASS entries="..total)
