#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "mmenu.h"

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

static SDL_Surface* screen;
static SDL_Surface* buffer;
static SDL_Surface* overlay;
static SDL_Surface* ui_top_bar;
static SDL_Surface* ui_bottom_bar;
static SDL_Surface* ui_menu_bg;
static SDL_Surface* ui_menu3_bg;
static SDL_Surface* ui_menu_bar;
static SDL_Surface* ui_slot_bg;
static SDL_Surface* ui_slot_overlay;
static SDL_Surface* ui_disc_bg;
static SDL_Surface* ui_browse_icon;
static SDL_Surface* ui_round_button;
static SDL_Surface* ui_menu_icon;
static SDL_Surface* ui_arrow_right;
static SDL_Surface* ui_arrow_right_w;
static SDL_Surface* ui_selected_dot;
static SDL_Surface* ui_empty_slot;
static SDL_Surface* ui_no_preview;

static TTF_Font* font;
static TTF_Font* tiny;

static SDL_Color gold = {0xd2,0xb4,0x6c};
static SDL_Color bronze = {0x9f,0x89,0x52};
static SDL_Color white = {0xff,0xff,0xff};

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
static void error_handler(int sig) {
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "mmenu Error: signal %d:\n", sig);
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

///////////////////////////////////////

// only use to write single-line files!
static void put_file(char* path, char* contents) {
	FILE* file = fopen(path, "w");
	fputs(contents, file);
	fclose(file);
}
static void get_file(char* path, char* buffer) {
	FILE *file = fopen(path, "r");
	fseek(file, 0L, SEEK_END);
	size_t size = ftell(file);
	rewind(file);
	fread(buffer, sizeof(char), size, file);
	fclose(file);
	buffer[size] = '\0';
}

static int exists(char* path) {
	return access(path, F_OK)==0;
}

///////////////////////////////////////
// TODO: just copied from MinUI/main.c :shrug:

static int* key[10];
static void (*loadSystemState)(void*);
static void (*saveSystemState)(void*);
static int (*GetKeyShm)(void*,int);
static void (*SetKeyShm)(void*,int,int);

static int getVolume(void) {
	return GetKeyShm(key, 0);
}
static int getBrightness(void) {
	return GetKeyShm(key, 1);
}

static void setVolume(int value) { // 0-20
	// SetKeyShm(key,0,value);
	// saveSystemState(key);
	
	int val = value * (63.0f / 20.0f);
	char cmd[32];
	sprintf(cmd, "tinymix set 22 %d", val);
	system(cmd);
}
static void setBrightness(int value) { // 0-10
	// SetKeyShm(key,1,value);
	// saveSystemState(key);
	
	// int val = value<9 ? 70 + (value * 3) : 130 - (6 * (10-value));
	int val = 70 + (5 * value); // match bin/keymon-patched formula
	char cmd[64];
	sprintf(cmd, "echo %d > /sys/class/disp/disp/attr/lcdbl", val);
	system(cmd);
}

static void initTrimuiAPI(void) {
	void* libtmenu = dlopen("/usr/trimui/lib/libtmenu.so", RTLD_LAZY);

	void (*InitKeyShm)(int* [10]) = dlsym(libtmenu, "InitKeyShm");
	loadSystemState = dlsym(libtmenu, "loadSystemState");
	saveSystemState = dlsym(libtmenu, "saveSystemState");
	GetKeyShm = dlsym(libtmenu, "GetKeyShm");
	SetKeyShm = dlsym(libtmenu, "SetKeyShm");

	InitKeyShm(key);
}

#define kCPUDead 0x0112 // 16MHz (dead)
#define kCPULow 0x00c00532 // 192MHz (lowest)
#define kCPUNormal 0x02d01d22 // 720MHz (default)
#define kCPUHigh 0x03601a32 // 864MHz (highest stable)

static void setCPU(uint32_t mhz) {
	volatile uint32_t* mem;
	volatile uint8_t memdev = 0;
	memdev = open("/dev/mem", O_RDWR);
	if (memdev>0) {
		mem = (uint32_t*)mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, memdev, 0x01c20000);
		if (mem==MAP_FAILED) {
			puts("Could not mmap CPU hardware registers!");
			close(memdev);
			return;
		}
	}
	else puts("Could not open /dev/mem");
	
	uint32_t v = mem[0];
	v &= 0xffff0000;
	v |= (mhz & 0x0000ffff);
	mem[0] = v;
	
	if (memdev>0) close(memdev);
}
static void initLCD(void) {
	int address = 0x01c20890;
	int pagesize = sysconf(_SC_PAGESIZE);
	int addrmask1 = address & (0-pagesize);
	int addrmask2 = address & (pagesize-1);
	int memhandle = open("/dev/mem",O_RDWR|O_SYNC);
	unsigned char *memaddress = mmap(NULL,pagesize,PROT_READ|PROT_WRITE,MAP_SHARED,memhandle,addrmask1);
	volatile unsigned char *modaddress = (memaddress + addrmask2);
	volatile int moddata = *(unsigned int*)modaddress;
	if ((moddata & 1) != 0) { *(unsigned int*)modaddress = moddata & 0xF0FFFFFF | 0x03000000 ; }
	munmap(memaddress,pagesize);
	close(memhandle);
}

static void fauxSleep(void) {
	int v = getVolume();
	int b = getBrightness();
	// printf("before v:%i b:%i\n",v,b);
	setVolume(0);
	system("echo 0 > /sys/class/disp/disp/attr/lcdbl"); // setBrightness(0);
	setCPU(kCPUDead);
	
	// system("echo 1 > /sys/devices/virtual/disp/disp/attr/suspend");
	system("killall -s STOP keymon");
	
	SDL_Event event;
	int L = 0;
	int R = 0;
	int wake = 0;
	while (!wake) {
		SDL_Delay(1000);
		while (SDL_PollEvent(&event)) {
			SDLKey btn = event.key.keysym.sym;
			switch( event.type ){
				case SDL_KEYDOWN:
					if (btn==TRIMUI_L) L = 1;
					else if (btn==TRIMUI_R) R = 1;
					else { // any face button
						if (L && R) wake = 1;
					}
				break;
				case SDL_KEYUP:
					if (btn==TRIMUI_L) L = 0;
					else if (btn==TRIMUI_R) R = 0;
				break;
			}
		}
	}
	
	setVolume(v);
	setBrightness(b);
	setCPU(kCPUNormal);

	system("killall -s CONT keymon");
	// system("echo 0 > /sys/devices/virtual/disp/disp/attr/suspend");
	// initLCD();
	// setBrightness(b);
	
	// v = getVolume();
	// b = getBrightness();
	// printf("after v:%i b:%i\n",v,b);
}

///////////////////////////////////////

__attribute__((constructor)) static void init(void) {
	// signal(SIGSEGV, error_handler); // runtime error reporting
	
	initTrimuiAPI();
	
	// SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	
	// screen = SDL_SetVideoMode(320, 240, 16, SDL_HWSURFACE | SDL_DOUBLEBUF);
	buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0, 0, 0, 0);
	
	font = TTF_OpenFont("/usr/res/BPreplayBold.otf", 16);
	tiny = TTF_OpenFont("/usr/res/BPreplayBold.otf", 14);
	
	overlay = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0, 0, 0, 0);
	SDL_SetAlpha(overlay, SDL_SRCALPHA, 0x80);
	SDL_FillRect(overlay, NULL, 0);
	
	ui_top_bar = IMG_Load("/usr/trimui/res/skin/navbg.png");
	ui_bottom_bar = IMG_Load("/usr/trimui/res/skin/statbg.png");
	ui_menu_bg = IMG_Load("/usr/trimui/res/skin/menu-5line-bg.png");
	ui_menu3_bg = IMG_Load("/usr/trimui/res/skin/menu-3line-bg.png");
	ui_menu_bar = IMG_Load("/usr/trimui/res/skin/list-item-select-bg-short.png");
	ui_slot_bg = IMG_Load("/mnt/SDCARD/System/res/save-slot-bg.png");
	ui_slot_overlay = IMG_Load("/mnt/SDCARD/System/res/save-slot-overlay.png");
	ui_disc_bg = IMG_Load("/mnt/SDCARD/System/res/disc-bg.png");
	
	ui_browse_icon = IMG_Load("/usr/trimui/res/skin/stat-nav-icon.png");
	ui_round_button = IMG_Load("/mnt/SDCARD/System/res/nav-bar-item-bg.png");
	ui_menu_icon = IMG_Load("/usr/trimui/res/skin/stat-menu-icon.png");
	
	ui_arrow_right = IMG_Load("/usr/trimui/res/skin/right-arrow-icon-normal-small.png");
	ui_arrow_right_w = IMG_Load("/usr/trimui/res/skin/right-arrow-small.png");
	ui_selected_dot = IMG_Load("/mnt/SDCARD/System/res/selected-slot-dot.png");
	
	ui_empty_slot = TTF_RenderUTF8_Blended(tiny, "Empty Slot", gold);
	ui_no_preview = TTF_RenderUTF8_Blended(tiny, "No Preview", gold);
}

__attribute__((destructor)) static void quit(void) {
	SDL_FreeSurface(ui_empty_slot);
	SDL_FreeSurface(ui_no_preview);
	SDL_FreeSurface(ui_selected_dot);
	SDL_FreeSurface(ui_arrow_right);
	SDL_FreeSurface(ui_arrow_right_w);
	SDL_FreeSurface(ui_browse_icon);
	SDL_FreeSurface(ui_menu_icon);
	SDL_FreeSurface(ui_round_button);
	SDL_FreeSurface(ui_disc_bg);
	SDL_FreeSurface(ui_slot_overlay);
	SDL_FreeSurface(ui_slot_bg);
	SDL_FreeSurface(ui_menu_bar);
	SDL_FreeSurface(ui_menu3_bg);
	SDL_FreeSurface(ui_menu_bg);
	SDL_FreeSurface(ui_bottom_bar);
	SDL_FreeSurface(ui_top_bar);
	SDL_FreeSurface(overlay);
	SDL_FreeSurface(buffer);
	
	TTF_CloseFont(tiny);
	TTF_CloseFont(font);
	
	TTF_Quit();
	// SDL_Quit();
}

#define kItemCount 5
static char* items[kItemCount] = {
	"Continue",
	"Save",
	"Load",
	"Advanced",
	"Exit Game",
};
enum {
	kItemContinue,
	kItemSave,
	kItemLoad,
	kItemAdvanced,
	kItemExitGame,
};

typedef struct __attribute__((__packed__)) uint24_t {
	uint8_t a,b,c;
} uint24_t;
static SDL_Surface* thumbnail(SDL_Surface* src_img) {
	// unsigned long then = SDL_GetTicks();
	SDL_Surface* dst_img = SDL_CreateRGBSurface(0,160,120,src_img->format->BitsPerPixel,src_img->format->Rmask,src_img->format->Gmask,src_img->format->Bmask,src_img->format->Amask);

	uint8_t* src_px = src_img->pixels;
	uint8_t* dst_px = dst_img->pixels;
	int step = dst_img->format->BytesPerPixel;
	int step2 = step * 2;
	int stride = src_img->pitch;
	// SDL_LockSurface(dst_img);
	for (int y=0; y<dst_img->h; y++) {
		for (int x=0; x<dst_img->w; x++) {
			switch(step) {
				case 1:
					*dst_px = *src_px;
					break;
				case 2:
					*(uint16_t*)dst_px = *(uint16_t*)src_px;
					break;
				case 3:
					*(uint24_t*)dst_px = *(uint24_t*)src_px;
					break;
				case 4:
					*(uint32_t*)dst_px = *(uint32_t*)src_px;
					break;
			}
			dst_px += step;
			src_px += step2;
		}
		src_px += stride;
	}
	// SDL_UnlockSurface(dst_img);
	// printf("duration %lums\n", SDL_GetTicks() - then); // 3ms on device, 2ms with -O3/-Ofast

	return dst_img;
}

static char* copy_string(char* str) {
	int len = strlen(str);
	char* copy = malloc(sizeof(char)*(len+1));
	strcpy(copy, str);
	copy[len] = '\0';
	return copy;
} // NOTE: caller must free() result!
static int exact_match(char* str1, char* str2) {
	int len1 = strlen(str1);
	if (len1!=strlen(str2)) return 0;
	return  (strncmp(str1,str2,len1)==0);
}

#define kSlotCount 8
static int slot = 0;

#define kRootDir "/mnt/SDCARD"
#define kScreenshotsPath kRootDir "/.minui/screenshots.txt"
#define kScreenshotPathTemplate kRootDir "/.minui/screenshots/screenshot-%03i.bmp"
static int enable_screenshots = 0;
static int screenshots = 0;
void load_screenshots(void) {
	enable_screenshots = exists(kRootDir "/.minui/enable-screenshots");
	if (!enable_screenshots) return;
	if (exists(kScreenshotsPath)) {
		char tmp[16];
		get_file(kScreenshotsPath, tmp);
		screenshots = atoi(tmp);
	}
}
void save_screenshot(SDL_Surface* surface) {
	if (!enable_screenshots) return;
	
	char screenshot_path[256];
	sprintf(screenshot_path, kScreenshotPathTemplate, ++screenshots);
	SDL_RWops* out = SDL_RWFromFile(screenshot_path, "wb");
	SDL_SaveBMP_RW(surface ? surface : screen, out, 1);
	char count[16];
	sprintf(count, "%i", screenshots);
	put_file(kScreenshotsPath, count);
}

#define kResumeSlotPath "/tmp/mmenu_slot.txt"
int ResumeSlot(void) {
	if (!exists(kResumeSlotPath)) return -1;
	
	char tmp[16];
	get_file(kResumeSlotPath, tmp);
	unlink(kResumeSlotPath);
	slot = atoi(tmp); // update slot so mmenu has it preselected as well
	return slot;
}

#define kLastPath "/tmp/last.txt"
#define kChangeDiscPath "/tmp/change_disc.txt"
int ChangeDisc(char* disc_path) {
	if (!exists(kChangeDiscPath)) return 0;
	get_file(kChangeDiscPath, disc_path);
	return 1;
}

MenuReturnStatus ShowMenu(char* rom_path, char* save_path_template, SDL_Surface* frame, MenuReturnEvent keyEvent) {
	screen = SDL_GetVideoSurface();
	SDL_Surface* text;
	SDL_Surface* copy = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0, 0, 0, 0);	
	SDL_BlitSurface(frame, NULL, copy, NULL);
	
	int supports_save_load = save_path_template!=NULL;
	
	SDL_EnableKeyRepeat(300,100);
	
	char* tmp;
	char rom_file[128]; // with extension
	char rom_name[128]; // without extension or cruft
	char slot_path[128];
	
	tmp = strrchr(rom_path,'/');
	if (tmp==NULL) tmp = rom_path;
	else tmp += 1;

	strcpy(rom_name, tmp);
	strcpy(rom_file, tmp);
	tmp = strrchr(rom_name, '.');
	if (tmp!=NULL) tmp[0] = '\0';
	
	// remove trailing parens (round and square)
	char safe[128];
	strcpy(safe,rom_name);
	while ((tmp=strrchr(rom_name, '('))!=NULL || (tmp=strrchr(rom_name, '['))!=NULL) {
		tmp[0] = '\0';
		tmp = rom_name;
	}
	if (rom_name[0]=='\0') strcpy(rom_name,safe);
	
	char mmenu_dir[128]; // /full/path/to/rom_dir/.mmenu
	strcpy(mmenu_dir, rom_path);
	tmp = mmenu_dir + strlen("/mnt/SDCARD/Roms/");
	tmp = strchr(tmp, '/') + 1;
	strcpy(tmp, ".mmenu");
	mkdir(mmenu_dir, 0755);
	
	sprintf(slot_path, "%s/%s.txt", mmenu_dir, rom_file);
	strcpy(slot_path, mmenu_dir);
	strcpy(slot_path+strlen(slot_path), "/");
	strcpy(slot_path+strlen(slot_path), rom_file);
	tmp = strrchr(slot_path, '.') + 1;
	strcpy(tmp, "txt");
	
	// does this game have an m3u?
	int rom_disc = -1;
	int disc = rom_disc;
	int total_discs = 0;
	char disc_name[16];
	char* disc_paths[9]; // up to 9 paths, Arc the Lad Collection is 7 discs
	char ext[8];
	tmp = strrchr(rom_path, '.');
	strncpy(ext, tmp, 8);
	for (int i=0; i<4; i++) {
		ext[i] = tolower(ext[i]);
	}
	// only check for m3u if rom is a cue
	if (strncmp(ext, ".cue", 8)==0) {
		// construct m3u path based on parent directory
		char m3u_path[256];
		strcpy(m3u_path, rom_path);
		tmp = strrchr(m3u_path, '/') + 1;
		tmp[0] = '\0';
	
		// path to parent directory
		char base_path[128];
		strcpy(base_path, m3u_path);
	
		tmp = strrchr(m3u_path, '/');
		tmp[0] = '\0';
	
		// get parent directory name
		char dir_name[128];
		tmp = strrchr(m3u_path, '/');
		strcpy(dir_name, tmp);
	
		// dir_name is also our m3u file name
		tmp = m3u_path + strlen(m3u_path); 
		strcpy(tmp, dir_name);
	
		// add extension
		tmp = m3u_path + strlen(m3u_path);
		strcpy(tmp, ".m3u");
	
		if (exists(m3u_path)) {
			//read m3u file
			FILE* file = fopen(m3u_path, "r");
			if (file) {
				char line[256];
				while (fgets(line,256,file)!=NULL) {
					int len = strlen(line);
					if (len>0 && line[len-1]=='\n') line[len-1] = 0; // trim newline
					if (strlen(line)==0) continue; // skip empty lines
			
					char disc_path[256];
					strcpy(disc_path, base_path);
					tmp = disc_path + strlen(disc_path);
					strcpy(tmp, line);
					
					// found a valid disc path
					if (exists(disc_path)) {
						disc_paths[total_discs] = copy_string(disc_path);
						// matched our current disc
						if (exact_match(disc_path, rom_path)) {
							rom_disc = total_discs;
							disc = rom_disc;
							sprintf(disc_name, "Disc %i", disc+1);
						}
						total_discs += 1;
					}
				}
				fclose(file);
			}
		}
	}
	
	load_screenshots();
	
	int status = kStatusContinue;
	int selected = 0; // resets every launch
	if (exists(slot_path)) {
		char tmp[16];
		get_file(slot_path, tmp);
		slot = atoi(tmp);
	}
	
	char save_path[256];
	char bmp_path[324];
	
	int quit = 0;
	int acted = 0;
	int is_dirty = 1;
	int save_exists = 0;
	int preview_exists = 0;
	SDL_Event event;
	unsigned long cancel_start = SDL_GetTicks();
	while (!quit) {
		unsigned long frame_start = SDL_GetTicks();
		int pressed_menu = 0;
		while (SDL_PollEvent(&event)) {
			switch( event.type ){
				case SDL_KEYUP: {
					if (acted && keyEvent==kMenuEventKeyUp) {
						SDLKey key = event.key.keysym.sym;
						cancel_start = frame_start;
						if (key==TRIMUI_B || key==TRIMUI_A) {
							quit = 1;
						}
					}
				} break;
				case SDL_KEYDOWN: {
					if (acted) break;
					SDLKey key = event.key.keysym.sym;
					cancel_start = frame_start;
					if (key==TRIMUI_UP) {
						selected -= 1;
						if (selected<0) selected += kItemCount;
						is_dirty = 1;
					}
					else if (key==TRIMUI_DOWN) {
						selected += 1;
						if (selected==kItemCount) selected -= kItemCount;
						is_dirty = 1;
					}
					else if (key==TRIMUI_LEFT) {
						if (total_discs && selected==kItemContinue) {
							disc -= 1;
							if (disc<0) disc += total_discs;
							is_dirty = 1;
							sprintf(disc_name, "Disc %i", disc+1);
						}
						else if (selected==kItemSave || selected==kItemLoad) {
	 						slot -= 1;
							if (slot<0) slot += kSlotCount;
							is_dirty = 1;
						}
					}
					else if (key==TRIMUI_RIGHT) {
						if (total_discs && selected==kItemContinue) {
							disc += 1;
							if (disc==total_discs) disc -= total_discs;
							is_dirty = 1;
							sprintf(disc_name, "Disc %i", disc+1);
						}
						else if (selected==kItemSave || selected==kItemLoad) {
							slot += 1;
							if (slot==kSlotCount) slot -= kSlotCount;
							is_dirty = 1;
						}
					}
					
					if (!supports_save_load) {
						// NOTE: should not be able to reach save load at this point
						if (selected==kItemSave && key==TRIMUI_DOWN) selected += 2;
						else if (selected==kItemLoad && key==TRIMUI_UP) selected -= 2;
					}
					
					if (enable_screenshots) {
						if (key==TRIMUI_Y) {
							save_screenshot(NULL);
							save_screenshot(copy);
						}
					}
				
					if (is_dirty && (selected==kItemSave || selected==kItemLoad) && supports_save_load) {
						sprintf(save_path, save_path_template, slot);
						sprintf(bmp_path, "%s/%s.%d.bmp", mmenu_dir, rom_file, slot);
					
						save_exists = exists(save_path);
						preview_exists = save_exists && exists(bmp_path);
					}
				
					if (key==TRIMUI_MENU) {
						pressed_menu = 1;
					}
					else if (key==TRIMUI_B) {
						if (keyEvent==kMenuEventKeyDown) quit = 1;
						else acted = 1;
						
						status = kStatusContinue;
						is_dirty = 1;
					}
					else if (key==TRIMUI_A) {
						if (selected==kItemLoad && !save_exists) break;
				
						if (keyEvent==kMenuEventKeyDown) quit = 1;
						else acted = 1;
						
						switch(selected) {
							case kItemContinue:
								if (total_discs && rom_disc!=disc) {
									status = kStatusChangeDisc;
									char* disc_path = disc_paths[disc];
									char last_path[256];
									get_file(kLastPath, last_path);
									if (!exact_match(last_path, "/mnt/SDCARD/Recently Played")) {
										put_file(kLastPath, disc_path);
									}
									put_file(kChangeDiscPath, disc_path);
								}
								else {
									status = kStatusContinue;
								}
							break;
							case kItemSave:
								status = kStatusSaveSlot + slot;
								if (supports_save_load) {
									SDL_Surface* preview = thumbnail(copy);
									SDL_RWops* out = SDL_RWFromFile(bmp_path, "wb");
									SDL_SaveBMP_RW(preview, out, 1);
									SDL_FreeSurface(preview);
								}
							break;
							case kItemLoad:
								status = kStatusLoadSlot + slot;
							break;
							case kItemAdvanced:
								status = kStatusOpenMenu;
							break;
							case kItemExitGame:
								status = kStatusExitGame;
							break;
						}
						
						if (selected==kItemSave || selected==kItemLoad) {
							char slot_str[8];
							sprintf(slot_str, "%d", slot);
							put_file(slot_path, slot_str);
						}
						
						is_dirty = 1;
					}
				} break;
			}
		}
		
		#define kSleepDelay 30000
		if (pressed_menu || frame_start-cancel_start>=kSleepDelay) {
			SDL_FillRect(buffer, NULL, 0);
			SDL_BlitSurface(buffer, NULL, screen, NULL);
			SDL_Flip(screen);
			
			fauxSleep();
			cancel_start = SDL_GetTicks();
			
			is_dirty = 1;
		}
		
		if (is_dirty) {
			if (acted) {
				// draw emu screen immediately so the wait for keyup feels like emu delay (because it is)
				SDL_BlitSurface(copy, NULL, buffer, NULL);
			}
			else {
				// ui
				SDL_BlitSurface(copy, NULL, buffer, NULL); // full screen image effectively clears buffer
				SDL_BlitSurface(overlay, NULL, buffer, NULL);
				SDL_BlitSurface(ui_top_bar, NULL, buffer, NULL);
				SDL_BlitSurface(ui_bottom_bar, NULL, buffer, &(SDL_Rect){0,210,0,0});
			
				// game name
				text = TTF_RenderUTF8_Blended(tiny, rom_name, gold);
				SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){(320-text->w)/2,6,0,0});
				SDL_FreeSurface(text);
			
				// menu
				{
					int x = 14;
					int y = 75;
					if (supports_save_load) {
						SDL_BlitSurface(ui_menu_bg, NULL, buffer, &(SDL_Rect){6,71,0,0});
					}
					else {
						SDL_BlitSurface(ui_menu3_bg, NULL, buffer, &(SDL_Rect){6,71+50,0,0});
						y += 50;
					}
	
					for (int i=0; i<kItemCount; i++) {
						char* item = items[i];
						if (total_discs && i==kItemContinue) {
							if (rom_disc!=disc) item = "Insert";
							else item = "Continue";
						}
						
						if (!supports_save_load && (i==kItemSave || i==kItemLoad)) continue;
							
						SDL_Color color = gold;
						if (i==selected) {
							SDL_BlitSurface(ui_menu_bar, NULL, buffer, &(SDL_Rect){6,y,0,0});
							color = white;
						}
	
						text = TTF_RenderUTF8_Blended(font, item, color);
						SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){x,y+4,0,0});
						SDL_FreeSurface(text);
				
						if (i==kItemSave || i==kItemLoad || (total_discs && i==kItemContinue)) {
							SDL_BlitSurface(i==selected?ui_arrow_right_w:ui_arrow_right, NULL, buffer, &(SDL_Rect){132,y+8,0,0});
						}
		
						y += 25;
					}
				}
			
				// disc change
				if (total_discs && selected==kItemContinue) {
					SDL_BlitSurface(ui_disc_bg, NULL, buffer, &(SDL_Rect){148,71,0,0});
					
					text = TTF_RenderUTF8_Blended(font, disc_name, gold);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){210,75+4,0,0});
					SDL_FreeSurface(text);
				}
				// slot preview
				else if (supports_save_load && (selected==kItemSave || selected==kItemLoad)) {
					SDL_BlitSurface(ui_slot_bg, NULL, buffer, &(SDL_Rect){148,71,0,0});
				
					if (preview_exists) { // has save, has preview
						SDL_Surface* preview = IMG_Load(bmp_path);
						SDL_BlitSurface(preview, NULL, buffer, &(SDL_Rect){151,74,0,0});
						SDL_FreeSurface(preview);
					}
					else if (save_exists) { // has save, no preview
						SDL_BlitSurface(ui_no_preview, NULL, buffer, &(SDL_Rect){151+(160-ui_no_preview->w)/2,126,0,0});
					}
					else {
						SDL_BlitSurface(ui_empty_slot, NULL, buffer, &(SDL_Rect){151+(160-ui_empty_slot->w)/2,126,0,0});
					}
					SDL_BlitSurface(ui_slot_overlay, NULL, buffer, &(SDL_Rect){151,74,0,0});
				
					SDL_BlitSurface(ui_selected_dot, NULL, buffer, &(SDL_Rect){200+(slot * 8),197,0,0});
				}
			
				// hints
				{
					// browse
					SDL_BlitSurface(ui_menu_icon, NULL, buffer, &(SDL_Rect){10,218-1,0,0});
					text = TTF_RenderUTF8_Blended(tiny, "SLEEP", white);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){56,220-1,0,0});
					SDL_FreeSurface(text);
	
					// A (varies)
					SDL_BlitSurface(ui_round_button, NULL, buffer, &(SDL_Rect){10+251,218-1,0,0});
					text = TTF_RenderUTF8_Blended(font, "A", bronze);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){10+251+6,218,0,0});
					SDL_FreeSurface(text);
	
					text = TTF_RenderUTF8_Blended(tiny, "ACT", white);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){10+276,220-1,0,0});
					SDL_FreeSurface(text);
	
					// B Back
					SDL_BlitSurface(ui_round_button, NULL, buffer, &(SDL_Rect){10+251-68,218-1,0,0});
					text = TTF_RenderUTF8_Blended(font, "B", bronze);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){10+251+6-68+1,218,0,0});
					SDL_FreeSurface(text);

					text = TTF_RenderUTF8_Blended(tiny, "BACK", white);
					SDL_BlitSurface(text, NULL, buffer, &(SDL_Rect){10+276-68,220-1,0,0});
					SDL_FreeSurface(text);
				}
			}
	
			SDL_BlitSurface(buffer, NULL, screen, NULL);
			SDL_Flip(screen);
			
			is_dirty = 0;
		}
		
		// slow down to 60fps
		unsigned long frame_duration = SDL_GetTicks() - frame_start; // 0-1 on non-dirty frames, 11-12 on dirty ones
		// printf("frame_duration:%lu\n", frame_duration);
		#define kTargetFrameDuration 17
		if (frame_duration<kTargetFrameDuration) SDL_Delay(kTargetFrameDuration-frame_duration);
	}
	
	// push emulator screen so any lag is on the emulator not our menu (because it is!)
	SDL_BlitSurface(copy, NULL, screen, NULL);
	SDL_FreeSurface(copy);
	SDL_Flip(screen);
	
	for (int i=0; i<total_discs; i++) {
		free(disc_paths[i]);
	}
	
	return status;
}