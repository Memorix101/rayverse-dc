
// Dreamcast platform layer: init, frame loop and Maple controller input.
// The game runs in keyboard mode (input_mode = 1) and consumes DOS scancodes through
// PC_keyboard_interrupt_handler(), so the controller is translated into scancode
// press/release events, exactly like the SDL backend does with the host keyboard.
//
// Button mapping:
//   D-pad / analog stick -> arrow keys (move)
//   A -> Ctrl (jump)   + Space (validate in menus)
//   X -> Alt (fist)
//   B -> X (action)
//   Y -> Space (validate)
//   Start -> Escape (in-game options menu / back)
//   L trigger -> Tab (cancel in menus)
//   R trigger -> Enter (start button)

s64 get_clock(void) {
    return (s64)timer_ns_gettime64();
}

float get_seconds_elapsed(s64 start, s64 end) {
    return ((float)(end - start)) / 1e9f;
}

void message_box(const char* message) {
    printf("Rayverse: %s\n", message);
}

typedef struct dc_button_mapping_t {
    u32 maple_mask;
    u8 scancodes[2]; // up to two scancodes per button, 0 = unused
} dc_button_mapping_t;

static const dc_button_mapping_t dc_button_mappings[] = {
    { CONT_DPAD_UP,    { SC_UP, 0 } },
    { CONT_DPAD_DOWN,  { SC_DOWN, 0 } },
    { CONT_DPAD_LEFT,  { SC_LEFT, 0 } },
    { CONT_DPAD_RIGHT, { SC_RIGHT, 0 } },
    { CONT_A,          { SC_CONTROL, SC_SPACE } },
    { CONT_X,          { SC_ALT, 0 } },
    { CONT_B,          { SC_X, 0 } },
    { CONT_Y,          { SC_SPACE, 0 } },
    { CONT_START,      { SC_ESCAPE, 0 } },
};

#define DC_ANALOG_DEADZONE 40
#define DC_TRIGGER_THRESHOLD 64

// Synthetic button bits for inputs that aren't plain CONT_* masks
#define DC_VBTN_LTRIG (1u << 24)
#define DC_VBTN_RTRIG (1u << 25)

static u32 dc_prev_buttons;

static void dc_send_key(u8 scancode, bool is_down) {
    if (!is_down) {
        scancode |= 0x80;
    }
    PC_keyboard_interrupt_handler(scancode);
}

static void dc_process_input(void) {
    u32 buttons = 0;

    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (cont) {
        cont_state_t* state = (cont_state_t*)maple_dev_status(cont);
        if (state) {
            buttons = state->buttons;

            // Fold the analog stick into the d-pad bits
            if (state->joyx < -DC_ANALOG_DEADZONE) buttons |= CONT_DPAD_LEFT;
            if (state->joyx > DC_ANALOG_DEADZONE)  buttons |= CONT_DPAD_RIGHT;
            if (state->joyy < -DC_ANALOG_DEADZONE) buttons |= CONT_DPAD_UP;
            if (state->joyy > DC_ANALOG_DEADZONE)  buttons |= CONT_DPAD_DOWN;

            if (state->ltrig > DC_TRIGGER_THRESHOLD) buttons |= DC_VBTN_LTRIG;
            if (state->rtrig > DC_TRIGGER_THRESHOLD) buttons |= DC_VBTN_RTRIG;
        }
    }

    u32 changed = buttons ^ dc_prev_buttons;

    for (u32 i = 0; i < COUNT(dc_button_mappings); ++i) {
        const dc_button_mapping_t* mapping = &dc_button_mappings[i];
        if (changed & mapping->maple_mask) {
            bool is_down = (buttons & mapping->maple_mask) != 0;
            for (u32 k = 0; k < COUNT(mapping->scancodes); ++k) {
                if (mapping->scancodes[k]) {
                    dc_send_key(mapping->scancodes[k], is_down);
                }
            }
        }
    }
    if (changed & DC_VBTN_LTRIG) {
        dc_send_key(SC_TAB, (buttons & DC_VBTN_LTRIG) != 0);
    }
    if (changed & DC_VBTN_RTRIG) {
        dc_send_key(SC_ENTER, (buttons & DC_VBTN_RTRIG) != 0);
    }

    dc_prev_buttons = buttons;
}

// Called from advance_frame() in engine.c once per game frame.
// Frame pacing comes from pvr_wait_ready() inside dc_render_frame(): the PVR renders at the
// video refresh rate (60 Hz on NTSC/VGA), which matches the game's 60 Hz timing.
void dc_advance_frame(app_state_t* app_state, u8* frame, rgb_palette_t* palette) {
    dc_process_input();
    dc_render_frame(frame, palette);
    dc_poll_sound();
    dc_music_update();
    if (!app_state->running) {
        exit(0);
    }
}

int main(int argc, char** argv) {
    app_state_t* app_state = &global_app_state;

    dc_init_video();
    dc_init_sound(app_state);
    dc_music_init();
    dc_vmu_init();

    app_state->target_game_hz = 60;
    app_state->target_seconds_per_frame = 1.0f / (float)app_state->target_game_hz;
    app_state->client_width = 640;
    app_state->client_height = 480;

    game_init_sound(&app_state->game.sound_buffer, 44100);

    if (!app_state->game.initialized) {
        game_init(&app_state->game);
    }

    app_state->running = true;
    return main_Ray(argc, argv); // run the game!
}
