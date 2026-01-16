/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include <SDL.h>

#ifdef __webos__
#include <GLES/gl.h>
#include <cmath>
#include "PDL.h"
#define USE_GL 1
#elif defined(HAVE_OPENGL)
#   ifdef __APPLE__
#       include <OpenGL/gl.h>
#       include <OpenGL/glu.h>
#   else
#       include <GL/gl.h>
#       include <GL/glu.h>
#   endif    /* __APPLE__ */
#endif    /* HAVE_OPENGL */

#include "common.h"

#include "filter.h"
#include "video.h"
#include "image.h"
#include "setup.h"

SDL_Surface *window = NULL, *surface = NULL;
image *screen = NULL;
int win_xscale, win_yscale, mouse_xscale, mouse_yscale;
int xres, yres;

extern palette *lastl;
extern flags_struct flags;
#if defined(HAVE_OPENGL) || defined(USE_GL)
GLfloat texcoord[4];
GLuint texid;
SDL_Surface *texture = NULL;
#ifdef USE_GL
// GLES vertex arrays for rendering quad
static GLfloat gles_vertices[8];
static GLfloat gles_texcoords[8];
#endif
#endif

static void update_window_part(SDL_Rect *rect);

//
// power_of_two()
// Get the nearest power of two
//
static int power_of_two(int input)
{
    int value;
    for(value = 1 ; value < input ; value <<= 1);
    return value;
}

//
// set_mode()
// Set the video mode
//
void set_mode(int mode, int argc, char **argv)
{
    const SDL_VideoInfo *vidInfo;
    int vidFlags;

#ifdef __webos__
    // webOS: use OpenGL ES for hardware acceleration
    printf("Video : webOS GLES mode\n");
    flags.gl = 1;
    flags.doublebuf = 1;
    flags.fullscreen = 1;
    flags.xres = 1024;
    flags.yres = 768;

    // Request OpenGL ES 1.x context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    vidFlags = SDL_OPENGL | SDL_FULLSCREEN;
#else
    vidFlags = SDL_HWPALETTE;
    // Check for video capabilities
    vidInfo = SDL_GetVideoInfo();
    if(vidInfo->hw_available)
        vidFlags |= SDL_HWSURFACE;
    else
        vidFlags |= SDL_SWSURFACE;

    if(flags.fullscreen)
        vidFlags |= SDL_FULLSCREEN;

    if(flags.doublebuf)
        vidFlags |= SDL_DOUBLEBUF;

    // Try using opengl hw accell
    if(flags.gl) {
#ifdef HAVE_OPENGL
        printf("Video : OpenGL enabled\n");
        // allow doublebuffering in with gl too
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, flags.doublebuf);
        // set video gl capability
        vidFlags |= SDL_OPENGL;
#else
        // ignore the option if not available
        printf("Video : OpenGL disabled (Support missing in executable)\n");
        flags.gl = 0;
#endif
    }
#endif

    // Calculate the window scale
    win_xscale = mouse_xscale = (flags.xres << 16) / xres;
    win_yscale = mouse_yscale = (flags.yres << 16) / yres;

    // force no scaling when using GL, let the hw do it
    if (flags.gl) {
        win_xscale = win_yscale = 1 << 16;
    }

    // Set the icon for this window.  Looks nice on taskbars etc.
    SDL_WM_SetIcon(SDL_LoadBMP("abuse.bmp"), NULL);

    // Create the window
#ifdef __webos__
    window = SDL_SetVideoMode(flags.xres, flags.yres, 0, vidFlags);
#else
    // Create the window with a preference for 8-bit (palette animations!), but accept any depth */
    window = SDL_SetVideoMode(flags.xres, flags.yres, 8, vidFlags | SDL_ANYFORMAT);
#endif
    if(window == NULL)
    {
        printf("Video : Unable to set video mode : %s\n", SDL_GetError());
        exit(1);
    }

    // Create the screen image
    screen = new image(vec2i(xres, yres), NULL, 2);
    if(screen == NULL)
    {
        // Our screen image is no good, we have to bail.
        printf("Video : Unable to create screen image.\n");
        exit(1);
    }
    screen->clear();

    if (flags.gl)
    {
#if defined(HAVE_OPENGL) || defined(USE_GL)
        int w, h;

        // texture width/height should be power of 2
        w = power_of_two(xres);
        h = power_of_two(yres);

        // create texture surface
        texture = SDL_CreateRGBSurface(SDL_SWSURFACE, w , h , 32,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
#else
                0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
#endif

        // setup 2D gl environment
#ifdef USE_GL
        // GLES setup
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        glViewport(0, 0, window->w, window->h);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0.0f, (GLfloat)window->w, (GLfloat)window->h, 0.0f, 0.0f, 1.0f);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Setup vertex arrays for GLES
        gles_vertices[0] = 0.0f;           gles_vertices[1] = 0.0f;
        gles_vertices[2] = (GLfloat)window->w; gles_vertices[3] = 0.0f;
        gles_vertices[4] = 0.0f;           gles_vertices[5] = (GLfloat)window->h;
        gles_vertices[6] = (GLfloat)window->w; gles_vertices[7] = (GLfloat)window->h;

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#else
        // Desktop OpenGL setup
        glPushAttrib(GL_ENABLE_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        glViewport(0, 0, window->w, window->h);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)window->w, (GLdouble)window->h, 0.0, 0.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
#endif

        // texture coordinates
        texcoord[0] = 0.0f;
        texcoord[1] = 0.0f;
        texcoord[2] = (GLfloat)xres / texture->w;
        texcoord[3] = (GLfloat)yres / texture->h;

#ifdef USE_GL
        // GLES texture coords array
        gles_texcoords[0] = texcoord[0]; gles_texcoords[1] = texcoord[1];
        gles_texcoords[2] = texcoord[2]; gles_texcoords[3] = texcoord[1];
        gles_texcoords[4] = texcoord[0]; gles_texcoords[5] = texcoord[3];
        gles_texcoords[6] = texcoord[2]; gles_texcoords[7] = texcoord[3];
#endif

        // create an RGBA texture for the texture surface
        glGenTextures(1, &texid);
        glBindTexture(GL_TEXTURE_2D, texid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flags.antialias ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flags.antialias ? GL_LINEAR : GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->w, texture->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->pixels);
#endif
    }

    // Create our 8-bit surface
    surface = SDL_CreateRGBSurface(SDL_SWSURFACE, window->w, window->h, 8, 0xff, 0xff, 0xff, 0xff);
    if(surface == NULL)
    {
        // Our surface is no good, we have to bail.
        printf("Video : Unable to create 8-bit surface.\n");
        exit(1);
    }

    printf("Video : %dx%d %dbpp\n", window->w, window->h, window->format->BitsPerPixel);

    // Set the window caption
    SDL_WM_SetCaption("Abuse", "Abuse");

    // Grab and hide the mouse cursor
    SDL_ShowCursor(0);
    if(flags.grabmouse)
        SDL_WM_GrabInput(SDL_GRAB_ON);

    update_dirty(screen);
}

//
// close_graphics()
// Shutdown the video mode
//
void close_graphics()
{
    if(lastl)
        delete lastl;
    lastl = NULL;
    // Free our 8-bit surface
    if(surface)
        SDL_FreeSurface(surface);

#if defined(HAVE_OPENGL) || defined(USE_GL)
    if (texture)
        SDL_FreeSurface(texture);
#endif
    delete screen;
}

// put_part_image()
// Draw only dirty parts of the image
//
void put_part_image(image *im, int x, int y, int x1, int y1, int x2, int y2)
{
    int xe, ye;
    SDL_Rect srcrect, dstrect;
    int ii, jj;
    int srcx, srcy, xstep, ystep;
    Uint8 *dpixel;
    Uint16 dinset;

    if(y > yres || x > xres)
        return;

    CHECK(x1 >= 0 && x2 >= x1 && y1 >= 0 && y2 >= y1);

    // Adjust if we are trying to draw off the screen
    if(x < 0)
    {
        x1 += -x;
        x = 0;
    }
    srcrect.x = x1;
    if(x + (x2 - x1) >= xres)
        xe = xres - x + x1 - 1;
    else
        xe = x2;

    if(y < 0)
    {
        y1 += -y;
        y = 0;
    }
    srcrect.y = y1;
    if(y + (y2 - y1) >= yres)
        ye = yres - y + y1 - 1;
    else
        ye = y2;

    if(srcrect.x >= xe || srcrect.y >= ye)
        return;

    // Scale the image onto the surface
    srcrect.w = xe - srcrect.x;
    srcrect.h = ye - srcrect.y;
    dstrect.x = ((x * win_xscale) >> 16);
    dstrect.y = ((y * win_yscale) >> 16);
    dstrect.w = ((srcrect.w * win_xscale) >> 16);
    dstrect.h = ((srcrect.h * win_yscale) >> 16);

    xstep = (srcrect.w << 16) / dstrect.w;
    ystep = (srcrect.h << 16) / dstrect.h;

    srcy = ((srcrect.y) << 16);
    dinset = ((surface->w - dstrect.w)) * surface->format->BytesPerPixel;

    // Lock the surface if necessary
    if(SDL_MUSTLOCK(surface))
        SDL_LockSurface(surface);

    dpixel = (Uint8 *)surface->pixels;
    dpixel += (dstrect.x + ((dstrect.y) * surface->w)) * surface->format->BytesPerPixel;

    // Update surface part
    if ((win_xscale==1<<16) && (win_yscale==1<<16)) // no scaling or hw scaling
    {
        srcy = srcrect.y;
        dpixel = ((Uint8 *)surface->pixels) + y * surface->w + x ;
        for(ii=0 ; ii < srcrect.h; ii++)
        {
            memcpy(dpixel, im->scan_line(srcy) + srcrect.x , srcrect.w);
            dpixel += surface->w;
            srcy ++;
        }
    }
    else    // sw scaling
    {
        xstep = (srcrect.w << 16) / dstrect.w;
        ystep = (srcrect.h << 16) / dstrect.h;

        srcy = ((srcrect.y) << 16);
        dinset = ((surface->w - dstrect.w)) * surface->format->BytesPerPixel;

        dpixel = (Uint8 *)surface->pixels + (dstrect.x + ((dstrect.y) * surface->w)) * surface->format->BytesPerPixel;

        for(ii = 0; ii < dstrect.h; ii++)
        {
            srcx = (srcrect.x << 16);
            for(jj = 0; jj < dstrect.w; jj++)
            {
                memcpy(dpixel, im->scan_line((srcy >> 16)) + ((srcx >> 16) * surface->format->BytesPerPixel), surface->format->BytesPerPixel);
                dpixel += surface->format->BytesPerPixel;
                srcx += xstep;
            }
            dpixel += dinset;
            srcy += ystep;
        }
//        dpixel += dinset;
//        srcy += ystep;
    }

    // Unlock the surface if we locked it.
    if(SDL_MUSTLOCK(surface))
        SDL_UnlockSurface(surface);

    // Now blit the surface
    update_window_part(&dstrect);
}

//
// load()
// Set the palette
//
void palette::load()
{
    if(lastl)
        delete lastl;
    lastl = copy();

    // Force to only 256 colours.
    // Shouldn't be needed, but best to be safe.
    if(ncolors > 256)
        ncolors = 256;

    SDL_Color colors[ncolors];
    for(int ii = 0; ii < ncolors; ii++)
    {
        colors[ii].r = red(ii);
        colors[ii].g = green(ii);
        colors[ii].b = blue(ii);
    }
    SDL_SetColors(surface, colors, 0, ncolors);
    if(window->format->BitsPerPixel == 8)
        SDL_SetColors(window, colors, 0, ncolors);

    // Now redraw the surface
    update_window_part(NULL);
    update_window_done();
}

//
// load_nice()
//
void palette::load_nice()
{
    load();
}

// ---- support functions ----

void update_window_done()
{
#if defined(HAVE_OPENGL) || defined(USE_GL)
    // opengl blit complete surface to window
    if(flags.gl)
    {
        // convert color-indexed surface to RGB texture
        SDL_BlitSurface(surface, NULL, texture, NULL);

        // Texturemap complete texture to surface so we have free scaling
        // and antialiasing
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, 0, texture->w, texture->h,
                        GL_RGBA, GL_UNSIGNED_BYTE, texture->pixels);

#ifdef USE_GL
        // GLES rendering with vertex arrays
        glVertexPointer(2, GL_FLOAT, 0, gles_vertices);
        glTexCoordPointer(2, GL_FLOAT, 0, gles_texcoords);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

#ifdef __webos__
        // Draw touch buttons with color coding
        extern void get_touch_button_rects(int *out_buttons, float *out_colors, int *out_count);
        extern void get_aim_stick_info(int *cx, int *cy, int *radius, int *offset_x, int *offset_y);
        int btn_data[8*4];  // max 8 buttons, 4 ints each (x,y,w,h)
        float btn_colors[8*3];  // max 8 buttons, RGB
        int btn_count;
        get_touch_button_rects(btn_data, btn_colors, &btn_count);

        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Draw buttons
        for (int i = 0; i < btn_count; i++) {
            int bx = btn_data[i*4 + 0];
            int by = btn_data[i*4 + 1];
            int bw = btn_data[i*4 + 2];
            int bh = btn_data[i*4 + 3];

            // Semi-transparent colored button (40% opacity)
            glColor4f(btn_colors[i*3], btn_colors[i*3+1], btn_colors[i*3+2], 0.4f);

            GLfloat btn_verts[] = {
                (GLfloat)bx, (GLfloat)by,
                (GLfloat)(bx+bw), (GLfloat)by,
                (GLfloat)bx, (GLfloat)(by+bh),
                (GLfloat)(bx+bw), (GLfloat)(by+bh)
            };
            glVertexPointer(2, GL_FLOAT, 0, btn_verts);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        // Draw aim stick circle
        int aim_cx, aim_cy, aim_r, aim_ox, aim_oy;
        get_aim_stick_info(&aim_cx, &aim_cy, &aim_r, &aim_ox, &aim_oy);

        // Draw circle using triangle fan (32 segments)
        glColor4f(0.5f, 0.5f, 0.5f, 0.4f);  // Gray circle
        GLfloat circle_verts[34*2];  // center + 32 points + close
        circle_verts[0] = (GLfloat)aim_cx;
        circle_verts[1] = (GLfloat)aim_cy;
        for (int i = 0; i <= 32; i++) {
            float angle = (float)i * 6.28318f / 32.0f;
            circle_verts[(i+1)*2 + 0] = aim_cx + aim_r * cosf(angle);
            circle_verts[(i+1)*2 + 1] = aim_cy + aim_r * sinf(angle);
        }
        glVertexPointer(2, GL_FLOAT, 0, circle_verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 34);

        // Restore state
        glDisable(GL_BLEND);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
#else
        // Desktop OpenGL immediate mode
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(texcoord[0], texcoord[1]); glVertex3i(0, 0, 0);
        glTexCoord2f(texcoord[2], texcoord[1]); glVertex3i(window->w, 0, 0);
        glTexCoord2f(texcoord[0], texcoord[3]); glVertex3i(0, window->h, 0);
        glTexCoord2f(texcoord[2], texcoord[3]); glVertex3i(window->w, window->h, 0);
        glEnd();
#endif

        if(flags.doublebuf)
            SDL_GL_SwapBuffers();
    }
#else
    // swap buffers in case of double buffering
    // do nothing in case of single buffering
    if(flags.doublebuf)
        SDL_Flip(window);
#endif
}

static void update_window_part(SDL_Rect *rect)
{
    // no partial blit's in case of opengl
    // complete blit + scaling just before flip
    if (flags.gl)
        return;

    SDL_BlitSurface(surface, rect, window, rect);

    // no window update needed until end of run
    if(flags.doublebuf)
        return;

    // update window part for single buffer
    if(rect == NULL)
        SDL_UpdateRect(window, 0, 0, 0, 0);
    else
        SDL_UpdateRect(window, rect->x, rect->y, rect->w, rect->h);
}
