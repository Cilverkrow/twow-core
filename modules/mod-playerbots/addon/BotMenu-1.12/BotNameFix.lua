-- BotNameFix (twow-repo#518, client fallback of the display-name probe).
--
-- When the server shows bots as "Name Surname" (Debug.NameQueryDisplayNames,
-- test realm), the 1.12 client sends that display name back as the target of a
-- right-click whisper, /r, /invite, /friend, /ignore and mail - or, from a chat
-- link, only its last word. These wrappers turn a known display name (from
-- BotSurnames.lua) back into the character name before the call goes out.
-- They never send anything themselves and change nothing else.
--
-- Lua 5.0 / 1.12 client: no '#', no '%'.

BOTNAMEFIX_LASTWORD = {};  -- last word of a surname -> character name (unique ones only)

local function BotNameFix_Build()
	local seen = {};
	for name, surname in pairs(BOTMENU_SURNAMES or {}) do
		local last = surname;
		local i = string.find(last, " [^ ]*$");
		if ( i ) then
			last = string.sub(last, i + 1);
		end
		if ( seen[last] ) then
			BOTNAMEFIX_LASTWORD[last] = nil; -- ambiguous: leave it alone
		else
			BOTNAMEFIX_LASTWORD[last] = name;
		end
		seen[last] = true;
	end
end

function BotNameFix_Strip(name)
	if ( type(name) ~= "string" or not BOTMENU_SURNAMES or (BotMenuDB and BotMenuDB.nameFix == 0) ) then
		return name;
	end
	local space = string.find(name, " ", 1, true);
	if ( not space ) then
		return BOTNAMEFIX_LASTWORD[name] or name;
	end
	local first = string.sub(name, 1, space - 1);
	if ( BOTMENU_SURNAMES[first] and name == first.." "..BOTMENU_SURNAMES[first] ) then
		return first;
	end
	return name;
end

BotNameFix_Build();

local originalChat = SendChatMessage;
SendChatMessage = function(msg, chatType, language, target)
	if ( chatType == "WHISPER" ) then
		target = BotNameFix_Strip(target);
	end
	return originalChat(msg, chatType, language, target);
end

local originalInvite = InviteByName;
InviteByName = function(name)
	return originalInvite(BotNameFix_Strip(name));
end

local originalFriend = AddFriend;
AddFriend = function(name)
	return originalFriend(BotNameFix_Strip(name));
end

local originalIgnore = AddOrDelIgnore;
AddOrDelIgnore = function(name)
	return originalIgnore(BotNameFix_Strip(name));
end

local originalMail = SendMail;
SendMail = function(recipient, subject, body)
	return originalMail(BotNameFix_Strip(recipient), subject, body);
end
