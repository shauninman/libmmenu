#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>

#include <mmenu.h>

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

static void error_handler(int sig) {
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "test Error: signal %d:\n", sig);
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

static SDL_Surface* screen;
static SDL_Surface* buffer;

static SDL_Surface* tmp;
void render(void) {
	SDL_FillRect(buffer, &buffer->clip_rect, 0);
	SDL_BlitSurface(tmp, NULL, buffer, NULL);
	SDL_BlitSurface(buffer, NULL, screen, NULL);
	SDL_Flip(screen);
}

#define TRIMUI_UP 		SDLK_UP
#define TRIMUI_DOWN 	SDLK_DOWN
#define TRIMUI_LEFT 	SDLK_LEFT
#define TRIMUI_RIGHT 	SDLK_RIGHT
#define TRIMUI_A 		SDLK_SPACE
#define TRIMUI_B 		SDLK_LCTRL
#define TRIMUI_X 		SDLK_LSHIFT
#define TRIMUI_Y 		SDLK_LALT
#define TRIMUI_START 	SDLK_RETURN
#define TRIMUI_SELECT 	SDLK_RCTRL
#define TRIMUI_L 		SDLK_TAB
#define TRIMUI_R 		SDLK_BACKSPACE
#define TRIMUI_MENU 	SDLK_ESCAPE

int main(int argc , char* argv[]) {
	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(0);
	
	screen = SDL_SetVideoMode(320, 240, 16, SDL_HWSURFACE | SDL_DOUBLEBUF);
	buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0, 0, 0, 0);
	
	tmp = IMG_Load("tmp.png");
	
	render();
	
	int quit = 0;
	SDL_Event event;
	while (!quit) {
		while (SDL_PollEvent(&event)) {
			switch( event.type ){
				case SDL_KEYDOWN:
				if (event.key.keysym.sym==TRIMUI_MENU) {
					int status = ShowMenu("/mnt/SDCARD/Roms/Genesis/Sonic the Hedgehog (U).bin", NULL, buffer);
					
					if (status==kStatusExitGame) {
						puts("exit game");
						quit = 1;
					}
					else if (status==kStatusOpenMenu) {
						puts("open emulator menu");
					}
					else if (status>=kStatusLoadSlot) {
						int slot = status - kStatusLoadSlot;
						printf("load slot %i\n", slot);
					}
					else if (status>=kStatusSaveSlot) {
						int slot = status - kStatusSaveSlot;
						printf("save slot %i\n", slot);
					}
					else {
						printf("continue (%i)\n", status);
					}
					
					render();
				}
				else if (event.key.keysym.sym==TRIMUI_B) {
					quit = 1;
				}
				break;
			}
		}
	}
	
	SDL_FillRect(buffer, &buffer->clip_rect, 0);
	SDL_BlitSurface(buffer, NULL, screen, NULL);
	SDL_Flip(screen);
	
	SDL_FreeSurface(tmp);
	SDL_FreeSurface(screen);
	SDL_FreeSurface(buffer);
	SDL_Quit();
	return 0;
}