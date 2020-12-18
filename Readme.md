# EMU 1.0 to C++ transpiler

Hello, this is trupples' second attempt at an emulator, which works lightning fast by virtue of modern compiler optimisation, but is unable to run/compile long roms (such as the 330MB rickroll) on average consumer hardware (I expect tens of gigs of ram and maybe a few CPU hours are required).

`emu1c.cpp` contains template c++ code, with some "REPLACE:XXX" strings which are processed by
`emu1c.py` the transpiler.

Usage:
```
python3 emu1c.py hello.rom > hello.cpp
g++ hello.cpp -lpthread $(sdl2-config --cflags --libs) -o hello-compiled
./hello-compiled
```

Usage for ENET client functionality:
```
python3 emu1c.py hello.rom --client=host:port > hello.cpp
g++ hello.cpp -lpthread $(sdl2-config --cflags --libs) -o hello-compiled
./hello-compiled
```

This /should/ also work on windows with minimal tweaks to the build commands. Tested on WSL 1 with X11 forwarding and Debian.

I also included 5 out of the 6 submitted demos and the resulting binaries.
```
./run-demo.sh nyan
./run-demo.sh GOL
./run-demo.sh snak
./run-demo.sh redstoneBlockchain
./run-demo.sh paint --client=emu-1.quphoria.co.uk:1337
```

