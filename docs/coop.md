# Story co-op (work in progress)

Simon's story for 2 to 4 players on a listen server, joined through Steam's relay network: no Radmin, no port forwarding, and the players' IP addresses stay hidden. The maps are the retail ones, unmodified; everything is done in code. Single player is unchanged: none of this runs unless a game is started with `rc_coop 1` and more than one player slot.

The design and the reasons are in the research reports (`I:\COFPP\reports`, local); the bug rows are B368 to B382 in [bugs.md](bugs.md).

## Playing

Every player needs the same ReCoF build and Steam running.

- **Host:** open the console and type `rc_coop_start` (Simon's story from the start) or `rc_coop_start <map>` (for example `rc_coop_start c_apartment1`). It also works as a launch option, `+rc_coop_start c_apartment1`. When Steam has given the game an address (a few seconds), the console shows `your friends join with: rc_coop_join 169.254.x.x:port`.
- **Invite:** `rc_coop_invite` opens Steam's own invite list in the overlay; a friend who accepts joins. Friends can also use Join Game on the host in their Steam friends list, or type the `rc_coop_join` line the host's console shows.
- **Join by hand:** `rc_coop_join 169.254.x.x:port` in the console.
- **Status:** `rc_coop_status` shows the datagrams through Steam and the relay network's state.
- **Leaving:** a guest whose host loads a save or restarts connects again by himself (up to 10 tries). A guest who leaves (the menu's Disconnect, `disconnect` or `rc_coop_leave`) stays out, and `rc_coop_join` brings him back.
- **Saving:** the game saves itself as `coop_auto` once everyone is in on each new level, and as `coop_tape` when anyone uses a tape recorder (on the hardest difficulty it costs that player a tape); the host can also type `save <name>`. The host loads with `load coop_auto`, `load coop_tape` or `load <name>`; every saved player is kept for his player, and the guests come back by themselves.

## What the co-op does

- **Level exits wait for the group.** A player who walks into an exit waits there with "Waiting at the exit: k of N players" on everyone's screen until the whole group is in it, or until `rc_coop_exit_wait` seconds pass (default 60), then the players who are not there are brought to the exit. The host can end the wait with `rc_coop_go`. Exits fired by a door or a script do not wait.
- **Everyone keeps their things through a level change.** The engine carries every player through the single-player transition (health, weapons, inventory), as it does for Simon alone.
- **No bouncing back.** A player who arrives inside the exit back counts for exits only after he has stepped out of every exit once.
- **Players still loading cannot be hurt.**
- **Respawn.** A player who dies comes back next to a living teammate after `rc_coop_respawn_time` seconds (default 10), with his weapons and ammo. Reviving by CPR works as in the police co-op.
- **Keys belong to the group.** A key, fuse or other story item one player picks up is in everyone's inventory; when it is used on its door or lock it is gone for everyone.
- **One code for the group.** The padlock combination and the keypad code are the same for every player.
- **Scenes take the group along.** A scene that teleports Simon teleports the whole group; an arrival room takes everyone standing in it; nobody lands inside a teammate.
- **Cameras** already show every scene to every player (Cry of Fear's own code).
- **Skipping scenes.** The host's Enter (or `rc_cutscene_skip`) skips a scene he is watching for everyone: the game hurries through it, and the players watching it see the skip screen. A guest's Enter does not skip, as it would hurry the host's world too.
- **Teammates.** No friendly fire; a player who joins comes in next to a teammate.
- **Pause.** Only the host pauses (his menu pauses everyone); a guest's menu pauses nobody.
- **Simon, not a police officer.** Simon's voice, hints, dual wield, the slow walk at low health and the map's own sky, as in single player.
- **A team wipe** brings everyone back at the level's start with their things, without restarting the map.
- **Story effects for everyone.** Objectives, on-screen texts, grey fades, rain changes and the phone going dead reach every player (the music already did).
- **Late joiners** come in with the weapons of the teammate they appear next to; the group's keys follow by themselves.
- **Harder with more players.** Monsters get 25% more health for each player beyond the first (at most double), the monsters the map places too; a player joining or leaving changes it, and a hurt monster stays as hurt. This takes the place of the police co-op's damage multiplier; the police co-op's hunting monster Simon never appears.

## Settings (host)

| Setting | Default | What it does |
|---|---|---|
| `rc_coop` | 0 | 1 with more than one slot plays the story in co-op (set by `rc_coop_start`) |
| `rc_coop_exit_wait` | 60 | seconds an exit waits for the group; 0 changes level at once |
| `rc_coop_respawn_time` | 10 | seconds before a dead player comes back with his things; 0 keeps Cry of Fear's rule |
| `rc_coop_autosave` | 1 | saves `coop_auto` once everyone is in on each new level |
| `rc_coop_monster_health` | 25 | percent more monster health for each player beyond the first, at most double; 0 for single player's |

These are server settings: they live in the host's game, so the host's value applies to everyone and a guest cannot change it. The same holds for the movement settings (`rc_bhopcap`, `rc_hlmovement`, `rc_ducktap`), which already reach every player's prediction.

## Not done yet

- Matching saved players by identity: with 3 or 4 players coming back in another order after a load, they get each other's saved player.
- A load menu for co-op saves, and loading the last save after a team wipe.
- The menu: Cry of Fear's main menu is made of pictures, so Host and Invite buttons need their own; for now the console commands and the `+rc_coop_start` launch option.
- Starting ReCoF instead of retail Cry of Fear when a friend accepts an invite with the game closed (Steam launch options, decision 11).
- The 28 maps that need a rule of their own (bosses, vehicles, endings) and the rest of the places where the story only looks at the first player (G7): bosses, puzzles, sounds played "from the player", the look-at scenes.
- A vote for guests to skip a scene, the voice chat, presets.

## Tested

On 2026-11-17 with two PCs and two Steam accounts, every packet through Valve's relays: joining, the level change c_start to c_start2 with both players' health and weapons, the exit wait and gathering, the arrival guard, respawn with weapons, a team wipe, the apartment key in c_apartment1 shared and used up for both, the shared padlock code, the teleport step-aside, Simon's voice, saving and loading with both players (a guest coming back by himself), the autosave after a level change holding both players, host-only pause, the host skipping the intro for both (on to c_nightmare), monster health with one and two players and after a load, a guest leaving and joining again, the single-player level change and intro skip unchanged, and the speedrun address check. A tape recorder saved the group (the host using it; loading coop_tape gave back the saved health). Not tested: the overlay invite and Join Game clicks, a guest using a tape recorder, four players, two players on different internet connections, Proton.

## For testers

- `rc_coop_dump` (host console) prints every player's health, place, weapons, inventory and codes.
- `rc_coop_host [relay] [nomap]` and `rc_coop_join <ip:port> [relay]` are the pieces `rc_coop_start` uses; `relay` turns off direct connections, as between two homes.
- Starting `cof.exe` with `-rccmdfile` runs the console commands written to `rc_cmd.txt` next to `cof.exe` once a second (test scripts only).
- `rc_coop_steam.log` next to `cof.exe` logs the Steam side (no Steam IDs or addresses).
