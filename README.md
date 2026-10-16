# Death Tracker Editor

A Windows-only fork of <cy>Death Tracker</c> by **abb2k** that adds an <cr>Editor Mode</c>: change <cy>any</c> stat of a level by hand (deaths, runs, new bests, sessions, playtime and even the real **GD attempts / best %**) without breaking the numbers that belong together.

The Death Tracker mod will <cy>automatically</c> track your <cr>deaths</c>, <cf>playtime</c>, and <co>sessions</c> while you play!\
To <cy>check out your progress</c> press the <cg>death tracker button</c> (button with a skull) on the bottom left of the <cy>level page!</c>

## Editor Mode

Turn on <cy>Editor Mode</c> in the mod settings, then open the <cg>level options</c> inside the Death Tracker menu. The Modify tab is replaced by four tabs:

- **Edit** - add, remove or set deaths and runs, and add/remove new bests, for the lifetime totals or for one session
- **GD** - set the real level's <cy>attempts</c>, <cy>best %</c> and <cy>practice best %</c>
- **Sessions** - create, move and delete sessions, edit playtime (active / paused / dead, from 0 / runs)
- **Check** - verify everything, fix problems, undo, back up

### GD attempts vs Death Tracker attempts

They are two different numbers. **GD attempts** live in the level itself (Geometry Dash saves them), so editing them changes the level even without the mod. **Death Tracker attempts** are the deaths and runs the mod recorded. Editor Mode keeps them consistent:

- adding a from-0 death also adds the same number of GD attempts (setting `Link GD Attempts`)
- adding a death above the GD best also raises the GD best % and its new-best list (setting `Sync GD Best %`). A death at 100% is only recorded in Death Tracker - the level is never marked as beaten in the real game
- removing deaths lowers GD attempts, but never below the number of deaths Death Tracker has
- runs (practice / mid-level) only move GD attempts if you turn on `Runs Count As Attempts`
- platformer levels have no best %, so GD best is never touched there

### The strict rules

- a new best needs a death at that percent
- adding a death marks it as a new best exactly like the mod does while playing
- sessions are part of the lifetime totals: adding a death in a session adds it to the totals, and removing deaths from the totals takes the excess out of the newest sessions first
- you can't remove more than exists, and the percent / amount limits are enforced
- GD attempts can't be set below the tracked deaths and GD best can't be set below a tracked death (tick *Force* in the GD tab to override)
- every edit is all-or-nothing: if anything fails nothing is changed. The result box lists every side effect that was applied
- a backup is made before the first edit (setting `Backup Before Editing`) and **Undo last edit** reverts GD values too
- save files that can't be read are never edited

### Your existing data

The fork has its own mod id, so by default (`Use Original Death Tracker Saves`) it reads and writes the original Death Tracker's save folder when that exists. Don't run both mods at the same time.

## Features

- Automatic death tracking
- Automatic session death tracking
- Customizable layout
- Customizable Graphs to view your data
- Session grouping by time
- Accurate playtime by nanoseconds
- Linking between level saves
- Seperate level into named sections
- Basic cheat detection (Noclip, Speedhack)
- Safe mode
- Automatic backups
- Manual backups
- Exporting/Importing save file
- Calculator
- Editor Mode (see above)
- Run filtering (hide by length, specific start positions)
- Resetting as death


## Special Thanks
------------------

**abb2k** for Death Tracker, which this fork is built on.

**ElohmroW** for making the original V1 version of the mod.

**Mod Icon:** *taz*\
**UI help and assets:** *ZhulinskiiDanil*\
**Coding and learning help**: *Geode dev chat*\
**Testing and suggestions**: *death tracker beta testers! (ty all !!)*
