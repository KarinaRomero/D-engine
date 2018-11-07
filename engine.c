#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>

#undef main
#define W 640
#define H 480
/* Define various vision related constants*/
#define EyeHeight 6      // carema height from floor when standing
#define DuckHeight 2.5   // And when crouching
#define HeadMargin 1     // How much roo there is above camera before the head hits the celling
#define kneeHeight 2     // How tall obtacles the player can simply walk over withhout jumping
#define hfov (0.73f * H) // Affects the horizontal field of vision
#define vfov (.2f * H)   // Affects the vertical field of vision

static SDL_Surface *surface = NULL;
SDL_Window *gWindow = NULL;

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
    struct vec3d where, velocity;         // Current position
    float angle, anglesin, anglecos, yaw; // Current motion vector
    unsigned sector;
} player;

// Utility functions to calculate like a math library
#define min(a, b) (((a) < (b)) ? (a) : (b))                                                // Choose the smaller value
#define max(a, b) (((a) > (b)) ? (a) : (b))                                                // Choose the greater value
#define clamp(a, mi, ma) min(max(a, mi), ma)                                               // Clamp value into set range
#define vxs(x0, y0, x1, y1) ((x0) * (y1) - (x1) * (y0))                                    // Vector cross product
#define Overlap(a0, a1, b0, b1) (min(a0, a1) <= max(b0, b1) && min(b0, b1) <= max(a0, a1)) // overlap range
#define IntersectBox(x0, y0, x1, y1, x2, y2, x3, y3) (Overlap(x0, x1, x2, x3) && Overlap(y0, y1, y2, y3))
#define PointSide(px, py, x0, y0, x1, y1) vxs((x1) - (x0), (y1) - (y0), (px) - (x0), (py) - (y0))
#define Intersect(x1, y1, x2, y2, x3, y3, x4, y4) ((struct vec2d){                                                                     \
    vxs(vxs(x1, y1, x2, y2), (x1) - (x2), vxs(x3, y3, x4, y4), (x3) - (x4)) / vxs((x1) - (x2), (y1) - (y2), (x3) - (x4), (y3) - (y4)), \
    vxs(vxs(x1, y1, x2, y2), (y1) - (y2), vxs(x3, y3, x4, y4), (y3) - (y4)) / vxs((x1) - (x2), (y1) - (y2), (x3) - (x4), (y3) - (y4))})

static void LoadData()
{
    FILE *fp = fopen("m.txt", "rt");
    if (!fp)
    {
        perror("m.txt");
        exit(1);
    }
    char buf[256], word[256], *ptr;
    struct vec2d *vert = NULL, v;
    int n, m, NumVertices = 0;
    while (fgets(buf, sizeof buf, fp))
    {
        switch (sscanf(ptr = buf, "%32s%n", word, &n) == 1 ? word[0] : '\0')
        {
        case 'v':
            for (sscanf(ptr += n, "%f%n", &v.y, &n); sscanf(ptr += n, "%f%n", &v.x, &n) == 1;)
            {
                vert = realloc(vert, ++NumVertices * sizeof(*vert));
                vert[NumVertices - 1] = v;
            }
            break;
        case 's':
            sectors = realloc(sectors, ++NumSectors * sizeof(*sectors));
            struct sector *sect = &sectors[NumSectors - 1];
            int *num = NULL;
            sscanf(ptr += n, "%f%f%n", &sect->floor, &sect->ceil, &n);
            for (m = 0; sscanf(ptr += n, "%32s%n", word, &n) == 1 && word[0] != '#';)
            {
                num = realloc(num, ++m * sizeof(*num));
                num[m - 1] = word[0] == 'x' ? -1 : atoi(word);
            }
            sect->npoints = m /= 2;
            sect->neightbors = malloc((m) * sizeof(*sect->neightbors));
            sect->vertex = malloc((m + 1) * sizeof(*sect->vertex));

            for (n = 0; n < m; ++n)
            {
                sect->neightbors[n] = num[m + n];
            }
            for (n = 0; n < m; ++n)
            {
                sect->vertex[n+1] = vert[num[n]];
            }
            sect->vertex[0] = sect->vertex[m];
            free(num);
            break;
        case 'p':;
            float angle;
            sscanf(ptr += n, "%f %f %f %d", &v.x, &v.y, &angle, &n);
            player = (struct player){
                {v.x, v.y, 0}, {0, 0, 0}, angle, 0, 0, 0, n};
            player.where.z = sectors[player.sector].floor + EyeHeight;
        }
    }
    fclose(fp);
    free(vert);
}

static void UnloadData()
{
    for (unsigned a = 0; a < NumSectors; ++a)
    {
        free(sectors[a].vertex);
    }
    for (unsigned a = 0; a < NumSectors; ++a)
    {
        free(sectors[a].neightbors);
    }
    free(sectors);
    sectors = NULL;
    NumSectors = 0;
}

static void vline(int x, int y1, int y2, int top, int middle, int bottom)
{
    int *pix = (int *)surface->pixels;
    y1 = clamp(y1, 0, H - 1);
    y2 = clamp(y2, 0, H - 1);

    if (y2 == y1)
    {
        pix[y1 * W + x] = middle;
    }
    else if (y2 > y1)
    {
        pix[y1 * W + x] = top;
        for (int y = y1 + 1; y < y2; ++y)
        {
            pix[y * W + x] = middle;
        }
        pix[y2 * W + x] = bottom;
    }
}

static void movePlayer(float dx, float dy)
{
    float px = player.where.x, py = player.where.y;

    const struct sector *const sect = &sectors[player.sector];
    const struct vec2d *const vert = sect->vertex;

    for (unsigned s = 0; s < sect->npoints; ++s)
    {
        if (sect->neightbors[s] >= 0
		&& IntersectBox(px, py, px + dx, py + dy, vert[s + 0].x, vert[s + 0].y, vert[s + 1].x, vert[s + 1].y)
		&& PointSide(px + dx, py + dy, vert[s + 0].x, vert[s + 0].y, vert[s + 1].x, vert[s + 1].y) < 0)
        {
            player.sector = sect->neightbors[s];
            break;
        }
    }
    player.where.x += dx;
    player.where.y += dy;
    player.anglesin = sinf(player.angle);
    player.anglecos = cosf(player.angle);
}
static void DrawScreen()
{
    enum
    {
        MaxQueue = 32
    };
    struct item
    {
        int sectorno, sx1, sx2;
    };

    struct item queue[MaxQueue];
    struct item *head = queue;
    struct item *tail = queue;

    int ytop[W] = {0};
    int ybottom[W];
    int renderedsectors[NumSectors];

    for(unsigned x=0; x<W; ++x)
	{
		ybottom[x] = H-1;
	}
    for(unsigned n=0; n<NumSectors; ++n)
	{
		renderedsectors[n] = 0;
	}

    *head = (struct item){player.sector, 0, W - 1};
    if (++head == queue + MaxQueue)
    {
        head = queue;
    }

    do
    {
        const struct item now = *tail;

        if (++tail == queue + MaxQueue)
        {
            tail = queue;
        }
        if (renderedsectors[now.sectorno] & 0x21)
        {
            continue;
        }
        ++renderedsectors[now.sectorno];
        const struct sector *const sect = &sectors[now.sectorno];
        for (unsigned s = 0; s < sect->npoints; ++s)
        {
            float vx1 = sect->vertex[s + 0].x - player.where.x;
            float vy1 = sect->vertex[s + 0].y - player.where.y;
            float vx2 = sect->vertex[s + 1].x - player.where.x;
            float vy2 = sect->vertex[s + 1].y - player.where.y;

            float pcos = player.anglecos;
            float psin = player.anglesin;

            float tx1 = vx1 * psin - vy1 * pcos;
            float tz1 = vx1 * pcos + vy1 * psin;

            float tx2 = vx2 * psin - vy2 * pcos;
            float tz2 = vx2 * pcos + vy2 * psin;

			if(tz1 <= 0 && tz2 <= 0)
			{
				continue;
			}
            if (tz1 <= 0 || tz2 <= 0)
            {
                float nearz = 1e-4f;
                float farz = 5;
                float nearside = 1e-5f;
                float farside = 20.f;

                struct vec2d i1 = Intersect(tx1, tz1, tx2, tz2, -nearside, nearz, -farside, farz);
                struct vec2d i2 = Intersect(tx1, tz1, tx2, tz2, nearside, nearz, farside, farz);
                if (tz1 < nearz)
                {
                    if (i1.y > 0)
                    {
                        tx1 = i1.x;
                        tz1 = i1.y;
                    }
                    else
                    {
                        tx1 = i2.x;
                        tz1 = i2.y;
                    }
                }
                if (tz2 < nearz)
                {
                    if (i1.y > 0)
                    {
                        tx2 = i1.x;
                        tz2 = i1.y;
                    }
                    else
                    {
                        tx2 = i2.x;
                        tz2 = i2.y;
                    }
                }
            }
            float xscale1 = hfov / tz1;
            float yscale1 = vfov / tz1;

            int x1 = W / 2 - (int)(tx1 * xscale1);

            float xscale2 = hfov / tz2;
            float yscale2 = vfov / tz2;

            int x2 = W / 2 - (int)(tx2 * xscale2);

            if(x1 >= x2 || x2 < now.sx1 || x1 > now.sx2)
            {
                continue;
            }

            float yceil = sect->ceil - player.where.z;
            float yfloor = sect->floor - player.where.z;

            int neighbor = sect->neightbors[s];

            float nyceil = 0;
            float nyfloor = 0;

            if (neighbor >= 0)
            {
                nyceil = sectors[neighbor].ceil - player.where.z;
                nyfloor = sectors[neighbor].floor - player.where.z;
            }
#define Yaw(y, z) (y + z * player.yaw)
            int y1a = H / 2 - (int)(Yaw(yceil, tz1) * yscale1);
            int y1b = H / 2 - (int)(Yaw(yfloor, tz1) * yscale1);

            int y2a = H / 2 - (int)(Yaw(yceil, tz2) * yscale2);
            int y2b = H / 2 - (int)(Yaw(yfloor, tz2) * yscale2);

            int ny1a = H / 2 - (int)(Yaw(nyceil, tz1) * yscale1);
            int ny1b = H / 2 - (int)(Yaw(nyfloor, tz1) * yscale1);

            int ny2a = H / 2 - (int)(Yaw(nyceil, tz2) * yscale2);
            int ny2b = H / 2 - (int)(Yaw(nyfloor, tz2) * yscale2);

            int beginx = max(x1, now.sx1);
            int endx = min(x2, now.sx2);

            for (int x = beginx; x <= endx; ++x)
            {
                int z = ((x - x1) * (tz2 - tz1) / (x2 - x1) + tz1) * 8;

                int ya = (x - x1) * (y2a - y1a) / (x2 - x1) + y1a;
                int cya = clamp(ya, ytop[x], ybottom[x]);

                int yb = (x - x1) * (y2b - y1b) / (x2 - x1) + y1b;
                int cyb = clamp(yb, ytop[x], ybottom[x]);

                vline(x, ytop[x], cya - 1, 0x111111, 0x222222, 0x111111);
                vline(x, cyb + 1, ybottom[x], 0x0000FF, 0x0000AA, 0x0000FF);

                if (neighbor >= 0)
                {
                    int nya = (x - x1) * (ny2a - ny1a) / (x2 - x1) + ny1a;
                    int cnya = clamp(nya, ytop[x], ybottom[x]);

                    int nyb = (x - x1) * (ny2b - ny1b) / (x2 - x1) + ny1b;
                    int cnyb = clamp(nyb, ytop[x], ybottom[x]);

                    unsigned r1 = 0x010101 * (255 - z);
                    unsigned r2 = 0x040007 * (31 - z / 8);

                    vline(x, cya, cnya - 1, 0, x == x1 || x == x2 ? 0 : r1, 0);
                    ytop[x] = clamp(max(cya, cnya), ytop[x], H - 1);

                    vline(x, cnyb + 1, cyb, 0, x == x1 || x == x2 ? 0 : r1, 0);
                    ybottom[x] = clamp(min(cyb, cnyb), 0, ybottom[x]);
                }
                else
                {
                    unsigned r = 0x010101 * (255 - z);
                    vline(x, cya, cyb, 0, x == x1 || x == x2 ? 0 : r, 0);
                }
            }
            if (neighbor >= 0 && endx >= beginx && (head + MaxQueue + 1 - tail) % MaxQueue)
            {
                *head = (struct item){neighbor, beginx, endx};
                if (++head == queue + MaxQueue)
                {
                    head = queue;
                }
            }
        }
        ++renderedsectors[now.sectorno];
    } while (head != tail);
}

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
    }
    gWindow = SDL_CreateWindow("SDL Doom", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H, SDL_WINDOW_SHOWN);
    if (gWindow == NULL)
    {
        printf("Window could not be created! SDL Error: %s\n", SDL_GetError());
    }
    surface = SDL_GetWindowSurface(gWindow);
    LoadData();

    int wsad[4] = {0, 0, 0, 0};
    int ground = 0;
    int falling = 1;
    int moving = 0;
    int ducking = 0;
    float yaw = 0;
    for (;;)
    {
        float eyeheight = ducking ? DuckHeight : EyeHeight;
        ground = !falling;
        if (falling)
        {
            player.velocity.z -= 0.05f; // gravity
            float nextz = player.where.z + player.velocity.z;

            if (player.velocity.z < 0 && nextz < sectors[player.sector].floor + eyeheight)
            {
                player.where.z = sectors[player.sector].floor + eyeheight;
                player.velocity.z = 0;
                falling = 0;
                ground = 1;
            }
            else if (player.velocity.z > 0 && nextz > sectors[player.sector].ceil)
            {
                player.velocity.z = 0;
                falling = 1;
            }
            if (falling)
            {
                player.where.z += player.velocity.z;
                moving = 1;
            }
        }
        if (moving)
        {
            float px = player.where.x;
            float py = player.where.y;
            float dx = player.velocity.x;
            float dy = player.velocity.y;

            const struct sector *const sect = &sectors[player.sector];
            const struct vec2d *const vert = sect->vertex;

            for (unsigned s = 0; s < sect->npoints; ++s)
            {
                if (IntersectBox(px, py, px + dx, py + dy, vert[s + 0].x, vert[s + 0].y, vert[s + 1].x, vert[s + 1].y) && PointSide(px + dx, py + dy, vert[s + 0].x, vert[s + 0].y, vert[s + 1].x, vert[s + 1].y) < 0)
                {
                    float hole_low = sect->neightbors[s] < 0 ? 9e9 : max(sect->floor, sectors[sect->neightbors[s]].floor);
                    float hole_high = sect->neightbors[s] < 0 ? -9e9 : min(sect->ceil, sectors[sect->neightbors[s]].ceil);

                    if (hole_high < player.where.z + HeadMargin || hole_low > player.where.z - eyeheight + kneeHeight)
                    {
                        float xd = vert[s + 1].x - vert[s + 0].x;
                        float yd = vert[s + 1].y - vert[s + 0].y;
                        dx = xd * (dx * xd + yd * dy) / (xd * xd + yd * yd);
                        dy = yd * (dx * xd + yd * dy) / (xd * xd + yd * yd);
                        moving = 0;
                    }
                }
            }
            movePlayer(dx, dy);
            falling = 1;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                switch (event.key.keysym.sym)
                {
                case 'w':
                    wsad[0] = event.type == SDL_KEYDOWN;
                    break;
                case 's':
                    wsad[1] = event.type == SDL_KEYDOWN;
                    break;
                case 'a':
                    wsad[2] = event.type == SDL_KEYDOWN;
                    break;
                case 'd':
                    wsad[3] = event.type == SDL_KEYDOWN;
                    break;
                case 'q':
                    goto done;
                case ' ':
                    if (ground)
                    {
                        player.velocity.z += 0.5;
                        falling = 1;
                    }
                    break;
                case SDLK_LCTRL:
                case SDLK_RCTRL:
                    ducking = event.type == SDL_KEYDOWN;
                    falling = 1;
                    break;
                default:
                    break;
                }
                break;
            case SDL_QUIT:
                goto done;
            }
        }
            int x, y;
            SDL_GetRelativeMouseState(&x, &y);
            player.angle += x * 0.03f;
            yaw = clamp(yaw - y * 0.05f, -5, 5);
            player.yaw = yaw - player.velocity.z * 0.5f;
            movePlayer(0, 0);

            float move_vec[2] = {0.f, 0.f};
            if (wsad[0])
            {
                move_vec[0] += player.anglecos * 0.2f;
                move_vec[1] += player.anglesin * 0.2f;
            }
            if (wsad[1])
            {
                move_vec[0] -= player.anglecos * 0.2f;
                move_vec[1] -= player.anglesin * 0.2f;
            }
            if (wsad[2])
            {
                move_vec[0] += player.anglesin * 0.2f;
                move_vec[1] -= player.anglecos * 0.2f;
            }
            if (wsad[3])
            {
                move_vec[0] -= player.anglesin * 0.2f;
                move_vec[1] += player.anglecos * 0.2f;
            }
            int pushing = wsad[0] || wsad[1] || wsad[2] || wsad[3];
            float aceleration = pushing ? 0.4 : 0.2;

            player.velocity.x = player.velocity.x * (1 - aceleration) + move_vec[0] * aceleration;
            player.velocity.y = player.velocity.y * (1 - aceleration) + move_vec[1] * aceleration;

            if (pushing)
            {
                moving = 1;
            }

            SDL_LockSurface(surface);
            DrawScreen();
            SDL_UnlockSurface(surface);
            SDL_UpdateWindowSurface(gWindow);
            SDL_Delay(10);
    }
done:
    UnloadData();
    SDL_DestroyWindow( gWindow );
    gWindow = NULL;
    SDL_Quit();
    return 0;
}
