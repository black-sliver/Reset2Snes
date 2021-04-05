#include "USB2SNES.h"
#include <chrono>
#include <stdio.h>

int main(int argc, char** argv)
{
    USB2SNES snes("Reset2SNES");
    for (uint8_t i=0; i<100 && !snes.snesConnected(); i++) {
        snes.connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!snes.snesConnected()) {
        printf("could not connect to snes!\n");
        return 1;
    }
    if (snes.reset()) {
        printf("snes reset!\n");
        return 0;
    }
    printf("error\n");
    return 1;
}
