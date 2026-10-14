#ifndef RTSO_HOTKEY_DEFAULTS_H
#define RTSO_HOTKEY_DEFAULTS_H

// The [hotkey.*] sections of 0 A.D. 0.28 binaries/data/config/default.cfg
// (GPL-2.0-or-later), copied verbatim by tools/extract_0ad_hotkeys.py, followed
// by RTS0's own [hotkey.rts0.*] keys.
// Parsed by hotkeys.cpp; user overrides load from user://config/user.cfg on top.

namespace rtso {
namespace hotkeys {
namespace detail {

// Split into pieces to stay under MSVC's 16 KB string-literal limit.
static const char kDefaultCfg[] =
R"cfg([hotkey]
; Each one of the specified keys will trigger the action on the left
; for multiple-key combinations, separate keys with '+'.
; See keys.txt for the list of key names.

; > SYSTEM SETTINGS
exit = ""                                ; 'Custom' exit to desktop, SDL handles the native command via SDL_Quit.
cancel = Escape                          ; Close or cancel the current dialog box/popup
confirm = Return                         ; Confirm the current command
pause = Pause, "Shift+Space"             ; Pause/unpause game
screenshot = F2                          ; Take PNG screenshot
bigscreenshot = "Shift+F2"               ; Take large BMP screenshot
togglefullscreen = "Alt+Return"          ; Toggle fullscreen/windowed mode
screenshot.watermark = "Alt+K"           ; Toggle product/company watermark for official screenshots
wireframe = "Alt+Shift+W"                ; Toggle wireframe mode
silhouettes = "Alt+Shift+S"              ; Toggle unit silhouettes
mousegrabtoggle = F3                     ; Toggle mouse grabbing mode

; > DIALOG HOTKEYS
summary = "Ctrl+Tab"                     ; Toggle in-game summary
lobby = "Alt+L"                          ; Show the multiplayer lobby in a dialog window.
structree = "Alt+Shift+T"                ; Show structure tree
civinfo = "Alt+Shift+H"                  ; Show civilization info

; > CLIPBOARD CONTROLS
copy = "Ctrl+C"                       ; Copy to clipboard
paste = "Ctrl+V"                      ; Paste from clipboard
cut = "Ctrl+X"                        ; Cut selected text and copy to the clipboard

; > CONSOLE SETTINGS
console.toggle = BackQuote, F9        ; Open/close console

; > OVERLAY KEYS
fps.toggle = "Alt+F"                  ; Toggle frame counter
realtime.toggle = "Alt+T"             ; Toggle current display of computer time
timeelapsedcounter.toggle = "F12"     ; Toggle time elapsed counter
ceasefirecounter.toggle = ""          ; Toggle ceasefire counter

; > HOTKEYS ONLY
chat = Return                         ; Toggle chat window
teamchat = "T"                        ; Toggle chat window in team chat mode
privatechat = "L"                     ; Toggle chat window and select the previous private chat partner

; > QUICKSAVE
quicksave = "Shift+F5"
quickload = "Shift+F8"

[hotkey.camera]
reset = "R"                                   ; Reset camera rotation to default.
follow = ""                                   ; Follow the first unit in the selection
rallypointfocus = ""                          ; Focus the camera on the rally point of the selected building
lastattackfocus = "Space"                     ; Focus the camera on the last notified attack
togglebirdseyeview = "Shift+Tab"              ; Toggle bird's eye view
zoom.in = Plus, NumPlus                       ; Zoom camera in (continuous control)
zoom.out = Minus, NumMinus                    ; Zoom camera out (continuous control)
zoom.wheel.in = WheelUp                       ; Zoom camera in (stepped control)
zoom.wheel.out = WheelDown                    ; Zoom camera out (stepped control)
rotate.up = "Ctrl+UpArrow", "Ctrl+W"          ; Rotate camera to look upwards
rotate.down = "Ctrl+DownArrow", "Ctrl+S"      ; Rotate camera to look downwards
rotate.cw = "Ctrl+LeftArrow", "Ctrl+A", Q     ; Rotate camera clockwise around terrain
rotate.ccw = "Ctrl+RightArrow", "Ctrl+D", E   ; Rotate camera anticlockwise around terrain
rotate.wheel.cw = "Shift+WheelUp", MouseX1    ; Rotate camera clockwise around terrain (stepped control)
rotate.wheel.ccw = "Shift+WheelDown", MouseX2 ; Rotate camera anticlockwise around terrain (stepped control)
pan = MouseMiddle                             ; Enable scrolling by moving mouse
left = A, LeftArrow                           ; Scroll or rotate left
right = D, RightArrow                         ; Scroll or rotate right
up = W, UpArrow                               ; Scroll or rotate up/forwards
down = S, DownArrow                           ; Scroll or rotate down/backwards
scroll.speed.increase = "Ctrl+Shift+S"        ; Increase scroll speed
scroll.speed.decrease = "Ctrl+Alt+S"          ; Decrease scroll speed
rotate.speed.increase = "Ctrl+Shift+R"        ; Increase rotation speed
rotate.speed.decrease = "Ctrl+Alt+R"          ; Decrease rotation speed
zoom.speed.increase = "Ctrl+Shift+Z"          ; Increase zoom speed
zoom.speed.decrease = "Ctrl+Alt+Z"            ; Decrease zoom speed

)cfg"
R"cfg([hotkey.camera.jump]
1 = F5                       ; Jump to position N
2 = F6
3 = F7
4 = F8
;5 =
;6 =
;7 =
;8 =
;9 =
;10 =
[hotkey.camera.jump.set]
1 = "Ctrl+F5"                ; Set jump position N
2 = "Ctrl+F6"
3 = "Ctrl+F7"
4 = "Ctrl+F8"
;5 =
;6 =
;7 =
;8 =
;9 =
;10 =

[hotkey.profile]
toggle = "F11"               ; Enable/disable real-time profiler
save = "Shift+F11"           ; Save current profiler data to logs/profile.txt
[hotkey.profile2]
toggle = "Ctrl+F11"          ; Enable/disable HTTP/GPU modes for new profiler

[hotkey.selection]
cancel = Esc                            ; Un-select all units and cancel building placement
add = Shift                             ; Add units to selection
militaryonly = Alt                      ; Add only military units to the selection
nonmilitaryonly = "Alt+Y"               ; Add only non-military units to the selection
idleonly = "I"                          ; Select only idle units
woundedonly = "O"                       ; Select only wounded units
remove = Ctrl                           ; Remove units from selection
idlebuilder = Semicolon                 ; Select next idle builder
idleworker = Period, NumDecimal         ; Select next idle worker
idlewarrior = Slash, NumDivide          ; Select next idle warrior
idleunit = BackSlash                    ; Select next idle unit
offscreen = Alt                         ; Include offscreen units in selection
singleselection = "" ; Modifier to select units individually, opposed to per formation.
[hotkey.selection.group.add]
1 = "Shift+1", "Shift+Num1"
2 = "Shift+2", "Shift+Num2"
3 = "Shift+3", "Shift+Num3"
4 = "Shift+4", "Shift+Num4"
5 = "Shift+5", "Shift+Num5"
6 = "Shift+6", "Shift+Num6"
7 = "Shift+7", "Shift+Num7"
8 = "Shift+8", "Shift+Num8"
9 = "Shift+9", "Shift+Num9"
10 = "Shift+0", "Shift+Num0"
[hotkey.selection.group.save]
1 = "Ctrl+1", "Ctrl+Num1"
2 = "Ctrl+2", "Ctrl+Num2"
3 = "Ctrl+3", "Ctrl+Num3"
4 = "Ctrl+4", "Ctrl+Num4"
5 = "Ctrl+5", "Ctrl+Num5"
6 = "Ctrl+6", "Ctrl+Num6"
7 = "Ctrl+7", "Ctrl+Num7"
8 = "Ctrl+8", "Ctrl+Num8"
9 = "Ctrl+9", "Ctrl+Num9"
10 = "Ctrl+0", "Ctrl+Num0"
[hotkey.selection.group.select]
1 = 1, Num1
2 = 2, Num2
3 = 3, Num3
4 = 4, Num4
5 = 5, Num5
6 = 6, Num6
7 = 7, Num7
8 = 8, Num8
9 = 9, Num9
10 = 0, Num0

)cfg"
R"cfg([hotkey.gamesetup]
mapbrowser.open = "M"

[hotkey.session]
kill = Delete, Backspace     ; Destroy selected units
stop = "H"                   ; Stop the current action
backtowork = "Y"             ; The unit will go back to work
unload = "U"                 ; Unload garrisoned units when a building/mechanical unit is selected
unloadturrets = "U" ; Unload turreted units.
leaveturret = "U" ; Leave turret point.
move = ""                    ; Modifier to move to a point instead of another action (e.g. gather)
capture = "C"                ; Modifier to capture instead of another action (e.g. attack)
attack = "F"                 ; Modifier to attack instead of another action (e.g. capture)
attackmove = Ctrl            ; Modifier to attackmove when clicking on a point
attackmoveUnit = "Ctrl+Q"    ; Modifier to attackmove targeting only units when clicking on a point
garrison = Ctrl              ; Modifier to garrison when clicking on building
occupyturret = Ctrl ; Modifier to occupy a turret when clicking on a turret holder.
autorallypoint = Ctrl        ; Modifier to set the rally point on the building itself
guard = "G"                  ; Modifier to escort/guard when clicking on unit/building
patrol = "P"                 ; Modifier to patrol a unit
repair = "J"                 ; Modifier to repair when clicking on building/mechanical unit
queue = Shift                ; Modifier to queue unit orders instead of replacing
pushorderfront = ""          ; Modifier to push unit orders to the front instead of replacing.
orderone = Alt               ; Modifier to order only one entity in selection.
batchtrain = Shift           ; Modifier to train units in batches
massbarter = Shift           ; Modifier to barter bunch of resources
masstribute = Shift          ; Modifier to tribute bunch of resources
noconfirmation = Shift       ; Do not ask confirmation when deleting a building/unit
fulltradeswap = Shift        ; Modifier to put the desired trade resource to 100%
unloadtype = Shift           ; Modifier to unload all units of type
deselectgroup = Ctrl         ; Modifier to deselect units when clicking group icon, instead of selecting
rotate.cw = RightBracket     ; Rotate building placement preview clockwise
rotate.ccw = LeftBracket     ; Rotate building placement preview anticlockwise
snaptoedges = Ctrl           ; Modifier to align new structures with nearby existing structure
toggledefaultformation = ""  ; Switch between null default formation and the last default formation used (defaults to "box")
flare = K                    ; Modifier to send a flare to your allies
flareactivate = ""           ; Modifier to activate the mode to send a flare to your allies
calltoarms = ""              ; Modifier to call the selected units to the arms.
focusfire = "F"              ; Modifier to control exclusively a building's arrows if it can attack
; Overlays
showstatusbars = ""          ; Toggle display of status bars
devcommands.toggle = "Alt+D" ; Toggle developer commands panel
highlightguarding = PageDown ; Toggle highlight of guarding units
highlightguarded = PageUp    ; Toggle highlight of guarded units
diplomacycolors = "Alt+X"    ; Toggle diplomacy colors
toggleattackrange = "Alt+C"  ; Toggle display of attack range overlays of selected defensive structures
toggleaurasrange = "Alt+V"   ; Toggle display of aura range overlays of selected units and structures
togglehealrange = "Alt+B"    ; Toggle display of heal range overlays of selected units

[hotkey.session.gui]
toggle = "Alt+G"             ; Toggle visibility of session GUI
menu.toggle = "F10"          ; Toggle in-game menu
diplomacy.toggle = "Ctrl+H"  ; Toggle in-game diplomacy page
barter.toggle = "Ctrl+B"     ; Toggle in-game barter/trade page
matchsettings.toggle = "Ctrl+O" ; Toggle in-game match settings page
tutorial.toggle = "Ctrl+P"   ; Toggle in-game tutorial panel

[hotkey.session.savedgames]
delete = Delete, Backspace    ; Delete the selected saved game asking confirmation
noconfirmation = Shift        ; Do not ask confirmation when deleting a game

)cfg"
R"cfg([hotkey.session.queueunit]   ; > UNIT TRAINING
1 = "Z"            ; add first unit type to queue
2 = "X"            ; add second unit type to queue
3 = "C"            ; add third unit type to queue
4 = "V"            ; add fourth unit type to queue
5 = "B"            ; add fivth unit type to queue
6 = "N"            ; add sixth unit type to queue
7 = "M"            ; add seventh unit type to queue
8 = Comma          ; add eighth unit type to queue

[hotkey.session.timewarp]
fastforward = "Ctrl+Space"         ; If timewarp mode enabled, speed up the game
rewind = "Shift+Backspace"        ; If timewarp mode enabled, go back to earlier point in the game

[hotkey.tab]
next = "Tab", "Alt+S"             ; Show the next tab
prev = "Shift+Tab", "Alt+W"       ; Show the previous tab

[hotkey.item]
next = "RightArrow"               ; Show the next item of a list
prev = "LeftArrow"                ; Show the previous item of a list

[hotkey.text]    ; > GUI TEXTBOX HOTKEYS
delete.left = "Ctrl+Backspace"    ; Delete word to the left of cursor
delete.right = "Ctrl+Del"         ; Delete word to the right of cursor
move.left = "Ctrl+LeftArrow"      ; Move cursor to start of word to the left of cursor
move.right = "Ctrl+RightArrow"    ; Move cursor to start of word to the right of cursor


; ---------------------------------------------------------------------------
; RTS0 additions (not in 0 A.D.'s default.cfg).
; ---------------------------------------------------------------------------
[hotkey.rts0.build]          ; Building placement. B opens it with the house; the others only work while placing
menu = B
)cfg"
R"cfg(civilcentre = C
house = H
barracks = R
storehouse = S
defensetower = T
farmstead = F
market = M
fortress = K
gate = G
temple = E, P
wonder = W
dock = D
wall = L
corral = O

[hotkey.rts0.stance]         ; Unit stances (0 A.D. only has the stance buttons)
aggressive = ""
defensive = ""
standground = ""
violent = ""
passive = ""

[hotkey.rts0.debug]          ; Developer battles
battle500 = "Ctrl+Alt+F7"
battle1000 = "Ctrl+Alt+F8"
battle2000 = "Ctrl+Alt+F9"

)cfg";

} // namespace detail
} // namespace hotkeys
} // namespace rtso

#endif
