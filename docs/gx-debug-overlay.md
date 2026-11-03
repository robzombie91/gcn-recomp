# GX Debug Overlay (WIP)

The desktop OpenGL frontend includes an optional, dependency-free inspector.
No additional packages are required beyond the existing project dependencies.

Run the game normally and press **F10** to display the overlay.

| Key | Action |
| --- | --- |
| F10 | Show/hide the overlay |
| Up / Down | Select previous/next draw in the most recent captured batch |
| Page Up / Page Down | Move by 10 draws |
| F9 | Freeze/unfreeze *the inspector snapshot only* |
| F8 | Export the captured draw/state list to `saves/gx-debug-snapshot.txt` |

The panel displays the selected draw's command index, geometry counts,
TEV stage configuration, texture map identifiers and EFB flags, blend/depth/alpha
registers. This is a first-pass inspection panel; it does not yet render full
texture thumbnails, provide per-fragment shader stepping, or pause/rewind
execution of the game. In particular, F9 only freezes what the panel displays:
**the game continues running**.

Current limitations:
- The overlay is drawn for the ordinary desktop render path; stereo/eye
  frontend modes currently skip it.
- The inspector snapshots the newest render batch only. EFB copy and present
  commands are counted but cannot yet be selected independently.
- The panel does not intercept controller bindings; use the listed keyboard keys.
- The host OpenGL driver must support the project's normal desktop GL profile.
- Saving/loading through the game's memory card remains separate from this
  debugger. The overlay does not modify the card.

When debugging a reproducible scene, use an existing in-game save or input
replay to reach it, then F9 to retain a particular displayed batch and F8 to
export its state. Exports are overwritten on the next F8 press; copy the file
to a unique name when comparing captures.
