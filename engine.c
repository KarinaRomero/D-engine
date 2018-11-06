#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL/SDL.h>

#undef main
#define W 640
#define H 480
/* Define various vision related constants*/
#define EyeHeight 6 // carema height from floor when standing
#define DuckHeight 2.5 // And when crouching
#define HeadMargin 1 // How much roo there is above camera before the head hits the celling
#define kneeHeight 2 // How tall obtacles the player can simply walk over withhout jumping
#define hfov (0.73f*H) // Affects the horizontal field of vision
#define vfov (.2f*H) // Affects the vertical field of vision

static SDL_Surface *surface = NULL;

static struct vec2d
{
    float x;
    float y;
};
static struct vec3d
{
    float x;
    float y;
    float z;
};
/**Serctors: Floor and celing height; list of edge vertices and neighbors*/
static struct sector
{
    float floor, ceil;
    struct vec2d *vertex;
    signed char *neightbors;
    unsigned int npoints;
} *sectors = NULL;
static unsigned NumSectors = 0;
/*Location of the player*/

static struct player
{
    struct vec3d where, velocity; // Current position
    float angle, anglesing, anglecos,yaw; // Current motion vector
    unsigned sector;
} player;

// Utility functions to calculate like a math library
#define min(a,b) (((a)<(b)) ? (a):(b)) // Choose the smaller value
#define max(a,b) (((a)>(b)) ? (a):(b)) // Choose the greater value
#define clamp(a, mi, ma) min(max(a,mi),ma) // Clamp value into set range
#define vsx(x0, y0, x1, y1) ((x0)*(y1) - (x1)*(y0)) // Vector cross product
#define Overlap(a0, a1, b0, b1) (min(a0, a1) <= max(b0,b1) && min(b0, b1) <= max(a0,a1)) // overlap range
#define IntersectBox(x0, y0, x1, y1, x2, y2, x3, y3) (Overlap(x0, x1, x2, x3) && Overlap(y0, y1, y2, y3))
#define PointSide(px, py, x0, y0, x1, y1) vsx ((x1)-(x0), (y1)-(y0), (px)-(x0), (py)-(y0))
#define Intersect(x1,y1, x2,y2, x3,y3, x4,y4) ((struct xy) { \
    vxs(vxs(x1,y1, x2,y2), (x1)-(x2), vxs(x3,y3, x4,y4), (x3)-(x4)) / vxs((x1)-(x2), (y1)-(y2), (x3)-(x4), (y3)-(y4)), \
    vxs(vxs(x1,y1, x2,y2), (y1)-(y2), vxs(x3,y3, x4,y4), (y3)-(y4)) / vxs((x1)-(x2), (y1)-(y2), (x3)-(x4), (y3)-(y4)) })

static void LoadData()
{
    FILE* fp = fopen("map-clear.txt", "rt");
    if(!fp)
    {
        perror("map-clear.txt");
        exit(1);
    }
    char buf[256], word[256], *ptr;
    struct vec2d *vert = NULL, v;
    int n, m, NumVertices = 0;
    while(fgets(buf, sizeof buf,fp))
    {
        switch(scanf(ptr = buf, "%32s%n", word, &n) == 1 ? word[0] : '\0')
        {
            case 'v':
                for(sscanf(ptr += n, "%f%n", &v.y, &n); sscanf(ptr += n, "%f%n", &v.x, &n) == 1; )
                {
                    vert = realloc(vert, ++NumVertices * sizeof(*vert));
                    vert[NumVertices-1] = v;
                }
            break;
            case 's':
                sectors = realloc(sectors, ++NumSectors * sizeof(sectors));
                struct sector* sect = &sectors[NumSectors-1];
                int* num = NULL;
                sscanf(ptr += n, "%f%f%n", &sect->floor, &sect->ceil, &n);
                for(m=0; sscanf(ptr += n, "%32s%n", word, &n) == 1 && word[0] != '#';)
                {
                    num = realloc(num, ++m * sizeof(*num));
                    num[m-1] = word[0]=='x' ? -1 : atoi(word);
                }
                sect->npoints = m/= 2;
                sect->neightbors = malloc((m) * sizeof(*sect->neightbors));
                sect->vertex = malloc((m+1) * sizeof(*sect->vertex));

                for(n=0; n<m; ++n)
                {
                    sect->neightbors[n]= num[m+n];
                }
                for(n=0; n<m; ++n)
                {
                    sect->vertex[n]= vert[num[n]];
                }
                sect->vertex[0] = sect->vertex[m];
                free(num);
            break;
            case 'p':;
            float angle;
            sscanf(ptr += n, "%f %f %f %d", &v.x, &v.y, &angle, &n);
            player = (struct player)
            {
                {v.x, v.y, 0}, {0, 0, 0}, angle, 0, 0, 0, n
            };
            player.where.z = sectors[player.sector].floor + EyeHeight;
        }
    }
    fclose(fp);
    free(vert);
}

static void UnloadData ()
{
    for(unsigned a = 0; a<NumSectors; ++a)
    {
        free(sectors[a].vertex);
    }
    for(unsigned a = 0; a<NumSectors; ++a)
    {
        free(sectors[a].neightbors);
    }
    free(sectors);
    sectors = NULL;
    NumSectors = 0;
}

static void vLine(int x, int y1, int y2, int top, int middle, int bottom)
{
    int *pix = (int*) surface->pixels;
    y1 = clamp(y1, 0, H-1);
    y2 = clamp(y2, 0, H-1);

    if(y2 == y1)
    {
        pix[y1*W+x] = middle;
    } else if(y2 > y1)
    {
        pix[y1*W+x] = top;
        for(int y = y1 + 1; y<y2 ; ++y)
        {
            pix[y*W+x] = middle;
        }
        pix[y2*W+x] = bottom;
    }
}

static void movePlayer (float dx, float dy)
{
    float px = player.where.x, py = player.where.y;

    const struct sector* const sect = &sectors[player.sector];
    const struct vec2d* const vert = sect->vertex;

    for(unsigned s=0; s< sect->npoints; ++s)
    {
        if(sect->neightbors[s] >= 0
        && IntersectBox(px, py, px+dx, py+dy, vert[s+0].x, vert[s+0].y, vert[s+1].x, vert[s+1].y)
        && PointSide(px+dx, py+dy, vert[s+0].x, vert[s+0].y, vert[s+1].x, vert[s+1].y) < 0)
        {
            player.sector = sect->neightbors[s];
            break;
        }
    }
    player.where.x += dx;
    player.where.y += dy;
    player.anglesing = sinf(player.angle);
    player.anglecos = cosf(player.angle);
}

int main()
{
    surface = SDL_SetVideoMode(W,H,32,0);

    SDL_EnableKeyRepeat(150, 30);
    SDL_ShowCursor(SDL_DISABLE);

    for(;;)
    {
        SDL_LockSurface(surface);
        SDL_UnlockSurface(surface);
        SDL_Flip(surface);
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    switch(event.key.keysym.sym)
                    {
                        case 'q': goto done;
                    }
                break;
                case SDL_QUIT:
                    goto done;
            }
        }
    }
done:
    SDL_Quit();
    return 0;
}
