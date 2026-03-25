#pragma once
#include <cstdint>

// Maximum number of neighbors per particle
#define MAXNEIGH 24

// Number of extra events (root, write, thermostat, etc.)
#define EXTRAEVENTS 12

#ifndef M_PI
#define M_PI 3.1415926535897932
#endif

//=============================================================
//  Particle struct — pure 2D EDMD
//  Also serves as event node in the binary search tree / list
//=============================================================
struct Particle
{
    // ---- Physical state (2D) ----
    double x, y;             // Position
    double vx, vy;           // Velocity
    double xn, yn;           // Center of neighbor list shell
    double radius;
    double mass;
    double t;                // Last update time
    uint8_t type;            // Particle type (0, 1, 2, ...)

    // ---- Neighbor list ----
    Particle* neighbors[MAXNEIGH];
    uint8_t nneigh;          // Number of current neighbors

    // ---- Cell list ----
    int cell;                // Current cell index
    uint8_t nearboxedge;     // Whether this cell touches the box edge
    Particle* prev;          // Doubly‐linked cell list
    Particle* next;

    // ---- Periodic boundary bookkeeping ----
    int boxestraveledx, boxestraveledy;

    // ---- Collision bookkeeping ----
    unsigned int counter;    // Collision events experienced
    unsigned int counter2;   // Partner's counter at time of scheduling

    // ---- Event calendar (BST / linked list) ----
    double eventtime;
    Particle* left;          // Left child / previous in linear list
    Particle* right;         // Right child / next in linear list
    Particle* parent;        // Parent in tree
    Particle* p2;            // Collision partner (or self for neighbor‐list update)
    int queue;               // Index of the event queue the event lives in
    unsigned char eventtype; // 0 = collision, 8 = neighbor update, 100 = write, 101 = thermostat
};

//=============================================================
//  Global variables (defined in edmd2d.cpp)
//=============================================================
extern double       simtime;
extern double       maxtime;
extern int          N;
extern Particle*    particles;
extern Particle**   eventlists;
extern Particle**   celllist;
extern double       xsize;
extern double       ysize;

//=============================================================
//  Function declarations
//=============================================================

// Core simulation
void init();
void step();
void loadConfig(const char* filename);

// Initialization helpers
void initParticles(int n);
void squareLattice();
void loadParticles();
void randomMovement();

// Cell list
void initCellList();
int  cellOffset(int a, int b);
void addToCellList(Particle* p, int cellx, int celly);
void removeFromCellList(Particle* p);

// Neighbor list
void   makeNeighborList(Particle* p1);
double findNeighborListUpdate(Particle* p1);

// Collision detection & handling
int  findCollision(Particle* p1, Particle* p2, double* tmin);
void findCollisions(Particle* p1);
void collision(Particle* p1);

// Event calendar
void initEvents();
void addEvent(Particle* ev);
void addEventToTree(Particle* ev);
void removeEvent(Particle* ev);
void createEvent(double time, Particle* p1, Particle* p2, int type);
void addNextEventList();

// Update & utilities
void   update(Particle* p1);
void   backInBox(Particle* p);
double randomGaussian();

// Output
void printSummary();
void outputSnapshot();
void writeOutput(Particle* ev);
void thermostat(Particle* ev);
