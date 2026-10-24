/* Device model for the IIGS soft switches the games read ($E0:C0xx / $E1:C0xx), shared by the oracle and the translated code so both see
 * exactly the same machine. Everything that is not listed here is plain memory.
 *  keyboard   $C000 (key | 0x80 while a key waits), $C010 (read: clear the strobe, returns key | any-key-down)
 *  modifiers  $C025 (bit0 = open apple), $C027 (keyboard/mouse GLU status), $C024 (mouse data, not modelled: reads 0x80)
 *  buttons    $C061 (button 0, bit 7), $C062 (button 1), $C063 (button 2)
 *  paddles    $C070 (read: trigger the timers), $C064/$C065 (paddle 0/1: bit 7 stays set for pdl_len reads after the trigger)
 * The platform fills `io` from the real joystick, keys and mouse. */
#ifndef IIGS_IO_H
#define IIGS_IO_H
#include <stdint.h>

typedef struct IIGSIO {
    uint8_t key;            /* ASCII of the last key with bit 7 clear */
    uint8_t key_waiting;    /* strobe */
    uint8_t anykey;         /* $C010 bit 7 */
    uint8_t mods;           /* $C025 */
    uint8_t enabled;        /* 0: the soft switches are plain memory (the shop tests rely on that) */
    uint8_t btn[3];         /* bit 7 set = pressed */
    uint16_t pdl_len[4];    /* number of reads the paddle timer stays high after a trigger */
    uint16_t pdl_left[4];
    uint8_t ramrd, ramwrt;  /* $C002-$C005: reads / writes of bank 0 $0200-$BFFF go to bank 1 (the auxiliary memory) */
    uint8_t shr;            /* $C029 bit 7: the super-hires screen is shown (clear: the 40-column text page at $E0:0400, INTER's own screens) */
} IIGSIO;

extern IIGSIO io;
void io_poll(void);      /* called when the game polls the keyboard and nothing is waiting: the platform pumps its events, the tests may supply a key */
static inline int io_is_reg(uint32_t a) { return io.enabled && (a & 0xFEFF00) == 0xE0C000; }

static inline uint8_t io_rd8(uint32_t a, uint8_t backing)
{
    if ((a & 0xF0) == 0x00) {            /* $C000-$C00F all read the keyboard register */
        if (!io.key_waiting) io_poll();
        return (uint8_t)(io.key | (io.key_waiting ? 0x80 : 0));
    }
    switch (a & 0xFF) {
    case 0x10: { if (!io.key_waiting) io_poll(); uint8_t r = (uint8_t)(io.key | (io.anykey ? 0x80 : 0)); io.key_waiting = 0; return r; }
    case 0x24: return 0x80;
    case 0x25: return io.mods;
    case 0x27: return 0;
    case 0x61: return io.btn[0];
    case 0x62: return io.btn[1];
    case 0x63: return io.btn[2];
    case 0x64: case 0x65: case 0x66: case 0x67: {
        int p = (a & 3);
        if (io.pdl_left[p]) { io.pdl_left[p]--; return 0x80; }
        return 0;
    }
    case 0x70: { int p; for (p = 0; p < 4; p++) io.pdl_left[p] = io.pdl_len[p]; return backing; }
    default: return backing;
    }
}
static inline void io_wr8(uint32_t a, uint8_t v)
{
    switch (a & 0xFF) {
    case 0x10: io.key_waiting = 0; break;
    case 0x29: io.shr = (uint8_t)((v >> 7) & 1); break;
    case 0x02: io.ramrd = 0; break;
    case 0x03: io.ramrd = 1; break;
    case 0x04: io.ramwrt = 0; break;
    case 0x05: io.ramwrt = 1; break;
    }
    (void)v;
}
#endif
