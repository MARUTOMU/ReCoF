# Menus

ReCoF gives its menus one modern look: flat dark panels, plain text, one red accent and the Inter font, after cof-enhanced's (the user's choice of 2026-11-17, which replaced the picture main menu kept the same morning). Bug rows B397 to B401 and B405 in [bugs.md](bugs.md).

## Look

- **The main menu** is a short text list under the words CRY OF FEAR and ReCoF: New Game, Load Game, Story Co-op ([coop.md](coop.md)), Custom Campaign, Multiplayer (Find Servers, Host Server), Extras (Unlockables, Bonus Content, Language), Options and Quit. The entry under the cursor turns white with a short red bar. New Game opens a panel in the same look: the four difficulties, the developer commentary once unlocked, and Back. Game, General, **Classic main menu** (`rc_menu_classic 1`) brings Cry of Fear's picture menu back.
- **Every GameUI window** (Options, the console, the server list, the Esc menu) uses one dark theme in Cry of Fear's red: dark panels, light grey text in three steps, red for the selected tab, the hovered entry and the checks. It is set in GameUI.dll's code (src/gameui/RcTheme.cpp), so a mod that rewrites resource/trackerscheme.res no longer changes ReCoF's menus.
- **The font** of those windows is Inter (SIL Open Font License, src/gameui/fonts), carried inside GameUI.dll and registered for the game's process only; nothing is installed or added to the game folder.

## Size

Everything grows with the screen's height from 720 lines up: a window, its text and its controls take their 720p size times the screen's height over 720, so 720p and smaller screens look as before and 1080p, 1440p and 4K keep the same proportions. Game, General, **Menu size** (`rc_menu_scale`, 100 to 200 percent) makes them larger still, as far as they fit on the screen. The main menu's text is a fraction of the screen's height as well, and Cry of Fear's picture menu grows the same way (its pictures are drawn larger).

## Options

Each option says what it does in one line at the bottom of the window while the mouse is over it; nothing else is written on the pages. The Options window holds the settings that were spread over other screens: subtitle language and developer commentary (Game, General), the story co-op host's rules and the voice (Multiplayer, Co-op), and ReCoF's own key actions on the Keyboard page (duck-tap, skip a scene, the run timer, Options). `rc_options` opens it from the console or a key.

## Game menu

The menu Esc opens in a game (and the list after leaving one) keeps Cry of Fear's entries with short names: Resume game, New game, Load game, Main menu (the main menu's map), Leave game, Unlockables, Find servers, Host server, Story co-op (the lobby on the main menu), Options and Quit. They are renamed in code by their command, so the game's GameMenu.res stays as it is.

## Not done yet

- Cry of Fear's other picture screens (difficulty, save and load, language, unlockables, extras) and the in-game panels (inventory, notes) still have their fixed sizes.
- Borders keep the scheme file's colours; a pause menu over the live game, gamepad navigation and translations of ReCoF's own texts are not done.
- The launcher writes its own values into config.cfg at every start, so an option set there wins over the same option changed in the game.
