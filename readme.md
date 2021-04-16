# libmmenu

[MinUI](https://github.com/shauninman/minui) doubles down on the good ideas of the official TrimUI firmware: the simplicity and consistency of the experience. The best of which is probably the in-emulator menu. libmmenu reimplements this functionality but extends it to allow opening an emulator's built-in settings from that menu (as well as changing discs in multi-disc games and resuming the last used save state on launch when requested).

## Prerequisites

To be a good citizen in the MinUI eco-system an emulator should observe the following:

* The MENU button (and only the MENU button) should open the menu (and only open the menu). The player should not be able to rebind the MENU button.
* Emulators should save state and config files (including bios) inside a _hidden_ folder inside their rom folder.
* Default key bindings should map as closely as possible to the original console, either by position, or preferably, by name (eg. don't map shoulder buttons or START/SELECT to Y/X). While unlabeled on the device, the agreed upon arrangement is:

		    L                                                   R
		       UP                                          X/△
		LEFT        RIGHT                           Y/□           A/◯
		      DOWN         SELECT    MENU    START         B/☓

* Power and reset buttons or custom hotkeys should not be bound by default (eg. Mapping "load quicksave" to L while convenient will conflict with the system-wide brightness shortcut and could cause an unsuspecting player to lose their progress. Mapping power and reset to START and SELECT is just cruel.)
* Emulators should not draw messages on screen, "Press MENU to open menu" and "Loaded save slot 0" should be obvious. On such a small screen, these messages can obscure important gameplay information that can, at best, result in an inconvenient delay, or at worst, a game over.
* Emulators should clear the screen to black before quitting so input feels responsive.
* Emulators should default to integer scaling where possible or native if not. (The one exception I've found is the [1.5x Sharp scaler][scaler] I created that prioritizes consistent stroke weight in lineart and text for scaling the Game Boy's 160x144 screen.)

[scaler]:https://github.com/shauninman/gambatte-dms/blob/trimui-model-s/gambatte_sdl/scaler.c#L116

Design is how it works, let's keep it simple and consistent.

## How it works

### `ShowMenu()`

Instead of calling an emulator's built-in menu function, you call libmmenu's:

 	MenuReturnStatus ShowMenu(char* rom_path, char* save_path_template, SDL_Surface* frame, MenuReturnEvent keyEvent)

`rom_path` is the full path to the currently open rom (essentially `argv[1]`, eg. `/mnt/SDCARD/Roms/Game Boy/Tetris.gb`).

`save_path_template` is the full path to a save state with an `%i` in place of the slot number. This is usually unique to the emulator but shouldn't be too hard to create (eg. `/mnt/SDCARD/Roms/Game Boy/.gambatte/saves/Tetris_%i.gqs`).

`screen` is a pointer to an `SDL_Surface` representing the fullscreen of the emulator (usually the return value of the `SDL_SetVideoMode()` call). It should be 320x160. (Don't pass the emulated console's screen!) libmmenu makes a copy and leaves the original surface untouched.

`keyEvent` is an enum (or integer) indicating whether the function should return on keydown or keyup. Most emulators can pass in `kMenuEventKeyDown` or `0` but if the emulator doesn't distinguish between a button being pressed and a button _just_ being pressed you'll need to pass in `kMenuEventKeyUp` or `1`.

The `MenuReturnStatus` is an enum (or integer) value that tells the emulator which action was requested by the player in the menu.

`kStatusContinue` or `0` requests that emulation to continue as if the built-in menu had been exited normally.

`kStatusOpenMenu` or `23` requests that the built-in menu be opened.

`kStatusChangeDisc` or `24` requests that the disc be changed.

`kStatusExitGame` or `31` requests that the emulator itself quit.

`kStatusSaveSlot` and `kStatusLoadSlot` are slightly different. An integer greater than or equal to either of these also encodes which slot should be saved or loaded. Simply subtract the enum (or `1` and `11`) from the value to get the slot number, 0-7. Sounds kinda complicated but the example below contains the specific logic you can use to handle these return values.

### `ChangeDisc()`

When `ShowMenu()` returns `kStatusChangeDisc` you need to call an additional function to get the path to the new disc:

	int ChangeDisc(char* disc_path);

`disc_path` should be a `char` buffer (`char[256]` is used internally) and will be set to the path of the requested disc. 

It will return `1` if `disc_path` exists and was set or `0` if not.

### `ResumeSlot()`

To know which (if any) save state to load on launch you need to call this function:

	int ResumeSlot(void);

The return value is a 0-based slot number or `-1` if the player does not want to resume from the last used save state.

## Building

Checkout or download this repo, open up the makefile, and make sure `CROSS_COMPILE` and `PREFIX` point to the correct locations for your toolchain and includes/libs. Then run `make` to build and install mmenu.h and libmmenu.so.

## Integrating

### ShowMenu()

Open up the emulator's makefile and add the following to `LDFLAGS`:

	-lSDL_image -lSDL_ttf -ldl

If present, remove the following from `LDFLAGS` (otherwise the above libraries won't be linked if they're not already used by the emulator):

	-Wl,--as-needed

Determine where the emulator's menu function is called (usually in an input handling loop). Add the following headers:

	#include <dlfcn.h>
	#include <mmenu.h> // wrap in extern "C" { } if adding to a .cpp file

Create a static variable to hold the pointer to the libmmenu library:

	static void* mmenu = NULL; // extern in a header if main() and your input handler are in separate files

Then in `main()` (or equivalent) try to open the library:

	mmenu = dlopen("libmmenu.so", RTLD_LAZY);

Finally, replace the call to the emulator's menu function (simply called `menu()` in this example) with the following:

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

### ChangeDisc()

For disc-based systems MinUI supports m3u files for multi-disc games and `ShowMenu()` supports quickly changing discs. To handle that properly you will need a char buffer and `ChangeDisc()`:

	char disc_path[256];
	ChangeDisc_t ChangeDisc = (ChangeDisc_t)dlsym(mmenu, "ChangeDisc");

Then handle the status returned by `ShowMenu()` by calling the emulator's disc change function (called `change_disc()` in this example):

	else if (status==kStatusChangeDisc && ChangeDisc(disc_path)) {
		change_disc(disc_path);
	}

### ResumeSlot()

All emulators provided with MinUI support launching directly into the most recently used save state. `ShowMenu()` will keep track of which one that is (since file timestamps are useless on this device) but each emulator still needs to check for and act on a request to resume on launch. For that you'll call `ResumeSlot()`:

	int resume_slot = -1;
	if (mmenu) {
		ResumeSlot_t ResumeSlot = (ResumeSlot_t)dlsym(mmenu, "ResumeSlot");
		if (ResumeSlot) resume_slot = ResumeSlot();
	}

Then once the emulator has loaded the rom and is ready to load states, call the emulator's load state function (called `load_state()` in this example):

	if (resume_slot!=-1) {
		load_state(resume_slot);
		resume_slot = -1; // be sure to reset if this needs to live in an loop
	}

Some emulators need a couple frames to initialize audio correctly so you might need to use something like `SDL_GetTicks()` to track elapsed time and delay loading the state. Others might already support command line options for this behavior in which case you can just insert `resume_slot` into the existing logic.

## Custom launcher considerations

If you are creating a custom launcher for Trimui (hi eggs!) and want to support libmmenu you just need to be sure to add `/mnt/SDCARD/System/lib` to your `LD_LIBRARY_PATH`:

	export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/mnt/SDCARD/System/lib"

That's it!