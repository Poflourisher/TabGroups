# Tab Groups

**Chrome-style tab groups for Unreal Editor.** Name them, colour them, and collapse them so your tab bar stops squeezing every tab down to an icon.

It's editor-only: it adds nothing to your game, cooked builds or runtime.

Built for **Unreal Engine 5.8 on Windows**.

---

## Using it

### Make a group
Right-click any tab and choose **Add tab to new group**. A small popup opens so you can type a name and pick a colour.

To add more tabs, right-click them and choose **Add tab to group**, or drag a tab between two tabs of the group.

### Collapse and expand
**Left-click the coloured chip.** Collapsed groups show their name and a count, like `Meshes · 3`, and the rest of your tabs get the room back.

If you open an asset whose tab is in a collapsed group, the group expands by itself.

### Rename, recolour, ungroup
**Right-click the chip.**

| Button | What it does |
|---|---|
| **Ungroup** | Keeps the tabs open and removes the group |
| **Close group** | Closes every tab in the group |

### Take a tab out
Drag it out of the group, or right-click it and choose **Remove from group**.

### Pin a tab
Right-click a tab and choose **Pin tab**. It shrinks to an icon at the left of the bar, and **Close All** and **Close Others** skip it.

### Workspaces
Save a whole set of asset tabs and their groups under a name, then switch between sets.

Go to **Window > Tab workspaces**, type a name and press Enter. Switching closes the open asset tabs and opens the other set. Unsaved assets stay unsaved in memory, and the workspace you're leaving is saved first.

Workspaces cover asset tabs on the main tab bar. For panels like Details, use **Window > Save Layout**.

### After a restart
Groups, colours, pinned tabs and workspaces all come back. Assets in expanded groups reopen. Assets in collapsed groups stay unloaded until you expand the group, which keeps startup fast.

---

## Settings

**Editor Preferences > Plugins > Tab Groups**

| Setting | Default | What it does |
|---|---|---|
| Compact Tabs | On | Tabs are only as wide as their name, instead of all sharing one width |
| Min Tab Width | 90 | Below this width, tabs wrap to the next row if more rows are allowed |
| Max Rows | 2 | Set to 1 to never wrap |
| Wrap Top Row Tabs | On | Lets the top bar wrap too. This takes height from the editor below |
| Remember Groups | On | Brings groups and pinned tabs back when the project reopens |
| Reopen Grouped Assets | On | Reopens assets of expanded groups at startup |

Groups are saved per project in `Saved/TabGroups/State.json`.

---

## If something goes wrong

Start the editor with **`-NoTabGroups`** to switch the whole plugin off.

If the plugin can't attach to the tab bar, for example on a different engine version, it turns itself off for that session and logs an error under `LogTabGroups`. The editor keeps working normally.

---

## Good to know

- **Windows only, and tied to UE 5.8.** Unreal's tab bar is private engine code, so this plugin patches it in memory while the editor runs. Each new engine version needs a rebuild and a retest.
- Epic doesn't support this technique, so disable the plugin before reporting editor crashes to them.
- Tabs stay in Unreal's own tab list the whole time. Grouping only changes how they're laid out.
- A group lives on one tab bar. Dragging a tab to another bar or window takes it out of the group.
- The Home and Level Editor tabs can't be grouped or pinned, because Unreal fixes them in place.
