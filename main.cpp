//=============================================================
//  main.cpp  —  EDMD 2D 
//=============================================================
#include <cstdio>
#include "edmd2d.h"

int main()
{
    loadConfig("config.txt");
    init();
    printf("Starting\n");

    while (simtime <= maxtime)
        step();

    simtime = maxtime;
    printSummary();
    outputSnapshot();

    delete[] celllist;
    delete[] particles;
    delete[] eventlists;
    return 0;
}
