-- twow-repo#408 / #437 (train 8, owner approval 2026-09-29 in twow-repo#338, relayed by OB-00):
-- missing quest giver and turn-in relations for Turtle quests whose NPC is already spawned.
-- Source: quest Objectives/Details text and the quest chain (a follow-up is offered by the NPC
-- that completes the previous quest); dbcheck #438 found the gaps. Only unambiguous cases.
-- A giver is added only when the quest can also be turned in, so no quest becomes acceptable
-- but uncompletable (40821 -> Echo of Vandol and 41921 have no spawned turn-in yet: train 9).
-- Every NPC below has a spawn and the quest-giver npc flag. Replay-safe: INSERT IGNORE on the
-- (id, quest) primary key.
-- Rollback: remove exactly the listed (id, quest) pairs from both tables (none existed before);
-- the rollback statements are in the PR description (Cilverkrow/twow-core, twow-repo#408).

-- Quest starters
INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`) VALUES
(2543,  40820), -- Archmage Ansirem Runeweaver: The Key to Karazhan IV (finished III with him)
(62460, 41709), -- Hydromancer Finnigan: Ceaseless Storms
(62460, 41710), -- Hydromancer Finnigan: Piece Of A Bigger Picture
(62460, 41711), -- Hydromancer Finnigan: Calming The Tempest
(62571, 41815), -- Grexx: Free Merchandise
(62569, 41894), -- Razzo Copperfume: The Chromatic Servo-Motor
(62986, 41911), -- Ar'lia: Wolf in Sheep's Clothing
(62986, 41912), -- Ar'lia: An Ill Omen
(62920, 41913), -- Riftmaster Ral'pekta: Draenei Divination
(62920, 41914), -- Riftmaster Ral'pekta: Hooves and Horns, Clad in Red
(62986, 41915), -- Ar'lia: Answers from Father
(62850, 41916), -- Moro'gai K'la: The Elder's End
(62850, 41917), -- Moro'gai K'la: A Student's Determination
(62994, 41920), -- Maghan: Fallen One Cargo
(62796, 41941), -- Firespeaker Bewali: Duality of Flame
(62861, 41944), -- Cook Rem'sai: The Long Hunt
(62852, 41945), -- Fena Ma'dar: Respect the Elderly
(62922, 41946), -- Farmer Denphar: Farm Raiders
(63047, 41949), -- Chief Defender Hamaam: Horns of their Allies
(62920, 41951), -- Riftmaster Ral'pekta: Merchant's Knowledge
(62994, 41952), -- Maghan: Out of the Moonlight
(62920, 41953), -- Riftmaster Ral'pekta: Draenethyst Recovery
(62864, 41954), -- Master Craftsman T'kalpa: Skills of an Unknown World
(62864, 41955), -- Master Craftsman T'kalpa: Tricolored Hide-ra
(62980, 42001), -- Uz'tuk: What Upsets the Elements?
(62802, 42009), -- Muln Earthfury: Loktanag the Pure
(62980, 42049), -- Uz'tuk: In Need of Water
(62980, 42051), -- Uz'tuk: Bound in Stone
(80999, 80381); -- Elodia: Shellcoins

-- Quest turn-ins
INSERT IGNORE INTO `creature_involvedrelation` (`id`, `quest`) VALUES
(60731, 40820), -- Magus Halister: The Key to Karazhan IV
(4088,  41261), -- Elanaria: Lesson in Protection
(62460, 41709), -- Hydromancer Finnigan: Ceaseless Storms
(62460, 41710), -- Hydromancer Finnigan: Piece Of A Bigger Picture
(62460, 41711), -- Hydromancer Finnigan: Calming The Tempest
(62460, 41799), -- Hydromancer Finnigan: A Hydromancer's Curiosity
(62571, 41815), -- Grexx: Free Merchandise
(62569, 41894), -- Razzo Copperfume: The Chromatic Servo-Motor
(62986, 41910), -- Ar'lia: Ar'lia of the Moro'gai
(62986, 41911), -- Ar'lia: Wolf in Sheep's Clothing
(62920, 41912), -- Riftmaster Ral'pekta: An Ill Omen
(62920, 41913), -- Riftmaster Ral'pekta: Draenei Divination
(62986, 41914), -- Ar'lia: Hooves and Horns, Clad in Red
(62850, 41915), -- Moro'gai K'la: Answers from Father
(62850, 41916), -- Moro'gai K'la: The Elder's End
(62986, 41917), -- Ar'lia: A Student's Determination
(62854, 41920), -- P'li: Fallen One Cargo
(91781, 41922), -- Sanv K'la: Homecoming
(62861, 41944), -- Cook Rem'sai: The Long Hunt
(62852, 41945), -- Fena Ma'dar: Respect the Elderly
(62922, 41946), -- Farmer Denphar: Farm Raiders
(63047, 41947), -- Chief Defender Hamaam: Wanted: Tama'an the Ruthless
(63047, 41948), -- Chief Defender Hamaam: Wanted: Growlpaw
(63047, 41949), -- Chief Defender Hamaam: Horns of their Allies
(62920, 41950), -- Riftmaster Ral'pekta: (crystal crate)
(62994, 41951), -- Maghan: Merchant's Knowledge
(62920, 41952), -- Riftmaster Ral'pekta: Out of the Moonlight
(62920, 41953), -- Riftmaster Ral'pekta: Draenethyst Recovery
(62864, 41954), -- Master Craftsman T'kalpa: Skills of an Unknown World
(62864, 41955), -- Master Craftsman T'kalpa: Tricolored Hide-ra
(62980, 42001), -- Uz'tuk: What Upsets the Elements?
(62802, 42009), -- Muln Earthfury: Loktanag the Pure
(62851, 42012), -- Heghala: (Thobias's Satchel)
(62980, 42050), -- Uz'tuk: (cleansed Bundle of Beads)
(62981, 42051); -- Lotka Muddoll: Bound in Stone
