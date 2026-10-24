/* Skater movement on the overworld map. A C transcription of tools/overworld_model.py, which is a transcription of
 * state 0 of the original IIGS overlay F39 and was checked pass for pass against the original code running on a 65816
 * core (tools/cpu816/fuzz_model*.py). Names follow the original's variables: x=$3C (units of 2 px), y=$3E (px),
 * h=$40 (heading, 64 steps per turn, 0 = south), spd=$42, sub=$44, turn=$4A, tc=$4C, b8=$B8, fx=$BE, fy=$BC. */
typedef struct { int x, y, h, spd, sub, turn, tc, b8, fx, fy, exit_code, in_shop, compete_all; } Skater;

static const unsigned char SK_VX[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

static void sk_reset(Skater *s)
{
    s->x = 0x4B; s->y = 0x54; s->h = 0; s->spd = 0; s->sub = 0; s->turn = 0; s->tc = 0;
    s->b8 = 0; s->fx = 0; s->fy = 0; s->exit_code = 0; s->in_shop = 0; s->compete_all = 0;
}

static void sk_turn_right(Skater *s) { if (s->turn == -1) return; if (--s->tc == -1) { s->tc = 0; s->turn--; } }
static void sk_turn_left(Skater *s) { if (s->turn == 1) return; if (++s->tc == 1) { s->tc = 0; s->turn++; } }
static void sk_decay(Skater *s) { if (s->turn > 0) sk_turn_right(s); else if (s->turn < 0) sk_turn_left(s); }
static void sk_speed_up(Skater *s) { if (s->spd == 5) return; if (++s->sub >= 8) { s->spd++; s->sub = 0; } }
static void sk_speed_down(Skater *s) { if (s->spd == 0) return; if (--s->sub < 0) { s->spd--; s->sub = 8; } }

static void sk_x_plus(Skater *s)
{
    int x = s->x, y = s->y;
    if (x == 0x98) { s->exit_code = y >= 0x28 ? 2 : 1; return; }
    if (x == 0x25 && y < 0x6C) return;
    if (x == 0x4F && y < 0x6C) return;
    if (x == 0x29 && y >= 0x90) return;
    if (x == 0x7D && y >= 0x98) return;
    if (x == 0x81 && y >= 0x27 && y < 0x85) return;
    s->x++;
}
static void sk_x_minus(Skater *s)
{
    int x = s->x, y = s->y;
    if (x == 0) { s->exit_code = y >= 0x28 ? 5 : 1; s->compete_all = y < 0x28; return; }   /* $343B: the top of the west edge is the door of a competition in all events ($0768D0), unless it is practice */
    if (x == 0x47 && y < 0x6C) return;
    if (x == 0x71 && y < 0x6C) return;
    if (x == 0x6D && y >= 0x90) return;
    if (x == 0x19 && y >= 0x98) return;
    if (x == 0x16 && y >= 0x27 && y < 0x85) return;
    s->x--;
}
static void sk_y_plus(Skater *s)
{
    int x = s->x, y = s->y;
    if (y == 0xC0) { s->exit_code = x >= 0x2A ? 4 : 3; return; }
    if (y == 0x27 && x < 0x16) return;
    if (y == 0x27 && x >= 0x81) return;
    if (y == 0x98 && x < 0x19) return;
    if (y == 0x98 && x >= 0x7D) return;
    if (y == 0x90 && x >= 0x29 && x < 0x6D) return;
    s->y++;
}
static void sk_y_minus(Skater *s)
{
    int x = s->x, y = s->y;
    if (y == 0x15) return;
    if (y == 0x6C && x >= 0x25 && x < 0x47) return;
    if (y == 0x6C && x >= 0x4F && x < 0x71) return;
    if (y == 0x85 && x < 0x16) return;
    if (y == 0x85 && x >= 0x81) return;
    if (y < 0x53 && x >= 0x47 && x < 0x50) { s->in_shop = 1; return; }      /* the shop door */
    s->y--;
}

/* One pass of the original main loop. ctl: bit0 up (accelerate), bit1 down (brake), bit2 left, bit3 right; 0 = pressed. */
static void sk_step(Skater *s, int ctl)
{
    int n = ctl & 15, vx, vy, a, i;
    switch (n) {
    case 14: sk_decay(s); sk_speed_up(s); break;
    case 13: sk_decay(s); sk_speed_down(s); break;
    case 11: sk_turn_left(s); break;
    case 7: sk_turn_right(s); break;
    case 10: sk_speed_up(s); sk_turn_left(s); break;
    case 6: sk_speed_up(s); sk_turn_right(s); break;
    case 9: sk_speed_down(s); sk_turn_left(s); break;
    case 5: sk_speed_down(s); sk_turn_right(s); break;
    default: sk_decay(s); break;
    }
    s->b8 += s->turn;
    for (;;) {
        if (s->b8 < 0) { s->b8 += 2; if (s->h == -32) s->h = 32; s->h--; }
        else if (s->b8 >= 2) { s->b8 -= 2; if (s->h == 32) s->h = -32; s->h++; }
        else break;
    }
    a = s->h < 0 ? -s->h : s->h;
    vx = SK_VX[a]; vy = 16 - a;
    if (s->h < 0) vx = -vx;
    for (i = 0; i < s->spd; i++) {
        s->fx += vx; s->fy += vy;
        if (s->fx < 0) { if (s->fx < -47) { s->fx += 0x30; sk_x_minus(s); } }
        else if (s->fx >= 0x30) { s->fx -= 0x30; sk_x_plus(s); }
        if (s->fy < 0) { if (s->fy < -47) { s->fy += 0x30; sk_y_minus(s); if (s->in_shop) return; } }
        else if (s->fy >= 0x30) { s->fy -= 0x30; sk_y_plus(s); }
    }
}

/* sprite frames: torso, legs, board, shadow */
static void sk_frames(const Skater *s, int *f)
{
    int ae = (((s->h - 2) & 0xFFFF) >> 2 & 15) ^ 15;
    f[0] = (ae - s->turn) & 15; f[1] = 0x10 + ae; f[2] = 0x20 + ae; f[3] = 0x30 + ae;
}
