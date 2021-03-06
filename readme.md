# libmmenu

MinUI aims to double-down on the good ideas of the official TrimUI firmware: the simplicity and consistency of the UI. The best of which is probably the in-emulator menu. libmmenu reimplements this functionality but extends it to allow opening the emulator's built-in settings from that menu.

## Prerequisites

To be a good citizen in the MinUI eco-system an emulator should observe a following:

* The MENU button (and only the MENU button) should open the menu.
* Emulators should save state and config files inside a _hidden_ folder inside their rom folder.
* Default key bindings should map as closely as possible to the original console, either by position, or preferably, by name. While unlabeled on the device, the agreed upon arrangement is:

		    L                                                   R
		       UP                                          X/△
		LEFT        RIGHT                           Y/□           A/◯
		      DOWN         SELECT    MENU    START         B/☓

* Power and reset buttons or custom hotkeys should not be bound by default (eg. mapping "load quicksave" to L while convenient will conflict with the system-wide brightness shortcut and could cause an unsuspecting player to lose their progress)
* Emulators should not draw messages on screen, "Press MENU to open menu" and "Loaded save slot 0" should be obvious. On such a small screen, these messages can obscure important gameplay information that can, at best, result in an inconvenient delay, or at worst, a game over

## How it works

### The function

Instead of calling an emulator's built-in menu function, you call libmmenu's:

 	MenuReturnStatus ShowMenu(char* rom_path, char* save_path_template, SDL_Surface* frame, MenuReturnEvent keyEvent)`

`rom_path` is the full path to the currently open rom (essentially `argv[1]`, eg. `/mnt/SDCARD/Roms/Game Boy/Tetris.gb`).

`save_path_template` is the full path to a save state with an `%i` in place of the slot number. This is usually unique to the emulator but shouldn't be too hard to create (eg. `/mnt/SDCARD/Roms/Game Boy/.gambatte/saves/Tetris_%i.gqs`).

`screen` is a pointer to an `SDL_Surface` representing the fullscreen of the emulator (usually the return value of the `SDL_SetVideoMode()` call). libmmenu makes a copy of the surface and leaves the original SDL_Surface untouched.

`keyEvent` is an enum (or integer) indicating whether the function should return on keydown or keyup. Most emulators can pass in `kMenuEventKeyDown` or `0` but if the emulator doesn't distinguish between a button being pressed and a button _just_ being pressed you'll need to pass in `kMenuEventKeyUp` or `1`.

### The return value

TODO: finish me!

## Building

Checkout or download this repo, open up the makefile, and make sure `CROSS_COMPILE` and `PREFIX` point to the correct locations for your toolchain and includes/libs. Then run `make` to build and install mmenu.h and libmmenu.so.

## Integrating

Open up the emulator's makefile and add the following to `LDFLAGS`:

	-lSDL_image -lSDL_ttf -ldl

Next, determine where the emulator's menu function is called from.