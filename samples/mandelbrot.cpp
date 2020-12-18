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
r[14]=(r[0]+58)&63;
r[15]=(r[0]+0)&63;
CF ;case 3://lbl10270
;case 4://lbl5210
r[12]=(r[0]+56)&63;
r[13]=(r[0]+0)&63;
;case 7://lbl5220
r[11]=(r[0]+15)&63;
r[16]=(r[0]+0)&63;
r[17]=(r[0]+0)&63;
r[18]=(r[0]+0)&63;
r[19]=(r[0]+0)&63;
;case 13://lbl5230
CF ;case 14://lbl10270
r[3]=(r[18]+0)&63;
r[4]=(r[19]+0)&63;
r[5]=(r[18]+0)&63;
r[6]=(r[19]+0)&63;
r[63]=(r[0]+1)&63;
{ip=jdn(20,cond,1031,r[0]);break;}
;case 21://lbl10261
cond=r[1]>31;
CT {ip=jdn(23,cond,1027,r[0]);break;}
r[20]=(r[1]+0)&63;
r[21]=(r[2]+0)&63;
r[2]=(r[0]-r[2])&63;
r[1]=(r[0]-r[1])&63;
cond=r[2]!=0;
CT r[1]=(r[1]+63)&63;
r[22]=(r[1]+0)&63;
r[23]=(r[2]+0)&63;
r[3]=(r[16]+0)&63;
r[4]=(r[17]+0)&63;
r[5]=(r[16]+0)&63;
r[6]=(r[17]+0)&63;
r[63]=(r[0]+2)&63;
{ip=jdn(37,cond,1031,r[0]);break;}
;case 38://lbl10262
cond=r[1]>31;
CT {ip=jdn(40,cond,1027,r[0]);break;}
r[21]=(r[21]+r[2])&63;
cond=r[21]<r[2];
CT r[20]=(r[20]+1)&63;
r[20]=(r[20]+r[1])&63;
cond=r[20]>31;
CT ;case 46://lbl10270
CT {ip=jdn(47,cond,524,r[0]);break;}
r[23]=(r[23]+r[2])&63;
cond=r[23]<r[2];
CT r[22]=(r[22]+1)&63;
r[22]=(r[22]+r[1])&63;
r[3]=(r[18]+0)&63;
r[4]=(r[19]+0)&63;
r[5]=(r[16]+0)&63;
r[6]=(r[17]+0)&63;
r[63]=(r[0]+3)&63;
{ip=jdn(57,cond,1031,r[0]);break;}
;case 58://lbl10263
r[18]=(r[1]+0)&63;
r[19]=(r[2]+0)&63;
r[19]=(r[19]+r[2])&63;
cond=r[19]<r[2];
CT r[18]=(r[18]+1)&63;
r[18]=(r[18]+r[1])&63;
r[17]=(r[23]+r[13])&63;
cond=r[17]<r[13];
CT r[22]=(r[22]+1)&63;
r[16]=(r[22]+r[12])&63;
r[19]=(r[19]+r[15])&63;
cond=r[19]<r[15];
CT r[18]=(r[18]+1)&63;
r[18]=(r[18]+r[14])&63;
r[11]=(r[11]+63)&63;
cond=r[11]!=0;
CT {ip=jup(75,cond,523,r[0]);break;}
;case 76://lbl5240
cond=r[11]<1;
CT r[11]=(r[0]+50)&63;
CT {ip=jdn(79,cond,735,r[0]);break;}
cond=4>r[11];
CT r[11]=(r[0]+58)&63;
CT {ip=jdn(82,cond,735,r[0]);break;}
cond=r[11]<9;
CT r[11]=(r[0]+38)&63;
CT {ip=jdn(85,cond,735,r[0]);break;}
cond=12>r[11];
CT r[11]=(r[0]+59)&63;
CT {ip=jdn(88,cond,735,r[0]);break;}
cond=r[11]<15;
CT r[11]=(r[0]+60)&63;
CT {ip=jdn(91,cond,735,r[0]);break;}
cond=18>r[11];
CT r[11]=(r[0]+52)&63;
CT {ip=jdn(94,cond,735,r[0]);break;}
r[11]=(r[0]+36)&63;
;case 96://lbl7350
IO2(r[11]);
r[13]=(r[13]+6)&63;
cond=r[13]<6;
CT r[12]=(r[12]+1)&63;
CF ;case 101://lbl10270
cond=r[12]!=4;
CT {ip=jup(103,cond,522,r[0]);break;}
r[1]=(r[0]+62)&63;
IO2(r[1]);
r[15]=(r[15]+12)&63;
cond=r[15]<12;
CT r[14]=(r[14]+1)&63;
cond=r[14]!=6;
CT {ip=jdn(110,cond,521,r[0]);break;}
r[53]=(r[0]+13)&63;
{ip=jdn(112,cond,734,r[0]);break;}
CF ;case 113://lbl10270
;case 114://lbl10310
r[8]=(r[0]+0)&63;
r[1]=(r[0]+0)&63;
r[2]=(r[0]+0)&63;
cond=sgn(r[3])<sgn(r[0]);
CT r[8]=r[8]^1;
CT r[4]=r[4]^63;
CT r[3]=r[3]^63;
CT r[4]=(r[4]+1)&63;
CT cond=r[4]==0;
CT r[3]=(r[3]+1)&63;
cond=sgn(r[5])<sgn(0);
CT r[8]=r[8]^1;
CT r[6]=r[6]^63;
CT r[5]=r[5]^63;
CT r[6]=(r[6]+1)&63;
CT cond=r[6]==0;
CT r[5]=(r[5]+1)&63;
r[7]=(r[3]<<2)&63;
r[9]=(r[5]<<2)&63;
r[7]=((r[7]*r[9])>>0)&63;
r[9]=(r[0]+0)&63;
r[2]=(r[2]+r[7])&63;
cond=r[2]<r[7];
CT r[9]=(r[9]+1)&63;
r[7]=(r[0]+r[3])&63;
r[7]=((r[7]*r[6])>>2)&63;
r[2]=(r[2]+r[7])&63;
cond=r[2]<r[7];
CT r[9]=(r[9]+1)&63;
r[7]=(r[0]+r[4])&63;
r[7]=((r[7]*r[5])>>2)&63;
r[2]=(r[2]+r[7])&63;
cond=r[2]<r[7];
CT r[9]=(r[9]+1)&63;
r[7]=(r[0]+r[4])&63;
r[7]=((r[7]*r[6])>>8)&63;
r[2]=(r[2]+r[7])&63;
cond=r[2]<r[7];
CT r[9]=(r[9]+1)&63;
r[1]=(r[9]+0)&63;
r[9]=(r[0]+0)&63;
r[7]=(r[0]+r[3])&63;
r[7]=((r[7]*r[5])>>2)&63;
r[1]=(r[1]+r[7])&63;
cond=r[1]<r[7];
CT {ip=jup(160,cond,1027,r[0]);break;}
r[7]=(r[0]+r[3])&63;
r[7]=((r[7]*r[6])>>8)&63;
r[1]=(r[1]+r[7])&63;
cond=r[1]<r[7];
CT {ip=jup(165,cond,1027,r[0]);break;}
r[7]=(r[0]+r[4])&63;
r[7]=((r[7]*r[5])>>8)&63;
r[1]=(r[1]+r[7])&63;
cond=r[1]<r[7];
r[52]=(r[0]+23)&63;
CT {ip=jdn(171,cond,1027,r[0]);break;}
r[9]=(r[3]+0)&63;
r[9]=((r[9]*r[5])>>8)&63;
cond=r[9]!=0;
CT {ip=jdn(175,cond,1027,r[0]);break;}
cond=r[8]!=0;
CT r[2]=r[2]^63;
CT r[1]=r[1]^63;
CT r[2]=(r[2]+1)&63;
CT cond=r[2]==0;
CT r[1]=(r[1]+1)&63;
{ip=jup(182,cond,1026,r[63]);break;}
;case 183://lbl7340
r[1]=(r[0]+17)&63;
IO2(r[1]);
r[1]=(r[0]+14)&63;
IO2(r[1]);
r[1]=(r[0]+34)&63;
IO2(r[1]);
r[1]=(r[0]+62)&63;
IO2(r[1]);
IO4(r[1]);
;case 193://lbl15360
r[1]=IO4(r[0]);
cond=r[1]>1;
CF {ip=jup(196,cond,1536,r[0]);break;}
r[1]=(r[0]+17)&63;
IO2(r[1]);
r[1]=(r[0]+14)&63;
IO2(r[1]);
r[1]=(r[0]+34)&63;
IO2(r[1]);
r[1]=(r[0]+36)&63;
IO2(r[1]);
r[51]=(r[0]+14)&63;
r[1]=(r[0]+20)&63;
IO2(r[1]);
r[1]=(r[0]+18)&63;
IO2(r[1]);
r[1]=(r[0]+13)&63;
IO2(r[1]);
r[1]=(r[0]+62)&63;
IO2(r[1]);
IO4(r[1]);
;case 215://lbl15360
r[1]=IO4(r[0]);
cond=r[1]>1;
CF {ip=jup(218,cond,1536,r[0]);break;}
r[1]=(r[0]+32)&63;
IO2(r[1]);
r[1]=(r[0]+10)&63;
IO2(r[1]);
r[1]=(r[0]+23)&63;
IO2(r[1]);
r[1]=(r[0]+29)&63;
IO2(r[1]);
r[1]=(r[0]+36)&63;
IO2(r[1]);
r[1]=(r[0]+28)&63;
IO2(r[1]);
r[1]=(r[0]+30)&63;
IO2(r[1]);
r[1]=(r[0]+22)&63;
IO2(r[1]);
r[1]=(r[0]+60)&63;
IO2(r[1]);
IO2(r[1]);
IO2(r[1]);
IO4(r[1]);
;case 240://lbl15360
r[1]=IO4(r[0]);
cond=r[1]>1;
CF {ip=jup(243,cond,1536,r[0]);break;}
r[1]=(r[0]+15)&63;
IO2(r[1]);
r[1]=(r[0]+21)&63;
IO2(r[1]);
r[1]=(r[0]+10)&63;
IO2(r[1]);
r[1]=(r[0]+16)&63;
IO2(r[1]);
r[1]=(r[0]+53)&63;
IO2(r[1]);
r[1]=(r[0]+36)&63;
IO2(r[1]);
r[1]=(r[0]+62)&63;
IO2(r[1]);
;case 258://lbl15370
r[1]=IO0(r[0]);
cond=r[1]>3;
CF {ip=jup(261,cond,1537,r[0]);break;}
r[60]=IO1(r[0]);
r[61]=IO1(r[0]);
IO1(r[0]);
IO1(r[0]);
r[2]=(r[0]+0)&63;
;case 267://lbl15390
r[1]=(r[2]+r[60])&63;
r[63]=(r[0]+1)&63;
{ip=jdn(270,cond,1538,r[0]);break;}
;case 271://lbl15401
r[3]=(r[0]+r[1])&63;
r[1]=r[2]^r[61];
r[63]=(r[0]+2)&63;
{ip=jdn(275,cond,1538,r[0]);break;}
;case 276://lbl15402
r[3]=(r[3]-r[1])&63;
IO2(r[3]);
if(((r[2]+40)&63)!=0)r[(r[2]+40)&63]=r[3];
r[2]=(r[2]+1)&63;
cond=r[2]==12;
CF {ip=jup(282,cond,1539,r[0]);break;}
r[1]=(r[0]+0)&63;
r[50]=(r[0]+16)&63;
;case 285://lbl15390
r[2]=r[(r[1]+50)&63];
IO2(r[2]);
r[1]=(r[1]+1)&63;
cond=r[1]==4;
CF {ip=jup(290,cond,1539,r[0]);break;}
r[1]=(r[0]+10)&63;
r[2]=(r[0]+30)&63;
r[3]=(r[0]+50)&63;
r[4]=(r[0]+53)&63;
r[5]=(r[0]+62)&63;
r[6]=(r[0]+21)&63;
r[10]=(r[0]+0)&63;
;case 298://lbl15390
r[7]=(r[0]+0)&63;
r[12]=(r[0]+0)&63;
;case 301://lbl15410
r[7]=(r[7]<<1)&63;
r[8]=r[(r[12]+1)&63];
r[8]=r[8]&1;
r[7]=r[7]|r[8];
r[8]=r[(r[12]+1)&63];
r[8]=r[8]>>1;
if(((r[12]+1)&63)!=0)r[(r[12]+1)&63]=r[8];
r[12]=(r[12]+1)&63;
cond=r[12]==6;
CF {ip=jup(311,cond,1541,r[0]);break;}
IO2(r[7]);
r[10]=(r[10]+1)&63;
cond=r[10]==6;
CF {ip=jup(315,cond,1539,r[0]);break;}
{ip=jup(316,cond,1542,r[0]);break;}
;case 317://lbl15430
{printf("halt0\n");running=false;}//halt0
;case 319://lbl15380
cond=r[1]==35;
CT r[1]=r[1]^40;
CT {ip=jup(322,cond,1540,r[63]);break;}
cond=r[1]==29;
CT r[1]=r[1]^41;
CT {ip=jup(325,cond,1540,r[63]);break;}
cond=r[1]==34;
CT r[1]=r[1]^52;
CT {ip=jup(328,cond,1540,r[63]);break;}
cond=r[1]==16;
CT r[1]=r[1]^8;
CT {ip=jup(331,cond,1540,r[63]);break;}
cond=r[1]==12;
CT r[1]=r[1]^54;
CT {ip=jup(334,cond,1540,r[63]);break;}
cond=r[1]==13;
CT r[1]=r[1]^5;
CT {ip=jup(337,cond,1540,r[63]);break;}
cond=r[1]==50;
CT r[1]=r[1]^18;
CT {ip=jup(340,cond,1540,r[63]);break;}
cond=r[1]==0;
CT r[1]=r[1]^7;
CT {ip=jup(343,cond,1540,r[63]);break;}
cond=r[1]==42;
CT r[1]=r[1]^35;
CT {ip=jup(346,cond,1540,r[63]);break;}
cond=r[1]==49;
CT r[1]=r[1]^21;
CT {ip=jup(349,cond,1540,r[63]);break;}
cond=r[1]==48;
CT r[1]=r[1]^11;
CT {ip=jup(352,cond,1540,r[63]);break;}
cond=r[1]==31;
CT r[1]=r[1]^54;
CT {ip=jup(355,cond,1540,r[63]);break;}
cond=r[1]==14;
CT r[1]=r[1]^59;
CT {ip=jup(358,cond,1540,r[63]);break;}
cond=r[1]==28;
CT r[1]=r[1]^43;
CT {ip=jup(361,cond,1540,r[63]);break;}
cond=r[1]==18;
CT r[1]=r[1]^20;
CT {ip=jup(364,cond,1540,r[63]);break;}
cond=r[1]==61;
CT r[1]=r[1]^3;
CT {ip=jup(367,cond,1540,r[63]);break;}
cond=r[1]==7;
CT r[1]=r[1]^4;
CT {ip=jup(370,cond,1540,r[63]);break;}
cond=r[1]==53;
CT r[1]=r[1]^34;
CT {ip=jup(373,cond,1540,r[63]);break;}
cond=r[1]==51;
CT r[1]=r[1]^32;
CT {ip=jup(376,cond,1540,r[63]);break;}
cond=r[1]==54;
CT r[1]=r[1]^44;
CT {ip=jup(379,cond,1540,r[63]);break;}
cond=r[1]==23;
CT r[1]=r[1]^43;
CT {ip=jup(382,cond,1540,r[63]);break;}
cond=r[1]==63;
CT r[1]=r[1]^29;
CT {ip=jup(385,cond,1540,r[63]);break;}
cond=r[1]==9;
CT r[1]=r[1]^16;
CT {ip=jup(388,cond,1540,r[63]);break;}
cond=r[1]==26;
CT r[1]=r[1]^30;
CT {ip=jup(391,cond,1540,r[63]);break;}
cond=r[1]==59;
CT r[1]=r[1]^46;
CT {ip=jup(394,cond,1540,r[63]);break;}
cond=r[1]==3;
CT r[1]=r[1]^14;
CT {ip=jup(397,cond,1540,r[63]);break;}
cond=r[1]==6;
CT r[1]=r[1]^45;
CT {ip=jup(400,cond,1540,r[63]);break;}
cond=r[1]==32;
CT r[1]=r[1]^31;
CT {ip=jup(403,cond,1540,r[63]);break;}
cond=r[1]==2;
CT r[1]=r[1]^42;
CT {ip=jup(406,cond,1540,r[63]);break;}
cond=r[1]==41;
CT r[1]=r[1]^6;
CT {ip=jup(409,cond,1540,r[63]);break;}
cond=r[1]==25;
CT r[1]=r[1]^41;
CT {ip=jup(412,cond,1540,r[63]);break;}
cond=r[1]==55;
CT r[1]=r[1]^14;
CT {ip=jup(415,cond,1540,r[63]);break;}
cond=r[1]==20;
CT r[1]=r[1]^58;
CT {ip=jup(418,cond,1540,r[63]);break;}
cond=r[1]==10;
CT r[1]=r[1]^15;
CT {ip=jup(421,cond,1540,r[63]);break;}
cond=r[1]==27;
CT r[1]=r[1]^45;
CT {ip=jup(424,cond,1540,r[63]);break;}
cond=r[1]==11;
CT r[1]=r[1]^10;
CT {ip=jup(427,cond,1540,r[63]);break;}
cond=r[1]==4;
CT r[1]=r[1]^25;
CT {ip=jup(430,cond,1540,r[63]);break;}
cond=r[1]==58;
CT r[1]=r[1]^53;
CT {ip=jup(433,cond,1540,r[63]);break;}
cond=r[1]==36;
CT r[1]=r[1]^52;
CT {ip=jup(436,cond,1540,r[63]);break;}
cond=r[1]==60;
CT r[1]=r[1]^48;
CT {ip=jup(439,cond,1540,r[63]);break;}
cond=r[1]==56;
CT r[1]=r[1]^56;
CT {ip=jup(442,cond,1540,r[63]);break;}
cond=r[1]==8;
CT r[1]=r[1]^34;
CT {ip=jup(445,cond,1540,r[63]);break;}
cond=r[1]==22;
CT r[1]=r[1]^24;
CT {ip=jup(448,cond,1540,r[63]);break;}
cond=r[1]==19;
CT r[1]=r[1]^17;
CT {ip=jup(451,cond,1540,r[63]);break;}
cond=r[1]==37;
CT r[1]=r[1]^55;
CT {ip=jup(454,cond,1540,r[63]);break;}
cond=r[1]==17;
CT r[1]=r[1]^5;
CT {ip=jup(457,cond,1540,r[63]);break;}
cond=r[1]==46;
CT r[1]=r[1]^13;
CT {ip=jup(460,cond,1540,r[63]);break;}
cond=r[1]==39;
CT r[1]=r[1]^22;
CT {ip=jup(463,cond,1540,r[63]);break;}
cond=r[1]==30;
CT r[1]=r[1]^15;
CT {ip=jup(466,cond,1540,r[63]);break;}
cond=r[1]==57;
CT r[1]=r[1]^31;
CT {ip=jup(469,cond,1540,r[63]);break;}
cond=r[1]==44;
CT r[1]=r[1]^30;
CT {ip=jup(472,cond,1540,r[63]);break;}
cond=r[1]==5;
CT r[1]=r[1]^30;
CT {ip=jup(475,cond,1540,r[63]);break;}
cond=r[1]==24;
CT r[1]=r[1]^43;
CT {ip=jup(478,cond,1540,r[63]);break;}
cond=r[1]==40;
CT r[1]=r[1]^16;
CT {ip=jup(481,cond,1540,r[63]);break;}
cond=r[1]==21;
CT r[1]=r[1]^31;
CT {ip=jup(484,cond,1540,r[63]);break;}
cond=r[1]==33;
CT r[1]=r[1]^13;
CT {ip=jup(487,cond,1540,r[63]);break;}
cond=r[1]==45;
CT r[1]=r[1]^51;
CT {ip=jup(490,cond,1540,r[63]);break;}
cond=r[1]==52;
CT r[1]=r[1]^19;
CT {ip=jup(493,cond,1540,r[63]);break;}
cond=r[1]==43;
CT r[1]=r[1]^22;
CT {ip=jup(496,cond,1540,r[63]);break;}
cond=r[1]==47;
CT r[1]=r[1]^51;
CT {ip=jup(499,cond,1540,r[63]);break;}
cond=r[1]==15;
CT r[1]=r[1]^42;
CT {ip=jup(502,cond,1540,r[63]);break;}
cond=r[1]==1;
CT r[1]=r[1]^44;
CT {ip=jup(505,cond,1540,r[63]);break;}
cond=r[1]==38;
CT r[1]=r[1]^7;
CT {ip=jup(508,cond,1540,r[63]);break;}
cond=r[1]==62;
CT r[1]=r[1]^33;
CT {ip=jup(511,cond,1540,r[63]);break;}
;case 512://lbl15420
r[40]=r[40]^58;
r[41]=r[41]^18;
r[42]=r[42]^27;
r[43]=r[43]^11;
r[44]=r[44]^18;
r[45]=r[45]^1;
r[1]=r[1]&r[0];
;case 520://lbl15390
r[2]=r[(r[1]+40)&63];
IO2(r[2]);
r[1]=(r[1]+1)&63;
cond=r[1]!=6;
CT {ip=jup(525,cond,1539,r[0]);break;}
{ip=jup(526,cond,1543,r[0]);break;}

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
	X_JUMP_TARGET(3, 2, 1027, 0)\
	X_JUMP_TARGET(4, 0, 521, 0)\
	X_JUMP_TARGET(7, 0, 522, 0)\
	X_JUMP_TARGET(13, 0, 523, 0)\
	X_JUMP_TARGET(14, 2, 1027, 0)\
	X_JUMP_TARGET(21, 0, 1026, 1)\
	X_JUMP_TARGET(38, 0, 1026, 2)\
	X_JUMP_TARGET(46, 1, 1027, 0)\
	X_JUMP_TARGET(58, 0, 1026, 3)\
	X_JUMP_TARGET(76, 0, 524, 0)\
	X_JUMP_TARGET(96, 0, 735, 0)\
	X_JUMP_TARGET(101, 2, 1027, 0)\
	X_JUMP_TARGET(113, 2, 1027, 0)\
	X_JUMP_TARGET(114, 0, 1031, 0)\
	X_JUMP_TARGET(183, 0, 734, 0)\
	X_JUMP_TARGET(193, 0, 1536, 0)\
	X_JUMP_TARGET(215, 0, 1536, 0)\
	X_JUMP_TARGET(240, 0, 1536, 0)\
	X_JUMP_TARGET(258, 0, 1537, 0)\
	X_JUMP_TARGET(267, 0, 1539, 0)\
	X_JUMP_TARGET(271, 0, 1540, 1)\
	X_JUMP_TARGET(276, 0, 1540, 2)\
	X_JUMP_TARGET(285, 0, 1539, 0)\
	X_JUMP_TARGET(298, 0, 1539, 0)\
	X_JUMP_TARGET(301, 0, 1541, 0)\
	X_JUMP_TARGET(317, 0, 1543, 0)\
	X_JUMP_TARGET(319, 0, 1538, 0)\
	X_JUMP_TARGET(512, 0, 1542, 0)\
	X_JUMP_TARGET(520, 0, 1539, 0)

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

