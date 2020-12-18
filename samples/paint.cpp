#define NET_CLIENT

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
	.sin_port   = htons(1337),
	.sin_addr   = {
		.s_addr = htonl(528333879)
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
;case 1://lbl00
r[8]=(r[0]+0)&63;
r[9]=(r[0]+0)&63;
r[10]=(r[0]+0)&63;
r[11]=(r[0]+0)&63;
r[12]=(r[0]+0)&63;
r[13]=(r[0]+63)&63;
r[14]=(r[0]+0)&63;
r[15]=(r[0]+0)&63;
r[16]=(r[0]+0)&63;
r[17]=(r[0]+0)&63;
r[20]=(r[0]+0)&63;
IO16(r[20]);
{ip=jdn(14,cond,704,r[0]);break;}
;case 15://lbl8960
r[11]=(r[0]+0)&63;
IO22(r[0]);
;case 18://lbl8970
IO17(r[11]);
IO18(r[0]);
IO21(r[11]);
IO23(r[11]);
IO20(r[11]);
r[11]=(r[11]+1)&63;
cond=r[11]!=0;
CT {ip=jup(26,cond,897,r[0]);break;}
r[12]=(r[0]+1)&63;
;case 28://lbl6500
;case 29://lbl6510
r[8]=(r[0]+r[11])&63;
r[9]=(r[0]+r[12])&63;
r[10]=IO24(r[0]);
r[14]=r[10]&32;
r[15]=r[10]&16;
r[1]=r[10]&8;
cond=r[1]==8;
CT r[12]=(r[12]+63)&63;
r[1]=r[10]&4;
cond=r[1]==4;
CT r[12]=(r[12]+1)&63;
r[1]=r[10]&2;
cond=r[1]==2;
CT r[11]=(r[11]+63)&63;
r[1]=r[10]&1;
cond=r[1]==1;
CT r[11]=(r[11]+1)&63;
;case 47://lbl6520
r[1]=IO24(r[0]);
r[1]=r[1]&47;
cond=r[1]!=0;
CT {ip=jup(51,cond,652,r[0]);break;}
;case 52://lbl8980
r[16]=IO4(r[0]);
r[16]=r[16]&1;
cond=r[8]!=r[11];
CT {ip=jdn(56,cond,899,r[0]);break;}
cond=r[9]!=r[12];
CT {ip=jdn(58,cond,899,r[0]);break;}
cond=r[16]!=r[17];
CT {ip=jdn(60,cond,899,r[0]);break;}
{ip=jdn(61,cond,900,r[0]);break;}
;case 62://lbl8990
IO21(r[8]);
IO22(r[9]);
IO17(r[8]);
IO18(r[9]);
r[1]=IO19(r[0]);
IO23(r[1]);
cond=r[8]!=r[11];
CT r[17]=(r[0]+r[16])&63;
CT {ip=jup(71,cond,900,r[0]);break;}
cond=r[9]!=r[12];
CT r[17]=(r[0]+r[16])&63;
CT {ip=jup(74,cond,900,r[0]);break;}
{ip=jdn(75,cond,901,r[0]);break;}
;case 76://lbl9000
IO21(r[11]);
IO22(r[12]);
IO23(r[13]);
;case 80://lbl9010
cond=r[14]==0;
CT {ip=jdn(82,cond,902,r[0]);break;}
IO17(r[11]);
IO18(r[12]);
r[13]=IO19(r[0]);
;case 86://lbl9020
cond=r[15]==0;
CT {ip=jdn(88,cond,768,r[0]);break;}
;case 89://lbl6530
cond=r[12]==0;
CT {ip=jdn(91,cond,768,r[0]);break;}
IO21(r[11]);
IO22(r[12]);
IO23(r[13]);
IO16(r[0]);
IO17(r[11]);
IO18(r[12]);
IO20(r[13]);
;case 99://lbl7680
r[1]=IO0(r[0]);
cond=r[1]==0;
CT {ip=jup(102,cond,650,r[0]);break;}
r[1]=IO1(r[0]);
cond=r[1]!=62;
CT {ip=jup(105,cond,650,r[0]);break;}
;case 106://lbl7710
r[1]=IO0(r[0]);
cond=r[1]!=0;
CT IO1(r[0]);
CT {ip=jup(110,cond,771,r[0]);break;}
{ip=jdn(111,cond,705,r[0]);break;}
;case 112://lbl7690
r[1]=IO0(r[0]);
cond=r[1]==0;
CT {ip=jup(115,cond,769,r[0]);break;}
r[1]=IO1(r[0]);
cond=r[1]==34;
CT {ip=jdn(118,cond,832,r[0]);break;}
;case 119://lbl7700
r[1]=IO0(r[0]);
cond=r[1]!=0;
CT IO1(r[0]);
CT {ip=jup(123,cond,770,r[0]);break;}
{ip=jdn(124,cond,706,r[0]);break;}
;case 125://lbl8320
IO16(r[0]);
IO17(r[0]);
r[1]=(r[0]+1)&63;
IO18(r[1]);
r[11]=(r[0]+0)&63;
;case 131://lbl8330
r[12]=(r[0]+1)&63;
;case 133://lbl8340
r[1]=IO19(r[0]);
IO10(r[1]);
r[12]=(r[12]+1)&63;
cond=r[12]!=0;
CT {ip=jup(138,cond,834,r[0]);break;}
r[1]=IO19(r[0]);
r[11]=(r[11]+1)&63;
cond=r[11]!=0;
CT {ip=jup(142,cond,833,r[0]);break;}
;case 143://lbl8350
r[1]=IO8(r[0]);
cond=r[1]==0;
CT {ip=jup(146,cond,835,r[0]);break;}
r[1]=IO9(r[0]);
cond=r[1]!=63;
CT IO2(r[1]);
CT {ip=jup(150,cond,835,r[0]);break;}
{printf("halt0\n");running=false;}//halt0
;case 152://lbl7040
r[56]=(r[0]+33)&63;
IO2(r[56]);
r[56]=(r[0]+38)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+34)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+13)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+25)&63;
IO2(r[56]);
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+59)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+34)&63;
IO2(r[56]);
r[56]=(r[0]+38)&63;
IO2(r[56]);
r[56]=(r[0]+13)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+32)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+12)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+62)&63;
IO2(r[56]);
r[56]=(r[0]+25)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+28)&63;
IO2(r[56]);
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+23)&63;
IO2(r[56]);
r[56]=(r[0]+29)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+29)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+30)&63;
IO2(r[56]);
r[56]=(r[0]+25)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+13)&63;
IO2(r[56]);
r[56]=(r[0]+62)&63;
IO2(r[56]);
{ip=jup(249,cond,896,r[0]);break;}
;case 250://lbl7050
r[56]=(r[0]+29)&63;
IO2(r[56]);
r[56]=(r[0]+17)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+25)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+16)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+22)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+32)&63;
IO2(r[56]);
r[56]=(r[0]+18)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+17)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+29)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+15)&63;
IO2(r[56]);
r[56]=(r[0]+29)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+30)&63;
IO2(r[56]);
r[56]=(r[0]+25)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+13)&63;
IO2(r[56]);
r[56]=(r[0]+62)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+34)&63;
IO2(r[56]);
r[56]=(r[0]+24)&63;
IO2(r[56]);
r[56]=(r[0]+30)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+28)&63;
IO2(r[56]);
r[56]=(r[0]+30)&63;
IO2(r[56]);
r[56]=(r[0]+27)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+36)&63;
IO2(r[56]);
r[56]=(r[0]+46)&63;
IO2(r[56]);
r[56]=(r[0]+34)&63;
IO2(r[56]);
r[56]=(r[0]+47)&63;
IO2(r[56]);
r[56]=(r[0]+62)&63;
IO2(r[56]);
{ip=jup(355,cond,769,r[0]);break;}
;case 356://lbl7060
r[56]=(r[0]+12)&63;
IO2(r[56]);
r[56]=(r[0]+10)&63;
IO2(r[56]);
r[56]=(r[0]+23)&63;
IO2(r[56]);
r[56]=(r[0]+12)&63;
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+21)&63;
IO2(r[56]);
IO2(r[56]);
r[56]=(r[0]+14)&63;
IO2(r[56]);
r[56]=(r[0]+13)&63;
IO2(r[56]);
r[56]=(r[0]+62)&63;
IO2(r[56]);
{ip=jup(376,cond,650,r[0]);break;}

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
	X_JUMP_TARGET(1, 0, 0, 0)\
	X_JUMP_TARGET(15, 0, 896, 0)\
	X_JUMP_TARGET(18, 0, 897, 0)\
	X_JUMP_TARGET(28, 0, 650, 0)\
	X_JUMP_TARGET(29, 0, 651, 0)\
	X_JUMP_TARGET(47, 0, 652, 0)\
	X_JUMP_TARGET(52, 0, 898, 0)\
	X_JUMP_TARGET(62, 0, 899, 0)\
	X_JUMP_TARGET(76, 0, 900, 0)\
	X_JUMP_TARGET(80, 0, 901, 0)\
	X_JUMP_TARGET(86, 0, 902, 0)\
	X_JUMP_TARGET(89, 0, 653, 0)\
	X_JUMP_TARGET(99, 0, 768, 0)\
	X_JUMP_TARGET(106, 0, 771, 0)\
	X_JUMP_TARGET(112, 0, 769, 0)\
	X_JUMP_TARGET(119, 0, 770, 0)\
	X_JUMP_TARGET(125, 0, 832, 0)\
	X_JUMP_TARGET(131, 0, 833, 0)\
	X_JUMP_TARGET(133, 0, 834, 0)\
	X_JUMP_TARGET(143, 0, 835, 0)\
	X_JUMP_TARGET(152, 0, 704, 0)\
	X_JUMP_TARGET(250, 0, 705, 0)\
	X_JUMP_TARGET(356, 0, 706, 0)

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

