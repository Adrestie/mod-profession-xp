--[[
    This file is part of mod-profession-xp.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program. If not, see <http://www.gnu.org/licenses/>.
]]

--[[----------------------------------------------------------------------------
    Profession XP — the reward of the selected recipe, in the profession
    window, client side (shipped by AIO).

    Under the recipe's description, above its reagents: what crafting it pays
    (experience, or Spherite at the maximum level) and the first-time bonus.
    A line the player will not get is grayed, with the reason under it --
    except when the recipe is too easy (gray): that line is not shown at all.
    When nothing at all would be paid, the whole block is hidden.

    Two windows: the game's own (TradeSkillFrame) and ForeverUI's, which keeps
    TradeSkillFrame as its host and draws its own recipe card. From ForeverUI
    this file takes only the card it builds (ForeverUI.Professions.skin.card,
    .reagents) and the moment it fills it (Professions.UpdateCard).

    THE SERVER IS THE C++ MODULE, not a Lua script: it holds the settings and
    the list of what the character already made, and talks over an addon
    whisper (prefix ProfXP). The protocol is described in ProfessionXP.cpp,
    above ProfessionXPMgr::SendAddon. Texts: NONE here; they come from the
    module's `module_string` rows, in the client's language.

    The computation mirrors ProfessionXPMgr::OnCraft and Grant. Changing one
    without the other makes the window promise what the server does not pay.
------------------------------------------------------------------------------]]

local AIO = AIO or require("AIO")

if AIO.AddAddon() then
    return                                  -- server side: we stop here
end

local PREFIX = "ProfXP"

-- The texts, by their number in module_string. The reasons that mean "nothing
-- at all is paid" (110, 112 to 115, 117) are no longer shown: the block is
-- hidden instead. Their rows stay, ids are never reused.
local ID = {
    title = 101, crafting = 102, first = 103, xp = 104, spherite = 105, capped = 106,
    gray = 110, rank_cap = 111, no_max_level = 112, spherite_off = 113, crafting_off = 114,
    xp_locked = 115, already = 116, gray_first = 117, first_off = 118,
}

-- The colour of a recipe, as GetTradeSkillInfo names it, and its index in the
-- module's reward tables (ProfessionXPColor).
local COLOR = { trivial = 0, easy = 1, medium = 2, optimal = 3 }

local GRAY = { 0.5, 0.5, 0.5 }
local FIRST_TIME = { 0.25, 1.0, 0.25 }
-- TradeSkillTypeColor of Blizzard_TradeSkillUI, should it not be loaded yet.
local RECIPE_COLOR = {
    optimal = { 1.0, 0.5, 0.25 }, medium = { 1.0, 1.0, 0.0 },
    easy = { 0.25, 0.75, 0.25 }, trivial = { 0.5, 0.5, 0.5 },
}

local texts = {}
local settings                              -- nil until the server answered
local done = {}                             -- ["kind:entry"] = true

local floor = math.floor

-- ---------------------------------------------------------------------------
-- The server
-- ---------------------------------------------------------------------------

local function Ask(request)
    SendAddonMessage(PREFIX, request, "WHISPER", UnitName("player"))
end

local function Text(id, ...)
    local s = texts[id] or ""
    for i = 1, select("#", ...) do
        s = string.gsub(s, "{}", tostring((select(i, ...))), 1)
    end
    return s
end

-- The `C` line: its fields in the order ProfessionXPMgr::SendSettings writes
-- them. A version this file does not know leaves the window alone.
local function Settings(line)
    local f = {}
    for v in string.gmatch(line, "[^|]+") do f[#f + 1] = tonumber(v) or 0 end
    if f[1] ~= 1 then
        settings = nil
        return
    end
    settings = {
        enabled = f[2] == 1, rate = f[3],
        xp = { [0] = 0, f[4], f[5], f[6] }, xpFirst = f[7], multiplier = f[8], cap = f[9],
        firstTime = f[10] == 1, firstOnGray = f[11] == 1,
        rewardAtRankCap = f[12] == 1, maxSkill = f[13], maxLevel = f[14],
        spherite = f[15],                   -- 0 not built, 1 turned off, 2 on
        sph = { [0] = 0, f[16], f[17], f[18] }, sphFirst = f[19],
        boost = f[20], xpLocked = f[21] == 1,
    }
end

local Refresh                               -- below

local listener = CreateFrame("Frame")
listener:RegisterEvent("CHAT_MSG_ADDON")
listener:RegisterEvent("TRADE_SKILL_SHOW")
listener:RegisterEvent("PLAYER_XP_UPDATE")
listener:RegisterEvent("PLAYER_LEVEL_UP")
listener:RegisterEvent("ADDON_LOADED")
listener:SetScript("OnEvent", function(_, event, prefix, message, channel, sender)
    if event == "CHAT_MSG_ADDON" then
        if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then return end
        local kind, a, b = string.match(message, "^(%a)\t?([^\t]*)\t?(.*)$")
        if kind == "T" then
            texts[tonumber(a)] = b
        elseif kind == "C" then
            Settings(a)
            Refresh()
        elseif kind == "R" then
            done = {}
        elseif kind == "D" then
            for pair in string.gmatch(a, "[^,]+") do done[pair] = true end
        elseif kind == "E" then
            Refresh()
        elseif kind == "A" then
            done[a] = true
            Refresh()
        end
    elseif event == "TRADE_SKILL_SHOW" then
        Ask("S")                            -- level, prisms, settings may have moved
    elseif event == "ADDON_LOADED" then
        if prefix == "Blizzard_TradeSkillUI" then
            listener.Install()
        end
    else
        Refresh()
    end
end)

-- ---------------------------------------------------------------------------
-- What the selected recipe pays
-- ---------------------------------------------------------------------------

-- The key of the recipe in the done list: the item it makes, or its spell
-- when it makes none (an enchantment), as ProfessionXPMgr::OnCraft keys it.
local function RecipeKey(id)
    local link = GetTradeSkillItemLink(id)
    local item = link and string.match(link, "item:(%d+)")
    if item then return "0:" .. item end
    local spell = (link and string.match(link, "enchant:(%d+)"))
        or string.match(GetTradeSkillRecipeLink(id) or "", "enchant:(%d+)")
    return spell and ("1:" .. spell) or nil
end

local function Boosted(amount)
    return floor(amount * settings.boost / 10000)
end

local function XpOf(percent)
    if percent <= 0 then return 0 end
    return math.max(1, floor(UnitXPMax("player") * percent / 100 + 0.5))
end

-- The lines to show: { text, colour, reason } each, nil when nothing is shown.
local function Compute(id)
    if not settings or not id or id <= 0 then return nil end
    local name, skillType = GetTradeSkillInfo(id)
    local color = COLOR[skillType]
    if not name or not color then return nil end
    if IsTradeSkillLinked and IsTradeSkillLinked() then return nil end   -- someone else's

    local s = settings
    local xpMode = UnitLevel("player") < s.maxLevel

    -- WHAT BLOCKS EVERYTHING: nothing can be paid.
    local blocked
    if not s.enabled then
        blocked = ID.crafting_off
    elseif xpMode and (s.xpLocked or (IsXPUserDisabled and IsXPUserDisabled())) then
        blocked = ID.xp_locked
    elseif not xpMode and s.spherite == 0 then
        blocked = ID.no_max_level
    elseif not xpMode and s.spherite == 1 then
        blocked = ID.spherite_off
    end

    local function amount(xpPercent, spherite)
        if xpMode then return Text(ID.xp, XpOf(xpPercent)) end
        return Text(ID.spherite, Boosted(spherite))
    end

    -- NOTHING AT ALL IS PAID: the whole block stays hidden.
    if blocked then return nil end

    local lines = {}
    local paidPercent, paid = 0, false

    -- The colour of the recipe. A gray recipe is too easy to pay anything:
    -- its line is not shown at all, rather than grayed.
    if color > 0 then
        local colorPercent = s.xp[color] * s.rate * s.multiplier
        local colorSpherite = floor(s.sph[color] * s.rate + 0.5)
        local reason
        if not s.rewardAtRankCap then
            local _, rank, maxRank = GetTradeSkillLine()
            if rank and maxRank and rank >= maxRank and rank < s.maxSkill then
                reason = ID.rank_cap
            end
        end
        if not reason then
            paidPercent = paidPercent + colorPercent
            paid = true
        end
        local tint = (TradeSkillTypeColor and TradeSkillTypeColor[skillType]) or RECIPE_COLOR[skillType]
        lines[#lines + 1] = {
            Text(ID.crafting, amount(colorPercent, colorSpherite)),
            reason and GRAY or { tint.r or tint[1], tint.g or tint[2], tint.b or tint[3] },
            reason and Text(reason),
        }
    end

    -- The first time. On a gray recipe (unless the server pays it there) the
    -- craft is too easy again: hidden, not grayed.
    local key = RecipeKey(id)
    if key and (color > 0 or s.firstOnGray) then
        local firstPercent = s.xpFirst * s.multiplier
        local reason
        if not s.firstTime then
            reason = ID.first_off
        elseif done[key] then
            reason = ID.already
        end
        if not reason then
            paidPercent = paidPercent + firstPercent
            paid = true
        end
        lines[#lines + 1] = {
            Text(ID.first, amount(firstPercent, s.sphFirst)),
            reason and GRAY or FIRST_TIME,
            reason and Text(reason),
        }
    end

    -- No line pays anything: the whole block stays hidden too.
    if not paid then return nil end

    -- One reward is capped (experience only).
    if xpMode and paidPercent > s.cap then
        lines[#lines + 1] = { Text(ID.capped, Text(ID.xp, XpOf(s.cap))), GRAY }
    end
    return lines
end

-- ---------------------------------------------------------------------------
-- The block of lines, one per window
-- ---------------------------------------------------------------------------

local function NewBlock(parent, width, lineFont)
    local b = { lines = {} }
    local frame = CreateFrame("Frame", nil, parent)
    frame:SetWidth(width)
    frame:SetHeight(1)
    b.frame = frame
    b.title = frame:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    b.title:SetJustifyH("LEFT")
    b.title:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, 0)

    local function fontString(i, font)
        local fs = b.lines[i]
        if not fs then
            fs = frame:CreateFontString(nil, "ARTWORK")
            fs:SetJustifyH("LEFT")
            fs:SetJustifyV("TOP")
            fs:SetWidth(width)
            b.lines[i] = fs
        end
        fs:SetFontObject(font)
        return fs
    end

    -- Fills the block for recipe `id`; returns its height, 0 when hidden.
    function b.Render(id)
        local lines = Compute(id)
        if not lines then
            frame:Hide()
            return 0
        end
        b.title:SetText(Text(ID.title))
        local previous, height, n = b.title, b.title:GetHeight(), 0
        local function add(text, font, color, gap)
            n = n + 1
            local fs = fontString(n, font)
            fs:SetHeight(0)
            fs:SetText(text)
            fs:SetTextColor(color[1], color[2], color[3])
            fs:SetHeight(fs:GetStringHeight())
            fs:ClearAllPoints()
            fs:SetPoint("TOPLEFT", previous, "BOTTOMLEFT", 0, -gap)
            fs:Show()
            previous = fs
            height = height + gap + fs:GetHeight()
        end
        for _, l in ipairs(lines) do
            if l[1] then add(l[1], lineFont, l[2], 2) end
            if l[3] then add(l[3], GameFontDisableSmall, GRAY, 1) end
        end
        for i = n + 1, #b.lines do b.lines[i]:Hide() end
        frame:SetHeight(height)
        frame:Show()
        return height
    end
    return b
end

-- THE GAME'S WINDOW. The block goes under the description; the reagents label
-- follows it. The scroll child keeps its XML height (150) plus the block, so
-- that the last reagents stay reachable.
local stock
local STOCK_CHILD_HEIGHT = 150

local function RenderStock(id)
    if not TradeSkillDetailScrollChildFrame then return end
    if TradeSkillFrame.foreverSkin then        -- ForeverUI draws the window
        if stock then stock.frame:Hide() end
        return
    end
    stock = stock or NewBlock(TradeSkillDetailScrollChildFrame, 290, GameFontHighlightSmall)
    local height = stock.Render(id)
    if height > 0 then
        stock.frame:ClearAllPoints()
        if GetTradeSkillDescription(id) then
            stock.frame:SetPoint("TOPLEFT", TradeSkillDescription, "BOTTOMLEFT", 0, -10)
        else
            stock.frame:SetPoint("TOPLEFT", TradeSkillDescription, "TOPLEFT", 0, 0)
        end
        TradeSkillReagentLabel:SetPoint("TOPLEFT", stock.frame, "BOTTOMLEFT", 0, -10)
        TradeSkillDetailScrollChildFrame:SetHeight(STOCK_CHILD_HEIGHT + height + 10)
    else
        -- Back where TradeSkillFrame_SetSelection puts the label.
        if GetTradeSkillDescription(id) then
            TradeSkillReagentLabel:SetPoint("TOPLEFT", TradeSkillDescription, "BOTTOMLEFT", 0, -10)
        else
            TradeSkillReagentLabel:SetPoint("TOPLEFT", TradeSkillDescription, "TOPLEFT", 0, 0)
        end
        TradeSkillDetailScrollChildFrame:SetHeight(STOCK_CHILD_HEIGHT)
    end
    if TradeSkillDetailScrollFrame then TradeSkillDetailScrollFrame:UpdateScrollChildRect() end
end

-- FOREVERUI'S CARD. The block goes under its description; its reagents
-- follow it, with the gap ForeverUI gives them (N.reagents, 20).
local forever

local function RenderForever()
    local P = ForeverUI and ForeverUI.Professions
    local h = P and P.skin
    if not h or not h.card then return end
    local F = h.card
    forever = forever or NewBlock(F.content, 305, _G.ForeverUIFontHighlightSmall2 or GameFontHighlightSmall)
    local height = P.selected and forever.Render(P.selected) or 0
    if height <= 0 then
        -- Back where ForeverUI puts its reagents (N.reagents).
        forever.frame:Hide()
        h.reagents:ClearAllPoints()
        h.reagents:SetPoint("TOPLEFT", F.description, "BOTTOMLEFT", 0, -20)
        return
    end
    forever.frame:ClearAllPoints()
    forever.frame:SetPoint("TOPLEFT", F.description, "BOTTOMLEFT", 0, -10)
    h.reagents:ClearAllPoints()
    h.reagents:SetPoint("TOPLEFT", forever.frame, "BOTTOMLEFT", 0, -20)
end

Refresh = function()
    if not TradeSkillFrame or not TradeSkillFrame:IsShown() then return end
    if TradeSkillFrame.foreverSkin then
        RenderForever()
    else
        RenderStock(GetTradeSkillSelectionIndex())
    end
end

-- The hooks, once Blizzard_TradeSkillUI (load on demand) is there.
function listener.Install()
    if listener.installed or not TradeSkillFrame_SetSelection then return end
    listener.installed = true
    hooksecurefunc("TradeSkillFrame_SetSelection", RenderStock)
    -- ForeverUI fills its card in Professions.UpdateCard, called through its
    -- table: hooking the table's field catches every call.
    if ForeverUI and ForeverUI.Professions and ForeverUI.Professions.UpdateCard then
        hooksecurefunc(ForeverUI.Professions, "UpdateCard", RenderForever)
    end
end

listener.Install()
Ask("H")
