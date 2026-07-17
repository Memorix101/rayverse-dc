
// Dreamcast video backend.
// The game renders into a 320x200 8-bit palettized buffer. Each frame we resolve the palette
// into RGB565 through a lookup table, upload the result into a 512x256 PVR texture and draw it
// as a single fullscreen quad. The 320x200 image is stretched to the full 640x480 output, which
// matches how VGA mode 13h was displayed on a 4:3 screen (non-square pixels).

#define DC_TEXTURE_WIDTH 512
#define DC_TEXTURE_HEIGHT 256

// Double-buffered: while the PVR still renders from one texture, we upload into the other.
static pvr_ptr_t dc_screen_texture[2];
static pvr_poly_hdr_t dc_screen_poly_hdr[2];
static u32 dc_screen_texture_index;

// Staging buffer in main RAM; rows already use the texture pitch so the whole
// visible region can be pushed to VRAM with a single pvr_txr_load().
static u16 __attribute__((aligned(32))) dc_frame_stage[DC_TEXTURE_WIDTH * SCREEN_HEIGHT];

static u16 dc_palette_lut[256];

void dc_init_video(void) {
    pvr_init_defaults();

    for (u32 i = 0; i < 2; ++i) {
        dc_screen_texture[i] = pvr_mem_malloc(DC_TEXTURE_WIDTH * DC_TEXTURE_HEIGHT * sizeof(u16));
        if (!dc_screen_texture[i]) {
            printf("Error: couldn't allocate PVR memory for the screen texture\n");
            fatal_error();
        }

        pvr_poly_cxt_t cxt;
        pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
                         PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                         DC_TEXTURE_WIDTH, DC_TEXTURE_HEIGHT,
                         dc_screen_texture[i], PVR_FILTER_BILINEAR);
        pvr_poly_compile(&dc_screen_poly_hdr[i], &cxt);
    }
}

static void dc_update_palette_lut(rgb_palette_t* palette) {
    for (s32 i = 0; i < 256; ++i) {
        rgb_t c = palette->colors[i];
        dc_palette_lut[i] = (u16)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
    }
}

void dc_render_frame(u8* frame, rgb_palette_t* palette) {
    dc_update_palette_lut(palette);

    for (s32 y = 0; y < SCREEN_HEIGHT; ++y) {
        u8* src = frame + y * SCREEN_WIDTH;
        u16* dest = dc_frame_stage + y * DC_TEXTURE_WIDTH;
        for (s32 x = 0; x < SCREEN_WIDTH; ++x) {
            dest[x] = dc_palette_lut[src[x]];
        }
    }

    dc_screen_texture_index ^= 1;
    pvr_txr_load(dc_frame_stage, dc_screen_texture[dc_screen_texture_index],
                 DC_TEXTURE_WIDTH * SCREEN_HEIGHT * sizeof(u16));

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_prim(&dc_screen_poly_hdr[dc_screen_texture_index], sizeof(dc_screen_poly_hdr[0]));

    float u_max = (float)SCREEN_WIDTH / (float)DC_TEXTURE_WIDTH;
    float v_max = (float)SCREEN_HEIGHT / (float)DC_TEXTURE_HEIGHT;
    float screen_w = 640.0f;
    float screen_h = 480.0f;

    pvr_vertex_t vert;
    vert.flags = PVR_CMD_VERTEX;
    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;
    vert.z = 1.0f;

    vert.x = 0.0f;     vert.y = 0.0f;     vert.u = 0.0f;  vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.x = screen_w; vert.y = 0.0f;     vert.u = u_max; vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.x = 0.0f;     vert.y = screen_h; vert.u = 0.0f;  vert.v = v_max;
    pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = screen_w; vert.y = screen_h; vert.u = u_max; vert.v = v_max;
    pvr_prim(&vert, sizeof(vert));

    pvr_list_finish();
    pvr_scene_finish();
}
