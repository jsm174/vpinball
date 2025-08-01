# Visual Pinball Standalone

*An open source **cross platform** pinball table simulator.*

This sub-project of VPinballX is designed to run on non-Windows platforms.

[![Watch the video](https://img.youtube.com/vi/jK3TbGvTuIA/0.jpg)](https://www.youtube.com/watch?v=xjkgzIVL_QU)

## Background

Visual Pinball was built over 20 years ago using Microsoft technologies such as OLE Compound Documents, DirectX, VBScript, COM, and ATL -- many of which have no cross platform equivalents or support.

To make a successful port, we would need tackle several tasks:

> ***OLE Compound Documents***
> - Visual Pinball Table (`.vpx`) files are stored in [Compound File Binary Format](https://en.wikipedia.org/wiki/Compound_File_Binary_Format).
>
> **Solution**
> - Implement [POLE - portable library for structured storage](https://dimin.net/software/pole/) to load `.vpx` files.
> - Create `PoleStorage` wrapper to match `IStorage` interface.
> - Create `PoleStream` wrapper to match `IStream` interface.

> ***Windows Registry***
> - Visual Pinball stores and retrieves its settings from the Windows registry.
>
> **Solution**
> - Use the `ENABLE_INI` preprocessor definition which uses [RapidXML](https://github.com/timniederhausen/rapidxml) to save and retrieve settings from a file.

> ***VBScript***
> - Visual Pinball uses [VBScript](https://learn.microsoft.com/en-us/previous-versions/t0aew7h6(v=vs.85)) as the scripting engine for tables.
>
> **Solution**
> - Leverage the VBScript engine from [Wine](https://github.com/wine-mirror/wine/tree/master/dlls/vbscript).
> - Fix bugs in Wine's VBScript engine:
>   - ~~[Bug 53644](https://bugs.winehq.org/show_bug.cgi?id=53644) - vbscript can not compile classes with lists of private / public / dim declarations~~
>   - ~~[Bug 53670](https://bugs.winehq.org/show_bug.cgi?id=53670) - vbscript can not compile if expressions with reversed gte, lte, (=>, =<)~~
>   - ~~[Bug 53676](https://bugs.winehq.org/show_bug.cgi?id=53676) - vbscript can not exec_script - invalid number of arguments for Randomize~~
>   - ~~[Bug 53678](https://bugs.winehq.org/show_bug.cgi?id=53678) - vbscript can not compile CaseClausules that do not use a colon~~
>   - ~~[Bug 53766](https://bugs.winehq.org/show_bug.cgi?id=53766) - vbscript fails to handle SAFEARRAY assignment, access, UBounds, LBounds~~
>   - [Bug 53767](https://bugs.winehq.org/show_bug.cgi?id=53767) - vbscript fails to handle ReDim when variable is not yet created
>   - ~~[Bug 53782](https://bugs.winehq.org/show_bug.cgi?id=53782) - vbscript can not compile ReDim with list of variables~~
>   - ~~[Bug 53783](https://bugs.winehq.org/show_bug.cgi?id=53783) - vbscript can not compile private const expressions~~
>   - ~~[Bug 53807](https://bugs.winehq.org/show_bug.cgi?id=53807) - vbscript fails to redim original array in function when passed byref~~
>   - [Bug 53844](https://bugs.winehq.org/show_bug.cgi?id=53844) - vbscript invoke_vbdisp not handling let property correctly for VT_DISPATCH arguments
>   - ~~[Bug 53866](https://bugs.winehq.org/show_bug.cgi?id=53866) - vbscript fails to handle SAFEARRAY in for...each~~
>   - ~~[Bug 53867](https://bugs.winehq.org/show_bug.cgi?id=53867) - vbscript fails to retrieve property array by index~~
>   - ~~[Bug 53868](https://bugs.winehq.org/show_bug.cgi?id=53868) - vbscript fails to return TypeName for VT_DISPATCH~~
>   - ~~[Bug 53873](https://bugs.winehq.org/show_bug.cgi?id=53873) - vbscript fails to compile Else If when If is on same line~~
>   - [Bug 53877](https://bugs.winehq.org/show_bug.cgi?id=53877) - vbscript compile_assignment assertion when assigning multidimensional array by indices
>   - ~~[Bug 53888](https://bugs.winehq.org/show_bug.cgi?id=53888) - vbscript does not allow Mid on non VT_BSTR~~
>   - [Bug 53889](https://bugs.winehq.org/show_bug.cgi?id=53889) - vbscript does not support Get_Item call on IDispatch objects
>   - [Bug 54177](https://bugs.winehq.org/show_bug.cgi?id=54177) - vbscript fails to compile sub call when argument expression contains multiplication
>   - [Bug 54221](https://bugs.winehq.org/show_bug.cgi?id=54221) - vbscript: missing support for GetRef
>   - ~~[Bug 54234](https://bugs.winehq.org/show_bug.cgi?id=54234) - vbscript fails to compile when colon follows Else in If...Else~~
>   - [Bug 54291](https://bugs.winehq.org/show_bug.cgi?id=54291) - vbscript stuck in endless for loop when UBound on Empty and On Error Resume Next
>   - ~~[Bug 54456](https://bugs.winehq.org/show_bug.cgi?id=54456) - vbscript memory leak in For Each with SafeArray as group~~
>   - ~~[Bug 54457](https://bugs.winehq.org/show_bug.cgi?id=54457) - vbscript memory leaks in interp_redim_preserve~~
>   - ~~[Bug 54458](https://bugs.winehq.org/show_bug.cgi?id=54458) - vbscript memory leaks in Global_Split~~
>   - ~~[Bug 54489](https://bugs.winehq.org/show_bug.cgi?id=54489) - vbscript Abs on BSTR returns invalid value~~
>   - ~~[Bug 54490](https://bugs.winehq.org/show_bug.cgi?id=54490) - vbscript fails to compile when statement follows ElseIf~~
>   - ~~[Bug 54493](https://bugs.winehq.org/show_bug.cgi?id=54493) - vbscript fails to compile concat when used without space and expression begins with H~~
>   - ~~[Bug 54731](https://bugs.winehq.org/show_bug.cgi?id=54731) - vbscript: stack_pop_bool doesn't support floats or ole color~~
>   - ~~[Bug 54978](https://bugs.winehq.org/show_bug.cgi?id=54978) - vbscript fails to compile Sub when End Sub on same line~~
>   - [Bug 55006](https://bugs.winehq.org/show_bug.cgi?id=55006) - vbscript single line if else without else body fails compilation
>   - [Bug 55037](https://bugs.winehq.org/show_bug.cgi?id=55037) - vbscript: Colon on new line after Then fails
>   - ~~[Bug 55042](https://bugs.winehq.org/show_bug.cgi?id=55042) - IDictionary::Add() fails to add entries with numerical keys that have the same hashes~~
>   - ~~[Bug 55052](https://bugs.winehq.org/show_bug.cgi?id=55052) - For loop where right bound is string coercion issue~~
>   - [Bug 55093](https://bugs.winehq.org/show_bug.cgi?id=55093) - vbscript: if boolean condition should work without braces
>   - ~~[Bug 55185](https://bugs.winehq.org/show_bug.cgi?id=55185) - vbscript round does not handle numdecimalplaces argument~~
>   - ~~[Bug 55931](https://bugs.winehq.org/show_bug.cgi?id=55931) - vbscript: empty MOD 100000 returns garbage instead of 0~~
>   - ~~[Bug 55969](https://bugs.winehq.org/show_bug.cgi?id=55969) - vbscript fails to return TypeName for Nothing~~
>   - ~~[Bug 56139](https://bugs.winehq.org/show_bug.cgi?id=56139) - scrrun: Dictionary does not allow storing at key Undefined~~
>   - [Bug 56280](https://bugs.winehq.org/show_bug.cgi?id=56280) - vbscript: String coerced to Integer instead of Long?
>   - [Bug 56281](https://bugs.winehq.org/show_bug.cgi?id=56281) - vbscript: String number converted to ascii value instead of parsed value
>   - ~~[Bug 56464](https://bugs.winehq.org/show_bug.cgi?id=56464) - vbscript: Join on array with "empty" items fails~~
>   - [Bug 56480](https://bugs.winehq.org/show_bug.cgi?id=56480) - vbscript: underscore line continue issues
>   - ~~[Bug 56781](https://bugs.winehq.org/show_bug.cgi?id=56781) - srcrrun: Dictionary setting item to object fails~~
>   - [Bug 56931](https://bugs.winehq.org/show_bug.cgi?id=56931) - vbscript: Const used before declaration fails (explicit)
>   - [Bug 57511](https://bugs.winehq.org/show_bug.cgi?id=57511) - vbscript: For loop where loop var is not defined throws error without context
>   - ~~[Bug 57563](https://bugs.winehq.org/show_bug.cgi?id=57563) - vbscript: mid() throws when passed VT_EMPTY instead of returning empty string~~
>   - [Bug 58051](https://bugs.winehq.org/show_bug.cgi?id=58051) - vbscript: Dictionary direct Keys/Items access causes parse error
>   - [Bug 58056](https://bugs.winehq.org/show_bug.cgi?id=58056) - vbscript: Directly indexing a Split returns Empty
>   - [Bug 58248](https://bugs.winehq.org/show_bug.cgi?id=58248) - vbscript: Me(Idx) fails to compile

> - Add support for `Scripting.FileSystemObject` and `Scripting.Dictionary` leveraging Wine's `scrrun` code.
> - Add support for `E_NOTIMPL` commands to Wine's VBScript engine:
>   - `Execute`
>   - `ExecuteGlobal`
>   - `Eval`
>   - `GetRef`

> ***COM / ATL***
> - Visual Pinball uses [COM](https://learn.microsoft.com/en-us/windows/win32/com/the-component-object-model) as a bridge between the scripting engine and itself. The scripting engine uses COM to access other components such as [VPinMAME](https://github.com/vpinball/pinmame) and [B2S.Server](https://github.com/vpinball/b2s-backglass).
> - Visual Pinball uses [ATL](https://learn.microsoft.com/en-us/cpp/atl/active-template-library-atl-concepts?view=msvc-170) to dispatch events from itself to the scripting engine, ex: `DISPID_GameEvents_Init` and `DISPID_HitEvents_Hit`
>
> **Solution**
> - Leverage just enough Wine source code to set up the platform to match Windows, ex: `wchar_t` is 2 bytes in Windows while 4 bytes on most other platforms. (`_WINE_UNICODE_NATIVE_`)
> - Provide stubs for as much COM and ATL code as possible by using Wine and [ReactOS](https://github.com/reactos/reactos/tree/master/sdk/lib/atl) source code.
> - Embed Wine's VBScript engine instead of trying to replicate Windows [RPC](https://learn.microsoft.com/en-us/windows/win32/rpc/rpc-start-page).
> - Replace Windows `typelib` magic with class methods for `GetIDsOfNames`, `Invoke`, `FireDispID`, and `GetDocumentation`
> - Create two `idlparser` utilities that read [Interface Definition File](https://learn.microsoft.com/en-us/windows/win32/midl/interface-definition-idl-file) and automatically generates C and C++ code for `GetIDsOfNames`, `Invoke`, `FireDispID`, and `GetDocumentation`
> - Embed `libpinmame` to provide support for PinMAME based games. [Future]

> ***Graphics***
> - Visual Pinball uses either DirectX or OpenGL for graphics.
>
> **Solution**
> - MacOS uses OpenGL 4.1.
> - Linux (Ubuntu 22.04) uses OpenGL 4.6.
> - Android, iOS, tvOS, Raspberry PI, and RK3588 use OpenGLES 3.0.

> ***Input***
> - Visual Pinball uses XInput and either DirectInput 8 or SDL2 for getting keyboard and joystick info.
>
> **Solution**
> - Make a `SDLK_TO_DIK` translation table that converts SDL Keys to DirectInput Keys.
> - Add additional support for `SDL_KEYUP` and `SDL_KEYDOWN` events.

> ***Updating***
> - The Visual Pinball repository is constantly updated with new features and bug fixes.
>
> **Solution**
> - Updates must be easy to rebase, so all changes must be wrapped in:
>
>   ```
>   #ifdef __STANDALONE__
>   #endif
>   ```
>   or
>   ```
>   #ifndef __STANDALONE__
>   #else
>   #endif
>   ```

## Building

Refer to [make README](../make/README.md#compiling). 

## Running

Go to the build directory:
```
cd build
```

To list all command line arguments:
```
./VPinballX_GL -h
```

To launch a table, execute the following:
```
./VPinballX_GL -play <table.vpx>
```

To launch a table in windowed mode, execute the following:
```
./VPinballX_GL -DisableTrueFullscreen -play <table.vpx>
```

To extract a table script, execute the following:
```
./VPinballX_GL -extractvbs <table.vpx>
```

To list all available fullscreen resolutions and window fullscreen desktop resolutions, execute the following: 
```
./VPinballX_GL -listres
```

To list all available audio devices, execute the following:
```
./VPinballX_GL -listsnd
```

## Joystick Configuration

Visual Pinball Standalone uses **SDL Game Controller** mappings instead of SDL Joystick mappings. To set up your controller, add an entry to the `gamecontrollerdb.txt` file. You can find mappings for many popular controllers in the [SDL Game Controller Database](https://github.com/mdqinc/SDL_GameControllerDB).

Once you've updated `gamecontrollerdb.txt`, adjust the corresponding entries in `VPinballX.ini` for your controller. Here's an example configuration for an Xbox controller:

| Button         | Action            | Key = Value              |
|-----------------|-------------------|--------------------------|
| Left Shoulder   | Left Flipper      | JoyLFlipKey = 10         |
| Right Shoulder  | Right Flipper     | JoyRFlipKey = 11         |
| Left Stick      | Left Magna Save   | JoyLMagnaSave = 8        |
| Right Stick     | Right Magna Save  | JoyRMagnaSave = 9        |
| D-pad Up        | Center Tilt       | JoyCTiltKey = 12         |
| D-pad Left      | Left Tilt         | JoyLTiltKey = 14         |
| D-pad Right     | Right Tilt        | JoyRTiltKey = 15         |
| D-pad Down      | Plunger           | JoyPlungerKey = 13       |
| A               | Add Credit        | JoyAddCreditKey = 1      |
| B               | Start             | JoyStartGameKey = 2      |
| X               | FPS               | JoyFrameCount = 3        |
| Y               | Cancel            | JoyPMCancel = 4          |
| Guide           | Fire              | JoyLockbarKey = 6        |

Note: Game controller button indexes in `VPinballX.ini` are **1-based**, meaning they start from **1** instead of **0**.

## Keyboard

If you want to customize the keyboard input, update the `; Keyboard input mappings` section in the `VPinballX.ini` file.

The settings are decimal values based on DirectInput `DIK_` values found [here](https://gitlab.winehq.org/wine/wine/-/blob/master/include/dinput.h#L470-607).

| Key | Value | Notes |
| --- | --- | --- |
| ESCAPE | 1 | |
| 1 | 2 | |
| 2 | 3 | |
| 3 | 4 | |
| 4 | 5 | |
| 5 | 6 | |
| 6 | 7 | |
| 7 | 8 | |
| 8 | 9 | |
| 9 | 10 | |
| 0 | 11 | |
| MINUS | 12 | - on main keyboard |
| EQUALS | 13 | |
| BACK | 14 | Backspace |
| TAB | 15 | |
| Q | 16 | |
| W | 17 | |
| E | 18 | |
| R | 19 | |
| T | 20 | |
| Y | 21 | |
| U | 22 | |
| I | 23 | |
| O | 24 | |
| P | 25 | |
| LBRACKET | 26 | |
| RBRACKET | 27 | |
| RETURN | 28 | Enter on main keyboard |
| LCONTROL | 29 | |
| A | 30 | |
| S | 31 | |
| D | 32 | |
| F | 33 | |
| G | 34 | |
| H | 35 | |
| J | 36 | |
| K | 37 | |
| L | 38 | |
| SEMICOLON | 39 | |
| APOSTROPHE | 40 | |
| GRAVE | 41 | |
| LSHIFT | 42 | |
| BACKSLASH | 43 | |
| Z | 44 | |
| X | 45 | |
| C | 46 | |
| V | 47 | |
| B | 48 | |
| N | 49 | |
| M | 50 | |
| COMMA | 51 | |
| PERIOD | 52 | . on main keyboard |
| SLASH | 53 | / on main keyboard |
| RSHIFT | 54 | |
| MULTIPLY | 55 | * on numeric keypad |
| LMENU | 56 | Left Alt |
| SPACE | 57 | |
| CAPITAL | 58 | |
| F1 | 59 | |
| F2 | 60 | |
| F3 | 61 | |
| F4 | 62 | |
| F5 | 63 | |
| F6 | 64 | |
| F7 | 65 | |
| F8 | 66 | |
| F9 | 67 | |
| F10 | 68 | |
| NUMLOCK | 69 | |
| SCROLL | 70 | Scroll Lock |
| NUMPAD7 | 71 | |
| NUMPAD8 | 72 | |
| NUMPAD9 | 73 | |
| SUBTRACT | 74 | - on numeric keypad |
| NUMPAD4 | 75 | |
| NUMPAD5 | 76 | |
| NUMPAD6 | 77 | |
| ADD | 78 | + on numeric keypad |
| NUMPAD1 | 79 | |
| NUMPAD2 | 80 | |
| NUMPAD3 | 81 | |
| NUMPAD0 | 82 | |
| DECIMAL | 83 | . on numeric keypad |
| F11 | 87 | |
| F12 | 88 | |
| F13 | 100 | |
| F14 | 101 | |
| F15 | 102 | |
| NUMPADEQUALS | 141 | = on numeric keypad |
| AT | 145 | |
| COLON | 146 | |
| UNDERLINE | 147 | |
| STOP | 149 | |
| NUMPADENTER | 156 | Enter on numeric keypad |
| RCONTROL | 157 | |
| NUMPADCOMMA | 179 | , on numeric keypad |
| DIVIDE | 181 | / on numeric keypad |
| RMENU | 184 | Right Alt |
| PAUSE | 197 | Pause |
| HOME | 199 | Home on arrow keypad |
| UP | 200 | Up Arrow on arrow keypad |
| PRIOR | 201 | Page Up on arrow keypad |
| LEFT | 203 | Left Arrow on arrow keypad |
| RIGHT | 205 | Right Arrow on arrow keypad |
| END | 207 | End on arrow keypad |
| DOWN | 208 | Down Arrow on arrow keypad |
| NEXT | 209 | Page Down on arrow keypad |
| INSERT | 210 | Insert on arrow keypad |
| DELETE | 211 | Delete on arrow keypad |
| LWIN | 219 | Left Windows |
| RWIN | 220 | Right Windows |

## External DMDs

MacOS and Linux builds have built in support for [ZeDMD](https://www.pincabpassion.net/t14796-zedmd-installation-english) and [Pixelcade](https://pixelcade.org/) devices.

This feature is powered by [libdmdutil](https://github.com/vpinball/libdmdutil).

## Paths

Here are some important paths and files for MacOS and Linux:

| Path | Notes |
| --- | --- |
`$HOME/.vpinball/user` | VPinball user directory |
`$HOME/.vpinball/music` | VPinball music directory |
`$HOME/.vpinball/VPinballX.ini` | VPinball settings (created on first run) |
`$HOME/.vpinball/VPReg.ini` | <sup>*</sup>VPinball table settings |
`$HOME/.vpinball/vpinball.log` | VPinball log file |
`$HOME/.pinmame` | <sup>**</sup>PinMAME root directory |
`$HOME/.pinmame/roms` | <sup>**</sup>PinMAME ROMs directory |
`$HOME/.pinmame/nvram` | <sup>**</sup>PinMAME NVRAM directory  |
`$HOME/.pinmame/altcolor` | <sup>**</sup>Serum colorizations directory `<rom_name>/<rom.cRZ>` |

Notes:

- The command-line option `-PrefPath <path>` can be used to override `$HOME/.vpinball`
- <sup>*</sup>The `VPRegPath` entry in the `[Standalone]` section of `VPinballX.ini` can be used to override the VPinball table settings. If set to `./`, the current table path will be used.
- <sup>**</sup>The `PinMAMEPath` entry in the `[Plugin.PinMAME]` section of `VPinballX.ini` will only be used if no `pinmame` folder exists in the current table directory. If not set `$HOME/.pinmame` will be used.

## Debugging

Debugging can be done using [Visual Studio Code](https://code.visualstudio.com/).

### MacOS

Perform the steps outlined above in *Compiling* and *Running*.

In Visual Studio Code:
  - Install the [`C/C++ Extension Pack`](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack) extension.
  - Open the `vpinball` folder.
  - If prompted, select the latest version of clang, ex: `Clang 14.0.0 arm64-apple-darwin21.6.0`
  - Go to `Settings` -> `CMake: Debug Config` and click `Edit in settings.json`
  - Update `settings.json` with:
  ```
      "cmake.debugConfig": {
         "args": [ "-DisableTrueFullscreen", "-Play", "assets/exampleTable.vpx" ],
      }
  ```
  - Click the bug button (to the left of the play button) in the bottom bar

### Raspberry Pi 4

Perform the steps outlined above in *Compiling* and *Running*.

Since we are using the Lite version of Raspberry Pi OS, there is no X11 to run the full version Visual Studio Code. Instead will we use the [Remote Development](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.vscode-remote-extensionpack) extension in Visual Studio Code running on a host computer.

**NOTE**: The 2GB version of the Raspberry Pi 4 can easily run out of memory and become unresponsive.

Make sure SSH is configured and enabled on your Raspberry Pi.

On the host computer, in Visual Studio Code:
  - Install the [`Remote Development`](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.vscode-remote-extensionpack) extension.
  - Select `Remote-SSH: Connect to Host...` from the Command Palette (F1, ⇧⌘P)
  - Enter the `user@address` of the Raspberry Pi.
  - Once connected, install the [`C/C++ Extension Pack`](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack) extension.
  - If prompted, select the latest version of gcc, ex: `GCC 10.2.1 aarch64-linux-gnu`
  - Go to `Settings` (`Remote SSH`) -> `CMake: Debug Config` and click `Edit in settings.json`
  - Update `settings.json` with:
  ```
    "cmake.parallelJobs": 1,
    "cmake.generator": "Unix Makefiles",
    "cmake.debugConfig": {
        "args": [ "-Play", "assets/exampleTable.vpx" ],
    }
  ```
  - Click the bug button (to the left of the play button) in the bottom bar

## Workarounds

### My game is not displaying a DMD

Many newer tables render the DMD inside Visual Pinball's window using Textbox objects. Other tables will rely on positioning VPinMAME or FlexDMD's application window over Visual Pinball's window. Since standalone does not support rendering a separate VPinMAME or FlexDMD window, you need to determine if the table already has a Textbox with the visible property set to false. You can do this by searching the Live Editor. Some possible names to search for are: `Textbox01`, `Textbox001`, `Scorebox`, `Scoretext`, `DMD`.

Standalone also adds an `ImplicitDMD` object to all tables just for this specific reason. To enable it, you'll need to add some code to the table script.

For tables that use VPinMAME:

```
Dim UseVPMDMD : UseVPMDMD = true 
' Dim UseVPMColoredDMD : UseVPMColoredDMD = true

Sub ImplicitDMD_Init
   Me.x = 30
   Me.y = 30
   Me.width = 128 * 2
   Me.height = 32 * 2
   Me.fontColor = RGB(255, 0, 0)
   Me.visible = true
   Me.intensityScale = 1.5
End Sub
```

Notes:

- This must appear before the `LoadVPM` call. 
- Always check the table for existing `UseVPMDMD` or `UseVPMColoredDMD` variables.

For tables that use FlexDMD:

```
Sub ImplicitDMD_Init
   Me.x = 30
   Me.y = 30
   Me.width = 128 * 2
   Me.height = 32 * 2
   Me.fontColor = RGB(255, 255, 255)
   Me.visible = true
   Me.intensityScale = 1.5
   Me.timerenabled = true
End Sub

Sub ImplicitDMD_Timer
   Dim DMDp: DMDp = FlexDMD.DMDColoredPixels
   If Not IsEmpty(DMDp) Then
      DMDWidth = FlexDMD.Width
      DMDHeight = FlexDMD.Height
      DMDColoredPixels = DMDp
   End If
End Sub
```

Notes:

- Replace the `FlexDMD` variable accordingly.
- Replace `DMDColoredPixels` with `DMDPixels` if your FlexDMD is not RGB24.

### DTArray (Drop Targets)

Wine's vbscript engine cannot handle multi-dimension array assignments. For example:

```
DTArray(i)(4) = DTAnimate(DTArray(i)(0),DTArray(i)(1),DTArray(i)(2),DTArray(i)(3),DTArray(i)(4))
```

1) Switch to use the `DropTarget` class, by adding the following code:

```
Class DropTarget
  Private m_primary, m_secondary, m_prim, m_sw, m_animate, m_isDropped

  Public Property Get Primary(): Set Primary = m_primary: End Property
  Public Property Let Primary(input): Set m_primary = input: End Property

  Public Property Get Secondary(): Set Secondary = m_secondary: End Property
  Public Property Let Secondary(input): Set m_secondary = input: End Property

  Public Property Get Prim(): Set Prim = m_prim: End Property
  Public Property Let Prim(input): Set m_prim = input: End Property

  Public Property Get Sw(): Sw = m_sw: End Property
  Public Property Let Sw(input): m_sw = input: End Property

  Public Property Get Animate(): Animate = m_animate: End Property
  Public Property Let Animate(input): m_animate = input: End Property

  Public Property Get IsDropped(): IsDropped = m_isDropped: End Property
  Public Property Let IsDropped(input): m_isDropped = input: End Property

  Public default Function init(primary, secondary, prim, sw, animate, isDropped)
    Set m_primary = primary
    Set m_secondary = secondary
    Set m_prim = prim
    m_sw = sw
    m_animate = animate
    m_isDropped = isDropped

    Set Init = Me
  End Function
End Class
```

2) Update the DT definitions to use `DropTarget` instead of `Array`:

```
DT7 = Array(dt1, dt1a, pdt1, 7, 0, false)
DT27 = Array(dt2, dt2a, pdt2, 27, 0, false)
DT37 = Array(dt3, dt3a, pdt3, 37, 0, false)
```

becomes:

```
Set DT7 = (new DropTarget)(dt1, dt1a, pdt1, 7, 0, false)
Set DT27 = (new DropTarget)(dt2, dt2a, pdt2, 27, 0, false)
Set DT37 = (new DropTarget)(dt3, dt3a, pdt3, 37, 0, false)
```

3) Search and replace:

| From | To | Vi |
| --- | --- | --- |
| `DTArray(i)(0)` | `DTArray(i).primary` | `:%s/DTArray(i)(0)/DTArray(i).primary/g` |
| `DTArray(i)(1)` | `DTArray(i).secondary` | `:%s/DTArray(i)(1)/DTArray(i).secondary/g` |
| `DTArray(i)(2)` | `DTArray(i).prim` | `:%s/DTArray(i)(2)/DTArray(i).prim/g` |
| `DTArray(i)(3)` | `DTArray(i).sw` | `:%s/DTArray(i)(3)/DTArray(i).sw/g` |
| `DTArray(i)(4)` | `DTArray(i).animate` | `:%s/DTArray(i)(4)/DTArray(i).animate/g` |
| `DTArray(i)(5)` | `DTArray(i).isDropped` | `:%s/DTArray(i)(5)/DTArray(i).isDropped/g` |
| `DTArray(ind)(5)` | `DTArray(ind).isDropped` | `:%s/DTArray(ind)(5)/DTArray(ind).isDropped/g` |

### STArray (Standup Targets)

Wine's vbscript engine cannot handle multi-dimension array assignments. For example:

```
STArray(i)(3) = STCheckHit(Activeball,STArray(i)(0))
```

1) Switch to use the `StandupTarget` class, by adding the following code:

```
Class StandupTarget
  Private m_primary, m_prim, m_sw, m_animate

  Public Property Get Primary(): Set Primary = m_primary: End Property
  Public Property Let Primary(input): Set m_primary = input: End Property

  Public Property Get Prim(): Set Prim = m_prim: End Property
  Public Property Let Prim(input): Set m_prim = input: End Property

  Public Property Get Sw(): Sw = m_sw: End Property
  Public Property Let Sw(input): m_sw = input: End Property

  Public Property Get Animate(): Animate = m_animate: End Property
  Public Property Let Animate(input): m_animate = input: End Property

  Public default Function init(primary, prim, sw, animate)
    Set m_primary = primary
    Set m_prim = prim
    m_sw = sw
    m_animate = animate

    Set Init = Me
  End Function
End Class
```

2) Update the ST definitions to use `StandupTarget` instead of `Array`:

```
ST41 = Array(sw41, Target_Rect_Fat_011_BM_Lit_Room, 41, 0)
ST42 = Array(sw42, Target_Rect_Fat_010_BM_Lit_Room, 42, 0)
ST43 = Array(sw43, Target_Rect_Fat_005_BM_Lit_Room, 43, 0)
```

becomes:

```
Set ST41 = (new StandupTarget)(sw41, Target_Rect_Fat_011_BM_Lit_Room, 41, 0)
Set ST42 = (new StandupTarget)(sw42, Target_Rect_Fat_010_BM_Lit_Room, 42, 0)
Set ST43 = (new StandupTarget)(sw43, Target_Rect_Fat_005_BM_Lit_Room, 43, 0)
```

3) Search and replace:

| From | To | Vi |
| --- | --- | --- |
| `STArray(i)(0)` | `STArray(i).primary` | `:%s/STArray(i)(0)/STArray(i).primary/g` |
| `STArray(i)(1)` | `STArray(i).prim` | `:%s/STArray(i)(1)/STArray(i).prim/g` |
| `STArray(i)(2)` | `STArray(i).sw` | `:%s/STArray(i)(2)/STArray(i).sw/g` |
| `STArray(i)(3)` | `STArray(i).animate` | `:%s/STArray(i)(3)/STArray(i).animate/g` |

### BSize and BMass Constants

Some scripts define `BSize` and `BMass` as `Const` and `core.vbs` re-defines as `Dim`. If this causes errors:

1) Replace the following:

```
Const BSize = 25
Const BMass = 1.7
```

with

```
Dim BSize : BSize = 25
Dim BMass : BMass = 1.7
```

or convert to use:

```
Const BallSize = 50
Const BallMass = 1
```

## VBScript Issues

See [vbscript-issues](docs/vbscript.md) for more information.

## Table Patches

Some older tables need to have their scripts patched in order to run.

If you find a table that does not work, please check the [vpx-standalone-scripts](https://github.com/jsm174/vpx-standalone-scripts) repository.

## Shoutouts

Wine and the amazing people who support the VBScript engine
- Robert Wilhelm
- Nikolay Sivov
- Jacek Caban  

The people who make this such an exciting hobby to be a part of
- @Apophis, @Bord, @ClarkKent, @Cupiii, @ecurtz, @freezy, @Iaaki, @Lucidish, @mkalkbrenner, @Niwak, @onevox, @Scottacus64, @Somatik, @superhac, @Thalamus, @toxie, @wylte, @Zedrummer
- and the rest of the Visual Pinball community!
