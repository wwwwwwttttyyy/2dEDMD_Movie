//=============================================================
//  edmd2d.cpp  —  2D Event‐Driven Molecular Dynamics
//
//  Pure 2D implementation: no z‐dimension anywhere.
//  IO via SnapshotIO class (snapshot_io.h).
//  Format:  Lx Ly N  /  x y r type
//=============================================================
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <random>

#include "edmd2d.h"
#include "snapshot_io.h"

//-------------------------------------------------------------
//  Simulation parameters  （默认值，可被 config.txt 覆盖）
//-------------------------------------------------------------
unsigned long seed      = 1;
static std::mt19937                      rng;
static std::normal_distribution<double>  gauss_dist(0.0, 1.0);
static std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
double maxtime          = 10000;
int    makesnapshots    = 0;
double writeinterval    = 100.0;
double snapshotinterval = 1.0;

int    initialconfig    = 1;
char   inputfilename[100] = "init.dat";
double packfrac         = 0.72;
int    N                = 4096;

//-------------------------------------------------------------
//  Event-list tuning
//-------------------------------------------------------------
double maxscheduletime         = 1.0;
int    numeventlists;
double eventlisttimemultiplier = 1;
double eventlisttime;

//-------------------------------------------------------------
//  Neighbor-list shell
//-------------------------------------------------------------
double shellsize = 1.5;

//-------------------------------------------------------------
//  Internal state
//-------------------------------------------------------------
double simtime     = 0;
double reftime     = 0;
int    currentlist = 0;
int    totalevents;

int listcounter1 = 0, listcounter2 = 0, mergecounter = 0;

Particle** eventlists;    // numeventlists + 1 (last = overflow)
Particle*  particles;
Particle** celllist;
Particle*  root;

double xsize, ysize;      // Box dimensions
double hx, hy;             // Half‐box (for PBC nearest‐image)
double icxsize, icysize;   // Inverse cell size
int    cx, cy;             // Number of cells in x, y

double       dvtot      = 0;  // Accumulated momentum transfer (virial)
unsigned int colcounter  = 0;

int    usethermostat      = 1;
double thermostatinterval = 0.01;


//=============================================================
//  loadConfig  —  从 config.txt 读取参数（key = value 格式）
//=============================================================
void loadConfig(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
    {
        printf("Config file '%s' not found, using defaults.\n", filename);
        return;
    }
    printf("Reading config from '%s'\n", filename);

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        // 跳过注释和空行
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char key[64];
        char val[128];
        if (sscanf(p, "%63[^= ] = %127[^\n]", key, val) != 2) continue;

        // 去掉 val 中行内注释
        for (char* c = val; *c; c++)
            if (*c == '#') { *c = '\0'; break; }
        // 去掉尾部空白
        for (int i = (int)strlen(val) - 1; i >= 0 && (val[i]==' '||val[i]=='\t'); i--)
            val[i] = '\0';

        if      (strcmp(key, "seed")                    == 0) seed                    = (unsigned long)atol(val);
        else if (strcmp(key, "maxtime")                 == 0) maxtime                 = atof(val);
        else if (strcmp(key, "makesnapshots")            == 0) makesnapshots            = atoi(val);
        else if (strcmp(key, "writeinterval")            == 0) writeinterval            = atof(val);
        else if (strcmp(key, "snapshotinterval")         == 0) snapshotinterval         = atof(val);
        else if (strcmp(key, "initialconfig")            == 0) initialconfig            = atoi(val);
        else if (strcmp(key, "inputfilename")            == 0) snprintf(inputfilename, sizeof(inputfilename), "%s", val);
        else if (strcmp(key, "packfrac")                 == 0) packfrac                 = atof(val);
        else if (strcmp(key, "N")                        == 0) N                        = atoi(val);
        else if (strcmp(key, "maxscheduletime")          == 0) maxscheduletime          = atof(val);
        else if (strcmp(key, "eventlisttimemultiplier")  == 0) eventlisttimemultiplier  = atof(val);
        else if (strcmp(key, "shellsize")                == 0) shellsize                = atof(val);
        else if (strcmp(key, "usethermostat")            == 0) usethermostat            = atoi(val);
        else if (strcmp(key, "thermostatinterval")       == 0) thermostatinterval       = atof(val);
        else printf("  Warning: unknown key '%s'\n", key);
    }
    fclose(fp);
}


//=============================================================
//  printSummary  —  end‐of‐simulation statistics
//=============================================================
void printSummary()
{
    double v2tot  = 0;
    double afilled = 0;

    for (int i = 0; i < N; i++)
    {
        Particle* p = particles + i;
        v2tot  += p->mass * (p->vx * p->vx + p->vy * p->vy);
        afilled += p->radius * p->radius;           // Σ r_i²
    }
    afilled *= M_PI;                                 // Σ π r_i²

    double area    = xsize * ysize;
    double dens    = N / area;
    double press   = -dvtot / (2.0 * area * simtime);   // virial / (D * A * t)
    double pressid = dens;                               // ideal gas
    double presstot = press + pressid;

    printf("Average kinetic energy: %lf\n", 0.5 * v2tot / N);
    printf("Total time simulated  : %lf\n", simtime);
    printf("Packing fraction      : %lf\n", afilled / area);
    printf("Measured pressure     : %lf + %lf = %lf\n", press, pressid, presstot);
}


//=============================================================
//  init
//=============================================================
void init()
{
    printf("Seed: %lu\n", seed);
    rng.seed(seed);

    if (initialconfig == 0) loadParticles();
    else                    squareLattice();

    randomMovement();
    hx = 0.5 * xsize;
    hy = 0.5 * ysize;

    initEvents();

    for (int i = 0; i < N; i++)
    {
        Particle* p = particles + i;
        p->boxestraveledx = 0;
        p->boxestraveledy = 0;
        p->nneigh  = 0;
        p->counter = 0;
        p->t  = 0;
        p->xn = p->x;
        p->yn = p->y;
    }

    initCellList();

    for (int i = 0; i < N; i++)
        makeNeighborList(particles + i);

    printf("Done adding collisions\n");
}


//=============================================================
//  initParticles  —  allocate particle + extra‐event array
//=============================================================
void initParticles(int n)
{
    N = n;
    totalevents = N + EXTRAEVENTS;
    particles = new Particle[totalevents]();   // zero‐initialised
    if (!particles)
    {
        printf("Failed to allocate memory for particles\n");
        exit(3);
    }
}


//=============================================================
//  squareLattice  —  place particles on a 2D square lattice
//=============================================================
void squareLattice()
{
    int ncell = (int)ceil(sqrt((double)N));
    int N_actual = ncell * ncell;

    if (N_actual != N)
        printf("Adjusting N from %d to %d (square lattice)\n", N, N_actual);

    // φ = N π r² / A   →   A = N π r² / φ   (r = 0.5)
    double area = N_actual * M_PI * 0.25 / packfrac;
    xsize = sqrt(area);
    ysize = xsize;

    double step = xsize / ncell;
    printf("step: %lf\n", step);
    initParticles(N_actual);
    printf("Placing particles on square lattice\n");

    Particle* p = particles;
    for (int i = 0; i < ncell; i++)
        for (int j = 0; j < ncell; j++)
        {
            p->x = (i + 0.5) * step;
            p->y = (j + 0.5) * step;
            p->radius = 0.5;
            p->type   = 0;
            p->mass   = 1;
            p++;
        }

    printf("Packing fraction: %lf\n", M_PI * 0.25 * N / (xsize * ysize));
    printf("Starting configuration from square lattice\n");
}


//=============================================================
//  loadParticles  —  read from file via SnapshotIO
//=============================================================
void loadParticles()
{
    SnapshotIO::Snapshot snap = SnapshotIO::load(inputfilename);
    xsize = snap.lx;
    ysize = snap.ly;

    initParticles((int)snap.particles.size());
    printf("Placing particles\n");

    double afilled = 0;
    for (int i = 0; i < N; i++)
    {
        Particle* p = &particles[i];
        const auto& rec = snap.particles[i];
        p->x      = rec.x;
        p->y      = rec.y;
        p->radius = rec.radius;
        p->type   = rec.type;
        p->mass   = 1;
        backInBox(p);
        afilled += p->radius * p->radius;
    }

    printf("Packing fraction: %lf\n", M_PI * afilled / (xsize * ysize));
    printf("Starting configuration read from %s\n", inputfilename);
}


//=============================================================
//  randomMovement  —  Maxwell‑Boltzmann at kT = 1, 2 DOF
//=============================================================
void randomMovement()
{
    double v2tot = 0, vxtot = 0, vytot = 0, mtot = 0;

    for (int i = 0; i < N; i++)
    {
        Particle* p = particles + i;
        double imsq = 1.0 / sqrt(p->mass);
        p->vx = imsq * randomGaussian();
        p->vy = imsq * randomGaussian();
        vxtot += p->mass * p->vx;
        vytot += p->mass * p->vy;
        mtot  += p->mass;
    }

    vxtot /= mtot;
    vytot /= mtot;

    for (int i = 0; i < N; i++)
    {
        Particle* p = &particles[i];
        p->vx -= vxtot;
        p->vy -= vytot;
        v2tot += p->mass * (p->vx * p->vx + p->vy * p->vy);
    }

    // E_kin / N = kT  (for 2 DOF)  →  Σm v² / N = 2 kT = 2
    double fac = sqrt(2.0 / (v2tot / N));
    for (int i = 0; i < N; i++)
    {
        Particle* p = &particles[i];
        p->vx *= fac;
        p->vy *= fac;
    }
}


//=============================================================
//  update  —  extrapolate particle position to simtime
//=============================================================
void update(Particle* p1)
{
    double dt = simtime - p1->t;
    p1->t  = simtime;
    p1->x += dt * p1->vx;
    p1->y += dt * p1->vy;
}


//=============================================================
//  initCellList  —  2D cell grid
//=============================================================
void initCellList()
{
    // Find largest particle radius
    double maxr = 0;
    for (int i = 0; i < N; i++)
        if (particles[i].radius > maxr) maxr = particles[i].radius;

    double cellsize = shellsize * 2.0 * maxr;

    cx = (int)((xsize - 0.0001) / cellsize);
    cy = (int)((ysize - 0.0001) / cellsize);
    if (cx < 3) cx = 3;
    if (cy < 3) cy = 3;

    while (cx * cy > 8 * N)
    {
        cx = (int)(cx * 0.9);
        cy = (int)(cy * 0.9);
        if (cx < 3) cx = 3;
        if (cy < 3) cy = 3;
    }

    printf("Cells: %d x %d\n", cx, cy);
    celllist = new Particle*[cx * cy]();   // zero‐initialised

    icxsize = cx / xsize;
    icysize = cy / ysize;

    for (int i = 0; i < N; i++)
    {
        Particle* p = particles + i;
        addToCellList(p, (int)(p->x * icxsize), (int)(p->y * icysize));
    }
}


//=============================================================
//  cellOffset  —  2D cell index from (ix, iy)
//=============================================================
int cellOffset(int a, int b)
{
    return a + b * cx;
}


//=============================================================
//  addToCellList / removeFromCellList
//=============================================================
void addToCellList(Particle* p, int cellx, int celly)
{
    p->cell = cellOffset(cellx, celly);
    p->next = celllist[p->cell];
    if (p->next) p->next->prev = p;
    celllist[p->cell] = p;
    p->prev = nullptr;
    p->nearboxedge = (cellx == 0 || celly == 0 ||
                      cellx == cx - 1 || celly == cy - 1);
}

void removeFromCellList(Particle* p)
{
    if (p->prev) p->prev->next = p->next;
    else         celllist[p->cell] = p->next;
    if (p->next) p->next->prev = p->prev;
}


//=============================================================
//  step  —  process the next event
//=============================================================
void step()
{
    Particle* ev = root->right;
    while (ev == nullptr)
    {
        addNextEventList();
        ev = root->right;
    }
    while (ev->left) ev = ev->left;

    simtime = ev->eventtime;
    removeEvent(ev);

    switch (ev->eventtype)
    {
        case 0:   collision(ev);           break;
        case 8:   makeNeighborList(ev);    break;
        case 100: writeOutput(ev);         break;
        case 101: thermostat(ev);          break;
    }
}


//=============================================================
//  makeNeighborList  —  rebuild neighbor list for one particle
//=============================================================
void makeNeighborList(Particle* p1)
{
    update(p1);

    // Periodic boundaries
    if (p1->x >= xsize) { p1->x -= xsize; p1->boxestraveledx++; }
    else if (p1->x < 0) { p1->x += xsize; p1->boxestraveledx--; }
    if (p1->y >= ysize) { p1->y -= ysize; p1->boxestraveledy++; }
    else if (p1->y < 0) { p1->y += ysize; p1->boxestraveledy--; }

    p1->xn = p1->x;
    p1->yn = p1->y;

    removeFromCellList(p1);

    // Remove p1 from its old neighbors' lists
    for (int i = 0; i < p1->nneigh; i++)
    {
        Particle* p2 = p1->neighbors[i];
        for (int j = 0; j < p2->nneigh; j++)
        {
            if (p2->neighbors[j] == p1)
            {
                p2->nneigh--;
                p2->neighbors[j] = p2->neighbors[p2->nneigh];
                break;
            }
        }
    }

    int cellx = (int)(p1->x * icxsize);
    int celly = (int)(p1->y * icysize);
    addToCellList(p1, cellx, celly);

    cellx += cx;   // offset for safe modular arithmetic
    celly += cy;
    p1->nneigh = 0;

    // 2D: loop over 3×3 = 9 neighboring cells
    for (int cdx = cellx - 1; cdx < cellx + 2; cdx++)
        for (int cdy = celly - 1; cdy < celly + 2; cdy++)
        {
            Particle* p2 = celllist[cellOffset(cdx % cx, cdy % cy)];
            while (p2)
            {
                if (p2 != p1)
                {
                    double dx = p1->xn - p2->xn;
                    double dy = p1->yn - p2->yn;
                    if (p1->nearboxedge)
                    {
                        if (dx >  hx) dx -= xsize; else if (dx < -hx) dx += xsize;
                        if (dy >  hy) dy -= ysize; else if (dy < -hy) dy += ysize;
                    }
                    double r2 = dx * dx + dy * dy;
                    double rm = (p1->radius + p2->radius) * shellsize;
                    if (r2 < rm * rm)
                    {
                        p1->neighbors[p1->nneigh++] = p2;
                        p2->neighbors[p2->nneigh++] = p1;
                        if (p1->nneigh >= MAXNEIGH || p2->nneigh >= MAXNEIGH)
                        {
                            printf("Too many neighbors. Increase MAXNEIGH.\n");
                            exit(3);
                        }
                    }
                }
                p2 = p2->next;
            }
        }

    findCollisions(p1);
}


//=============================================================
//  findNeighborListUpdate
//  Time until p1 leaves its neighbor‐list shell
//=============================================================
double findNeighborListUpdate(Particle* p1)
{
    double dx  = p1->x - p1->xn;
    double dy  = p1->y - p1->yn;
    double dvx = p1->vx;
    double dvy = p1->vy;

    double b   = dx * dvx + dy * dvy;
    double dv2 = dvx * dvx + dvy * dvy;
    double dr2 = dx * dx + dy * dy;
    double md  = (shellsize - 1) * p1->radius;

    double disc = b * b - dv2 * (dr2 - md * md);
    return (-b + sqrt(disc)) / dv2;
}


//=============================================================
//  findCollision  —  2D elastic hard‐disk collision detection
//=============================================================
int findCollision(Particle* p1, Particle* p2, double* tmin)
{
    double dt2 = simtime - p2->t;
    double dx  = p1->x - p2->x - dt2 * p2->vx;
    double dy  = p1->y - p2->y - dt2 * p2->vy;
    if (p1->nearboxedge)
    {
        if (dx >  hx) dx -= xsize; else if (dx < -hx) dx += xsize;
        if (dy >  hy) dy -= ysize; else if (dy < -hy) dy += ysize;
    }

    double dvx = p1->vx - p2->vx;
    double dvy = p1->vy - p2->vy;

    double b = dx * dvx + dy * dvy;
    if (b > 0) return 0;                           // flying apart

    double dr2 = dx * dx + dy * dy;
    double md  = p1->radius + p2->radius;
    double A   = md * md - dr2;
    if (2 * b * *tmin > A) return 0;               // quick reject

    double dv2  = dvx * dvx + dvy * dvy;
    double disc = b * b + dv2 * A;
    if (disc < 0) return 0;

    double t = (-b - sqrt(disc)) / dv2;
    if (t < *tmin)
    {
        *tmin = t;
        return 1;
    }
    return 0;
}


//=============================================================
//  findCollisions  —  schedule earliest event for p1
//=============================================================
void findCollisions(Particle* p1)
{
    double tmin = findNeighborListUpdate(p1);
    int type = 8;                                   // default: neighbor update
    Particle* partner = p1;

    for (int i = 0; i < p1->nneigh; i++)
    {
        Particle* p2 = p1->neighbors[i];
        if (findCollision(p1, p2, &tmin))
        {
            partner = p2;
            type = 0;
        }
    }
    createEvent(tmin + simtime, p1, partner, type);
    p1->counter2 = partner->counter;
}


//=============================================================
//  collision  —  2D elastic hard‐disk collision response
//=============================================================
void collision(Particle* p1)
{
    Particle* p2 = p1->p2;
    update(p1);

    if (p1->counter2 != p2->counter)                // stale event
    {
        findCollisions(p1);
        return;
    }

    update(p2);
    p1->counter++;
    p2->counter++;

    double m1 = p1->mass;
    double m2 = p2->mass;
    double r  = p1->radius + p2->radius;
    double rinv = 1.0 / r;

    double dx = p1->x - p2->x;
    double dy = p1->y - p2->y;
    if (p1->nearboxedge)
    {
        if (dx >  hx) dx -= xsize; else if (dx < -hx) dx += xsize;
        if (dy >  hy) dy -= ysize; else if (dy < -hy) dy += ysize;
    }
    dx *= rinv;
    dy *= rinv;

    double dvx = p1->vx - p2->vx;
    double dvy = p1->vy - p2->vy;

    double b = dx * dvx + dy * dvy;
    b *= 2.0 / (m1 + m2);

    double dv1  = b * m2;
    double dv2b = b * m1;

    p1->vx -= dv1  * dx;
    p1->vy -= dv1  * dy;
    p2->vx += dv2b * dx;
    p2->vy += dv2b * dy;

    dvtot += dv1 * m1 * r;
    colcounter++;

    removeEvent(p2);
    findCollisions(p1);
    findCollisions(p2);
}


//=============================================================
//  Event calendar
//=============================================================
void initEvents()
{
    eventlisttime = eventlisttimemultiplier / N;
    numeventlists = (int)ceil(maxscheduletime / eventlisttime);
    maxscheduletime = numeventlists * eventlisttime;
    printf("Number of event lists: %d\n", numeventlists);

    eventlists = new Particle*[numeventlists + 1]();
    if (!eventlists) { printf("Failed to allocate event lists\n"); exit(3); }

    // Root sentinel
    root = particles + N;
    root->eventtime = -99999999999.99;
    root->eventtype = 127;
    root->parent    = nullptr;

    // Write event
    Particle* we   = particles + N + 1;
    we->eventtime  = 0;
    we->eventtype  = 100;
    we->p2         = nullptr;
    addEvent(we);
    printf("Event tree initialised.\n");

    // Thermostat
    if (usethermostat)
    {
        Particle* te   = particles + N + 2;
        te->eventtime  = thermostatinterval;
        te->eventtype  = 101;
        te->p2         = nullptr;
        addEvent(te);
        printf("Started thermostat\n");
    }
}


void addEventToTree(Particle* ev)
{
    double time = ev->eventtime;
    Particle* loc = root;
    int busy = 1;
    while (busy)
    {
        if (time < loc->eventtime)
        {
            if (loc->left) loc = loc->left;
            else { loc->left = ev; busy = 0; }
        }
        else
        {
            if (loc->right) loc = loc->right;
            else { loc->right = ev; busy = 0; }
        }
    }
    ev->parent = loc;
    ev->left   = nullptr;
    ev->right  = nullptr;
}


void addEvent(Particle* ev)
{
    double dt = ev->eventtime - reftime;

    if (dt < eventlisttime)
    {
        ev->queue = currentlist;
        addEventToTree(ev);
    }
    else
    {
        int list_id;
        if (dt >= numeventlists * eventlisttime) list_id = numeventlists;
        else
        {
            list_id = currentlist + (int)(dt / eventlisttime);
            if (list_id >= numeventlists) list_id -= numeventlists;
        }
        ev->queue = list_id;
        ev->right = eventlists[list_id];
        ev->left  = nullptr;
        if (ev->right) ev->right->left = ev;
        eventlists[list_id] = ev;
    }
}


void createEvent(double time, Particle* p1, Particle* p2, int type)
{
    p1->eventtime = time;
    p1->eventtype = type;
    p1->p2 = p2;
    addEvent(p1);
}


void addNextEventList()
{
    currentlist++;
    reftime += eventlisttime;

    if (currentlist == numeventlists)
    {
        currentlist = 0;
        Particle* ev = eventlists[numeventlists];
        eventlists[numeventlists] = nullptr;
        listcounter2 = 0;
        while (ev)
        {
            Particle* nxt = ev->right;
            addEvent(ev);
            ev = nxt;
            listcounter2++;
        }
    }

    Particle* ev = eventlists[currentlist];
    while (ev)
    {
        Particle* nxt = ev->right;
        addEventToTree(ev);
        ev = nxt;
        listcounter1++;
    }
    eventlists[currentlist] = nullptr;
    mergecounter++;
}


void removeEvent(Particle* old)
{
    if (old->queue != currentlist)               // in a linear list, not the tree
    {
        if (old->right) old->right->left = old->left;
        if (old->left)  old->left->right = old->right;
        else            eventlists[old->queue] = old->right;
        return;
    }

    Particle* par  = old->parent;
    Particle* node;

    if (old->left == nullptr)
    {
        node = old->right;
        if (node) node->parent = par;
    }
    else if (old->right == nullptr)
    {
        node = old->left;
        node->parent = par;
    }
    else
    {
        node = old->right;
        while (node->left) node = node->left;
        Particle* pn = node->parent;
        if (pn != old)
        {
            pn->left = node->right;
            if (node->right) node->right->parent = pn;
            old->left->parent  = node;
            node->left         = old->left;
            old->right->parent = node;
            node->right        = old->right;
        }
        else
        {
            old->left->parent = node;
            node->left        = old->left;
        }
        node->parent = par;
    }
    if (par->left == old) par->left  = node;
    else                  par->right = node;
}


//=============================================================
//  outputSnapshot  —  final snapshot via SnapshotIO
//=============================================================
void outputSnapshot()
{
    std::vector<SnapshotIO::Record> recs(N);
    for (int i = 0; i < N; i++)
    {
        Particle* p = &particles[i];
        double dt = simtime - p->t;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->t  = simtime;

        recs[i] = { p->x + xsize * p->boxestraveledx,
                     p->y + ysize * p->boxestraveledy,
                     p->radius,
                     (int)p->type };
    }
    SnapshotIO::save("snapshot_end.dat", xsize, ysize, recs);
}


//=============================================================
//  writeOutput  —  periodic screen + file output
//=============================================================
void writeOutput(Particle* we)
{
    static int    counter = 0;
    static bool   firstMovie = true;
    static double lastsnap   = -999999999.9;
    static double dvtotlast  = 0;
    static double timelast   = 0;

    // Kinetic energy & neighbor stats
    double en = 0;
    int maxn = 0, minn = 100;
    for (int i = 0; i < N; i++)
    {
        Particle* p = particles + i;
        en += p->mass * (p->vx * p->vx + p->vy * p->vy);
        if (p->nneigh > maxn) maxn = p->nneigh;
        if (p->nneigh < minn) minn = p->nneigh;
    }
    double temperature = en / (2.0 * N);   // T = Σm v² / (D N)

    double area    = xsize * ysize;
    double pressid = (double)N / area;
    double pressnow = -(dvtot - dvtotlast) / (2.0 * area * (simtime - timelast)) + pressid;
    dvtotlast = dvtot;
    timelast  = simtime;
    if (colcounter == 0) pressnow = 0;

    double ls1 = mergecounter ? (double)listcounter1 / mergecounter : 0;
    int    ls2 = listcounter2;
    listcounter1 = listcounter2 = mergecounter = 0;

    printf("t: %lf  col: %u  P: %lf  T: %lf  lists: (%.1f, %d)  neigh: %d-%d\n",
           simtime, colcounter, pressnow, temperature, ls1, ls2, minn, maxn);

    // Movie frame
    if (makesnapshots && simtime - lastsnap > snapshotinterval - 0.001)
    {
        std::vector<SnapshotIO::Record> recs(N);
        for (int i = 0; i < N; i++)
        {
            Particle* p = &particles[i];
            update(p);
            recs[i] = { p->x + xsize * p->boxestraveledx,
                         p->y + ysize * p->boxestraveledy,
                         p->radius,
                         (int)p->type };
        }
        char fname[200];
        sprintf(fname, "mov.n%d.a%.4lf.dat", N, area);
        SnapshotIO::appendFrame(fname, xsize, ysize, recs, firstMovie);
        firstMovie = false;
        lastsnap = simtime;
    }

    // Pressure data
    {
        char fname[200];
        sprintf(fname, "press.n%d.a%.4lf.dat", N, xsize * ysize);
        SnapshotIO::appendPressure(fname, simtime, pressnow, counter == 0);
    }

    counter++;
    we->eventtime = simtime + writeinterval;
    addEvent(we);
}


//=============================================================
//  backInBox  —  periodic fold (initialisation only)
//=============================================================
void backInBox(Particle* p)
{
    p->x -= xsize * floor(p->x / xsize);
    p->y -= ysize * floor(p->y / ysize);
}


//=============================================================
//  thermostat  —  Andersen‐style velocity resampling
//=============================================================
void thermostat(Particle* te)
{
    int freq = N / 100;
    if (freq == 0) freq = 1;

    for (int i = 0; i < freq; i++)
    {
        int num = (int)(uniform_dist(rng) * N);
        Particle* p = particles + num;
        double imsq = 1.0 / sqrt(p->mass);
        update(p);
        p->vx = randomGaussian() * imsq;
        p->vy = randomGaussian() * imsq;
        p->counter++;
        removeEvent(p);
        findCollisions(p);
    }
    te->eventtime = simtime + thermostatinterval;
    addEvent(te);
}


//=============================================================
//  randomGaussian  —  standard normal via STL
//=============================================================
double randomGaussian()
{
    return gauss_dist(rng);
}
