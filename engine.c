#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL/SDL.h>

#undef main
#define W 640
#define H 480

static SDL_Surface *surface = NULL;

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
