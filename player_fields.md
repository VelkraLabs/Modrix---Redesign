# Terraria.Player: campos de instância (Player_Fields)

Origem: dump do Il2CppDumper v6.7.46 sobre `libil2cpp.so` (arm64-v8a, Unity 2021.3.56f2) e `global-metadata.dat` (v31).
Tipos C vêm de `il2cpp.h` (`struct Terraria_Player_Fields : Terraria_Entity_Fields`); offsets e tipos C# vêm de `dump.cs`, cruzados campo a campo pela ordem de declaração (nomes conferidos).

> **Sobre os offsets:** são relativos ao início do objeto `Player_o` (o ponteiro `Player*`), e já incluem o cabeçalho de 0x10 bytes (`klass` + `monitor`). Para indexar `Player_Fields` diretamente, subtraia 0x10.

- Campos herdados de `Entity`: 16
- Campos declarados em `Player`: 1041
- **Total: 1057 campos** (último offset: `0xD78`)

## Campos de combate (destaque)

| Campo | Offset | Tipo C# | Tipo C (il2cpp.h) |
|---|---|---|---|
| **`statLife`** ⚔️ | **`0x704`** | `int` | `int32_t` |
| **`statLifeMax`** ⚔️ | **`0x6FC`** | `int` | `int32_t` |
| **`statDefense`** ⚔️ | **`0x6F8`** | `int` | `int32_t` |
| **`immune`** ⚔️ | **`0x3A8`** | `bool` | `bool` |
| **`immuneTime`** ⚔️ | **`0x3AC`** | `int` | `int32_t` |

Vizinhos úteis: `statLifeMax2` 0x700, `statMana` 0x708, `statManaMax2` 0x710, `lifeRegen` 0x714, `dead` 0x534, `immuneNoBlink` 0x3A9

## Herdados de Terraria.Entity

| Campo | Offset | Tipo C# | Tipo C (il2cpp.h) |
|---|---|---|---|
| `whoAmI` | `0x10` | `int` | `int32_t` |
| `active` | `0x14` | `bool` | `bool` |
| `entityId` | `0x18` | `long` | `int64_t` |
| `position` | `0x20` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `velocity` | `0x28` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `oldPosition` | `0x30` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `oldVelocity` | `0x38` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `oldDirection` | `0x40` | `int` | `int32_t` |
| `direction` | `0x44` | `int` | `int32_t` |
| `width` | `0x48` | `int` | `int32_t` |
| `height` | `0x4C` | `int` | `int32_t` |
| `wet` | `0x50` | `bool` | `bool` |
| `shimmerWet` | `0x51` | `bool` | `bool` |
| `honeyWet` | `0x52` | `bool` | `bool` |
| `wetCount` | `0x53` | `byte` | `uint8_t` |
| `lavaWet` | `0x54` | `bool` | `bool` |

## Declarados em Terraria.Player (ordem de declaração)

| Campo | Offset | Tipo C# | Tipo C (il2cpp.h) |
|---|---|---|---|
| `emoteTime` | `0x58` | `int` | `int32_t` |
| `creativeTracker` | `0x60` | `CreativeUnlocksTracker` | `struct Terraria_GameContent_Creative_CreativeUnlocksTracker_o*` |
| `chatOverhead` | `0x68` | `Player.OverheadMessage` | `struct Terraria_Player_OverheadMessage_o` |
| `DpadRadial` | `0x88` | `Player.SelectionRadial` | `struct Terraria_Player_SelectionRadial_o*` |
| `CircularRadial` | `0x90` | `Player.SelectionRadial` | `struct Terraria_Player_SelectionRadial_o*` |
| `QuicksRadial` | `0x98` | `Player.SelectionRadial` | `struct Terraria_Player_SelectionRadial_o*` |
| `alchemyTable` | `0xA0` | `bool` | `bool` |
| `HotbarOffset` | `0xA4` | `int` | `int32_t` |
| `GoingDownWithGrapple` | `0xA8` | `bool` | `bool` |
| `spelunkerTimer` | `0xA9` | `byte` | `uint8_t` |
| `hideInfo` | `0xB0` | `bool[]` | `struct System_Boolean_array*` |
| `builderAccStatus` | `0xB8` | `int[]` | `struct System_Int32_array*` |
| `lostCoins` | `0xC0` | `long` | `int64_t` |
| `lostCoinString` | `0xC8` | `string` | `struct System_String_o*` |
| `soulDrain` | `0xD0` | `int` | `int32_t` |
| `drainBoost` | `0xD4` | `float` | `float` |
| `dd2Accessory` | `0xD8` | `bool` | `bool` |
| `name` | `0xE0` | `string` | `struct System_String_o*` |
| `taxMoney` | `0xE8` | `int` | `int32_t` |
| `taxTimer` | `0xEC` | `int` | `int32_t` |
| `numberOfDeathsPVE` | `0xF0` | `int` | `int32_t` |
| `numberOfDeathsPVP` | `0xF4` | `int` | `int32_t` |
| `basiliskCharge` | `0xF8` | `float` | `float` |
| `lastDeathPostion` | `0xFC` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `lastDeathTime` | `0x108` | `DateTime` | `struct System_DateTime_o` |
| `showLastDeath` | `0x110` | `bool` | `bool` |
| `usedAegisCrystal` | `0x111` | `bool` | `bool` |
| `usedAegisFruit` | `0x112` | `bool` | `bool` |
| `usedArcaneCrystal` | `0x113` | `bool` | `bool` |
| `usedGalaxyPearl` | `0x114` | `bool` | `bool` |
| `usedGummyWorm` | `0x115` | `bool` | `bool` |
| `usedAmbrosia` | `0x116` | `bool` | `bool` |
| `extraAccessorySlots` | `0x118` | `int` | `int32_t` |
| `extraAccessorySlotsUse` | `0x11C` | `int` | `int32_t` |
| `extraAccessory` | `0x120` | `bool` | `bool` |
| `dontConsumeWand` | `0x121` | `bool` | `bool` |
| `tankPet` | `0x124` | `int` | `int32_t` |
| `tankPetReset` | `0x128` | `bool` | `bool` |
| `stringColor` | `0x12C` | `int` | `int32_t` |
| `counterWeight` | `0x130` | `int` | `int32_t` |
| `yoyoString` | `0x134` | `bool` | `bool` |
| `yoyoGlove` | `0x135` | `bool` | `bool` |
| `beetleOrbs` | `0x138` | `int` | `int32_t` |
| `beetleCounter` | `0x13C` | `float` | `float` |
| `beetleCountdown` | `0x140` | `int` | `int32_t` |
| `beetleDefense` | `0x144` | `bool` | `bool` |
| `beetleOffense` | `0x145` | `bool` | `bool` |
| `beetleBuff` | `0x146` | `bool` | `bool` |
| `solarShields` | `0x148` | `int` | `int32_t` |
| `solarCounter` | `0x14C` | `int` | `int32_t` |
| `solarShieldPos` | `0x150` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `solarShieldVel` | `0x158` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `solarDashing` | `0x160` | `bool` | `bool` |
| `solarDashConsumedFlare` | `0x161` | `bool` | `bool` |
| `nebulaLevelLife` | `0x164` | `int` | `int32_t` |
| `nebulaLevelMana` | `0x168` | `int` | `int32_t` |
| `nebulaManaCounter` | `0x16C` | `int` | `int32_t` |
| `nebulaLevelDamage` | `0x170` | `int` | `int32_t` |
| `manaMagnet` | `0x174` | `bool` | `bool` |
| `lifeMagnet` | `0x175` | `bool` | `bool` |
| `treasureMagnet` | `0x176` | `bool` | `bool` |
| `chiselSpeed` | `0x177` | `bool` | `bool` |
| `lifeForce` | `0x178` | `bool` | `bool` |
| `calmed` | `0x179` | `bool` | `bool` |
| `inferno` | `0x17A` | `bool` | `bool` |
| `flameRingRot` | `0x17C` | `float` | `float` |
| `flameRingScale` | `0x180` | `float` | `float` |
| `flameRingFrame` | `0x184` | `byte` | `uint8_t` |
| `flameRingAlpha` | `0x185` | `byte` | `uint8_t` |
| `netManaTime` | `0x188` | `int` | `int32_t` |
| `netLifeTime` | `0x18C` | `int` | `int32_t` |
| `netMana` | `0x190` | `bool` | `bool` |
| `netLife` | `0x191` | `bool` | `bool` |
| `beetlePos` | `0x198` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `beetleVel` | `0x1A0` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `beetleFrame` | `0x1A8` | `int` | `int32_t` |
| `beetleFrameCounter` | `0x1AC` | `int` | `int32_t` |
| `manaSickReduction` | `0x1B0` | `float` | `float` |
| `manaSick` | `0x1B4` | `bool` | `bool` |
| `afkCounter` | `0x1B8` | `int` | `int32_t` |
| `stairFall` | `0x1BC` | `bool` | `bool` |
| `loadStatus` | `0x1C0` | `int` | `int32_t` |
| `itemFlamePos` | `0x1C8` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `itemFlameCount` | `0x1D0` | `int` | `int32_t` |
| `outOfRange` | `0x1D4` | `bool` | `bool` |
| `lifeSteal` | `0x1D8` | `float` | `float` |
| `ghostDmg` | `0x1DC` | `float` | `float` |
| `teleporting` | `0x1E0` | `bool` | `bool` |
| `teleportTime` | `0x1E4` | `float` | `float` |
| `teleportStyle` | `0x1E8` | `int` | `int32_t` |
| `sloping` | `0x1EC` | `bool` | `bool` |
| `chilled` | `0x1ED` | `bool` | `bool` |
| `dazed` | `0x1EE` | `bool` | `bool` |
| `frozen` | `0x1EF` | `bool` | `bool` |
| `stoned` | `0x1F0` | `bool` | `bool` |
| `lastStoned` | `0x1F1` | `bool` | `bool` |
| `ichor` | `0x1F2` | `bool` | `bool` |
| `webbed` | `0x1F3` | `bool` | `bool` |
| `tipsy` | `0x1F4` | `bool` | `bool` |
| `noBuilding` | `0x1F5` | `bool` | `bool` |
| `ropeCount` | `0x1F8` | `int` | `int32_t` |
| `manaRegenBonus` | `0x1FC` | `int` | `int32_t` |
| `manaRegenDelayBonus` | `0x200` | `float` | `float` |
| `dashType` | `0x204` | `int` | `int32_t` |
| `dash` | `0x208` | `int` | `int32_t` |
| `dashTime` | `0x20C` | `int` | `int32_t` |
| `timeSinceLastDashStarted` | `0x210` | `int` | `int32_t` |
| `dashDelay` | `0x214` | `int` | `int32_t` |
| `eocDash` | `0x218` | `int` | `int32_t` |
| `eocHit` | `0x21C` | `int` | `int32_t` |
| `accRunSpeed` | `0x220` | `float` | `float` |
| `cordage` | `0x224` | `bool` | `bool` |
| `gem` | `0x228` | `int` | `int32_t` |
| `gemCount` | `0x22C` | `int` | `int32_t` |
| `ownedLargeGems` | `0x230` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `meleeEnchant` | `0x231` | `byte` | `uint8_t` |
| `pulleyDir` | `0x232` | `byte` | `uint8_t` |
| `pulley` | `0x233` | `bool` | `bool` |
| `pulleyFrame` | `0x234` | `int` | `int32_t` |
| `pulleyFrameCounter` | `0x238` | `float` | `float` |
| `blackBelt` | `0x23C` | `bool` | `bool` |
| `sliding` | `0x23D` | `bool` | `bool` |
| `slideDir` | `0x240` | `int` | `int32_t` |
| `snowBallLauncherInteractionCooldown` | `0x244` | `int` | `int32_t` |
| `iceSkate` | `0x248` | `bool` | `bool` |
| `carpet` | `0x249` | `bool` | `bool` |
| `spikedBoots` | `0x24C` | `int` | `int32_t` |
| `carpetFrame` | `0x250` | `int` | `int32_t` |
| `carpetFrameCounter` | `0x254` | `float` | `float` |
| `canCarpet` | `0x258` | `bool` | `bool` |
| `carpetTime` | `0x25C` | `int` | `int32_t` |
| `miscCounter` | `0x260` | `int` | `int32_t` |
| `infernoCounter` | `0x264` | `int` | `int32_t` |
| `insanityShadowCooldown` | `0x268` | `int` | `int32_t` |
| `starCloakCooldown` | `0x26C` | `int` | `int32_t` |
| `sandStorm` | `0x270` | `bool` | `bool` |
| `crimsonRegen` | `0x271` | `bool` | `bool` |
| `ghostHeal` | `0x272` | `bool` | `bool` |
| `ghostHurt` | `0x273` | `bool` | `bool` |
| `sticky` | `0x274` | `bool` | `bool` |
| `slippy` | `0x275` | `bool` | `bool` |
| `slippy2` | `0x276` | `bool` | `bool` |
| `powerrun` | `0x277` | `bool` | `bool` |
| `runningOnSand` | `0x278` | `bool` | `bool` |
| `flapSound` | `0x279` | `bool` | `bool` |
| `iceBarrier` | `0x27A` | `bool` | `bool` |
| `dangerSense` | `0x27B` | `bool` | `bool` |
| `luckPotion` | `0x27C` | `byte` | `uint8_t` |
| `oldLuckPotion` | `0x27D` | `byte` | `uint8_t` |
| `endurance` | `0x280` | `float` | `float` |
| `whipRangeMultiplier` | `0x284` | `float` | `float` |
| `whipUseTimeMultiplier` | `0x288` | `float` | `float` |
| `loveStruck` | `0x28C` | `bool` | `bool` |
| `stinky` | `0x28D` | `bool` | `bool` |
| `resistCold` | `0x28E` | `bool` | `bool` |
| `electrified` | `0x28F` | `bool` | `bool` |
| `dryadWard` | `0x290` | `bool` | `bool` |
| `panic` | `0x291` | `bool` | `bool` |
| `brainOfConfusionItem` | `0x298` | `Item` | `struct Terraria_Item_o*` |
| `brainOfConfusionDodgeAnimationCounter` | `0x2A0` | `int` | `int32_t` |
| `iceBarrierFrame` | `0x2A4` | `byte` | `uint8_t` |
| `iceBarrierFrameCounter` | `0x2A5` | `byte` | `uint8_t` |
| `shadowDodge` | `0x2A6` | `bool` | `bool` |
| `shadowDodgeCount` | `0x2A8` | `float` | `float` |
| `palladiumRegen` | `0x2AC` | `bool` | `bool` |
| `onHitDodge` | `0x2AD` | `bool` | `bool` |
| `onHitRegen` | `0x2AE` | `bool` | `bool` |
| `onHitPetal` | `0x2AF` | `bool` | `bool` |
| `onHitTitaniumStorm` | `0x2B0` | `bool` | `bool` |
| `titaniumStormCooldown` | `0x2B4` | `int` | `int32_t` |
| `hasTitaniumStormBuff` | `0x2B8` | `bool` | `bool` |
| `petalTimer` | `0x2BC` | `int` | `int32_t` |
| `shadowDodgeTimer` | `0x2C0` | `int` | `int32_t` |
| `boneGloveTimer` | `0x2C4` | `int` | `int32_t` |
| `phantomPhoneixCounter` | `0x2C8` | `int` | `int32_t` |
| `fishingSkill` | `0x2CC` | `int` | `int32_t` |
| `cratePotion` | `0x2D0` | `bool` | `bool` |
| `sonarPotion` | `0x2D1` | `bool` | `bool` |
| `accFishingLine` | `0x2D2` | `bool` | `bool` |
| `accFishingBobber` | `0x2D3` | `bool` | `bool` |
| `accTackleBox` | `0x2D4` | `bool` | `bool` |
| `accLavaFishing` | `0x2D5` | `bool` | `bool` |
| `maxMinions` | `0x2D8` | `int` | `int32_t` |
| `numMinions` | `0x2DC` | `int` | `int32_t` |
| `slotsMinions` | `0x2E0` | `float` | `float` |
| `pygmy` | `0x2E4` | `bool` | `bool` |
| `raven` | `0x2E5` | `bool` | `bool` |
| `slime` | `0x2E6` | `bool` | `bool` |
| `hornetMinion` | `0x2E7` | `bool` | `bool` |
| `impMinion` | `0x2E8` | `bool` | `bool` |
| `twinsMinion` | `0x2E9` | `bool` | `bool` |
| `spiderMinion` | `0x2EA` | `bool` | `bool` |
| `nextCycledSpiderMinionType` | `0x2EC` | `int` | `int32_t` |
| `pirateMinion` | `0x2F0` | `bool` | `bool` |
| `sharknadoMinion` | `0x2F1` | `bool` | `bool` |
| `UFOMinion` | `0x2F2` | `bool` | `bool` |
| `DeadlySphereMinion` | `0x2F3` | `bool` | `bool` |
| `stardustMinion` | `0x2F4` | `bool` | `bool` |
| `stardustGuardian` | `0x2F5` | `bool` | `bool` |
| `stardustDragon` | `0x2F6` | `bool` | `bool` |
| `batsOfLight` | `0x2F7` | `bool` | `bool` |
| `babyBird` | `0x2F8` | `bool` | `bool` |
| `vampireFrog` | `0x2F9` | `bool` | `bool` |
| `stormTiger` | `0x2FA` | `bool` | `bool` |
| `highestStormTigerGemOriginalDamage` | `0x2FC` | `int` | `int32_t` |
| `smolstar` | `0x300` | `bool` | `bool` |
| `empressBlade` | `0x301` | `bool` | `bool` |
| `flinxMinion` | `0x302` | `bool` | `bool` |
| `abigailMinion` | `0x303` | `bool` | `bool` |
| `highestAbigailCounterOriginalDamage` | `0x304` | `int` | `int32_t` |
| `wingTime` | `0x308` | `float` | `float` |
| `wings` | `0x30C` | `int` | `int32_t` |
| `wingsLogic` | `0x310` | `int` | `int32_t` |
| `wingTimeMax` | `0x314` | `int` | `int32_t` |
| `wingFrame` | `0x318` | `int` | `int32_t` |
| `wingFrameCounter` | `0x31C` | `int` | `int32_t` |
| `skinVariant` | `0x320` | `int` | `int32_t` |
| `ghost` | `0x324` | `bool` | `bool` |
| `ghostFrame` | `0x328` | `int` | `int32_t` |
| `ghostFrameCounter` | `0x32C` | `int` | `int32_t` |
| `miscTimer` | `0x330` | `int` | `int32_t` |
| `environmentBuffImmunityTimer` | `0x334` | `int` | `int32_t` |
| `_framesLeftEligibleForDeadmansChestDeathAchievement` | `0x338` | `int` | `int32_t` |
| `pvpDeath` | `0x33C` | `bool` | `bool` |
| `zone1` | `0x33D` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `zone2` | `0x33E` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `zone3` | `0x33F` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `zone4` | `0x340` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `zone5` | `0x341` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `_wasInShimmerZone` | `0x342` | `bool` | `bool` |
| `boneArmor` | `0x343` | `bool` | `bool` |
| `frostArmor` | `0x344` | `bool` | `bool` |
| `honey` | `0x345` | `bool` | `bool` |
| `crystalLeaf` | `0x346` | `bool` | `bool` |
| `crystalLeafCooldown` | `0x348` | `int` | `int32_t` |
| `portableStoolInfo` | `0x34C` | `PortableStoolUsage` | `struct Terraria_DataStructures_PortableStoolUsage_o` |
| `preventAllItemPickups` | `0x35C` | `bool` | `bool` |
| `dontHurtCritters` | `0x35D` | `bool` | `bool` |
| `hasLucyTheAxe` | `0x35E` | `bool` | `bool` |
| `dontHurtNature` | `0x35F` | `bool` | `bool` |
| `doubleTapCardinalTimer` | `0x360` | `int[]` | `struct System_Int32_array*` |
| `holdDownCardinalTimer` | `0x368` | `int[]` | `struct System_Int32_array*` |
| `defendedByPaladin` | `0x370` | `bool` | `bool` |
| `hasPaladinShield` | `0x371` | `bool` | `bool` |
| `speedSlice` | `0x378` | `float[]` | `struct System_Single_array*` |
| `townNPCs` | `0x380` | `float` | `float` |
| `headFrameCounter` | `0x388` | `double` | `double` |
| `bodyFrameCounter` | `0x390` | `double` | `double` |
| `legFrameCounter` | `0x398` | `double` | `double` |
| `netSkip` | `0x3A0` | `int` | `int32_t` |
| `oldSelectItem` | `0x3A4` | `int` | `int32_t` |
| **`immune`** ⚔️ | **`0x3A8`** | `bool` | `bool` |
| `immuneNoBlink` | `0x3A9` | `bool` | `bool` |
| **`immuneTime`** ⚔️ | **`0x3AC`** | `int` | `int32_t` |
| `immuneAlphaDirection` | `0x3B0` | `int` | `int32_t` |
| `immuneAlpha` | `0x3B4` | `int` | `int32_t` |
| `team` | `0x3B8` | `int` | `int32_t` |
| `_timeSinceLastImmuneGet` | `0x3BC` | `int` | `int32_t` |
| `_immuneStrikes` | `0x3C0` | `int` | `int32_t` |
| `hbLocked` | `0x3C4` | `bool` | `bool` |
| `maxRegenDelay` | `0x3C8` | `float` | `float` |
| `sign` | `0x3CC` | `int` | `int32_t` |
| `editedChestName` | `0x3D0` | `bool` | `bool` |
| `reuseDelay` | `0x3D4` | `int` | `int32_t` |
| `aggro` | `0x3D8` | `int` | `int32_t` |
| `nearbyActiveNPCs` | `0x3DC` | `float` | `float` |
| `creativeInterface` | `0x3E0` | `bool` | `bool` |
| `mouseInterface` | `0x3E1` | `bool` | `bool` |
| `lastMouseInterface` | `0x3E2` | `bool` | `bool` |
| `noThrow` | `0x3E4` | `int` | `int32_t` |
| `changeItem` | `0x3E8` | `int` | `int32_t` |
| `lastHotbarItem` | `0x3EC` | `int` | `int32_t` |
| `selectedItem` | `0x3F0` | `int` | `int32_t` |
| `armor` | `0x3F8` | `Item[]` | `struct Terraria_Item_array*` |
| `dye` | `0x400` | `Item[]` | `struct Terraria_Item_array*` |
| `miscEquips` | `0x408` | `Item[]` | `struct Terraria_Item_array*` |
| `miscDyes` | `0x410` | `Item[]` | `struct Terraria_Item_array*` |
| `trashItem` | `0x418` | `Item` | `struct Terraria_Item_o*` |
| `itemRotation` | `0x420` | `float` | `float` |
| `itemWidth` | `0x424` | `int` | `int32_t` |
| `itemHeight` | `0x428` | `int` | `int32_t` |
| `itemLocation` | `0x42C` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `poundRelease` | `0x434` | `bool` | `bool` |
| `ghostFade` | `0x438` | `float` | `float` |
| `ghostDir` | `0x43C` | `float` | `float` |
| `buffType` | `0x440` | `int[]` | `struct System_Int32_array*` |
| `buffTime` | `0x448` | `int[]` | `struct System_Int32_array*` |
| `buffImmune` | `0x450` | `bool[]` | `struct System_Boolean_array*` |
| `heldProj` | `0x458` | `int` | `int32_t` |
| `breathCD` | `0x45C` | `int` | `int32_t` |
| `breathMax` | `0x460` | `int` | `int32_t` |
| `breath` | `0x464` | `int` | `int32_t` |
| `lavaCD` | `0x468` | `int` | `int32_t` |
| `lavaMax` | `0x46C` | `int` | `int32_t` |
| `lavaTime` | `0x470` | `int` | `int32_t` |
| `ignoreWater` | `0x474` | `bool` | `bool` |
| `armorEffectDrawShadow` | `0x475` | `bool` | `bool` |
| `armorEffectDrawShadowSubtle` | `0x476` | `bool` | `bool` |
| `armorEffectDrawOutlines` | `0x477` | `bool` | `bool` |
| `armorEffectDrawShadowLokis` | `0x478` | `bool` | `bool` |
| `armorEffectDrawShadowBasilisk` | `0x479` | `bool` | `bool` |
| `armorEffectDrawOutlinesForbidden` | `0x47A` | `bool` | `bool` |
| `armorEffectDrawShadowEOCShield` | `0x47B` | `bool` | `bool` |
| `socialShadowRocketBoots` | `0x47C` | `bool` | `bool` |
| `socialGhost` | `0x47D` | `bool` | `bool` |
| `shroomiteStealth` | `0x47E` | `bool` | `bool` |
| `ashWoodBonus` | `0x47F` | `bool` | `bool` |
| `socialIgnoreLight` | `0x480` | `bool` | `bool` |
| `stealthTimer` | `0x484` | `int` | `int32_t` |
| `stealth` | `0x488` | `float` | `float` |
| `beardGrowthTimer` | `0x48C` | `int` | `int32_t` |
| `isDisplayDollOrInanimate` | `0x490` | `bool` | `bool` |
| `isFullbright` | `0x491` | `bool` | `bool` |
| `isHatRackDoll` | `0x492` | `bool` | `bool` |
| `isFirstFractalAfterImage` | `0x493` | `bool` | `bool` |
| `firstFractalAfterImageOpacity` | `0x494` | `float` | `float` |
| `setBonus` | `0x498` | `string` | `struct System_String_o*` |
| `inventory` | `0x4A0` | `Item[]` | `struct Terraria_Item_array*` |
| `inventoryChestStack` | `0x4A8` | `bool[]` | `struct System_Boolean_array*` |
| `lastVisualizedSelectedItem` | `0x4B0` | `Item` | `struct Terraria_Item_o*` |
| `bank` | `0x4B8` | `Chest` | `struct Terraria_Chest_o*` |
| `bank2` | `0x4C0` | `Chest` | `struct Terraria_Chest_o*` |
| `bank3` | `0x4C8` | `Chest` | `struct Terraria_Chest_o*` |
| `bank4` | `0x4D0` | `Chest` | `struct Terraria_Chest_o*` |
| `voidVaultInfo` | `0x4D8` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `headRotation` | `0x4DC` | `float` | `float` |
| `bodyRotation` | `0x4E0` | `float` | `float` |
| `legRotation` | `0x4E4` | `float` | `float` |
| `headPosition` | `0x4E8` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `bodyPosition` | `0x4F0` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `legPosition` | `0x4F8` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `headVelocity` | `0x500` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `bodyVelocity` | `0x508` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `legVelocity` | `0x510` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `fullRotation` | `0x518` | `float` | `float` |
| `fullRotationOrigin` | `0x51C` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `fartKartCloudDelay` | `0x524` | `int` | `int32_t` |
| `nonTorch` | `0x528` | `int` | `int32_t` |
| `gfxOffY` | `0x52C` | `float` | `float` |
| `stepSpeed` | `0x530` | `float` | `float` |
| `dead` | `0x534` | `bool` | `bool` |
| `respawnTimer` | `0x538` | `int` | `int32_t` |
| `lastTimePlayerWasSaved` | `0x540` | `long` | `int64_t` |
| `attackCD` | `0x548` | `int` | `int32_t` |
| `potionDelay` | `0x54C` | `int` | `int32_t` |
| `difficulty` | `0x550` | `byte` | `uint8_t` |
| `wetSlime` | `0x551` | `byte` | `uint8_t` |
| `hitTile` | `0x558` | `HitTile` | `struct Terraria_HitTile_o*` |
| `hitReplace` | `0x560` | `HitTile` | `struct Terraria_HitTile_o*` |
| `startJumpY` | `0x568` | `float` | `float` |
| `jumping` | `0x56C` | `bool` | `bool` |
| `jump` | `0x570` | `int` | `int32_t` |
| `head` | `0x574` | `int` | `int32_t` |
| `body` | `0x578` | `int` | `int32_t` |
| `legs` | `0x57C` | `int` | `int32_t` |
| `handon` | `0x580` | `sbyte` | `int8_t` |
| `handoff` | `0x581` | `sbyte` | `int8_t` |
| `back` | `0x582` | `sbyte` | `int8_t` |
| `front` | `0x583` | `sbyte` | `int8_t` |
| `shoe` | `0x584` | `sbyte` | `int8_t` |
| `waist` | `0x585` | `sbyte` | `int8_t` |
| `shield` | `0x586` | `sbyte` | `int8_t` |
| `neck` | `0x587` | `sbyte` | `int8_t` |
| `face` | `0x588` | `sbyte` | `int8_t` |
| `balloon` | `0x589` | `sbyte` | `int8_t` |
| `backpack` | `0x58A` | `sbyte` | `int8_t` |
| `tail` | `0x58B` | `sbyte` | `int8_t` |
| `faceHead` | `0x58C` | `sbyte` | `int8_t` |
| `faceFlower` | `0x58D` | `sbyte` | `int8_t` |
| `balloonFront` | `0x58E` | `sbyte` | `int8_t` |
| `beard` | `0x58F` | `sbyte` | `int8_t` |
| `hideVisibleAccessory` | `0x590` | `bool[]` | `struct System_Boolean_array*` |
| `hideMisc` | `0x598` | `BitsByte` | `struct Terraria_BitsByte_o` |
| `headFrame` | `0x59C` | `Rectangle` | `struct Microsoft_Xna_Framework_Rectangle_o` |
| `bodyFrame` | `0x5AC` | `Rectangle` | `struct Microsoft_Xna_Framework_Rectangle_o` |
| `legFrame` | `0x5BC` | `Rectangle` | `struct Microsoft_Xna_Framework_Rectangle_o` |
| `hairFrame` | `0x5CC` | `Rectangle` | `struct Microsoft_Xna_Framework_Rectangle_o` |
| `LocalInputCache` | `0x5DC` | `Player.DirectionalInputSyncCache` | `struct Terraria_Player_DirectionalInputSyncCache_o` |
| `blockUseItem` | `0x5E0` | `bool` | `bool` |
| `holdUpJump` | `0x5E1` | `bool` | `bool` |
| `rewsing` | `0x5E2` | `bool` | `bool` |
| `InitialItemUsing` | `0x5E4` | `short` | `int16_t` |
| `disableUseUntilRelease` | `0x5E6` | `bool` | `bool` |
| `controlLeft` | `0x5E7` | `bool` | `bool` |
| `controlRight` | `0x5E8` | `bool` | `bool` |
| `controlUp` | `0x5E9` | `bool` | `bool` |
| `controlDown` | `0x5EA` | `bool` | `bool` |
| `controlJump` | `0x5EB` | `bool` | `bool` |
| `controlUseItem` | `0x5EC` | `bool` | `bool` |
| `controlUseTile` | `0x5ED` | `bool` | `bool` |
| `controlThrow` | `0x5EE` | `bool` | `bool` |
| `controlInv` | `0x5EF` | `bool` | `bool` |
| `controlHook` | `0x5F0` | `bool` | `bool` |
| `controlTorch` | `0x5F1` | `bool` | `bool` |
| `controlMap` | `0x5F2` | `bool` | `bool` |
| `controlSmart` | `0x5F3` | `bool` | `bool` |
| `controlMount` | `0x5F4` | `bool` | `bool` |
| `releaseJump` | `0x5F5` | `bool` | `bool` |
| `releaseUp` | `0x5F6` | `bool` | `bool` |
| `releaseUseItem` | `0x5F7` | `bool` | `bool` |
| `releaseUseTile` | `0x5F8` | `bool` | `bool` |
| `releaseInventory` | `0x5F9` | `bool` | `bool` |
| `releaseHook` | `0x5FA` | `bool` | `bool` |
| `releaseThrow` | `0x5FB` | `bool` | `bool` |
| `releaseQuickMana` | `0x5FC` | `bool` | `bool` |
| `releaseQuickHeal` | `0x5FD` | `bool` | `bool` |
| `releaseLeft` | `0x5FE` | `bool` | `bool` |
| `releaseRight` | `0x5FF` | `bool` | `bool` |
| `releaseSmart` | `0x600` | `bool` | `bool` |
| `releaseMount` | `0x601` | `bool` | `bool` |
| `releaseDown` | `0x602` | `bool` | `bool` |
| `controlQuickMana` | `0x603` | `bool` | `bool` |
| `controlQuickHeal` | `0x604` | `bool` | `bool` |
| `controlCreativeMenu` | `0x605` | `bool` | `bool` |
| `releaseCreativeMenu` | `0x606` | `bool` | `bool` |
| `controlQuickBuff` | `0x607` | `bool` | `bool` |
| `controlFireRelease` | `0x608` | `bool` | `bool` |
| `controlFire` | `0x609` | `bool` | `bool` |
| `controlInteractionRelease` | `0x60A` | `bool` | `bool` |
| `controlInteraction` | `0x60B` | `bool` | `bool` |
| `controlLayout1` | `0x60C` | `bool` | `bool` |
| `controlLayout2` | `0x60D` | `bool` | `bool` |
| `controlLayout3` | `0x60E` | `bool` | `bool` |
| `controlLayoutCycle` | `0x60F` | `bool` | `bool` |
| `lockInteractionForFrame` | `0x610` | `int` | `int32_t` |
| `UserData` | `0x618` | `PlatformUser` | `struct PlatformUser_o` |
| `LocalUser` | `0x628` | `LocalUser` | `struct LocalUser_o*` |
| `tileInteractionHappened` | `0x630` | `bool` | `bool` |
| `tileInteractAttempted` | `0x631` | `bool` | `bool` |
| `controlDownHold` | `0x632` | `bool` | `bool` |
| `isOperatingAnotherEntity` | `0x633` | `bool` | `bool` |
| `autoReuseAllWeapons` | `0x634` | `bool` | `bool` |
| `isControlledByFilm` | `0x635` | `bool` | `bool` |
| `tryKeepingHoveringDown` | `0x636` | `bool` | `bool` |
| `tryKeepingHoveringUp` | `0x637` | `bool` | `bool` |
| `altFunctionUse` | `0x638` | `int` | `int32_t` |
| `mapZoomIn` | `0x63C` | `bool` | `bool` |
| `mapZoomOut` | `0x63D` | `bool` | `bool` |
| `mapAlphaUp` | `0x63E` | `bool` | `bool` |
| `mapAlphaDown` | `0x63F` | `bool` | `bool` |
| `mapFullScreen` | `0x640` | `bool` | `bool` |
| `mapStyle` | `0x641` | `bool` | `bool` |
| `releaseMapFullscreen` | `0x642` | `bool` | `bool` |
| `releaseMapStyle` | `0x643` | `bool` | `bool` |
| `leftTimer` | `0x644` | `int` | `int32_t` |
| `rightTimer` | `0x648` | `int` | `int32_t` |
| `delayUseItem` | `0x64C` | `bool` | `bool` |
| `cursorItemIconEnabled` | `0x64D` | `bool` | `bool` |
| `cursorItemIconReversed` | `0x64E` | `bool` | `bool` |
| `cursorItemIconID` | `0x650` | `int` | `int32_t` |
| `cursorItemIconPush` | `0x654` | `int` | `int32_t` |
| `cursorChestItemIconID` | `0x658` | `int` | `int32_t` |
| `cursorItemIconText` | `0x660` | `string` | `struct System_String_o*` |
| `runSoundDelay` | `0x668` | `int` | `int32_t` |
| `opacityForAnimation` | `0x66C` | `float` | `float` |
| `shadowPos` | `0x670` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `shadowRotation` | `0x678` | `float[]` | `struct System_Single_array*` |
| `shadowOrigin` | `0x680` | `Vector2[]` | `struct Microsoft_Xna_Framework_Vector2_array*` |
| `shadowDirection` | `0x688` | `int[]` | `struct System_Int32_array*` |
| `shadowCount` | `0x690` | `int` | `int32_t` |
| `manaCost` | `0x694` | `float` | `float` |
| `fireWalk` | `0x698` | `bool` | `bool` |
| `channel` | `0x699` | `bool` | `bool` |
| `step` | `0x69C` | `int` | `int32_t` |
| `_channelShotCache` | `0x6A0` | `Player.ChannelCancelKey` | `struct Terraria_Player_ChannelCancelKey_o` |
| `skipAnimatingValuesInPlayerFrame` | `0x6A8` | `bool` | `bool` |
| `rabbitOrderFrame` | `0x6AC` | `Player.RabbitOrderFrameHelper` | `struct Terraria_Player_RabbitOrderFrameHelper_o` |
| `creativeGodMode` | `0x6B8` | `bool` | `bool` |
| `availableAdvancedShadowsCount` | `0x6BC` | `int` | `int32_t` |
| `_advancedShadows` | `0x6C0` | `EntityShadowInfo[]` | `struct Terraria_DataStructures_EntityShadowInfo_array*` |
| `_lastAddedAvancedShadow` | `0x6C8` | `int` | `int32_t` |
| `compositeFrontArm` | `0x6CC` | `Player.CompositeArmData` | `struct Terraria_Player_CompositeArmData_o` |
| `compositeBackArm` | `0x6D8` | `Player.CompositeArmData` | `struct Terraria_Player_CompositeArmData_o` |
| `anglerQuestsFinished` | `0x6E4` | `int` | `int32_t` |
| `golferScoreAccumulated` | `0x6E8` | `int` | `int32_t` |
| `bartenderQuestLog` | `0x6EC` | `int` | `int32_t` |
| `downedDD2EventAnyDifficulty` | `0x6F0` | `bool` | `bool` |
| `armorPenetration` | `0x6F4` | `int` | `int32_t` |
| **`statDefense`** ⚔️ | **`0x6F8`** | `int` | `int32_t` |
| **`statLifeMax`** ⚔️ | **`0x6FC`** | `int` | `int32_t` |
| `statLifeMax2` | `0x700` | `int` | `int32_t` |
| **`statLife`** ⚔️ | **`0x704`** | `int` | `int32_t` |
| `statMana` | `0x708` | `int` | `int32_t` |
| `statManaMax` | `0x70C` | `int` | `int32_t` |
| `statManaMax2` | `0x710` | `int` | `int32_t` |
| `lifeRegen` | `0x714` | `int` | `int32_t` |
| `lifeRegenCount` | `0x718` | `int` | `int32_t` |
| `lifeRegenTime` | `0x71C` | `float` | `float` |
| `manaRegen` | `0x720` | `int` | `int32_t` |
| `manaRegenCount` | `0x724` | `int` | `int32_t` |
| `manaRegenDelay` | `0x728` | `float` | `float` |
| `manaRegenBuff` | `0x72C` | `bool` | `bool` |
| `noKnockback` | `0x72D` | `bool` | `bool` |
| `shimmerImmune` | `0x72E` | `bool` | `bool` |
| `spaceGun` | `0x72F` | `bool` | `bool` |
| `gravDir` | `0x730` | `float` | `float` |
| `chloroAmmoCost80` | `0x734` | `bool` | `bool` |
| `huntressAmmoCost90` | `0x735` | `bool` | `bool` |
| `ammoCost80` | `0x736` | `bool` | `bool` |
| `ammoCost75` | `0x737` | `bool` | `bool` |
| `stickyBreak` | `0x738` | `int` | `int32_t` |
| `magicQuiver` | `0x73C` | `bool` | `bool` |
| `magmaStone` | `0x73D` | `bool` | `bool` |
| `lavaRose` | `0x73E` | `bool` | `bool` |
| `hasMoltenQuiver` | `0x73F` | `bool` | `bool` |
| `phantasmTime` | `0x740` | `int` | `int32_t` |
| `ammoBox` | `0x744` | `bool` | `bool` |
| `ammoPotion` | `0x745` | `bool` | `bool` |
| `chaosState` | `0x746` | `bool` | `bool` |
| `strongBees` | `0x747` | `bool` | `bool` |
| `sporeSac` | `0x748` | `bool` | `bool` |
| `shinyStone` | `0x749` | `bool` | `bool` |
| `empressBrooch` | `0x74A` | `bool` | `bool` |
| `volatileGelatin` | `0x74B` | `bool` | `bool` |
| `volatileGelatinCounter` | `0x74C` | `int` | `int32_t` |
| `hasMagiluminescence` | `0x750` | `bool` | `bool` |
| `shadowArmor` | `0x751` | `bool` | `bool` |
| `dontStarveShader` | `0x752` | `bool` | `bool` |
| `eyebrellaCloud` | `0x753` | `bool` | `bool` |
| `yoraiz0rEye` | `0x754` | `int` | `int32_t` |
| `yoraiz0rDarkness` | `0x758` | `bool` | `bool` |
| `hasUnicornHorn` | `0x759` | `bool` | `bool` |
| `hasAngelHalo` | `0x75A` | `bool` | `bool` |
| `hasRainbowCursor` | `0x75B` | `bool` | `bool` |
| `leinforsHair` | `0x75C` | `bool` | `bool` |
| `stardustMonolithShader` | `0x75D` | `bool` | `bool` |
| `nebulaMonolithShader` | `0x75E` | `bool` | `bool` |
| `vortexMonolithShader` | `0x75F` | `bool` | `bool` |
| `solarMonolithShader` | `0x760` | `bool` | `bool` |
| `moonLordMonolithShader` | `0x761` | `bool` | `bool` |
| `bloodMoonMonolithShader` | `0x762` | `bool` | `bool` |
| `shimmerMonolithShader` | `0x763` | `bool` | `bool` |
| `overrideFishingBobber` | `0x764` | `int` | `int32_t` |
| `unlockedBiomeTorches` | `0x768` | `bool` | `bool` |
| `ateArtisanBread` | `0x769` | `bool` | `bool` |
| `unlockedSuperCart` | `0x76A` | `bool` | `bool` |
| `enabledSuperCart` | `0x76B` | `bool` | `bool` |
| `suspiciouslookingTentacle` | `0x76C` | `bool` | `bool` |
| `crimsonHeart` | `0x76D` | `bool` | `bool` |
| `lightOrb` | `0x76E` | `bool` | `bool` |
| `blueFairy` | `0x76F` | `bool` | `bool` |
| `redFairy` | `0x770` | `bool` | `bool` |
| `greenFairy` | `0x771` | `bool` | `bool` |
| `bunny` | `0x772` | `bool` | `bool` |
| `turtle` | `0x773` | `bool` | `bool` |
| `eater` | `0x774` | `bool` | `bool` |
| `penguin` | `0x775` | `bool` | `bool` |
| `HasGardenGnomeNearby` | `0x776` | `bool` | `bool` |
| `magicLantern` | `0x777` | `bool` | `bool` |
| `rabid` | `0x778` | `bool` | `bool` |
| `sunflower` | `0x779` | `bool` | `bool` |
| `wellFed` | `0x77A` | `bool` | `bool` |
| `puppy` | `0x77B` | `bool` | `bool` |
| `grinch` | `0x77C` | `bool` | `bool` |
| `miniMinotaur` | `0x77D` | `bool` | `bool` |
| `flowerBoots` | `0x77E` | `bool` | `bool` |
| `fairyBoots` | `0x77F` | `bool` | `bool` |
| `hellfireTreads` | `0x780` | `bool` | `bool` |
| `moonLordLegs` | `0x781` | `bool` | `bool` |
| `arcticDivingGear` | `0x782` | `bool` | `bool` |
| `coolWhipBuff` | `0x783` | `bool` | `bool` |
| `wearsRobe` | `0x784` | `bool` | `bool` |
| `minecartLeft` | `0x785` | `bool` | `bool` |
| `onWrongGround` | `0x786` | `bool` | `bool` |
| `onTrack` | `0x787` | `bool` | `bool` |
| `cartRampTime` | `0x788` | `int` | `int32_t` |
| `cartFlip` | `0x78C` | `bool` | `bool` |
| `trackBoost` | `0x790` | `float` | `float` |
| `lastBoost` | `0x794` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `mount` | `0x7A0` | `Mount` | `struct Terraria_Mount_o*` |
| `blackCat` | `0x7A8` | `bool` | `bool` |
| `spider` | `0x7A9` | `bool` | `bool` |
| `squashling` | `0x7AA` | `bool` | `bool` |
| `petFlagDD2Gato` | `0x7AB` | `bool` | `bool` |
| `petFlagDD2Ghost` | `0x7AC` | `bool` | `bool` |
| `petFlagDD2Dragon` | `0x7AD` | `bool` | `bool` |
| `petFlagUpbeatStar` | `0x7AE` | `bool` | `bool` |
| `petFlagSugarGlider` | `0x7AF` | `bool` | `bool` |
| `petFlagBabyShark` | `0x7B0` | `bool` | `bool` |
| `petFlagLilHarpy` | `0x7B1` | `bool` | `bool` |
| `petFlagFennecFox` | `0x7B2` | `bool` | `bool` |
| `petFlagGlitteryButterfly` | `0x7B3` | `bool` | `bool` |
| `petFlagBabyImp` | `0x7B4` | `bool` | `bool` |
| `petFlagBabyRedPanda` | `0x7B5` | `bool` | `bool` |
| `petFlagPlantero` | `0x7B6` | `bool` | `bool` |
| `petFlagDynamiteKitten` | `0x7B7` | `bool` | `bool` |
| `petFlagBabyWerewolf` | `0x7B8` | `bool` | `bool` |
| `petFlagShadowMimic` | `0x7B9` | `bool` | `bool` |
| `petFlagVoltBunny` | `0x7BA` | `bool` | `bool` |
| `petFlagKingSlimePet` | `0x7BB` | `bool` | `bool` |
| `petFlagEyeOfCthulhuPet` | `0x7BC` | `bool` | `bool` |
| `petFlagEaterOfWorldsPet` | `0x7BD` | `bool` | `bool` |
| `petFlagBrainOfCthulhuPet` | `0x7BE` | `bool` | `bool` |
| `petFlagSkeletronPet` | `0x7BF` | `bool` | `bool` |
| `petFlagQueenBeePet` | `0x7C0` | `bool` | `bool` |
| `petFlagDestroyerPet` | `0x7C1` | `bool` | `bool` |
| `petFlagTwinsPet` | `0x7C2` | `bool` | `bool` |
| `petFlagSkeletronPrimePet` | `0x7C3` | `bool` | `bool` |
| `petFlagPlanteraPet` | `0x7C4` | `bool` | `bool` |
| `petFlagGolemPet` | `0x7C5` | `bool` | `bool` |
| `petFlagDukeFishronPet` | `0x7C6` | `bool` | `bool` |
| `petFlagLunaticCultistPet` | `0x7C7` | `bool` | `bool` |
| `petFlagMoonLordPet` | `0x7C8` | `bool` | `bool` |
| `petFlagFairyQueenPet` | `0x7C9` | `bool` | `bool` |
| `petFlagPumpkingPet` | `0x7CA` | `bool` | `bool` |
| `petFlagEverscreamPet` | `0x7CB` | `bool` | `bool` |
| `petFlagIceQueenPet` | `0x7CC` | `bool` | `bool` |
| `petFlagMartianPet` | `0x7CD` | `bool` | `bool` |
| `petFlagDD2OgrePet` | `0x7CE` | `bool` | `bool` |
| `petFlagDD2BetsyPet` | `0x7CF` | `bool` | `bool` |
| `petFlagQueenSlimePet` | `0x7D0` | `bool` | `bool` |
| `petFlagBerniePet` | `0x7D1` | `bool` | `bool` |
| `petFlagGlommerPet` | `0x7D2` | `bool` | `bool` |
| `petFlagDeerclopsPet` | `0x7D3` | `bool` | `bool` |
| `petFlagPigPet` | `0x7D4` | `bool` | `bool` |
| `petFlagChesterPet` | `0x7D5` | `bool` | `bool` |
| `petFlagJunimoPet` | `0x7D6` | `bool` | `bool` |
| `petFlagBlueChickenPet` | `0x7D7` | `bool` | `bool` |
| `petFlagSpiffo` | `0x7D8` | `bool` | `bool` |
| `petFlagCaveling` | `0x7D9` | `bool` | `bool` |
| `petFlagDirtiestBlock` | `0x7DA` | `bool` | `bool` |
| `companionCube` | `0x7DB` | `bool` | `bool` |
| `babyFaceMonster` | `0x7DC` | `bool` | `bool` |
| `magicCuffs` | `0x7DD` | `bool` | `bool` |
| `coldDash` | `0x7DE` | `bool` | `bool` |
| `sailDash` | `0x7DF` | `bool` | `bool` |
| `desertDash` | `0x7E0` | `bool` | `bool` |
| `desertBoots` | `0x7E1` | `bool` | `bool` |
| `eyeSpring` | `0x7E2` | `bool` | `bool` |
| `snowman` | `0x7E3` | `bool` | `bool` |
| `scope` | `0x7E4` | `bool` | `bool` |
| `dino` | `0x7E5` | `bool` | `bool` |
| `skeletron` | `0x7E6` | `bool` | `bool` |
| `hornet` | `0x7E7` | `bool` | `bool` |
| `zephyrfish` | `0x7E8` | `bool` | `bool` |
| `tiki` | `0x7E9` | `bool` | `bool` |
| `parrot` | `0x7EA` | `bool` | `bool` |
| `truffle` | `0x7EB` | `bool` | `bool` |
| `sapling` | `0x7EC` | `bool` | `bool` |
| `cSapling` | `0x7ED` | `bool` | `bool` |
| `wisp` | `0x7EE` | `bool` | `bool` |
| `lizard` | `0x7EF` | `bool` | `bool` |
| `archery` | `0x7F0` | `bool` | `bool` |
| `poisoned` | `0x7F1` | `bool` | `bool` |
| `venom` | `0x7F2` | `bool` | `bool` |
| `blind` | `0x7F3` | `bool` | `bool` |
| `blackout` | `0x7F4` | `bool` | `bool` |
| `headcovered` | `0x7F5` | `bool` | `bool` |
| `frostBurn` | `0x7F6` | `bool` | `bool` |
| `onFrostBurn` | `0x7F7` | `bool` | `bool` |
| `onFrostBurn2` | `0x7F8` | `bool` | `bool` |
| `burned` | `0x7F9` | `bool` | `bool` |
| `shimmering` | `0x7FA` | `bool` | `bool` |
| `timeShimmering` | `0x7FC` | `int` | `int32_t` |
| `shimmerTransparency` | `0x800` | `float` | `float` |
| `shimmerUnstuckHelper` | `0x804` | `ShimmerUnstuckHelper` | `struct Terraria_GameContent_ShimmerUnstuckHelper_o` |
| `suffocating` | `0x80C` | `bool` | `bool` |
| `suffocateDelay` | `0x80D` | `byte` | `uint8_t` |
| `dripping` | `0x80E` | `bool` | `bool` |
| `drippingSlime` | `0x80F` | `bool` | `bool` |
| `drippingSparkleSlime` | `0x810` | `bool` | `bool` |
| `onFire` | `0x811` | `bool` | `bool` |
| `onFire2` | `0x812` | `bool` | `bool` |
| `onFire3` | `0x813` | `bool` | `bool` |
| `noItems` | `0x814` | `bool` | `bool` |
| `cursed` | `0x815` | `bool` | `bool` |
| `hungry` | `0x816` | `bool` | `bool` |
| `starving` | `0x817` | `bool` | `bool` |
| `heartyMeal` | `0x818` | `bool` | `bool` |
| `windPushed` | `0x819` | `bool` | `bool` |
| `wereWolf` | `0x81A` | `bool` | `bool` |
| `wolfAcc` | `0x81B` | `bool` | `bool` |
| `hideMerman` | `0x81C` | `bool` | `bool` |
| `hideWolf` | `0x81D` | `bool` | `bool` |
| `forceMerman` | `0x81E` | `bool` | `bool` |
| `forceWerewolf` | `0x81F` | `bool` | `bool` |
| `rulerGrid` | `0x820` | `bool` | `bool` |
| `rulerLine` | `0x821` | `bool` | `bool` |
| `bleed` | `0x822` | `bool` | `bool` |
| `confused` | `0x823` | `bool` | `bool` |
| `accMerman` | `0x824` | `bool` | `bool` |
| `merman` | `0x825` | `bool` | `bool` |
| `trident` | `0x826` | `bool` | `bool` |
| `brokenArmor` | `0x827` | `bool` | `bool` |
| `silence` | `0x828` | `bool` | `bool` |
| `slow` | `0x829` | `bool` | `bool` |
| `gross` | `0x82A` | `bool` | `bool` |
| `tongued` | `0x82B` | `bool` | `bool` |
| `kbGlove` | `0x82C` | `bool` | `bool` |
| `autoReuseGlove` | `0x82D` | `bool` | `bool` |
| `meleeScaleGlove` | `0x82E` | `bool` | `bool` |
| `kbBuff` | `0x82F` | `bool` | `bool` |
| `remoteVisionForDrone` | `0x830` | `bool` | `bool` |
| `starCloakItem` | `0x838` | `Item` | `struct Terraria_Item_o*` |
| `starCloakItem_manaCloakOverrideItem` | `0x840` | `Item` | `struct Terraria_Item_o*` |
| `starCloakItem_starVeilOverrideItem` | `0x848` | `Item` | `struct Terraria_Item_o*` |
| `starCloakItem_beeCloakOverrideItem` | `0x850` | `Item` | `struct Terraria_Item_o*` |
| `longInvince` | `0x858` | `bool` | `bool` |
| `pStone` | `0x859` | `bool` | `bool` |
| `manaFlower` | `0x85A` | `bool` | `bool` |
| `moonLeech` | `0x85B` | `bool` | `bool` |
| `vortexDebuff` | `0x85C` | `bool` | `bool` |
| `trapDebuffSource` | `0x85D` | `bool` | `bool` |
| `witheredArmor` | `0x85E` | `bool` | `bool` |
| `witheredWeapon` | `0x85F` | `bool` | `bool` |
| `slowOgreSpit` | `0x860` | `bool` | `bool` |
| `parryDamageBuff` | `0x861` | `bool` | `bool` |
| `ballistaPanic` | `0x862` | `bool` | `bool` |
| `JustDroppedAnItem` | `0x863` | `bool` | `bool` |
| `meleeCrit` | `0x864` | `int` | `int32_t` |
| `magicCrit` | `0x868` | `int` | `int32_t` |
| `rangedCrit` | `0x86C` | `int` | `int32_t` |
| `meleeDamage` | `0x870` | `float` | `float` |
| `magicDamage` | `0x874` | `float` | `float` |
| `rangedDamage` | `0x878` | `float` | `float` |
| `rangedMultDamage` | `0x87C` | `float` | `float` |
| `arrowDamageAdditiveStack` | `0x880` | `float` | `float` |
| `arrowDamage` | `0x884` | `float` | `float` |
| `bulletDamage` | `0x888` | `float` | `float` |
| `rocketDamage` | `0x88C` | `float` | `float` |
| `minionDamage` | `0x890` | `float` | `float` |
| `minionKB` | `0x894` | `float` | `float` |
| `meleeSpeed` | `0x898` | `float` | `float` |
| `summonerWeaponSpeedBonus` | `0x89C` | `float` | `float` |
| `moveSpeed` | `0x8A0` | `float` | `float` |
| `pickSpeed` | `0x8A4` | `float` | `float` |
| `wallSpeed` | `0x8A8` | `float` | `float` |
| `tileSpeed` | `0x8AC` | `float` | `float` |
| `autoPaint` | `0x8B0` | `bool` | `bool` |
| `autoActuator` | `0x8B1` | `bool` | `bool` |
| `SpawnX` | `0x8B4` | `int` | `int32_t` |
| `SpawnY` | `0x8B8` | `int` | `int32_t` |
| `PotionOfReturnOriginalUsePosition` | `0x8BC` | `Nullable<Vector2>` | `struct System_Nullable_Vector2__o` |
| `PotionOfReturnHomePosition` | `0x8C8` | `Nullable<Vector2>` | `struct System_Nullable_Vector2__o` |
| `spX` | `0x8D8` | `int[]` | `struct System_Int32_array*` |
| `spY` | `0x8E0` | `int[]` | `struct System_Int32_array*` |
| `spN` | `0x8E8` | `string[]` | `struct System_String_array*` |
| `spI` | `0x8F0` | `int[]` | `struct System_Int32_array*` |
| `lastTileRangeX` | `0x8F8` | `int` | `int32_t` |
| `lastTileRangeY` | `0x8FC` | `int` | `int32_t` |
| `gravity` | `0x900` | `float` | `float` |
| `maxFallSpeed` | `0x904` | `float` | `float` |
| `maxRunSpeed` | `0x908` | `float` | `float` |
| `runAcceleration` | `0x90C` | `float` | `float` |
| `runSlowdown` | `0x910` | `float` | `float` |
| `TotalRunTime` | `0x914` | `int` | `int32_t` |
| `adjWater` | `0x918` | `bool` | `bool` |
| `adjHoney` | `0x919` | `bool` | `bool` |
| `adjLava` | `0x91A` | `bool` | `bool` |
| `oldAdjWater` | `0x91B` | `bool` | `bool` |
| `oldAdjHoney` | `0x91C` | `bool` | `bool` |
| `oldAdjLava` | `0x91D` | `bool` | `bool` |
| `adjTile` | `0x920` | `bool[]` | `struct System_Boolean_array*` |
| `oldAdjTile` | `0x928` | `bool[]` | `struct System_Boolean_array*` |
| `hairDye` | `0x930` | `byte` | `uint8_t` |
| `hairDyeColor` | `0x931` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `hairDyeVar` | `0x938` | `float` | `float` |
| `skinDyePacked` | `0x93C` | `int` | `int32_t` |
| `hairColor` | `0x940` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `skinColor` | `0x944` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `eyeColor` | `0x948` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `shirtColor` | `0x94C` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `underShirtColor` | `0x950` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `pantsColor` | `0x954` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `shoeColor` | `0x958` | `Color` | `struct Microsoft_Xna_Framework_Graphics_Color_o` |
| `hair` | `0x95C` | `int` | `int32_t` |
| `hostile` | `0x960` | `bool` | `bool` |
| `hermesStepSound` | `0x968` | `SoundPlaySet` | `struct Terraria_DataStructures_SoundPlaySet_o*` |
| `instantMovementAccumulatedThisFrame` | `0x970` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `accCompass` | `0x978` | `int` | `int32_t` |
| `accWatch` | `0x97C` | `int` | `int32_t` |
| `accDepthMeter` | `0x980` | `int` | `int32_t` |
| `accFishFinder` | `0x984` | `bool` | `bool` |
| `accWeatherRadio` | `0x985` | `bool` | `bool` |
| `accJarOfSouls` | `0x986` | `bool` | `bool` |
| `accCalendar` | `0x987` | `bool` | `bool` |
| `lastCreatureHit` | `0x988` | `int` | `int32_t` |
| `accThirdEye` | `0x98C` | `bool` | `bool` |
| `accThirdEyeCounter` | `0x98D` | `byte` | `uint8_t` |
| `accThirdEyeNumber` | `0x98E` | `byte` | `uint8_t` |
| `accStopwatch` | `0x98F` | `bool` | `bool` |
| `accOreFinder` | `0x990` | `bool` | `bool` |
| `accCritterGuide` | `0x991` | `bool` | `bool` |
| `accCritterGuideCounter` | `0x992` | `byte` | `uint8_t` |
| `accCritterGuideNumber` | `0x993` | `byte` | `uint8_t` |
| `accDreamCatcher` | `0x994` | `bool` | `bool` |
| `hasFootball` | `0x995` | `bool` | `bool` |
| `drawingFootball` | `0x996` | `bool` | `bool` |
| `ActuationRodLock` | `0x997` | `bool` | `bool` |
| `ActuationRodLockSetting` | `0x998` | `bool` | `bool` |
| `InfoAccMechShowWires` | `0x999` | `bool` | `bool` |
| `dpsStart` | `0x9A0` | `DateTime` | `struct System_DateTime_o` |
| `dpsEnd` | `0x9A8` | `DateTime` | `struct System_DateTime_o` |
| `dpsLastHit` | `0x9B0` | `DateTime` | `struct System_DateTime_o` |
| `dpsDamage` | `0x9B8` | `int` | `int32_t` |
| `dpsStarted` | `0x9BC` | `bool` | `bool` |
| `displayedFishingInfo` | `0x9C0` | `string` | `struct System_String_o*` |
| `discountEquipped` | `0x9C8` | `bool` | `bool` |
| `discountAvailable` | `0x9C9` | `bool` | `bool` |
| `hasLuckyCoin` | `0x9CA` | `bool` | `bool` |
| `boneGloveItem` | `0x9D0` | `Item` | `struct Terraria_Item_o*` |
| `goldRing` | `0x9D8` | `bool` | `bool` |
| `accDivingHelm` | `0x9D9` | `bool` | `bool` |
| `accFlipper` | `0x9DA` | `bool` | `bool` |
| `hasLuck_LuckyCoin` | `0x9DB` | `bool` | `bool` |
| `hasLuck_LuckyHorseshoe` | `0x9DC` | `bool` | `bool` |
| `doubleJumpsConsumed` | `0x9E0` | `int` | `int32_t` |
| `hasJumpOption_Cloud` | `0x9E4` | `bool` | `bool` |
| `canJumpAgain_Cloud` | `0x9E5` | `bool` | `bool` |
| `isPerformingJump_Cloud` | `0x9E6` | `bool` | `bool` |
| `hasJumpOption_Sandstorm` | `0x9E7` | `bool` | `bool` |
| `canJumpAgain_Sandstorm` | `0x9E8` | `bool` | `bool` |
| `isPerformingJump_Sandstorm` | `0x9E9` | `bool` | `bool` |
| `hasJumpOption_Blizzard` | `0x9EA` | `bool` | `bool` |
| `canJumpAgain_Blizzard` | `0x9EB` | `bool` | `bool` |
| `isPerformingJump_Blizzard` | `0x9EC` | `bool` | `bool` |
| `hasJumpOption_Fart` | `0x9ED` | `bool` | `bool` |
| `canJumpAgain_Fart` | `0x9EE` | `bool` | `bool` |
| `isPerformingJump_Fart` | `0x9EF` | `bool` | `bool` |
| `hasJumpOption_Sail` | `0x9F0` | `bool` | `bool` |
| `canJumpAgain_Sail` | `0x9F1` | `bool` | `bool` |
| `isPerformingJump_Sail` | `0x9F2` | `bool` | `bool` |
| `hasJumpOption_Unicorn` | `0x9F3` | `bool` | `bool` |
| `canJumpAgain_Unicorn` | `0x9F4` | `bool` | `bool` |
| `isPerformingJump_Unicorn` | `0x9F5` | `bool` | `bool` |
| `hasJumpOption_Santank` | `0x9F6` | `bool` | `bool` |
| `canJumpAgain_Santank` | `0x9F7` | `bool` | `bool` |
| `isPerformingJump_Santank` | `0x9F8` | `bool` | `bool` |
| `hasJumpOption_WallOfFleshGoat` | `0x9F9` | `bool` | `bool` |
| `canJumpAgain_WallOfFleshGoat` | `0x9FA` | `bool` | `bool` |
| `isPerformingJump_WallOfFleshGoat` | `0x9FB` | `bool` | `bool` |
| `hasJumpOption_Basilisk` | `0x9FC` | `bool` | `bool` |
| `canJumpAgain_Basilisk` | `0x9FD` | `bool` | `bool` |
| `isPerformingJump_Basilisk` | `0x9FE` | `bool` | `bool` |
| `isPerformingPogostickTricks` | `0x9FF` | `bool` | `bool` |
| `autoJump` | `0xA00` | `bool` | `bool` |
| `justJumped` | `0xA01` | `bool` | `bool` |
| `jumpSpeedBoost` | `0xA04` | `float` | `float` |
| `extraFall` | `0xA08` | `int` | `int32_t` |
| `FractionalDistanceTraveled` | `0xA0C` | `float` | `float` |
| `canFloatInWater` | `0xA10` | `bool` | `bool` |
| `hasFloatingTube` | `0xA11` | `bool` | `bool` |
| `frogLegJumpBoost` | `0xA12` | `bool` | `bool` |
| `skyStoneEffects` | `0xA13` | `bool` | `bool` |
| `spawnMax` | `0xA14` | `bool` | `bool` |
| `blockRange` | `0xA18` | `int` | `int32_t` |
| `grappling` | `0xA20` | `int[]` | `struct System_Int32_array*` |
| `grapCount` | `0xA28` | `int` | `int32_t` |
| `rocketTime` | `0xA2C` | `int` | `int32_t` |
| `rocketTimeMax` | `0xA30` | `int` | `int32_t` |
| `rocketDelay` | `0xA34` | `int` | `int32_t` |
| `rocketDelay2` | `0xA38` | `int` | `int32_t` |
| `rocketSoundDelay` | `0xA3C` | `int` | `int32_t` |
| `rocketRelease` | `0xA40` | `bool` | `bool` |
| `rocketFrame` | `0xA41` | `bool` | `bool` |
| `rocketBoots` | `0xA44` | `int` | `int32_t` |
| `vanityRocketBoots` | `0xA48` | `int` | `int32_t` |
| `canRocket` | `0xA4C` | `bool` | `bool` |
| `jumpBoost` | `0xA4D` | `bool` | `bool` |
| `noFallDmg` | `0xA4E` | `bool` | `bool` |
| `swimTime` | `0xA50` | `int` | `int32_t` |
| `killGuide` | `0xA54` | `bool` | `bool` |
| `killClothier` | `0xA55` | `bool` | `bool` |
| `equipmentBasedLuckBonus` | `0xA58` | `float` | `float` |
| `lastEquipmentBasedLuckBonus` | `0xA5C` | `float` | `float` |
| `hasCreditsSceneMusicBox` | `0xA60` | `bool` | `bool` |
| `lavaImmune` | `0xA61` | `bool` | `bool` |
| `gills` | `0xA62` | `bool` | `bool` |
| `slowFall` | `0xA63` | `bool` | `bool` |
| `findTreasure` | `0xA64` | `bool` | `bool` |
| `biomeSight` | `0xA65` | `bool` | `bool` |
| `invis` | `0xA66` | `bool` | `bool` |
| `detectCreature` | `0xA67` | `bool` | `bool` |
| `nightVision` | `0xA68` | `bool` | `bool` |
| `enemySpawns` | `0xA69` | `bool` | `bool` |
| `thorns` | `0xA6C` | `float` | `float` |
| `turtleArmor` | `0xA70` | `bool` | `bool` |
| `turtleThorns` | `0xA71` | `bool` | `bool` |
| `cactusThorns` | `0xA72` | `bool` | `bool` |
| `spiderArmor` | `0xA73` | `bool` | `bool` |
| `anglerSetSpawnReduction` | `0xA74` | `bool` | `bool` |
| `CanSeeInvisibleBlocks` | `0xA75` | `bool` | `bool` |
| `setSolar` | `0xA76` | `bool` | `bool` |
| `setVortex` | `0xA77` | `bool` | `bool` |
| `setNebula` | `0xA78` | `bool` | `bool` |
| `nebulaCD` | `0xA7C` | `int` | `int32_t` |
| `setStardust` | `0xA80` | `bool` | `bool` |
| `setForbidden` | `0xA81` | `bool` | `bool` |
| `setForbiddenCooldownLocked` | `0xA82` | `bool` | `bool` |
| `setSquireT3` | `0xA83` | `bool` | `bool` |
| `setHuntressT3` | `0xA84` | `bool` | `bool` |
| `setApprenticeT3` | `0xA85` | `bool` | `bool` |
| `setMonkT3` | `0xA86` | `bool` | `bool` |
| `setSquireT2` | `0xA87` | `bool` | `bool` |
| `setHuntressT2` | `0xA88` | `bool` | `bool` |
| `setApprenticeT2` | `0xA89` | `bool` | `bool` |
| `setMonkT2` | `0xA8A` | `bool` | `bool` |
| `maxTurrets` | `0xA8C` | `int` | `int32_t` |
| `maxTurretsOld` | `0xA90` | `int` | `int32_t` |
| `vortexStealthActive` | `0xA94` | `bool` | `bool` |
| `waterWalk` | `0xA95` | `bool` | `bool` |
| `waterWalk2` | `0xA96` | `bool` | `bool` |
| `forcedGravity` | `0xA98` | `int` | `int32_t` |
| `gravControl` | `0xA9C` | `bool` | `bool` |
| `gravControl2` | `0xA9D` | `bool` | `bool` |
| `honeyCombItem` | `0xAA0` | `Item` | `struct Terraria_Item_o*` |
| `wireOperationsCooldown` | `0xAA8` | `int` | `int32_t` |
| `lastChest` | `0xAAC` | `int` | `int32_t` |
| `piggyBankProjTracker` | `0xAB0` | `TrackedProjectileReference` | `struct Terraria_DataStructures_TrackedProjectileReference_o` |
| `interactableProjectiles` | `0xAC8` | `List<int>` | `struct System_Collections_Generic_List_int__o*` |
| `voidLensChest` | `0xAD0` | `TrackedProjectileReference` | `struct Terraria_DataStructures_TrackedProjectileReference_o` |
| `chest` | `0xAE4` | `int` | `int32_t` |
| `chestX` | `0xAE8` | `int` | `int32_t` |
| `chestY` | `0xAEC` | `int` | `int32_t` |
| `ToHellAndBackStatus` | `0xAF0` | `Player.eToHellAndBackStatus` | `int32_t` |
| `talkNPC` | `0xAF4` | `int` | `int32_t` |
| `fallStart` | `0xAF8` | `int` | `int32_t` |
| `fallStart2` | `0xAFC` | `int` | `int32_t` |
| `potionDelayTime` | `0xB00` | `int` | `int32_t` |
| `restorationDelayTime` | `0xB04` | `int` | `int32_t` |
| `mushroomDelayTime` | `0xB08` | `int` | `int32_t` |
| `_batbatCanHeal` | `0xB0C` | `bool` | `bool` |
| `_spawnTentacleSpikes` | `0xB0D` | `bool` | `bool` |
| `_spawnBloodButcherer` | `0xB0E` | `bool` | `bool` |
| `_spawnVolcanoExplosion` | `0xB0F` | `bool` | `bool` |
| `_spawnMuramasaCut` | `0xB10` | `bool` | `bool` |
| `isPettingAnimal` | `0xB11` | `bool` | `bool` |
| `isTheAnimalBeingPetSmall` | `0xB12` | `bool` | `bool` |
| `sitting` | `0xB14` | `PlayerSittingHelper` | `struct Terraria_GameContent_PlayerSittingHelper_o` |
| `sleeping` | `0xB24` | `PlayerSleepingHelper` | `struct Terraria_GameContent_PlayerSleepingHelper_o` |
| `eyeHelper` | `0xB38` | `PlayerEyeHelper` | `struct Terraria_GameContent_PlayerEyeHelper_o` |
| `tileEntityAnchor` | `0xB44` | `PlayerInteractionAnchor` | `struct Terraria_DataStructures_PlayerInteractionAnchor_o` |
| `doorHelper` | `0xB50` | `DoorOpeningHelper` | `struct Terraria_GameContent_DoorOpeningHelper_o*` |
| `currentShoppingSettings` | `0xB58` | `ShoppingSettings` | `struct Terraria_ShoppingSettings_o` |
| `cHead` | `0xB68` | `int` | `int32_t` |
| `cBody` | `0xB6C` | `int` | `int32_t` |
| `cLegs` | `0xB70` | `int` | `int32_t` |
| `cHandOn` | `0xB74` | `int` | `int32_t` |
| `cHandOff` | `0xB78` | `int` | `int32_t` |
| `cBack` | `0xB7C` | `int` | `int32_t` |
| `cFront` | `0xB80` | `int` | `int32_t` |
| `cShoe` | `0xB84` | `int` | `int32_t` |
| `cWaist` | `0xB88` | `int` | `int32_t` |
| `cShield` | `0xB8C` | `int` | `int32_t` |
| `cNeck` | `0xB90` | `int` | `int32_t` |
| `cFace` | `0xB94` | `int` | `int32_t` |
| `cFaceHead` | `0xB98` | `int` | `int32_t` |
| `cFaceFlower` | `0xB9C` | `int` | `int32_t` |
| `cBalloon` | `0xBA0` | `int` | `int32_t` |
| `cBalloonFront` | `0xBA4` | `int` | `int32_t` |
| `cWings` | `0xBA8` | `int` | `int32_t` |
| `cCarpet` | `0xBAC` | `int` | `int32_t` |
| `cFloatingTube` | `0xBB0` | `int` | `int32_t` |
| `cBackpack` | `0xBB4` | `int` | `int32_t` |
| `cTail` | `0xBB8` | `int` | `int32_t` |
| `cShieldFallback` | `0xBBC` | `int` | `int32_t` |
| `cGrapple` | `0xBC0` | `int` | `int32_t` |
| `cMount` | `0xBC4` | `int` | `int32_t` |
| `cMinecart` | `0xBC8` | `int` | `int32_t` |
| `cPet` | `0xBCC` | `int` | `int32_t` |
| `cLight` | `0xBD0` | `int` | `int32_t` |
| `cYorai` | `0xBD4` | `int` | `int32_t` |
| `cPortableStool` | `0xBD8` | `int` | `int32_t` |
| `cUnicornHorn` | `0xBDC` | `int` | `int32_t` |
| `cAngelHalo` | `0xBE0` | `int` | `int32_t` |
| `cBeard` | `0xBE4` | `int` | `int32_t` |
| `cMinion` | `0xBE8` | `int` | `int32_t` |
| `cLeinShampoo` | `0xBEC` | `int` | `int32_t` |
| `cFlameWaker` | `0xBF0` | `int` | `int32_t` |
| `ownedProjectileCounts` | `0xBF8` | `int[]` | `struct System_Int32_array*` |
| `npcTypeNoAggro` | `0xC00` | `bool[]` | `struct System_Boolean_array*` |
| `lastPortalColorIndex` | `0xC08` | `int` | `int32_t` |
| `_portalPhysicsTime` | `0xC0C` | `int` | `int32_t` |
| `portalPhysicsFlag` | `0xC10` | `bool` | `bool` |
| `lastTeleportPylonStyleUsed` | `0xC14` | `int` | `int32_t` |
| `MountFishronSpecialCounter` | `0xC18` | `float` | `float` |
| `MinionRestTargetPoint` | `0xC1C` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `MinionAttackTargetNPC` | `0xC24` | `int` | `int32_t` |
| `TouchedTiles` | `0xC28` | `List<Point>` | `struct System_Collections_Generic_List_Point__o*` |
| `itemAnimation` | `0xC30` | `int` | `int32_t` |
| `itemAnimationMax` | `0xC34` | `int` | `int32_t` |
| `itemTime` | `0xC38` | `int` | `int32_t` |
| `itemTimeMax` | `0xC3C` | `int` | `int32_t` |
| `toolTime` | `0xC40` | `int` | `int32_t` |
| `_blackListedTileCoordsForGrappling` | `0xC48` | `HashSet<Point>` | `struct System_Collections_Generic_HashSet_Point__o*` |
| `makeStrongBee` | `0xC50` | `bool` | `bool` |
| `equippedAnyTileRangeAcc` | `0xC51` | `bool` | `bool` |
| `equippedAnyTileSpeedAcc` | `0xC52` | `bool` | `bool` |
| `equippedAnyWallSpeedAcc` | `0xC53` | `bool` | `bool` |
| `behindBackWall` | `0xC54` | `bool` | `bool` |
| `_funkytownAchievementCheckCooldown` | `0xC58` | `int` | `int32_t` |
| `_stormShaderObstruction` | `0xC5C` | `float` | `float` |
| `_shaderObstructionInternalValue` | `0xC60` | `float` | `float` |
| `graveImmediateTime` | `0xC64` | `int` | `int32_t` |
| `_deerclopsBlizzardSmoothedEffect` | `0xC68` | `float` | `float` |
| `_lastSmartCursorToolStrategy` | `0xC6C` | `int` | `int32_t` |
| `nearbyTorch` | `0xC70` | `bool[]` | `struct System_Boolean_array*` |
| `dryCoralTorch` | `0xC78` | `bool` | `bool` |
| `luckyTorchCounter` | `0xC7C` | `int` | `int32_t` |
| `nearbyTorches` | `0xC80` | `int` | `int32_t` |
| `torchLuck` | `0xC84` | `float` | `float` |
| `_nextTorchLuckCheckCenter` | `0xC88` | `Vector2` | `struct Microsoft_Xna_Framework_Vector2_o` |
| `happyFunTorchTime` | `0xC90` | `bool` | `bool` |
| `torchFunTimer` | `0xC94` | `int` | `int32_t` |
| `torchGodCooldown` | `0xC98` | `int` | `int32_t` |
| `numberOfTorchAttacksMade` | `0xC9C` | `int` | `int32_t` |
| `unlitTorchX` | `0xCA0` | `int[]` | `struct System_Int32_array*` |
| `unlitTorchY` | `0xCA8` | `int[]` | `struct System_Int32_array*` |
| `ladyBugLuckTimeLeft` | `0xCB0` | `int` | `int32_t` |
| `luck` | `0xCB4` | `float` | `float` |
| `luckMinimumCap` | `0xCB8` | `float` | `float` |
| `luckMaximumCap` | `0xCBC` | `float` | `float` |
| `coinLuck` | `0xCC0` | `float` | `float` |
| `luckNeedsSync` | `0xCC4` | `bool` | `bool` |
| `disableVoidBag` | `0xCC8` | `int` | `int32_t` |
| `_quickGrappleCooldown` | `0xCCC` | `int` | `int32_t` |
| `movementAbilitiesCache` | `0xCD0` | `PlayerMovementAccsCache` | `struct Terraria_DataStructures_PlayerMovementAccsCache_o` |
| `wasScopeActionActive` | `0xCEC` | `bool` | `bool` |
| `wingAccRunSpeed` | `0xCF0` | `float` | `float` |
| `wingRunAccelerationMult` | `0xCF4` | `float` | `float` |
| `hasRaisableShield` | `0xCF8` | `bool` | `bool` |
| `shieldRaised` | `0xCF9` | `bool` | `bool` |
| `shieldParryTimeLeft` | `0xCFC` | `int` | `int32_t` |
| `shield_parry_cooldown` | `0xD00` | `int` | `int32_t` |
| `_forceForwardCursor` | `0xD04` | `bool` | `bool` |
| `_inputMouseCoordsForward` | `0xD08` | `Point` | `struct Microsoft_Xna_Framework_Point_o` |
| `_mainMouseCoordsForward` | `0xD10` | `Point` | `struct Microsoft_Xna_Framework_Point_o` |
| `_forceSmartSelectCursor` | `0xD18` | `bool` | `bool` |
| `_inputMouseCoordsSmartSelect` | `0xD1C` | `Point` | `struct Microsoft_Xna_Framework_Point_o` |
| `_mainMouseCoordsSmartSelect` | `0xD24` | `Point` | `struct Microsoft_Xna_Framework_Point_o` |
| `_tileTargetSmartSelect` | `0xD2C` | `Point` | `struct Microsoft_Xna_Framework_Point_o` |
| `botherWithUnaimedMinecartTracks` | `0xD34` | `bool` | `bool` |
| `_projectilesToInteractWith` | `0xD38` | `List<int>` | `struct System_Collections_Generic_List_int__o*` |
| `_lockTileInteractionsTimer` | `0xD40` | `int` | `int32_t` |
| `hurtCooldowns` | `0xD48` | `int[]` | `struct System_Int32_array*` |
| `meleeNPCHitCooldown` | `0xD50` | `int[]` | `struct System_Int32_array*` |
| `musicDist` | `0xD58` | `float` | `float` |
| `Loadouts` | `0xD60` | `EquipmentLoadout[]` | `struct Terraria_EquipmentLoadout_array*` |
| `CurrentLoadoutIndex` | `0xD68` | `int` | `int32_t` |
| `savedPerPlayerFieldsThatArentInThePlayerClass` | `0xD70` | `Player.SavedPlayerDataWithAnnoyingRules` | `struct Terraria_Player_SavedPlayerDataWithAnnoyingRules_o*` |
| `_temporaryItemSlots` | `0xD78` | `Item[]` | `struct Terraria_Item_array*` |
