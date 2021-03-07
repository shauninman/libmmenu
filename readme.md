# libmmenu

[MinUI](https://github.com/shauninman/minui) aims to double-down on the good ideas of the official TrimUI firmware: the simplicity and consistency of the UI. The best of which is probably the in-emulator menu. libmmenu reimplements this functionality but extends it to allow opening an emulator's built-in settings from that menu.

## Prerequisites

To be a good citizen in the MinUI eco-system an emulator should observe the following:

* The MENU button (and only the MENU button) should (only) open the menu.
* Emulators should save state and config files inside a _hidden_ folder inside their rom folder.
* Default key bindings should map as closely as possible to the original console, either by position, or preferably, by name. While unlabeled on the device, the agreed upon arrangement is:

		    L                                                   R
		       UP                                          X/△
		LEFT        RIGHT                           Y/□           A/◯
		      DOWN         SELECT    MENU    START         B/☓

* Power and reset buttons or custom hotkeys should not be bound by default (eg. Mapping "load quicksave" to L while convenient will conflict with the system-wide brightness shortcut and could cause an unsuspecting player to lose their progress. Mapping power and reset to START and SELECT is just cruel.)
* Emulators should not draw messages on screen, "Press MENU to open menu" and "Loaded save slot 0" should be obvious. On such a small screen, these messages can obscure important gameplay information that can, at best, result in an inconvenient delay, or at worst, a game over.
* Emulators should clear the screen to black before quitting so input feels responsive.

## How it works

### The function

Instead of calling an emulator's built-in menu function, you call libmmenu's:

 	MenuReturnStatus ShowMenu(char* rom_path, char* save_path_template, SDL_Surface* frame, MenuReturnEvent keyEvent)`

`rom_path` is the full path to the currently open rom (essentially `argv[1]`, eg. `/mnt/SDCARD/Roms/Game Boy/Tetris.gb`).

`save_path_template` is the full path to a save state with an `%i` in place of the slot number. This is usually unique to the emulator but shouldn't be too hard to create (eg. `/mnt/SDCARD/Roms/Game Boy/.gambatte/saves/Tetris_%i.gqs`).

`screen` is a pointer to an `SDL_Surface` representing the fullscreen of the emulator (usually the return value of the `SDL_SetVideoMode()` call). It should be 320x160. (Don't pass the emulated console's screen!) libmmenu makes a copy and leaves the original surface untouched.

`keyEvent` is an enum (or integer) indicating whether the function should return on keydown or keyup. Most emulators can pass in `kMenuEventKeyDown` or `0` but if the emulator doesn't distinguish between a button being pressed and a button _just_ being pressed you'll need to pass in `kMenuEventKeyUp` or `1`.

### The return value

The `MenuReturnStatus` is an enum (or integer) value that tells the emulator which action was requested by the player in the menu.

`kStatusContinue` or `0` requests that emulation to continue as if the built-in menu had been exited normally.

`kStatusOpenMenu` or `23` requests that the built-in menu be opened.

`kStatusExitGame` or `31` requests that the emulator itself quit.

`kStatusSaveSlot` and `kStatusLoadSlot` are slightly different. An integer greater than or equal to either of these also encodes which slot should be saved or loaded. Simply subtract the enum (or `1` and `11`) from the value to get the slot number, 0-7. Sounds kinda complicated but the example below contains the specific logic you can use to handle these return values.

## Building

Checkout or download this repo, open up the makefile, and make sure `CROSS_COMPILE` and `PREFIX` point to the correct locations for your toolchain and includes/libs. Then run `make` to build and install mmenu.h and libmmenu.so.

## Integrating

Open up the emulator's makefile and add the following to `LDFLAGS`:

	-lSDL_image -lSDL_ttf -ldl

Determine where the emulator's menu function is called (usually in an input handling loop). Add the following headers:

	#include <dlfcn.h>
	#include <mmenu.h> // wrap in extern "C" { } if adding to a .cpp file

Create a static variable to hold the pointer to the libmmenu library:

	static void* mmenu = NULL; // extern in a header if main() and your input handler are in separate files

Then in `main()` (or equivalent) try to open the library:

	mmenu = dlopen("libmmenu.so", RTLD_LAZY);

Finally, replace the call to the emulator's menu function (in this example simply called `menu()`) with the following:

	if (mmenu) {
		ShowMenu_t ShowMenu = (ShowMenu_t)dlsym(mmenu, "ShowMenu");
		MenuReturnStatus status = ShowMenu(rom_path, save_path, screen, kMenuEventKeyDown);
	
		if (status==kStatusExitGame) {
			// exiting varies from emulator to emulator
			// see its menu() function to determine how
		}
		else if (status==kStatusOpenMenu) {
			menu();
		}
		else if (status>=kStatusLoadSlot) {
			int slot = status - kStatusLoadSlot;
			// load state from slot, again check
			// the emulator's menu() function
		}
		else if (status>=kStatusSaveSlot) {
			int slot = status - kStatusSaveSlot;
			// save state to slot, again check
			// the emulator's menu() function
		}
	}
	else {
		menu();
	}

You can try closing the library before exiting the emulator but it is not actually necessary and in at least one emulator I added it to caused a crash so it's probably better to leave this out:

	if (mmenu) dlclose(mmenu);

That's it!