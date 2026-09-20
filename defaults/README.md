# Default configuration files

Screens and settings as files, for anybody who wants to start from what the
firmware ships with rather than from an empty page.

Nothing here is installed automatically. Push what you want:

```bash
uv run tools/omotectl.py --port /dev/ttyUSB0 push defaults/ui.json
```

The device applies it on the next start.

## `ui.json`

The numpad, as a file. It replaces the screen of the same name compiled into the
firmware - `src/guis/gui_numpad.cpp` stays in the repository as the reference
the file was written from.

**One thing the file deliberately does not reproduce.** The hand written numpad
looks at which scene is active and sends either the Samsung IR codes or, in the
FireTV scene, the digit as a keyboard string:

```cpp
if (gui_memoryOptimizer_getActiveSceneName() == scene_name_TV) {
  executeCommand(virtualKeyMapTVNumbers[user_data]);
} else if (... == scene_name_fireTV) {
  executeCommand(KEYBOARD_SENDSTRING, numberStr);
}
```

A widget binds to one command, and ui.json has no conditionals - on purpose, see
`configUi.h`. The way to express this is a second screen: give it the keyboard
commands, call it something like `NumpadFireTV`, and name that one in the
`guiList` of the FireTV scene in `scenes.json`. Two visible screens instead of
one screen with an invisible branch in it, which is also easier to explain to
whoever edits it next.

So `defaults/ui.json` covers the TV case. Pushing it to a device that uses the
FireTV scene changes what the number keys do there, which is why it is not
installed for you.

## What is not here yet

`gui_sceneSelection` needs a widget that lists the registered scenes, and that
list only exists at runtime - a file cannot name what it does not know. It stays
compiled in until there is a widget type for it.
