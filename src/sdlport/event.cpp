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

#include "common.h"

#include "image.h"
#include "palette.h"
#include "video.h"
#include "mouse.h"
#include "event.h"
#include "timing.h"
#include "sprite.h"
#include "game.h"
#include "keys.h"

extern int get_key_binding( char const *dir, int i );
extern int mouse_xscale, mouse_yscale;
short mouse_buttons[5] = { 0, 0, 0, 0, 0 };

#ifdef __webos__
// Touch button definitions for webOS (1024x768 screen)
struct TouchButton {
    int x, y, w, h;
    int key;
    int pressed;
};

// Left side: Fire above movement arrows
// Right side: Spec/Weap stacked left of Aim stick, Jump below Aim
// Top left: Menu/Pause button
static TouchButton touch_buttons[] = {
    { 10,   10,  80,  40, JK_ESC,   0 },  // Menu/Pause - top left corner
    { 10,  448, 100, 100, JK_SPACE, 0 },  // Fire - above arrows (SPACE is default fire key)
    { 10,  558, 100, 100, JK_LEFT,   0 },  // Left
    { 120, 558, 100, 100, JK_RIGHT,  0 },  // Right
    { 230, 558, 100, 100, JK_DOWN,   0 },  // Crouch - right of arrows
    { 844, 458,  50,  50, JK_ALT_L,  0 },  // Special - stacked top
    { 844, 518,  50,  50, JK_INSERT, 0 },  // Weapon - stacked bottom
    { 894, 578, 100, 100, JK_UP,     0 },  // Jump - below aim stick
};

// Aim joystick parameters (circle, diameter 120)
#define AIM_STICK_RADIUS 60
static int aim_stick_active = 0;
static int aim_center_x = 954;  // Center of aim circle
static int aim_center_y = 508;

// Track aim offset from player (for crosshair positioning)
static int aim_offset_x = 100;  // Default aim right
static int aim_offset_y = 0;

// Track previous gameplay state to detect transitions
static int was_in_gameplay = 0;
// Debounce timer to ignore input briefly after entering gameplay
static Uint32 gameplay_start_time = 0;
#define GAMEPLAY_DEBOUNCE_MS 300
#define NUM_TOUCH_BUTTONS (sizeof(touch_buttons)/sizeof(touch_buttons[0]))

// Check if a point is inside a button
static int point_in_button(int x, int y, TouchButton *btn) {
    return (x >= btn->x && x < btn->x + btn->w &&
            y >= btn->y && y < btn->y + btn->h);
}

// Check if point is in ANY button or aim stick area
static int point_in_any_button(int x, int y) {
    for (unsigned int i = 0; i < NUM_TOUCH_BUTTONS; i++) {
        if (point_in_button(x, y, &touch_buttons[i])) {
            return 1;
        }
    }
    // Also check aim stick
    int dx = x - aim_center_x;
    int dy = y - aim_center_y;
    if ((dx*dx + dy*dy) <= (AIM_STICK_RADIUS * AIM_STICK_RADIUS)) {
        return 1;
    }
    return 0;
}

// Track which button is currently being held (for release regardless of position)
static int active_touch_button = -1;

// Get the key for a button, with dual-mode support for dialogs vs gameplay
// Button indices: 0=Menu, 1=Fire, 2=Left, 3=Right, 4=Crouch, 5=Special, 6=Weapon, 7=Jump
static int get_button_key(unsigned int button_index) {
    int in_gameplay = the_game && playing_state(the_game->state);

    if (in_gameplay) {
        // Gameplay mode - use normal keys
        return touch_buttons[button_index].key;
    } else {
        // Dialog/menu mode - remap certain buttons for navigation
        switch (button_index) {
            case 1:  // Fire -> Enter (confirm/select)
                return JK_ENTER;
            case 5:  // Special -> Up (navigate up)
                return JK_UP;
            case 6:  // Weapon -> Down (navigate down)
                return JK_DOWN;
            default:
                return touch_buttons[button_index].key;
        }
    }
}

// Handle touch/mouse for buttons, returns key if button hit, 0 otherwise
// Note: For multi-touch support, we only release a button if the release is ON that button
// This way, touching the aim stick won't release a held button
static int check_touch_buttons(int x, int y, int pressed) {
    if (pressed) {
        // On press, check if we hit a button
        for (unsigned int i = 0; i < NUM_TOUCH_BUTTONS; i++) {
            if (point_in_button(x, y, &touch_buttons[i])) {
                touch_buttons[i].pressed = 1;
                return get_button_key(i);
            }
        }
    } else {
        // On release, only release if we're releasing ON a button
        for (unsigned int i = 0; i < NUM_TOUCH_BUTTONS; i++) {
            if (point_in_button(x, y, &touch_buttons[i]) && touch_buttons[i].pressed) {
                touch_buttons[i].pressed = 0;
                return get_button_key(i);
            }
        }
    }
    return 0;
}

// Button colors: R, G, B for each button (matches touch_buttons order)
static float button_colors[][3] = {
    { 0.5f, 0.5f, 0.5f },  // Menu - Gray
    { 1.0f, 0.5f, 0.0f },  // Fire - Orange
    { 0.2f, 0.4f, 1.0f },  // Left - Blue
    { 0.2f, 0.4f, 1.0f },  // Right - Blue
    { 0.6f, 0.4f, 1.0f },  // Crouch - Purple
    { 1.0f, 0.3f, 0.3f },  // Special - Red
    { 1.0f, 0.9f, 0.2f },  // Weapon - Yellow
    { 0.2f, 1.0f, 0.4f },  // Jump - Green
};

// Check if point is in aim stick area
static int point_in_aim_stick(int x, int y) {
    int dx = x - aim_center_x;
    int dy = y - aim_center_y;
    return (dx*dx + dy*dy) <= (AIM_STICK_RADIUS * AIM_STICK_RADIUS);
}

// Get aim stick info for rendering
void get_aim_stick_info(int *cx, int *cy, int *radius, int *offset_x, int *offset_y) {
    *cx = aim_center_x;
    *cy = aim_center_y;
    *radius = AIM_STICK_RADIUS;
    *offset_x = aim_offset_x;
    *offset_y = aim_offset_y;
}

// Get touch button info for rendering (called from video.cpp)
void get_touch_button_rects(int *out_buttons, float *out_colors, int *out_count) {
    *out_count = NUM_TOUCH_BUTTONS;
    for (unsigned int i = 0; i < NUM_TOUCH_BUTTONS; i++) {
        out_buttons[i*4 + 0] = touch_buttons[i].x;
        out_buttons[i*4 + 1] = touch_buttons[i].y;
        out_buttons[i*4 + 2] = touch_buttons[i].w;
        out_buttons[i*4 + 3] = touch_buttons[i].h;
        out_colors[i*3 + 0] = button_colors[i][0];
        out_colors[i*3 + 1] = button_colors[i][1];
        out_colors[i*3 + 2] = button_colors[i][2];
    }
}

// Get state of all touch buttons for continuous input
static void get_touch_button_states(Uint8 *keystate) {
    for (unsigned int i = 0; i < NUM_TOUCH_BUTTONS; i++) {
        if (touch_buttons[i].pressed) {
            // Map our keys to SDL key indices
            int sdl_key = 0;
            switch(touch_buttons[i].key) {
                case JK_LEFT:   sdl_key = SDLK_LEFT; break;
                case JK_RIGHT:  sdl_key = SDLK_RIGHT; break;
                case JK_UP:     sdl_key = SDLK_UP; break;
                case JK_SPACE:  sdl_key = SDLK_SPACE; break;
                case JK_ALT_L:  sdl_key = SDLK_LALT; break;
                case JK_INSERT: sdl_key = SDLK_INSERT; break;
            }
            if (sdl_key) keystate[sdl_key] = 1;
        }
    }
}
#endif

// Pre-declarations
void handle_mouse( event &ev );

//
// Constructor
//
event_handler::event_handler( image *screen, palette *pal )
{
    CHECK( screen && pal );
    mouse = new JCMouse( screen, pal );
    mhere = mouse->exsist();
    last_keystat = get_key_flags();
    ewaiting = 0;

    // Ignore activate events
    SDL_EventState( SDL_ACTIVEEVENT, SDL_IGNORE );
}

//
// Destructor
//
event_handler::~event_handler()
{
    delete mouse;
}

//
// flush_screen()
// Redraw the screen
//
void event_handler::flush_screen()
{
    update_dirty( screen );
}

//
// get_key_flags()
// Return the flag for the key modifiers
//
int event_handler::get_key_flags()
{
    SDLMod key_flag;

    key_flag = SDL_GetModState();

    return ( ( key_flag & KMOD_SHIFT ) != 0 ) << 3 |
           ( ( key_flag & KMOD_CTRL ) != 0 ) << 2 |
           ( ( key_flag & KMOD_ALT ) != 0 ) << 1;
}

//
// event_waiting()
// Are there any events in the queue?
//
int event_handler::event_waiting()
{
    if( ewaiting )
    {
        return 1;
    }
    if( SDL_PollEvent( NULL ) )
    {
        ewaiting = 1;
    }
    return ewaiting;
}

//
// add_redraw()
// Add a redraw rectangle.
//
void event_handler::add_redraw( int X1, int Y1, int X2, int Y2, void *Start )
{
    event *ev;
    ev = new event;
    ev->type = EV_REDRAW;
    ev->redraw.x1 = X1;
    ev->redraw.x2 = X2;
    ev->redraw.y1 = Y1;
    ev->redraw.y2 = Y2;
    ev->redraw.start = Start;
    events.add_end(ev);
}

//
// get_event()
// Get and handle waiting events
//
void event_handler::get_event( event &ev )
{
    event *ep;
    while( !ewaiting )
    {
        event_waiting();

        if (!ewaiting)
        {
            // Sleep for 1 millisecond if there are no events
            Timer tmp; tmp.WaitMs(1);
        }
    }

    ep = (event *)events.first();
    if( ep )
    {
        ev = *ep;
        events.unlink(ep);
        delete ep;
        ewaiting = events.first() != NULL;
    }
    else
    {
        // NOTE : that the mouse status should be known
        // even if another event has occurred.
        ev.mouse_move.x = mouse->x();
        ev.mouse_move.y = mouse->y();
        ev.mouse_button = mouse->button();

        // Gather events
        SDL_Event event;
        if( SDL_PollEvent( &event ) )
        {
#ifdef __webos__
            // Handle touch input
            // In menus: allow normal touch/mouse
            // In gameplay: only buttons and aim stick work, no random screen touches
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION) {
                int in_gameplay = the_game && playing_state(the_game->state);

                // Detect transition from menu to gameplay - clear mouse buttons and start debounce
                if (in_gameplay && !was_in_gameplay) {
                    ev.mouse_button = 0;
                    for (int i = 0; i < 5; i++) mouse_buttons[i] = 0;
                    aim_stick_active = 0;
                    gameplay_start_time = SDL_GetTicks();
                    // Flush pending mouse events from clicking Start Game
                    SDL_Event flush;
                    while (SDL_PeepEvents(&flush, 1, SDL_GETEVENT, SDL_MOUSEEVENTMASK) > 0) {}
                }
                was_in_gameplay = in_gameplay;

                // During debounce period, ignore all mouse/touch input
                int in_debounce = 0;
                if (in_gameplay && gameplay_start_time > 0) {
                    if (SDL_GetTicks() - gameplay_start_time < GAMEPLAY_DEBOUNCE_MS) {
                        in_debounce = 1;  // Still in debounce - skip this event
                    } else {
                        gameplay_start_time = 0;  // Debounce period over
                    }
                }

                if (in_debounce) {
                    // Ignore input during debounce period
                } else if (!in_gameplay) {
                    // In menus - allow normal mouse/touch interaction
                    handle_mouse(ev);
                    mouse->update(ev.mouse_move.x, ev.mouse_move.y, ev.mouse_button);
                } else {
                    // In gameplay - only aim stick updates mouse position
                    // Clear mouse buttons to prevent touch from triggering fire via mouse path
                    ev.mouse_button = 0;

                    int mx = (event.type == SDL_MOUSEMOTION) ? event.motion.x : event.button.x;
                    int my = (event.type == SDL_MOUSEMOTION) ? event.motion.y : event.button.y;

                    int dx = mx - aim_center_x;
                    int dy = my - aim_center_y;
                    int in_stick = (dx*dx + dy*dy) <= (AIM_STICK_RADIUS * AIM_STICK_RADIUS);

                    if (in_stick && (event.type == SDL_MOUSEBUTTONDOWN ||
                        (event.type == SDL_MOUSEMOTION && aim_stick_active))) {
                        aim_stick_active = 1;
                        // Map joystick position to game screen coordinates
                        // dx/dy range: -60 to +60 (joystick radius)
                        // Map to full game screen range
                        int game_w = screen->Size().x;
                        int game_h = screen->Size().y;
                        // Joystick center = screen center, edges = screen edges
                        int aim_x = (game_w / 2) + (dx * game_w / (2 * AIM_STICK_RADIUS));
                        int aim_y = (game_h / 2) + (dy * game_h / (2 * AIM_STICK_RADIUS));
                        // Clamp to screen bounds
                        if (aim_x < 0) aim_x = 0;
                        if (aim_x >= game_w) aim_x = game_w - 1;
                        if (aim_y < 0) aim_y = 0;
                        if (aim_y >= game_h) aim_y = game_h - 1;
                        ev.mouse_move.x = aim_x;
                        ev.mouse_move.y = aim_y;
                        mouse->update(aim_x, aim_y, ev.mouse_button);
                    } else if (event.type == SDL_MOUSEBUTTONUP && aim_stick_active && in_stick) {
                        // Only deactivate aim stick if release is in the aim area
                        aim_stick_active = 0;
                    }
                    // In gameplay, block other touches from affecting mouse
                }
            }
#else
            // always sort the mouse out
            handle_mouse( ev );
            mouse->update( ev.mouse_move.x, ev.mouse_move.y, ev.mouse_button );
#endif

            switch( event.type )
            {
                case SDL_QUIT:
                {
                    exit(0);
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                {
#ifdef __webos__
                    // Check touch buttons
                    {
                        int touch_key = check_touch_buttons(event.button.x, event.button.y, 0);
                        if (touch_key) {
                            ev.key = touch_key;
                            ev.type = EV_KEYRELEASE;
                            ev.mouse_button = 0;  // Clear mouse button
                            break;
                        }
                    }
#endif
                    switch( event.button.button )
                    {
                        case 4:        // Mouse wheel goes up...
                        {
                            ev.key = get_key_binding( "b4", 0 );
                            ev.type = EV_KEYRELEASE;
                            break;
                        }
                        case 5:        // Mouse wheel goes down...
                        {
                            ev.key = get_key_binding( "b3", 0 );
                            ev.type = EV_KEYRELEASE;
                            break;
                        }
                    }
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                {
#ifdef __webos__
                    // Check touch buttons
                    {
                        int touch_key = check_touch_buttons(event.button.x, event.button.y, 1);
                        if (touch_key) {
                            ev.key = touch_key;
                            ev.mouse_button = 0;  // Clear mouse button so it doesn't trigger fire via mouse path
                            ev.type = EV_KEY;
                            break;
                        }
                    }
#endif
                    switch( event.button.button )
                    {
                        case 4:        // Mouse wheel goes up...
                        {
                            ev.key = get_key_binding( "b4", 0 );
                            ev.type = EV_KEY;
                            break;
                        }
                        case 5:        // Mouse wheel goes down...
                        {
                            ev.key = get_key_binding( "b3", 0 );
                            ev.type = EV_KEY;
                            break;
                        }
                    }
                    break;
                }
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    // Default to EV_SPURIOUS
                    ev.key = EV_SPURIOUS;
                    if( event.type == SDL_KEYDOWN )
                    {
                        ev.type = EV_KEY;
                    }
                    else
                    {
                        ev.type = EV_KEYRELEASE;
                    }
                    switch( event.key.keysym.sym )
                    {
                        case SDLK_DOWN:            ev.key = JK_DOWN; break;
                        case SDLK_UP:            ev.key = JK_UP; break;
                        case SDLK_LEFT:            ev.key = JK_LEFT; break;
                        case SDLK_RIGHT:        ev.key = JK_RIGHT; break;
                        case SDLK_LCTRL:        ev.key = JK_CTRL_L; break;
                        case SDLK_RCTRL:        ev.key = JK_CTRL_R; break;
                        case SDLK_LALT:            ev.key = JK_ALT_L; break;
                        case SDLK_RALT:            ev.key = JK_ALT_R; break;
                        case SDLK_LSHIFT:        ev.key = JK_SHIFT_L; break;
                        case SDLK_RSHIFT:        ev.key = JK_SHIFT_R; break;
                        case SDLK_NUMLOCK:        ev.key = JK_NUM_LOCK; break;
                        case SDLK_HOME:            ev.key = JK_HOME; break;
                        case SDLK_END:            ev.key = JK_END; break;
                        case SDLK_BACKSPACE:    ev.key = JK_BACKSPACE; break;
                        case SDLK_TAB:            ev.key = JK_TAB; break;
                        case SDLK_RETURN:        ev.key = JK_ENTER; break;
                        case SDLK_SPACE:        ev.key = JK_SPACE; break;
                        case SDLK_CAPSLOCK:        ev.key = JK_CAPS; break;
                        case SDLK_ESCAPE:        ev.key = JK_ESC; break;
                        case SDLK_F1:            ev.key = JK_F1; break;
                        case SDLK_F2:            ev.key = JK_F2; break;
                        case SDLK_F3:            ev.key = JK_F3; break;
                        case SDLK_F4:            ev.key = JK_F4; break;
                        case SDLK_F5:            ev.key = JK_F5; break;
                        case SDLK_F6:            ev.key = JK_F6; break;
                        case SDLK_F7:            ev.key = JK_F7; break;
                        case SDLK_F8:            ev.key = JK_F8; break;
                        case SDLK_F9:            ev.key = JK_F9; break;
                        case SDLK_F10:            ev.key = JK_F10; break;
                        case SDLK_INSERT:        ev.key = JK_INSERT; break;
                        case SDLK_KP0:            ev.key = JK_INSERT; break;
                        case SDLK_PAGEUP:        ev.key = JK_PAGEUP; break;
                        case SDLK_PAGEDOWN:        ev.key = JK_PAGEDOWN; break;
                        case SDLK_KP8:            ev.key = JK_UP; break;
                        case SDLK_KP2:            ev.key = JK_DOWN; break;
                        case SDLK_KP4:            ev.key = JK_LEFT; break;
                        case SDLK_KP6:            ev.key = JK_RIGHT; break;
                        case SDLK_F11:
                        {
                            // Only handle key down
                            if( ev.type == EV_KEY )
                            {
                                // Toggle fullscreen
                                SDL_WM_ToggleFullScreen( SDL_GetVideoSurface() );
                            }
                            ev.key = EV_SPURIOUS;
                            break;
                        }
                        case SDLK_F12:
                        {
                            // Only handle key down
                            if( ev.type == EV_KEY )
                            {
                                // Toggle grab mouse
                                if( SDL_WM_GrabInput( SDL_GRAB_QUERY ) == SDL_GRAB_ON )
                                {
                                    the_game->show_help( "Grab Mouse: OFF\n" );
                                    SDL_WM_GrabInput( SDL_GRAB_OFF );
                                }
                                else
                                {
                                    the_game->show_help( "Grab Mouse: ON\n" );
                                    SDL_WM_GrabInput( SDL_GRAB_ON );
                                }
                            }
                            ev.key = EV_SPURIOUS;
                            break;
                        }
                        case SDLK_PRINT:    // print-screen key
                        {
                            // Only handle key down
                            if( ev.type == EV_KEY )
                            {
                                // Grab a screenshot
                                SDL_SaveBMP( SDL_GetVideoSurface(), "screenshot.bmp" );
                                the_game->show_help( "Screenshot saved to: screenshot.bmp.\n" );
                            }
                            ev.key = EV_SPURIOUS;
                            break;
                        }
                        default:
                        {
                            ev.key = (int)event.key.keysym.sym;
                            // Need to handle the case of shift being pressed
                            // There has to be a better way
                            if( (event.key.keysym.mod & KMOD_SHIFT) != 0 )
                            {
                                if( event.key.keysym.sym >= SDLK_a &&
                                    event.key.keysym.sym <= SDLK_z )
                                {
                                    ev.key -= 32;
                                }
                                else if( event.key.keysym.sym >= SDLK_1 &&
                                         event.key.keysym.sym <= SDLK_5 )
                                {
                                    ev.key -= 16;
                                }
                                else
                                {
                                    switch( event.key.keysym.sym )
                                    {
                                        case SDLK_6:
                                            ev.key = SDLK_CARET; break;
                                        case SDLK_7:
                                        case SDLK_9:
                                        case SDLK_0:
                                            ev.key -= 17; break;
                                        case SDLK_8:
                                            ev.key = SDLK_ASTERISK; break;
                                        case SDLK_MINUS:
                                            ev.key = SDLK_UNDERSCORE; break;
                                        case SDLK_EQUALS:
                                            ev.key = SDLK_PLUS; break;
                                        case SDLK_COMMA:
                                            ev.key = SDLK_LESS; break;
                                        case SDLK_PERIOD:
                                            ev.key = SDLK_GREATER; break;
                                        case SDLK_SLASH:
                                            ev.key = SDLK_QUESTION; break;
                                        case SDLK_SEMICOLON:
                                            ev.key = SDLK_COLON; break;
                                        case SDLK_QUOTE:
                                            ev.key = SDLK_QUOTEDBL; break;
                                        default:
                                            break;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
        // No more events
        ewaiting = 0;
    }
}

//
// Handle mouse motion and button presses
// We don't handle the mousewheel here as
// SDL_GetMouseState doesn't seem to be
// able to detect that.
//
void handle_mouse( event &ev )
{
    Uint8 buttons;
    int x, y;

    // always sort the mouse out
    buttons = SDL_GetMouseState( &x, &y );
    x = (x << 16) / mouse_xscale;
    y = (y << 16) / mouse_yscale;
    if( x > screen->Size().x - 1 )
    {
        x = screen->Size().x - 1;
    }
    if( y > screen->Size().y - 1 )
    {
        y = screen->Size().y - 1;
    }
    ev.mouse_move.x = x;
    ev.mouse_move.y = y;
    ev.type = EV_MOUSE_MOVE;

    // Left button
    if( (buttons & SDL_BUTTON(1)) && !mouse_buttons[1] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[1] = !mouse_buttons[1];
        ev.mouse_button |= LEFT_BUTTON;
    }
    else if( !(buttons & SDL_BUTTON(1)) && mouse_buttons[1] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[1] = !mouse_buttons[1];
        ev.mouse_button &= ( 0xff - LEFT_BUTTON );
    }

    // Middle button
    if( (buttons & SDL_BUTTON(2)) && !mouse_buttons[2] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[2] = !mouse_buttons[2];
        ev.mouse_button |= LEFT_BUTTON;
        ev.mouse_button |= RIGHT_BUTTON;
    }
    else if( !(buttons & SDL_BUTTON(2)) && mouse_buttons[2] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[2] = !mouse_buttons[2];
        ev.mouse_button &= ( 0xff - LEFT_BUTTON );
        ev.mouse_button &= ( 0xff - RIGHT_BUTTON );
    }

    // Right button
    if( (buttons & SDL_BUTTON(3)) && !mouse_buttons[3] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[3] = !mouse_buttons[3];
        ev.mouse_button |= RIGHT_BUTTON;
    }
    else if( !(buttons & SDL_BUTTON(3)) && mouse_buttons[3] )
    {
        ev.type = EV_MOUSE_BUTTON;
        mouse_buttons[3] = !mouse_buttons[3];
        ev.mouse_button &= ( 0xff - RIGHT_BUTTON );
    }
}
