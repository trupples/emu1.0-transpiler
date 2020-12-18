#include <stdio.h>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <SDL.h>
#include <string.h>

#define CT if(cond)
#define CF if(!cond)

bool running;

// Serial device
unsigned char ascii2emu(char x) {
	const unsigned char LUT[] = {63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 62, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 36, 57, 63, 50, 51, 63, 56, 63, 44, 45, 39, 37, 59, 38, 60, 40, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 61, 63, 41, 42, 43, 53, 63, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 46, 63, 47, 55, 52, 63, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 48, 54, 49, 58, 63};
	if(x >= 0 && x < 128) return LUT[x];
	return 63;
}

char emu2ascii(unsigned char x) {
	return "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-*/<=>()[]{}#$_?|^&!~,.:\n@"[x];
}

std::queue<char> stdin_buffer;
std::mutex serial_mutex;
void serial_thread_fn(void) {
	while(running) {
		char x = getchar();
		if(x == EOF) break;
		{
			std::lock_guard<std::mutex> lock(serial_mutex);
			stdin_buffer.push(x);
		}
	}
}

unsigned char IO0(unsigned char _) { std::lock_guard<std::mutex> lock(serial_mutex); return std::min((std::size_t)63, stdin_buffer.size()); }
unsigned char IO1(unsigned char _) { std::lock_guard<std::mutex> lock(serial_mutex); char x = stdin_buffer.front(); stdin_buffer.pop(); return ascii2emu(x); }
unsigned char IO2(unsigned char x) { putchar(emu2ascii(x)); fflush(stdout); return 0; }

// Networking client
#ifdef NET_CLIENT

struct sockaddr_in addr = {
	.sin_family = AF_INET,
	.sin_port   = htons(REPLACE:NET_PORT),
	.sin_addr   = {
		.s_addr = htonl(REPLACE:NET_HOST)
	}
};

int sock;
std::queue<unsigned char> net_buffer;
std::mutex net_mutex;
void net_thread_fn(void) {
	while(running) {
		unsigned char x;
		if(recv(sock, &x, 1, 0) == EOF) break;
		{
			std::lock_guard<std::mutex> lock(net_mutex);
			net_buffer.push(x);
		}
	}
}

unsigned char IO8(unsigned char _) { std::lock_guard<std::mutex> lock(net_mutex); return std::min((std::size_t)63, net_buffer.size()); }
unsigned char IO9(unsigned char _) { std::lock_guard<std::mutex> lock(net_mutex); char x = net_buffer.front(); net_buffer.pop(); return x; }
unsigned char IO10(unsigned char x) { send(sock, &x, 1, 0); return 0; }

#endif

// Extra memory extension
unsigned char extra_memory[64*64*64] = {};
unsigned int memptr;

unsigned char IO16(unsigned char hi) { memptr = (memptr & 0b000000111111111111) | (hi<<12); return 0; } // MEM ADDR HI
unsigned char IO17(unsigned char mi) { memptr = (memptr & 0b111111000000111111) | (mi<<6); return 0; } // MEM ADDR MID
unsigned char IO18(unsigned char lo) { memptr = (memptr & 0b111111111111000000) | (lo<<0); return 0; } // MEM ADDR LO
unsigned char IO19(unsigned char _) { memptr = (memptr + 1) % 262144; return extra_memory[memptr - 1]; } // MEM READ
unsigned char IO20(unsigned char x) { extra_memory[memptr] = x; memptr = (memptr + 1) % 262144; return 0; } // MEM WRITE

// Graphics extension
unsigned char screen[64][64], frontbuffer[64][64], cursor_x, cursor_y;

unsigned char IO21(unsigned char x) { cursor_x = x; return 0; } // GPU SET X
unsigned char IO22(unsigned char y) { cursor_y = y; return 0; } // GPU SET Y
unsigned char IO23(unsigned char c) { screen[cursor_y][cursor_x] = c; return 0;} // GPU DRAW

// DPAD extension
unsigned char dpad;

unsigned char IO24(unsigned char _) { return dpad; } // DPAD READ BUTTONS
bool refresh = false;
void console_thread_fn(void) {
	SDL_Window* window = NULL;
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
		return;
	}
	const int pixel_sz = 8;
	window = SDL_CreateWindow(
		"EMU 1.0 Console",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		64*pixel_sz, 64*pixel_sz,
		SDL_WINDOW_SHOWN
	);
	if (window == NULL) {
		fprintf(stderr, "could not create window: %s\n", SDL_GetError());
		return;
	}
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
	SDL_UpdateWindowSurface(window);
	std::chrono::time_point<std::chrono::high_resolution_clock> clock_start;
	while(running) {
		SDL_Event evt;
		while (SDL_PollEvent(&evt)) {
			if(evt.type == SDL_QUIT) running = false;

			if(evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
				int bit = 0;
				switch(evt.key.keysym.sym) {
					default: break;
					case SDLK_x: bit = 32; break;
					case SDLK_y: bit = 16; break;
					case SDLK_UP: bit = 8; break;
					case SDLK_DOWN: bit = 4; break;
					case SDLK_LEFT: bit = 2; break;
					case SDLK_RIGHT: bit = 1; break;
				}
				if(evt.key.state == SDL_PRESSED) {
					dpad |= bit;
				} else {
					dpad &= ~bit;
				}
			}
		}

		std::chrono::time_point<std::chrono::high_resolution_clock> clock_now = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed_cs = clock_now - clock_start;
		if(refresh || elapsed_cs.count() > 1/30.0) {
			memcpy(frontbuffer, screen, 4096);
			clock_start = clock_now;
			refresh = false;
		}

		for(int i=0; i<64; i++)
			for(int j=0; j<64; j++) {
				SDL_SetRenderDrawColor(renderer, (frontbuffer[i][j] >> 4) * 255 / 3, ((frontbuffer[i][j] >> 2) & 3) * 255 / 3, (frontbuffer[i][j] & 3) * 255 / 3, 255); // the rect color (solid red)
				SDL_Rect rect = {.x = j*pixel_sz, .y = i*pixel_sz, .w = pixel_sz, .h = pixel_sz }; // the rectangle
				SDL_RenderFillRect(renderer, &rect);
			}

		SDL_RenderPresent(renderer); // copy to screen
		SDL_Delay(1);
	}
	SDL_DestroyWindow(window);
	SDL_Quit();
}

// Clock device
std::chrono::time_point<std::chrono::high_resolution_clock> clock_start;

unsigned short clock_get() {
	std::chrono::time_point<std::chrono::high_resolution_clock> clock_now = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::ratio<1, 100>> elapsed_cs = clock_now - clock_start;
	unsigned short diff = std::min((unsigned short)4095, (unsigned short)elapsed_cs.count());
	return diff;
}

void clock_reset() {
	clock_start = std::chrono::high_resolution_clock::now();
}

unsigned char IO3(unsigned char x) { refresh = true; unsigned char clo = clock_get() % 64; if(x) clock_reset(); return clo; }
unsigned char IO4(unsigned char x) { refresh = true; unsigned char chi = clock_get() / 64; if(x) clock_reset(); return chi; }

signed char sgn(unsigned char x) {
	if(x >= 32) return (signed char) x - 64;
	return (signed char) x;
}

unsigned char usgn(signed char x) {
	x = x % 64;
	if(x < 0) return x + 64;
	return x;
}

int jdn(int, bool, int, int);
int jup(int, bool, int, int);

int main(int argc, char **argv) {
	running = true;

	clock_reset();
	std::thread serial_thread(serial_thread_fn);

#ifdef NET_CLIENT
	sock = socket(AF_INET, SOCK_STREAM, 0);
	connect(sock, (const sockaddr*)&addr, sizeof(addr));
	std::thread net_thread(net_thread_fn);
#endif

	std::thread console_thread(console_thread_fn);

	unsigned char r[64] = {};
	int ip = 0;
	bool cond = false;
	while(running) {
		//printf("ip = %d\n", ip);
		switch(ip) {
			case -1: printf("\n\nINFINITE LOOP!\n\n"); running = false; break;
			case 0:

(r[0]+r[0])&63;
r[1]=(r[0]+0)&63;
IO16(r[1]);
IO17(r[1]);
IO18(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+41)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+47)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+34)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+42)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+46)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+9)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+10)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+38)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+47)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+41)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+36)&63;
IO20(r[1]);
r[1]=(r[0]+35)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+9)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+40)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+35)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+47)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+41)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+51)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+29)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+35)&63;
IO20(r[1]);
r[1]=(r[0]+34)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+46)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+10)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+43)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+51)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+35)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+10)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+46)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+36)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+43)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+42)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+37)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+29)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+9)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+36)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+42)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+47)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+34)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+29)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+20)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+47)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+40)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+10)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+46)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+37)&63;
IO20(r[1]);
r[1]=(r[0]+11)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+43)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+43)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+33)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+38)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+29)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+9)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+10)&63;
IO20(r[1]);
r[1]=(r[0]+22)&63;
IO20(r[1]);
r[1]=(r[0]+21)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+38)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+37)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+40)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+50)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+49)&63;
IO20(r[1]);
r[1]=(r[0]+24)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+43)&63;
IO20(r[1]);
r[1]=(r[0]+40)&63;
IO20(r[1]);
r[1]=(r[0]+25)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+35)&63;
IO20(r[1]);
r[1]=(r[0]+12)&63;
IO20(r[1]);
r[1]=(r[0]+26)&63;
IO20(r[1]);
r[1]=(r[0]+16)&63;
IO20(r[1]);
r[1]=(r[0]+34)&63;
IO20(r[1]);
r[1]=(r[0]+39)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+18)&63;
IO20(r[1]);
r[1]=(r[0]+45)&63;
IO20(r[1]);
r[1]=(r[0]+19)&63;
IO20(r[1]);
r[1]=(r[0]+17)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+23)&63;
IO20(r[1]);
r[1]=(r[0]+13)&63;
IO20(r[1]);
r[1]=(r[0]+44)&63;
IO20(r[1]);
r[1]=(r[0]+40)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO16(r[1]);
r[1]=(r[0]+0)&63;
IO17(r[1]);
IO18(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+56)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+56)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+4)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+28)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+14)&63;
IO20(r[1]);
r[1]=(r[0]+8)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+4)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+30)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+7)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+3)&63;
IO20(r[1]);
r[1]=(r[0]+32)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+1)&63;
IO20(r[1]);
r[1]=(r[0]+48)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+56)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+31)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+62)&63;
IO20(r[1]);
r[1]=(r[0]+15)&63;
IO20(r[1]);
r[1]=(r[0]+63)&63;
IO20(r[1]);
r[1]=(r[0]+60)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
r[1]=(r[0]+0)&63;
IO20(r[1]);
;case 988://lbl10250
r[60]=(r[60]+1)&63;
cond=r[60]==54;
CT r[60]=(r[0]+0)&63;
r[3]=(r[0]+1)&63;
r[1]=(r[0]+0)&63;
;case 994://lbl10260
IO21(r[1]);
r[2]=(r[0]+0)&63;
;case 997://lbl10270
IO22(r[2]);
IO23(r[3]);
r[2]=(r[2]+1)&63;
cond=r[2]==0;
CF {ip=jup(1002,cond,1027,r[0]);break;}
r[1]=(r[1]+1)&63;
cond=r[1]==0;
CF {ip=jup(1005,cond,1026,r[0]);break;}
r[1]=(r[0]+8)&63;
r[2]=(r[0]+4)&63;
r[3]=(r[60]+0)&63;
r[63]=(r[0]+1)&63;
{ip=jdn(1010,cond,512,r[0]);break;}
;case 1011://lbl5131
r[1]=(r[0]+38)&63;
r[2]=(r[0]+6)&63;
r[3]=(r[60]+3)&63;
r[63]=(r[0]+2)&63;
{ip=jdn(1016,cond,512,r[0]);break;}
;case 1017://lbl5132
r[1]=(r[0]+58)&63;
r[2]=(r[0]+9)&63;
r[3]=(r[60]+8)&63;
r[63]=(r[0]+3)&63;
{ip=jdn(1022,cond,512,r[0]);break;}
;case 1023://lbl5133
r[1]=(r[0]+58)&63;
r[2]=(r[0]+33)&63;
r[3]=(r[60]+5)&63;
r[63]=(r[0]+4)&63;
{ip=jdn(1028,cond,512,r[0]);break;}
;case 1029://lbl5134
r[1]=(r[0]+55)&63;
r[2]=(r[0]+46)&63;
r[3]=(r[60]+1)&63;
r[63]=(r[0]+5)&63;
{ip=jdn(1034,cond,512,r[0]);break;}
;case 1035://lbl5135
r[1]=(r[0]+51)&63;
r[2]=(r[0]+59)&63;
r[3]=(r[60]+6)&63;
r[63]=(r[0]+6)&63;
{ip=jdn(1040,cond,512,r[0]);break;}
;case 1041://lbl5136
r[50]=(r[0]+48)&63;
r[51]=(r[0]+56)&63;
r[52]=(r[0]+42)&63;
r[53]=(r[0]+12)&63;
r[54]=(r[0]+14)&63;
r[55]=(r[0]+11)&63;
r[56]=(r[0]+3)&63;
r[57]=(r[0]+35)&63;
r[58]=(r[0]+50)&63;
r[3]=(r[0]+24)&63;
;case 1052://lbl10260
r[4]=(r[60]-r[3])&63;
;case 1054://lbl10270
cond=r[4]>8;
CT r[4]=(r[4]+56)&63;
CT {ip=jup(1057,cond,1027,r[0]);break;}
r[4]=r[(r[4]+50)&63];
r[1]=(r[0]+0)&63;
IO16(r[1]);
IO17(r[1]);
IO18(r[1]);
;case 1063://lbl10270
r[1]=IO19(r[0]);
r[2]=IO19(r[0]);
cond=r[1]==0;
CF cond=r[2]==0;
CT {ip=jdn(1068,cond,1024,r[0]);break;}
cond=r[1]>r[3];
CT r[2]=(r[0]-r[2])&63;
CT cond=r[2]>r[3];
r[2]=(r[0]-r[2])&63;
CF {ip=jup(1073,cond,1027,r[0]);break;}
r[1]=(r[1]-r[3])&63;
r[2]=(r[2]+r[3])&63;
IO21(r[1]);
IO22(r[2]);
IO23(r[4]);
{ip=jup(1079,cond,1027,r[0]);break;}
;case 1080://lbl10240
r[3]=(r[3]+63)&63;
cond=r[3]==0;
CF {ip=jup(1083,cond,1026,r[0]);break;}
r[1]=(r[0]+0)&63;
IO16(r[1]);
IO17(r[1]);
IO18(r[1]);
;case 1088://lbl10260
r[1]=IO19(r[0]);
r[2]=IO19(r[0]);
cond=r[1]==0;
CF cond=r[2]==0;
CT {ip=jdn(1093,cond,1024,r[0]);break;}
IO21(r[1]);
IO22(r[2]);
r[1]=(r[0]+0)&63;
IO23(r[1]);
{ip=jup(1098,cond,1026,r[0]);break;}
;case 1099://lbl10240
r[1]=(r[0]+1)&63;
IO16(r[1]);
r[1]=(r[0]+0)&63;
IO17(r[1]);
IO18(r[1]);
r[7]=(r[0]+63)&63;
r[1]=(r[0]+0)&63;
;case 1107://lbl10260
r[1]=(r[1]+8)&63;
IO21(r[1]);
r[1]=(r[1]+56)&63;
r[6]=(r[0]+9)&63;
r[2]=(r[0]+3)&63;
;case 1113://lbl10270
r[3]=IO19(r[0]);
r[4]=(r[0]+6)&63;
;case 1116://lbl10280
cond=r[3]>31;
CT IO22(r[6]);
CT IO23(r[7]);
r[6]=(r[6]+1)&63;
r[3]=(r[3]<<1)&63;
r[4]=(r[4]+63)&63;
cond=r[4]==0;
CF {ip=jup(1124,cond,1028,r[0]);break;}
r[2]=(r[2]+63)&63;
cond=r[2]==0;
CF {ip=jup(1127,cond,1027,r[0]);break;}
r[1]=(r[1]+1)&63;
cond=r[1]<46;
CT {ip=jup(1130,cond,1026,r[0]);break;}
r[1]=(r[0]+0)&63;
r[11]=(r[0]+63)&63;
;case 1133://lbl10260
r[1]=(r[1]+33)&63;
IO22(r[1]);
r[1]=(r[1]+31)&63;
r[2]=(r[0]+17)&63;
r[3]=(r[0]+18)&63;
r[4]=(r[0]+19)&63;
r[5]=(r[0]+31)&63;
r[6]=(r[0]+32)&63;
r[7]=(r[0]+33)&63;
r[8]=(r[0]+45)&63;
r[9]=(r[0]+46)&63;
r[10]=(r[0]+47)&63;
cond=r[1]<12;
CF cond=r[1]==14;
CT IO21(r[2]);
CT IO23(r[11]);
CT IO21(r[3]);
CT IO23(r[11]);
CT IO21(r[4]);
CT IO23(r[11]);
CT IO21(r[5]);
CT IO23(r[11]);
CT IO21(r[6]);
CT IO23(r[11]);
CT IO21(r[7]);
CT IO23(r[11]);
CT IO21(r[8]);
CT IO23(r[11]);
CT IO21(r[9]);
CT IO23(r[11]);
CT IO21(r[10]);
CT IO23(r[11]);
cond=r[1]==13;
CT IO21(r[2]);
CT IO23(r[11]);
CT IO21(r[3]);
CT IO23(r[11]);
CT IO21(r[5]);
CT IO23(r[11]);
CT IO21(r[6]);
CT IO23(r[11]);
CT IO21(r[8]);
CT IO23(r[11]);
CT IO21(r[9]);
CT IO23(r[11]);
cond=r[1]==15;
CT IO21(r[3]);
CT IO23(r[11]);
CT IO21(r[4]);
CT IO23(r[11]);
CT IO21(r[6]);
CT IO23(r[11]);
CT IO21(r[7]);
CT IO23(r[11]);
CT IO21(r[9]);
CT IO23(r[11]);
CT IO21(r[10]);
CT IO23(r[11]);
r[1]=(r[1]+1)&63;
cond=r[1]!=17;
CT {ip=jup(1194,cond,1026,r[0]);break;}
r[1]=(r[0]+1)&63;
IO3(r[1]);
;case 1197://lbl10260
r[1]=IO3(r[0]);
cond=r[1]<9;
CT {ip=jup(1200,cond,1026,r[0]);break;}
{ip=jup(1201,cond,1025,r[0]);break;}
;case 1202://lbl5120
cond=r[3]>8;
CT r[3]=(r[3]+55)&63;
CT {ip=jup(1205,cond,512,r[0]);break;}
cond=r[3]==0;
CT {ip=jup(1207,cond,513,r[63]);break;}
IO21(r[1]);
IO22(r[2]);
cond=r[3]==1;
CT r[4]=(r[0]+63)&63;
CT IO23(r[4]);
CT {ip=jup(1213,cond,513,r[63]);break;}
cond=r[3]==2;
CT r[4]=(r[0]+62)&63;
CT IO23(r[4]);
CT r[4]=(r[0]+63)&63;
CT r[5]=(r[1]+1)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT r[5]=(r[1]+63)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT IO21(r[1]);
CT r[5]=(r[2]+1)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT r[5]=(r[2]+63)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT {ip=jup(1231,cond,513,r[63]);break;}
cond=r[3]==3;
CT r[4]=(r[0]+61)&63;
CT r[6]=(r[0]+63)&63;
CT r[5]=(r[1]+1)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT r[5]=(r[1]+2)&63;
CT IO21(r[5]);
CT IO23(r[6]);
CT r[5]=(r[1]+63)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT r[5]=(r[1]+62)&63;
CT IO21(r[5]);
CT IO23(r[6]);
CT IO21(r[1]);
CT r[5]=(r[2]+1)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT r[5]=(r[2]+2)&63;
CT IO22(r[5]);
CT IO23(r[6]);
CT r[5]=(r[2]+63)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT r[5]=(r[2]+62)&63;
CT IO22(r[5]);
CT IO23(r[6]);
CT {ip=jup(1260,cond,513,r[63]);break;}
cond=r[3]==4;
CT r[4]=(r[0]+46)&63;
CT r[5]=(r[1]+2)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT r[5]=(r[1]+62)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT IO21(r[1]);
CT r[5]=(r[2]+2)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT r[5]=(r[2]+62)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT {ip=jup(1276,cond,513,r[63]);break;}
cond=r[3]==5;
CT r[4]=(r[0]+22)&63;
CT r[5]=(r[1]+2)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT r[5]=(r[1]+62)&63;
CT IO21(r[5]);
CT IO23(r[4]);
CT IO21(r[1]);
CT r[5]=(r[2]+2)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT r[5]=(r[2]+62)&63;
CT IO22(r[5]);
CT IO23(r[4]);
CT {ip=jup(1292,cond,513,r[63]);break;}
{ip=jup(1293,cond,513,r[63]);break;}

			ip = 0;
		}
	}

#ifdef NET_CLIENT
	shutdown(sock, 2);
	net_thread.join();
#endif

	printf("\n\nEMU 1.0 TERMINATED\nPress enter to end the input thread!\n");
	serial_thread.join();

	console_thread.join();
}

#define X_JUMP_TARGETS \
	X_JUMP_TARGET(988, 0, 1025, 0)\
	X_JUMP_TARGET(994, 0, 1026, 0)\
	X_JUMP_TARGET(997, 0, 1027, 0)\
	X_JUMP_TARGET(1011, 0, 513, 1)\
	X_JUMP_TARGET(1017, 0, 513, 2)\
	X_JUMP_TARGET(1023, 0, 513, 3)\
	X_JUMP_TARGET(1029, 0, 513, 4)\
	X_JUMP_TARGET(1035, 0, 513, 5)\
	X_JUMP_TARGET(1041, 0, 513, 6)\
	X_JUMP_TARGET(1052, 0, 1026, 0)\
	X_JUMP_TARGET(1054, 0, 1027, 0)\
	X_JUMP_TARGET(1063, 0, 1027, 0)\
	X_JUMP_TARGET(1080, 0, 1024, 0)\
	X_JUMP_TARGET(1088, 0, 1026, 0)\
	X_JUMP_TARGET(1099, 0, 1024, 0)\
	X_JUMP_TARGET(1107, 0, 1026, 0)\
	X_JUMP_TARGET(1113, 0, 1027, 0)\
	X_JUMP_TARGET(1116, 0, 1028, 0)\
	X_JUMP_TARGET(1133, 0, 1026, 0)\
	X_JUMP_TARGET(1197, 0, 1026, 0)\
	X_JUMP_TARGET(1202, 0, 512, 0)

int jdn(int ip, bool cond, int lab, int lc) {
	// get first matching target after ip
	#define X_JUMP_TARGET(tip, tcond, tab, tc) if(tip > ip && (tcond == 0 || (!cond && tcond == 2) || (cond && tcond == 1)) && tab == lab && tc == lc) return tip;
	X_JUMP_TARGETS
	#undef X_JUMP_TARGET

	// or first starting from the top (wrapped around)
	#define X_JUMP_TARGET(tip, tcond, tab, tc) if((tcond == 0 || (!cond && tcond == 2) || (cond && tcond == 1)) && tab == lab && tc == lc) return tip;
	X_JUMP_TARGETS
	#undef X_JUMP_TARGET

	// or just fail
	return -1;
}

int jup(int ip, bool cond, int lab, int lc) {
	int best = -1;
	// find last matching target before ip
	#define X_JUMP_TARGET(tip, tcond, tab, tc) if(tip < ip && (tcond == 0 || (!cond && tcond == 2) || (cond && tcond == 1)) && tab == lab && tc == lc) best = tip;
	X_JUMP_TARGETS
	#undef X_JUMP_TARGET

	if(best != -1) return best;

	// or last matching after ip (wrapped around)
	#define X_JUMP_TARGET(tip, tcond, tab, tc) if((tcond == 0 || (!cond && tcond == 2) || (cond && tcond == 1)) && tab == lab && tc == lc) best = tip;
	X_JUMP_TARGETS
	#undef X_JUMP_TARGET

	return best;
}

