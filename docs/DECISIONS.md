# Tab Groups: decisions to review

You asked me to carry on without you, choose what was best for the project, and list what I decided so you can check it. This is that list.

**Your own choices are at the top. Everything after that is a call I made alone.** Each one says how to change it, so nothing here is final.

---

## What you chose

| Topic | Your choice |
|---|---|
| Group look | **A. Chrome chip**: coloured pill plus a thin underline under its tabs |
| Which bars | **All tab bars**: top asset tabs and inner panels like Details |
| Extras | Remember after restart, Pinned tabs, Compact tabs, Workspaces, Two-row tabs |
| Left out | Tab search popup, Auto-group by type, Tab limit |

---

## Please check these first

These are the calls most likely to matter to you.

### 1. Pinned tabs can still be closed with a shortcut
Chrome does the same, but it's worth knowing. My first design locked pinned tabs with Unreal's own lock. **That crashed the editor** when something else closed the asset, such as Close All or deleting the asset. So pinned tabs are no longer locked all the time.

- Pinned tabs are skipped by **Close All** and **Close Others** in the tab's right-click menu. They are only locked while that menu is open.
- Ctrl+W, or an asset being deleted, still closes them.

### 2. Compact tabs are ON by default
Every tab bar in the editor changes look as soon as the plugin is enabled, even if you never make a group. Turn it off in **Editor Preferences > Plugins > Tab Groups > Compact Tabs**.

### 3. Two rows are ON by default, up to 2 rows
Tabs wrap to a second row when they would shrink below 90 px. On the top bar this takes height from the editor below it.

- Turn it off: set **Max Rows** to 1, or untick **Wrap Top Row Tabs** for the top bar only.
- Bars narrower than 270 px never wrap, because squeezed tabs on two rows looked worse than on one.

### 4. Restart behaviour
- Expanded groups **reopen their assets** at startup.
- Collapsed groups **do not** load anything. They wait as a chip, and their assets open when you expand it. This keeps startup fast. It's the same idea as the lazy loading someone asked for on the Unreal forums.
- Turn off reopening with **Reopen Grouped Assets**. Turn off all memory with **Remember Groups**.

### 5. Workspaces close your open asset tabs when you switch
Switching closes the asset tabs on the main bar and opens the other workspace's tabs. Unsaved assets stay unsaved in memory and are not lost, but there's no prompt. The workspace you're leaving is saved automatically first.

Workspaces cover **asset tabs on the main tab bar only**, not panels like Details. They live under **Window > Tab workspaces**.

---

## Design and behaviour calls

| Decision | What I chose | Why | To change it |
|---|---|---|---|
| Colours | Chrome's nine dark-theme colours: grey, blue, red, yellow, green, pink, purple, cyan, orange | You said to look at how Chrome does it | `TabGroupsColors.h` |
| Default colour for a new group | First colour not yet used on that bar, blue first, grey last | Blue reads better as a first group than grey | `CreateGroup` in `TabGroupsManager.cpp` |
| Left click on chip | Collapse or expand | Same as Chrome | `STabGroupChip::OnMouseButtonUp` |
| Right click on chip | Opens the popup: rename, colour, Ungroup, Close group | Same as Chrome | same |
| Collapsed chip text | Name plus a count, like `Meshes · 3` | Matches your mockup choice | `GetChipText` |
| New group | The popup opens straight away so you can type a name | Same as Chrome | `bOpenEditor` in `CreateGroup` |
| Group with no name | Small dot-sized chip showing only its colour | Same as Chrome | `NamelessChipWidth` |
| Collapsing a group holding the active tab | The nearest tab outside the group becomes active | Same as Chrome | `SetCollapsed` |
| Every tab on the bar is in that group | The group refuses to collapse | Something must stay visible | `SetCollapsed` |
| Opening an asset that sits in a collapsed group | The group expands | The tab you asked for must be visible | `ArrangeWell` |
| Dragging a tab between two grouped tabs | It joins that group | Same as Chrome | `ResolveDropGroup` |
| Dragging a grouped tab away | It leaves the group, including into another window | Same as Chrome | `ResolveDropGroup` |
| Level Editor and Home tabs | Can't be grouped or pinned | Unreal fixes these in place | `NumHeadTabs` |
| Pinned tab position | Just after Home and the Level Editor | Same reason | `ComputeNormalizedOrder` |
| Pinning a grouped tab | It leaves the group | Same as Chrome | `SetPinned` |
| Where groups are saved | `<Project>/Saved/TabGroups/State.json`, per project | Different projects have different assets. Not meant for source control | `GetStatePath` |
| Plugin name | "Tab Groups" | Plain and descriptive | `TabGroups.uplugin` |
| Test project | `E:\Projects\Unreal\TabGroups` | Keeps your real projects untouched | n/a |
| Module layout | Core, Editor and Tests modules | Same split as your GraphAutoLayout plugin | n/a |

---

## How it works, and the risks

Unreal's tab bar is private engine code, so a normal plugin can't change it. The plugin patches five functions of that class in memory while the editor runs. It finds them by asking the compiler, not with hard-coded numbers, so it adapts when the engine changes.

Tabs always stay in Unreal's own tab list. Grouping only changes how they're laid out, so saving, closing and finding tabs all keep working as normal.

**What this means for you:**

- **Windows only**, and tied to the engine version. Each new engine version needs a rebuild and a retest.
- **If the patch can't find what it needs, the plugin switches itself off** for that session and the editor runs normally. I saw this work during testing.
- **Safe mode:** start the editor with `-NoTabGroups` to turn the whole plugin off.
- This is an unusual technique. Epic doesn't support it, so don't expect them to help with crashes while it's enabled.

---

## Not built

| Item | Note |
|---|---|
| Dragging a whole group by its chip | Chrome has this. Moving tabs one at a time works. Good next feature |
| Selecting several tabs and grouping them at once | Chrome has Ctrl+click. Here you add tabs one by one |
| Dropping a tab onto a collapsed chip to add it | Use right click > Add tab to group |
| Animation when collapsing | Chrome slides; this snaps |
| Workspaces for inner panels | Asset tabs only. Use Window > Save Layout for panels |
| Linux and Mac | Windows only |

---

## Found along the way

**An engine bug that isn't from this plugin.** Quitting the editor from a script while a Static Mesh or Texture editor is open crashes during shutdown. I checked this with the plugin switched off and it crashed the same way, so it's already in UE 5.8. I only saw it with scripted quits, and I didn't test the normal close button on this path.

**Live Coding and building.** Your open `UnrealMCP` editor has Live Coding running, which makes Unreal refuse to build any project, even an unrelated one. I passed `-NoHotReloadFromIDE` on my build commands only. Your settings are unchanged.

---

## Test results

All of this ran on the final code, in real editors, using Slate's real input path for clicks and drags.

| Test | Result |
|---|---|
| Unit tests: layout, tab order, saved state | 17 of 17 |
| Asset tabs in a floating window | 26 of 26 |
| After a real restart | 7 of 7 |
| Asset tabs on the main window bar | 6 of 6 |
| Inner panel bars | 5 of 5 |
| Packaged plugin in a fresh Blueprint-only project, no compiling | 5 of 5 |

Phases that quit from a script with asset editors open end with the engine crash described above. The checks themselves had already finished, and the phases that closed their editors first exited cleanly.

## Not tested yet

Please try these by hand, since nobody has used this with a real mouse yet:

- **Your real workflow**, with real Blueprint, Material and Niagara editors. Tests used Static Mesh editors, because Material editors stall on shader compiles and Texture editors hit the scripted-quit crash.
- **The normal close button** with asset editors open. Tests closed the editor by script.
- **The popup fields by hand.** The popups opened from real clicks and I checked how they look, but typing a name and clicking a colour swatch were only run through the code behind them.
- **The Window > Tab workspaces menu.** Saving and switching were tested through the code behind the menu, not by clicking it.
- **Multiple monitors and display scaling** other than this PC's setup.
- **Tabs moved into a sidebar drawer** while grouped, and **Hide Tabs** on a bar with groups.

## Where everything is

| What | Where |
|---|---|
| Installable plugin | `E:\Projects\Unreal\TabGroups_UE5.8_Win64.zip` |
| Source | `E:\Projects\Unreal\TabGroups\Plugins\TabGroups` |
| Test project | `E:\Projects\Unreal\TabGroups\TabGroups.uproject` |
| Screenshots | `E:\Projects\Unreal\TabGroups\Screenshots` |
| User guide | `README.md` in the plugin folder |

I did not install the plugin into any of your existing projects. Nothing is committed to git, because the folder isn't a repository.
