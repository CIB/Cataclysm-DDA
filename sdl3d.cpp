#include "SDL2/SDL.h"
#include "SDL2/SDL_ttf.h"
#include "game.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>


static SDL_Window *window;
static SDL_Renderer *renderer;

static SDL_Surface *image;
static GLuint texture;
static GLfloat best_anisotropy;

static TTF_Font *font;

static int videoFlags;
SDL_mutex *maplock;
static SDL_Thread* drawing_thread;

static std::map<nc_color, SDL_Color> color_cache;

// These are updated by the SDL loop in this thread.
static float playerYRotation = 0; // rotation around the Y axis set by the player
static float playerXRotation = 60; // rotation around the X axis set by the player

static float playerXTranslation = 0; 
static float playerYTranslation = 0;
static float playerZTranslation = 2;
static float playerZoomOut = 30;

bool playerIsRotating = false;
bool playerIsTranslating = false;

SDL_Color RGB(char r, char g,  char b) {
    SDL_Color rval;
    rval.r = r; rval.g = g; rval.b = b;
    return rval;
}

void init_color_cache() {
    color_cache[c_black] = RGB(0, 0, 0);
    color_cache[c_white] = RGB(255, 255, 255);
    color_cache[c_ltgray] = RGB(180, 180, 180);
    color_cache[c_dkgray] = RGB(100, 100, 100);
    color_cache[c_red] = RGB(180, 0, 0);
    color_cache[c_green] = RGB(0, 180, 0);
    color_cache[c_blue] = RGB(0, 0, 189);
    color_cache[c_cyan] = RGB(70, 210, 210);
    color_cache[c_magenta] = RGB(229, 0, 79);
    color_cache[c_brown] = RGB(120, 40, 40);
    color_cache[c_ltred] = RGB(255, 0, 0);
    color_cache[c_ltgreen] = RGB(0, 255, 0);
    color_cache[c_ltblue] = RGB(0, 0, 255);
    color_cache[c_ltcyan] = RGB(0, 250, 250);
    color_cache[c_blue] = RGB(0, 0, 189);
    color_cache[c_cyan] = RGB(70, 210, 210);
    color_cache[c_pink] = RGB(250, 100, 250);
    color_cache[c_yellow] = RGB(250, 250, 50);

    color_cache[h_black] = RGB(0, 0, 0);
    color_cache[h_white] = RGB(255, 255, 255);
    color_cache[h_ltgray] = RGB(180, 180, 180);
    color_cache[h_dkgray] = RGB(100, 100, 100);
    color_cache[h_red] = RGB(180, 0, 0);
    color_cache[h_green] = RGB(0, 180, 0);
    color_cache[h_blue] = RGB(0, 0, 189);
    color_cache[h_cyan] = RGB(70, 210, 210);
    color_cache[h_magenta] = RGB(229, 0, 79);
    color_cache[h_brown] = RGB(120, 40, 40);
    color_cache[h_ltred] = RGB(255, 0, 0);
    color_cache[h_ltgreen] = RGB(0, 255, 0);
    color_cache[h_ltblue] = RGB(0, 0, 255);
    color_cache[h_ltcyan] = RGB(0, 250, 250);
    color_cache[h_blue] = RGB(0, 0, 189);
    color_cache[h_cyan] = RGB(70, 210, 210);
    color_cache[h_pink] = RGB(250, 100, 250);
    color_cache[h_yellow] = RGB(250, 250, 50);

    color_cache[i_black] = RGB(0, 0, 0);
    color_cache[i_white] = RGB(255, 255, 255);
    color_cache[i_ltgray] = RGB(180, 180, 180);
    color_cache[i_dkgray] = RGB(100, 100, 100);
    color_cache[i_red] = RGB(180, 0, 0);
    color_cache[i_green] = RGB(0, 180, 0);
    color_cache[i_blue] = RGB(0, 0, 189);
    color_cache[i_cyan] = RGB(70, 210, 210);
    color_cache[i_magenta] = RGB(229, 0, 79);
    color_cache[i_brown] = RGB(120, 40, 40);
    color_cache[i_ltred] = RGB(255, 0, 0);
    color_cache[i_ltgreen] = RGB(0, 255, 0);
    color_cache[i_ltblue] = RGB(0, 0, 255);
    color_cache[i_ltcyan] = RGB(0, 250, 250);
    color_cache[i_blue] = RGB(0, 0, 189);
    color_cache[i_cyan] = RGB(70, 210, 210);
    color_cache[i_pink] = RGB(250, 100, 250);
    color_cache[i_yellow] = RGB(250, 250, 50);

}

struct map_tile {
    long sym;
    nc_color col;
    int flags;
};

#define SEEX_3D 200
#define SEEY_3D 200
map_tile map_cache[SEEX_3D][SEEY_3D];

// Map symbols to textures.
static std::map<long, GLuint> texture_cache;

static int player_pos_x;
static int player_pos_y;
static bool is_driving;
static int turn_dir;

// This function will be called from the main thread, not the drawing thread.
void game::begin_3d_rendering() {
    SDL_mutexP(maplock);
    clear_map_cache();

    // Set rotation based on movement.
    /*int dx = g->u.posx - player_pos_x;
    int dy = g->u.posy - player_pos_y;
    if(abs(dx) == 1 || abs(dy) == 1) {
        double theta = atan2(dx, -dy);
        playerYRotation = theta * 180 / 3.14;
        playerXTranslation = -0.5f;
        playerYTranslation = -0.5f;
        playerZTranslation = 2;
        playerXRotation = 5;
        playerZoomOut = 2;
    }*/

    // Read and store some relevant data like player position etc. so that
    // this doesn't de-sync while rendering.
    player_pos_x = g->u.posx; player_pos_y = g->u.posy;
    is_driving = false;
    if(g->u.controlling_vehicle) {
         int veh_part = 0;
         vehicle *veh = g->m.veh_at(g->u.posx, g->u.posy, veh_part);
         if (veh) {
             is_driving = true;
             turn_dir = veh->turn_dir;
         }
    }
}

// This function will be called from the main thread, not the drawing thread.
void game::stop_3d_rendering() {
    SDL_mutexV(maplock);
}

void game::clear_map_cache() {
    for(int i=0; i<SEEX_3D; i++) {
        for(int j=0; j<SEEY_3D; j++) {
            map_cache[i][j].sym = 0;
        }
    }
}

void game::update_map_cache(int x, int y, long sym, nc_color col, int flags) {
    map_tile* tile = &map_cache[x][y];
    tile->sym = sym;
    tile->col = col;
    tile->flags = flags;
}

void initGL() {

    //glShadeModel( GL_SMOOTH );
    //glDisable(GL_LIGHTING);

    glClearColor(0, 0, 0, 0);

    glClearDepth( 1.0f );

    glEnable( GL_DEPTH_TEST );
    glEnable(GL_TEXTURE_2D);

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    glDepthFunc( GL_LEQUAL );

    glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );

    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &best_anisotropy);
}

void drawScene() {
    // Shouldn't draw while the map is being built.
    SDL_mutexP(maplock);

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();



    /*//gluLookAt(80,  10,  100,  80,  0,  80,  0,  1,  0);
    gluLookAt(g->u.posx,  20,  g->u.posy + 15,  g->u.posx,  0,  g->u.posy - 10,  0,  1,  0);
    */

    // If we're controlling a vehicle, try to face the same way as the vehicle.
    float yTranslation = playerZoomOut;
    float zTranslation = playerZTranslation;
    float yRotation = playerYRotation;
    float xRotation = playerXRotation;

    float xTranslationOffset = playerXTranslation;
    float yTranslationOffset = playerYTranslation;

    if(is_driving) {
         // Rotate the view by the vehicle's rotation
        //glRotatef(veh->turn_dir, 0.0f, 1.0f, 0.0f);
        yRotation = 90 + turn_dir;
        yTranslation = 8;
        zTranslation = 4;
        xRotation = 20;
        xTranslationOffset = 0;
        yTranslationOffset = 0;
    }

    glTranslatef(0, 0, -yTranslation);
    glRotatef(xRotation, 1, 0, 0);
    glRotatef(yRotation, 0.0f, 1.0f, 0.0f);
    glTranslatef(-player_pos_x + xTranslationOffset,  -zTranslation,  -player_pos_y + yTranslationOffset);

    for(int x=0; x<SEEX_3D; x++) {
        for(int y=0; y<SEEY_3D; y++) {
            map_tile *tile = &map_cache[x][y];
            bool is_player = x == player_pos_x && y == player_pos_y;
            if(tile->sym || is_player) {
                long sym = tile->sym;
                nc_color col = tile->col;
                if(is_player) {
                    sym = L'a';
                    col = c_red;
                }

                bool use_texture = !(tile->flags & (1<<1));

                if(use_texture) {
                    // Load texture
                    if(!texture_cache.count(sym)) {
                        char bytes[4]; memcpy(bytes, &tile->sym, 4);
                        std::string glyph(bytes, 4);
                        SDL_Color color = {255, 255, 255, 0}; SDL_Color bg_color = {0, 0, 0, 0};
                        image = TTF_RenderUTF8_Shaded(font, glyph.c_str(), color, bg_color);
                        glGenTextures(1, &texture);
                        glBindTexture(GL_TEXTURE_2D, texture);

                        if(!image) continue;
                        // The character surface might be in a weird format, convert it.
                        SDL_Surface* intermediary = SDL_CreateRGBSurface(0, image->w, image->h, 32,
                                0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);

                        SDL_BlitSurface(image, 0, intermediary, 0);

                        glGenTextures(1, &texture);
                        glBindTexture(GL_TEXTURE_2D, texture);
                        glTexImage2D(GL_TEXTURE_2D, 0, 4, intermediary->w, intermediary->h, 0,
                                        GL_RGBA, GL_UNSIGNED_BYTE, intermediary->pixels);
                        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, best_anisotropy);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                        texture_cache[tile->sym] = texture;

                        SDL_FreeSurface(image);
                        SDL_FreeSurface(intermediary);
                    }

                    glBindTexture(GL_TEXTURE_2D, texture_cache[sym]);
                } else {
                    glBindTexture(GL_TEXTURE_2D, 0);
                }

                float draw_x = x;
                float draw_y = y;

                SDL_Color color;
                if(color_cache.count(col)) {
                     color = color_cache[col];
                }

                glColor3f(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f);

                float height = 1.0f;
                if(tile->flags & (1<<2) && !is_player) {
                    height = 0.3f;
                }
                else if(tile->flags & (1<<3) || is_player) {
                    height = 0.8f;
                }
                if(tile->flags & 1 || tile->flags & (1<<2) || tile->flags & (1<<3) || is_player) {
                /*== FILL ==*/
                bool draw_on_sides = false;
                if(height >= 0.8f) draw_on_sides = true;
                glTranslatef(0, -0.05f, -0.05f);
                glBegin(GL_TRIANGLES);

                    // Front face
                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0, draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(0, 1);
                    glVertex3f(draw_x+0.0f, height,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);


                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(1, 0);
                    glVertex3f( draw_x+1.0f, 0.0f,  draw_y+1.0f);

                    // Back face
                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0, draw_y);
                    if(draw_on_sides) glTexCoord2f(0, 1);
                    glVertex3f(draw_x+0.0f, height,  draw_y);
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y);


                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y);
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y);
                    if(draw_on_sides) glTexCoord2f(1, 0);
                    glVertex3f( draw_x+1.0f, 0.0f,  draw_y);


                    // Top face
                    glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, height, draw_y+0.0f);
                    glTexCoord2f(0, 1);
                    glVertex3f(draw_x+0.0f, height,  draw_y+1.0f);
                    glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);


                    glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, height, draw_y+0.0f);
                    glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);
                    glTexCoord2f(1, 0);
                    glVertex3f( draw_x+1.0f, height,  draw_y+0.0f);

                    // Left face
                    if(draw_on_sides) glTexCoord2f(0, 1);
                    glVertex3f(draw_x, 0.0f, draw_y+0.0f);
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f(draw_x, 0.0f,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(1, 0);
                    glVertex3f( draw_x, height,  draw_y+1.0f);


                    if(draw_on_sides) glTexCoord2f(0, 1);
                    glVertex3f(draw_x, 0.0f, draw_y+0.0f);
                    if(draw_on_sides) glTexCoord2f(1, 0);
                    glVertex3f( draw_x, height,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f( draw_x, height,  draw_y+0.0f);

                    // Right face
                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f(draw_x+1.0f, 0.0f, draw_y+0.0f);
                    if(draw_on_sides) glTexCoord2f(0, 1);
                    glVertex3f(draw_x+1.0f, 0.0f,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);


                    if(draw_on_sides) glTexCoord2f(1, 1);
                    glVertex3f(draw_x+1.0f, 0.0f, draw_y+0.0f);
                    if(draw_on_sides) glTexCoord2f(0, 0);
                    glVertex3f( draw_x+1.0f, height,  draw_y+1.0f);
                    if(draw_on_sides) glTexCoord2f(1, 0);
                    glVertex3f( draw_x+1.0f, height,  draw_y+0.0f);

                glEnd();
                glTranslatef(0, 0.05f, 0.05f);

                /*== OUTLINES ==*/
                glColor3f(0, 0, 0);
                if((color.r <= 50 && color.g <= 50 && color.b <= 50) || (use_texture && !is_player)) {
                    // For dark cubes use bright outlines.
                    glColor3f(255, 255, 25);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glBegin(GL_LINES);
                    // Front face top
                    glVertex3f(draw_x+0.0f, height, draw_y);
                    glVertex3f(draw_x+1.0f, height,  draw_y);

                    // Front face bottom
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y);
                    glVertex3f(draw_x+1.0f, 0.0f,  draw_y);

                    // Front face left
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y);
                    glVertex3f(draw_x+0.0f, height,  draw_y);

                    // Front face right
                    glVertex3f(draw_x+1.0f, 0.0f, draw_y);
                    glVertex3f(draw_x+1.0f, height,  draw_y);

                    // Back face top
                    glVertex3f(draw_x+0.0f, height, draw_y+1.0);
                    glVertex3f(draw_x+1.0f, height,  draw_y+1.0);

                    // Back face bottom
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y+1.0);
                    glVertex3f(draw_x+1.0f, 0.0f,  draw_y+1.0);

                    // Back face left
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y+1.0);
                    glVertex3f(draw_x+0.0f, height,  draw_y+1.0);

                    // Back face right
                    glVertex3f(draw_x+1.0f, 0.0f, draw_y+1.0);
                    glVertex3f(draw_x+1.0f, height,  draw_y+1.0);

                    // Left face top
                    glVertex3f(draw_x+0.0f, height, draw_y);
                    glVertex3f(draw_x+0.0f, height,  draw_y+1.0);

                    // Left face bottom
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y);
                    glVertex3f(draw_x+0.0f, 0.0f,  draw_y+1.0);

                    // Right face top
                    glVertex3f(draw_x+1.0f, height, draw_y);
                    glVertex3f(draw_x+1.0f, height,  draw_y+1.0);

                    // Right face bottom
                    glVertex3f(draw_x+1.0f, 0.0f, draw_y);
                    glVertex3f(draw_x+1.0f, 0.0f,  draw_y+1.0);
                glEnd();

                } else {
                glBegin(GL_TRIANGLES);
                    glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y+0.0f);
                    glTexCoord2f(0, 1);
                    glVertex3f(draw_x+0.0f, 0.0f,  draw_y+1.0f);
                    glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, 0.0f,  draw_y+1.0f);


                    glTexCoord2f(0, 0);
                    glVertex3f(draw_x+0.0f, 0.0f, draw_y+0.0f);
                    glTexCoord2f(1, 1);
                    glVertex3f( draw_x+1.0f, 0.0f,  draw_y+1.0f);
                    glTexCoord2f(1, 0);
                    glVertex3f( draw_x+1.0f, 0.0f,  draw_y+0.0f);
                glEnd();
                }
            }
        }
    }

    glFlush();
    SDL_GL_SwapWindow(window);

    SDL_mutexV(maplock);
}

int resizeWindow(int width, int height)
{
    GLfloat ratio;

    if(height == 0) {
        return 0;
    }

    ratio = (GLfloat)width / (GLfloat)height;

    glViewport( 0, 0, (GLint)width, (GLint)height );

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity( );

    gluPerspective( 45.0f, ratio, 0.1f, 1000.0f );

    glMatrixMode( GL_MODELVIEW );

    glLoadIdentity( );

    return 1;
}

int tick_3d();

int sdl_thread(void* data) {
    SDL_Init( SDL_INIT_EVERYTHING );

    init_color_cache();

    TTF_Init();
    font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSansMono.ttf", 64);

    static int width = 400;
    static int height = 400;

    videoFlags  = SDL_WINDOW_OPENGL;
    videoFlags |= SDL_WINDOW_RESIZABLE;

    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

    SDL_CreateWindowAndRenderer(width, height, videoFlags, &window, &renderer);

    initGL();

    resizeWindow(width, height);

    while(tick_3d()) {
        SDL_Delay(30);
    }

    return 0;
}

void game::init_3d() {
    maplock = SDL_CreateMutex();
    drawing_thread = SDL_CreateThread(&sdl_thread, "3d_drawing_thread", NULL);
}

int tick_3d() {
    SDL_Event event;
    while ( SDL_PollEvent( &event ) )
    {
        switch( event.type )
        {
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                resizeWindow( event.window.data1, event.window.data2 );
            break;
            }
        break;
        case SDL_MOUSEBUTTONDOWN:
            if(event.button.button == SDL_BUTTON_RIGHT) {
                playerIsRotating = true;
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }
            else if(event.button.button == SDL_BUTTON_LEFT) {
                playerIsTranslating = true;
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }
        break;
        case SDL_MOUSEBUTTONUP:
            if(event.button.button == SDL_BUTTON_RIGHT) {
                playerIsRotating = false;
                SDL_ShowCursor(true);
                SDL_SetWindowGrab(window, SDL_FALSE);
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }
            else if(event.button.button == SDL_BUTTON_LEFT) {
                playerIsTranslating = false;
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }
        break;
        case SDL_MOUSEMOTION:
            if(playerIsRotating) {
                playerYRotation += (float)event.motion.xrel / 5;
                playerXRotation += (float)event.motion.yrel / 5;
            } else if(playerIsTranslating) {
                int dx = event.motion.xrel;
                int dy = event.motion.yrel;

                // Try to adjust the movement by our current yRotation.
                static const float PI = 3.14159;
                float theta = playerYRotation * PI / 180;

                float cs = cos(theta);
                float sn = sin(theta);

                float newx = dx * cs - dy * sn;
                float newy = dx * sn + dy * cs;
                
                playerXTranslation -= newx / 5;
                playerYTranslation -= newy / 5;
            }
        break;
        case SDL_MOUSEWHEEL:
            playerZoomOut -= event.wheel.y * 5;
        break;
        default:
            break;
        }
    }

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    if ( state[SDL_SCANCODE_PAGEDOWN] ) {
        playerZTranslation += 0.4f;
    } else if( state[SDL_SCANCODE_PAGEUP] ) {
        playerZTranslation -= 0.4f;
    }

    float dx = 0;
    float dy = 0;
    if( state[SDL_SCANCODE_A] ) {
        dx += 1.5f;
    } else if(state[SDL_SCANCODE_D]) {
        dx -= 1.5f;
    }
    if( state[SDL_SCANCODE_W] ) {
        dy += 1.5f;
    } else if(state[SDL_SCANCODE_S]) {
        dy -= 1.5f;
    }

    if(dx || dy) {
        // Try to adjust the movement by our current yRotation.
        static const float PI = 3.14159;
        float theta = playerYRotation * PI / 180;

        float cs = cos(theta);
        float sn = sin(theta);

        float newx = dx * cs - dy * sn;
        float newy = dx * sn + dy * cs;
        
        playerXTranslation += newx;
        playerYTranslation += newy;
    }

    drawScene();

    return 1;
}
