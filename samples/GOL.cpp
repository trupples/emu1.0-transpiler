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
r[5]=(r[0]+0)&63;
r[6]=(r[0]+0)&63;
IO16(r[0]);
IO17(r[0]);
IO18(r[0]);
r[5]=(r[0]+0)&63;
r[6]=(r[0]+0)&63;
;case 8://lbl13370
r[40]=(r[0]+1)&63;
IO20(r[0]);
r[5]=(r[5]+1)&63;
cond=r[5]==0;
CT r[6]=(r[6]+1)&63;
cond=r[6]==0;
CT cond=r[5]==0;
CT IO3(r[40]);
CT {ip=jdn(17,cond,350,r[0]);break;}
{ip=jup(18,cond,1337,r[0]);break;}
;case 19://lbl3500
IO16(r[0]);
r[1]=(r[0]+1)&63;
r[5]=(r[0]+10)&63;
r[6]=(r[0]+10)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+10)&63;
r[6]=(r[0]+11)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+10)&63;
r[6]=(r[0]+12)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+20)&63;
r[6]=(r[0]+10)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+20)&63;
r[6]=(r[0]+11)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+20)&63;
r[6]=(r[0]+12)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+10)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+11)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+12)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+40)&63;
r[6]=(r[0]+10)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+40)&63;
r[6]=(r[0]+11)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+40)&63;
r[6]=(r[0]+12)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+50)&63;
r[6]=(r[0]+10)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+50)&63;
r[6]=(r[0]+11)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+50)&63;
r[6]=(r[0]+12)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+51)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+31)&63;
r[6]=(r[0]+51)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+32)&63;
r[6]=(r[0]+51)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+33)&63;
r[6]=(r[0]+51)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+53)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+33)&63;
r[6]=(r[0]+53)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+29)&63;
r[6]=(r[0]+54)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+31)&63;
r[6]=(r[0]+54)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+32)&63;
r[6]=(r[0]+54)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+34)&63;
r[6]=(r[0]+54)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+29)&63;
r[6]=(r[0]+55)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+34)&63;
r[6]=(r[0]+55)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+29)&63;
r[6]=(r[0]+57)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+34)&63;
r[6]=(r[0]+57)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+29)&63;
r[6]=(r[0]+58)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+58)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+33)&63;
r[6]=(r[0]+58)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+34)&63;
r[6]=(r[0]+58)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+29)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+31)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+32)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+33)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+34)&63;
r[6]=(r[0]+59)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+30)&63;
r[6]=(r[0]+60)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+33)&63;
r[6]=(r[0]+60)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+31)&63;
r[6]=(r[0]+61)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+32)&63;
r[6]=(r[0]+61)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+31)&63;
r[6]=(r[0]+62)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[0]+32)&63;
r[6]=(r[0]+62)&63;
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
;case 247://lbl4200
r[63]=(r[0]+1)&63;
{ip=jdn(249,cond,79,r[0]);break;}
;case 250://lbl781
;case 251://lbl70
r[50]=IO3(r[0]);
cond=r[50]>10;
CT {ip=jdn(254,cond,8,r[0]);break;}
r[50]=IO4(r[0]);
cond=r[50]!=r[0];
CT {ip=jdn(257,cond,8,r[0]);break;}
{ip=jup(258,cond,7,r[0]);break;}
;case 259://lbl80
r[5]=(r[0]+0)&63;
r[6]=(r[0]+0)&63;
r[40]=(r[0]+1)&63;
;case 263://lbl6660
IO16(r[0]);
IO17(r[6]);
IO18(r[5]);
r[1]=IO19(r[0]);
r[63]=(r[0]+2)&63;
{ip=jdn(269,cond,404,r[0]);break;}
;case 270://lbl782
cond=r[2]<2;
CT r[1]=(r[0]+0)&63;
cond=r[2]>3;
CT r[1]=(r[0]+0)&63;
cond=r[2]==3;
CT r[1]=(r[0]+1)&63;
IO16(r[40]);
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[5]+1)&63;
cond=r[5]==0;
CT r[6]=(r[6]+1)&63;
cond=r[6]==0;
CT cond=r[5]==0;
CT {ip=jdn(286,cond,777,r[0]);break;}
{ip=jup(287,cond,666,r[0]);break;}
;case 288://lbl7770
;case 289://lbl40000
r[5]=(r[0]+0)&63;
r[6]=(r[0]+0)&63;
r[40]=(r[0]+1)&63;
;case 293://lbl40010
IO16(r[40]);
IO17(r[6]);
IO18(r[5]);
r[1]=IO19(r[0]);
IO16(r[0]);
IO17(r[6]);
IO18(r[5]);
IO20(r[1]);
r[5]=(r[5]+1)&63;
cond=r[5]==0;
CT r[6]=(r[6]+1)&63;
cond=r[6]==0;
CT cond=r[5]==0;
CT IO3(r[40]);
CT {ip=jdn(308,cond,651,r[0]);break;}
{ip=jup(309,cond,4001,r[0]);break;}
;case 310://lbl6510
{ip=jup(311,cond,420,r[0]);break;}
{printf("halt0\n");running=false;}//halt0
;case 313://lbl790
IO16(r[0]);
IO17(r[0]);
IO18(r[0]);
r[5]=(r[0]+0)&63;
r[6]=(r[0]+0)&63;
;case 319://lbl13370
r[40]=(r[0]+1)&63;
r[1]=IO19(r[0]);
cond=r[1]==0;
CF r[2]=(r[0]+60)&63;
CT r[2]=(r[0]+3)&63;
IO21(r[5]);
IO22(r[6]);
IO23(r[2]);
r[5]=(r[5]+1)&63;
cond=r[5]==0;
CT r[6]=(r[6]+1)&63;
cond=r[6]==0;
CT cond=r[5]==0;
CT IO3(r[40]);
CT {ip=jup(334,cond,78,r[63]);break;}
{ip=jup(335,cond,1337,r[0]);break;}
;case 336://lbl4040
r[2]=(r[0]+0)&63;
r[10]=(r[0]+r[5])&63;
r[11]=(r[0]+r[6])&63;
r[5]=(r[5]+1)&63;
cond=r[5]!=0;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[6]=(r[6]+1)&63;
cond=r[6]!=0;
CT cond=r[5]!=0;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[5]=(r[5]+63)&63;
cond=r[5]!=63;
CT cond=r[6]!=0;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[5]=(r[5]+63)&63;
cond=r[5]!=63;
CT cond=r[6]!=0;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[6]=(r[6]+63)&63;
cond=r[6]!=63;
CT cond=r[5]!=63;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[6]=(r[6]+63)&63;
cond=r[6]!=63;
CT cond=r[5]!=63;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[5]=(r[5]+1)&63;
cond=r[5]!=0;
CT cond=r[6]!=63;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[5]=(r[5]+1)&63;
cond=r[5]!=0;
CT cond=r[6]!=63;
CT IO16(r[0]);
CT IO17(r[6]);
CT IO18(r[5]);
CT r[12]=IO19(r[0]);
CT cond=r[12]==1;
CT r[2]=(r[2]+1)&63;
r[5]=(r[0]+r[10])&63;
r[6]=(r[0]+r[11])&63;
{ip=jup(413,cond,78,r[63]);break;}

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
	X_JUMP_TARGET(8, 0, 1337, 0)\
	X_JUMP_TARGET(19, 0, 350, 0)\
	X_JUMP_TARGET(247, 0, 420, 0)\
	X_JUMP_TARGET(250, 0, 78, 1)\
	X_JUMP_TARGET(251, 0, 7, 0)\
	X_JUMP_TARGET(259, 0, 8, 0)\
	X_JUMP_TARGET(263, 0, 666, 0)\
	X_JUMP_TARGET(270, 0, 78, 2)\
	X_JUMP_TARGET(288, 0, 777, 0)\
	X_JUMP_TARGET(289, 0, 4000, 0)\
	X_JUMP_TARGET(293, 0, 4001, 0)\
	X_JUMP_TARGET(310, 0, 651, 0)\
	X_JUMP_TARGET(313, 0, 79, 0)\
	X_JUMP_TARGET(319, 0, 1337, 0)\
	X_JUMP_TARGET(336, 0, 404, 0)

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

