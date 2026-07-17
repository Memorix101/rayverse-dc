
// VMU save metadata.
// KOS's fs_vmu handles the VMS package layer transparently: reads skip a valid VMS header
// and return only the payload, and writes get a header attached on close. We register a
// default header (descriptions + icon) once at startup; after that, plain fopen/fwrite in
// the game's save code produces proper VMU files that show up with name and icon in the
// BIOS memory manager. The icon is generated from icon.bmp by tools/png2vmuicon.py.

#include "dc_vmu_icon.h"

void dc_vmu_init(void) {
    vmu_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    strcpy(pkg.desc_short, "Rayman");
    strcpy(pkg.desc_long, "Rayman save game");
    strcpy(pkg.app_id, "RAYVERSE");
    pkg.icon_cnt = 1;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    memcpy(pkg.icon_pal, dc_vmu_icon_pal, sizeof(pkg.icon_pal));
    pkg.icon_data = (uint8_t*)dc_vmu_icon_data;
    pkg.eyecatch_data = NULL;
    pkg.data = NULL;
    pkg.data_len = 0;

    if (fs_vmu_set_default_header(&pkg) < 0) {
        printf("VMU: couldn't set default save header\n");
    }
}
