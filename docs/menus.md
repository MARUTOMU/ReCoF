# Menus

ReCoF keeps Cry of Fear's own picture main menu and gives every other window one look of its own (the choice of 2026-11-17: the picture menu is the game's identity, the dialogs should be readable and consistent). Bug rows B397 to B401 in [bugs.md](bugs.md).

## Look

- **The main menu** stays Cry of Fear's pictures (white labels, red under the cursor), with a Story Co-op entry made in the same look ([coop.md](coop.md)).
- **Every GameUI window** (Options, the console, the server list, the Esc menu) uses one dark theme in Cry of Fear's red: dark panels, light grey text in three steps, red for the selected tab, the hovered entry and the checks. It is set in GameUI.dll's code (src/gameui/RcTheme.cpp), so a mod that rewrites resource/trackerscheme.res no longer changes ReCoF's menus.
- **The font** of those windows is Inter (SIL Open Font License, src/gameui/fonts), carried inside GameUI.dll and registered for the game's process only; nothing is installed or added to the game folder.

## Size

Everything grows with the screen's height from 720 lines up: a window, its text and its controls take their 720p size times the screen's height over 720, so 720p and smaller screens look as before and 1080p, 1440p and 4K keep the same proportions. Game, General, **Menu size** (`rc_menu_scale`, 100 to 200 percent) makes them larger still, as far as they fit on the screen. Cry of Fear's picture menu grows the same way (its pictures are drawn larger).

## Options

The Options window holds the settings that were spread over other screens: subtitle language and developer commentary (Game, General), the story co-op host's rules and the voice (Multiplayer, Co-op), and ReCoF's own key actions on the Keyboard page (duck-tap, skip a scene, the run timer, Options). `rc_options` opens it from the console or a key.

## Not done yet

- Cry of Fear's other picture screens (difficulty, save and load, language, unlockables, extras) and the in-game panels (inventory, notes) still have their fixed sizes.
- Borders keep the scheme file's colours; a pause menu over the live game, gamepad navigation and translations of ReCoF's own texts are not done.
- The launcher writes its own values into config.cfg at every start, so an option set there wins over the same option changed in the game.
