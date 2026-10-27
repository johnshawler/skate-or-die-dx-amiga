/* variable addresses of the downhill jam overlay (F21) */
#define N_BGSRC 0x6448
#define N_PW 0x6456     /* width in words of the sprite being drawn */
#define N_FILL 0x652E   /* the word the sprite mask is ANDed with */

#define N_JY_RES 0x651C
#define N_JY_CNT 0x0018
#define N_JY_SAVE 1

#define N_JGLYPH 1
#define N_BG_SCROLL 1    /* nat_bgfill reports the scroll to the platform (c2p.h, C2P_SCROLL) */
#define N_STEP_K 2       /* nat_step_pos: a skater advances this many rows each time the original advances it by one (the NES version's skating speed; the original itself moves up to 2 rows a pass) */
#define N_STEP_PCT 64    /* nat_step_pos: ... but at 64% of that (80% of 80%): the skaters' speed (coasting and top) is 36% below the first NES-speed setting (1.28 rows per step of the original) */
#define N_TILE 1         /* nat_tile: the scenery tile drawing at $483A */
#define N_CAM_CAP 7      /* nat_camera: the most rows the scroll moves in a pass (the original: 4; the skaters now move up to 6; 7 is the most the scroll routine at $48B3 has a step for) */
#define N_CAM_LEAD 40    /* nat_camera: the camera never lets the leading skater get further than this many rows ahead of it (the original follows the middle between the two skaters, so a skater at full speed runs ahead of the scroll) */
