local AURA_SPELL_ID = 1126 -- Mark of the Wild (Rank 1), effect index 0 is an aura.
local TEST_CREATURE_ENTRY = 6 -- Kobold Vermin.

local function expect(condition, message)
    if not condition then
        error('Aura smoke assertion failed: ' .. message, 2)
    end
end

local function runAuraSmoke()
    PrintInfo('ELUNA_AURA_SMOKE_STARTED')

    -- Unsaved and timed: this object never touches the creature table and is
    -- removed automatically even if a later assertion fails.
    local creature = PerformIngameSpawn(
        1, TEST_CREATURE_ENTRY, 0, 0,
        -8949.95, -132.493, 83.531, 0,
        false, 30000)
    expect(creature ~= nil, 'temporary creature spawn returned nil')

    local aura = creature:AddAura(AURA_SPELL_ID, creature)
    expect(aura ~= nil, 'Unit:AddAura returned nil')
    expect(creature:HasAura(AURA_SPELL_ID), 'Unit:HasAura did not see the new aura')
    expect(creature:GetAura(AURA_SPELL_ID) ~= nil, 'Unit:GetAura did not return the new aura')

    expect(aura:GetAuraId() == AURA_SPELL_ID, 'GetAuraId returned the wrong spell')
    expect(aura:GetCaster() ~= nil, 'GetCaster returned nil')
    expect(aura:GetCaster():GetGUIDLow() == creature:GetGUIDLow(), 'GetCaster returned the wrong unit')
    expect(aura:GetCasterGUID() ~= nil, 'GetCasterGUID returned nil')
    expect(aura:GetCasterLevel() == creature:GetLevel(), 'GetCasterLevel returned the wrong level')
    expect(aura:GetOwner() ~= nil, 'GetOwner returned nil')
    expect(aura:GetOwner():GetGUIDLow() == creature:GetGUIDLow(), 'GetOwner returned the wrong unit')
    expect(type(aura:GetDuration()) == 'number', 'GetDuration did not return a number')
    expect(type(aura:GetMaxDuration()) == 'number', 'GetMaxDuration did not return a number')
    expect(aura:GetStackAmount() >= 1, 'GetStackAmount returned no stacks')

    aura:SetMaxDuration(60000)
    expect(aura:GetMaxDuration() == 60000, 'SetMaxDuration did not persist')

    aura:SetDuration(45000)
    local duration = aura:GetDuration()
    expect(duration <= 45000 and duration >= 44000, 'SetDuration returned an unexpected duration')

    aura:SetStackAmount(2)
    expect(aura:GetStackAmount() == 2, 'SetStackAmount did not persist')
    expect(aura:GetDuration() <= aura:GetMaxDuration(), 'stack reset exceeded max duration')

    if aura.GetSpellInfo == nil then
        PrintInfo('ELUNA_AURA_GETSPELLINFO_UNAVAILABLE_VMANGOS')
    else
        expect(aura:GetSpellInfo() ~= nil, 'GetSpellInfo returned nil')
    end

    aura:Remove()
    expect(not creature:HasAura(AURA_SPELL_ID), 'Aura:Remove left the aura applied')
    creature:DespawnOrUnsummon(0)

    PrintInfo('ELUNA_AURA_SMOKE_PASSED')
end

local function onWorldStartup()
    local ok, message = pcall(runAuraSmoke)
    if not ok then
        PrintError('ELUNA_AURA_SMOKE_FAILED: ' .. tostring(message))
        error(message)
    end
end

RegisterServerEvent(14, onWorldStartup)
