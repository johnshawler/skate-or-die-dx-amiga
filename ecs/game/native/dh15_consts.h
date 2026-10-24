/* variable addresses of the downhill race overlay (F15) */
#define N_BGSRC 0x66CD
#define N_PW 0x66DB     /* width in words of the sprite being drawn */
#define N_FILL 0x67A3   /* the word every drawn word is ANDed with */

#define N_JY_RES 0x6791
#define N_JY_CNT 0x0018
#define N_JGLYPH 1
#define N_JG_COL 0x66F1
#define N_JG_ROW 0x66F3
#define N_JG_RTB B2
#define N_JG_RTO 0xFB00
#define N_JG_FB B2
#define N_JG_FO 0xE000
#define N_JG_MASK 0x599B
#define N_BG_SCROLL 1    /* nat_bgfill reports the scroll to the platform (c2p.h, C2P_SCROLL) */
