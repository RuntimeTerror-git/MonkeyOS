#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>


// ============================================================
// LIMINE BASE REVISION
// ============================================================

// Set the base revision to 6.
//
// Limine uses a "base revision" to allow the kernel to tell
// the bootloader which version of the basic Limine protocol
// it expects.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);


// ============================================================
// FRAMEBUFFER REQUEST
// ============================================================

// Ask Limine to give our kernel information about a framebuffer.
//
// A framebuffer is an area of memory representing the pixels
// displayed on the screen.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};


// ============================================================
// LIMINE REQUEST START / END MARKERS
// ============================================================

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;


// ============================================================
// MEMORY FUNCTIONS
// ============================================================

// MonkeyOS is a freestanding kernel.
//
// Therefore we cannot depend on the normal C standard library.
//
// We implement the basic memory functions ourselves.


// ------------------------------------------------------------
// memcpy()
// ------------------------------------------------------------

void *memcpy(void *restrict dest,
             const void *restrict src,
             size_t n)
{
    uint8_t *restrict pdest = dest;
    const uint8_t *restrict psrc = src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}


// ------------------------------------------------------------
// memset()
// ------------------------------------------------------------

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}


// ------------------------------------------------------------
// memmove()
// ------------------------------------------------------------

void *memmove(void *dest,
              const void *src,
              size_t n)
{
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    // Destination is before source.
    //
    // Copy forwards.

    if ((uintptr_t)src > (uintptr_t)dest)
    {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    }

    // Destination is after source.
    //
    // Copy backwards so overlapping memory is handled safely.

    else if ((uintptr_t)src < (uintptr_t)dest)
    {
        for (size_t i = n; i > 0; i--) {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}


// ------------------------------------------------------------
// memcmp()
// ------------------------------------------------------------

int memcmp(const void *s1,
           const void *s2,
           size_t n)
{
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}


// ============================================================
// HALT AND CATCH FIRE
// ============================================================

// Permanently stop the CPU.
//
// We use this when something goes wrong or when the kernel
// has finished its current task.

static void hcf(void)
{
    for (;;)
    {
        // HLT stops the CPU until an interrupt occurs.

        asm ("hlt");
    }
}


// ============================================================
// I/O PORT INPUT
// ============================================================

// Read one byte from an x86 hardware I/O port.
//
// The keyboard controller uses I/O ports for communication
// with the CPU.

static uint8_t inb(uint16_t port)
{
    uint8_t value;

    asm volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


// ============================================================
// KEYBOARD PORTS
// ============================================================
//
// 0x64 -> Keyboard controller STATUS register
// 0x60 -> Keyboard DATA register
//

#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64


// ============================================================
// KEYBOARD POLLING
// ============================================================
//
// We are using polling instead of keyboard interrupts.
//
// The CPU repeatedly checks port 0x64.
//
// Bit 0 of the status register:
//
//     0 -> no data available
//     1 -> data available
//
// A PS/2 keyboard normally uses Set 1 scancodes here.
//
// Example:
//
//     A pressed  -> 0x1E
//     A released -> 0x9E
//
// Bit 7 is set in a release code.
//
// Therefore we ignore codes with bit 7 set.
//

static uint8_t keyboard_poll(void)
{
    // Read keyboard controller status.

    uint8_t status = inb(KEYBOARD_STATUS_PORT);


    // Check bit 0.
//
// No keyboard data is available.

    if ((status & 0x01) == 0)
    {
        return 0;
    }


    // Read the actual keyboard scancode.

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);


    // Ignore key-release codes.

    if (scancode & 0x80)
    {
        return 0;
    }


    // Return the key-press scancode.

    return scancode;
}


// ============================================================
// KEYBOARD SCANCODE TO ASCII
// ============================================================
//
// The keyboard does not send ASCII characters.
//
// It sends SCANCODES.
//
// Example:
//
//     0x1E -> A key
//     0x30 -> B key
//     0x2E -> C key
//
// This function converts those scancodes into characters.
//
// IMPORTANT:
//
// This is only a basic keyboard driver for now.
//
// Shift, Caps Lock, Backspace, Enter, etc. will be added later.
//

static char keyboard_scancode_to_ascii(uint8_t scancode)
{
    switch (scancode)
    {
        // ----------------------------------------------------
        // NUMBER ROW
        // ----------------------------------------------------

        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';


        // ----------------------------------------------------
        // TOP LETTER ROW
        // ----------------------------------------------------

        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';


        // ----------------------------------------------------
        // HOME LETTER ROW
        // ----------------------------------------------------

        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';


        // ----------------------------------------------------
        // BOTTOM LETTER ROW
        // ----------------------------------------------------

        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';


        // ----------------------------------------------------
        // SPACE
        // ----------------------------------------------------

        case 0x39: return ' ';


        // ----------------------------------------------------
        // UNKNOWN SCANCODE
        // ----------------------------------------------------

        default:
            return '\0';
        
        //----------------------------------------------------
        // ENTER
        // ----------------------------------------------------
        //
        // Enter key scancode:
        //
        //     0x1C
        //
        // We convert it to '\n'.
        //
        // console_putchar() already knows how to handle
        // newline characters.
        //

        case 0x1C: return '\n';
    }
}


// ============================================================
// FRAMEBUFFER
// ============================================================

// Draw ONE pixel on the screen.
//
// framebuffer -> framebuffer information from Limine
// x           -> horizontal position
// y           -> vertical position
// color       -> pixel color

static void put_pixel(struct limine_framebuffer *framebuffer,
                       uint32_t x,
                       uint32_t y,
                       uint32_t color)
{
    volatile uint32_t *fb = framebuffer->address;

    // pitch is the number of BYTES in one screen row.
//
// Each pixel is 4 bytes.
//
// Therefore:
//
//     pitch / 4
//
// gives us pixels per row.

    fb[y * (framebuffer->pitch / 4) + x] = color;
}


// ============================================================
// FONT DATA
// ============================================================
//
// Each character:
//
//     5 pixels wide
//     7 pixels high
//
// Each number below represents one row.
//
// Example:
//
//     10001
//
// means:
//
//     ON  OFF OFF OFF ON
//
// ============================================================


// ============================================================
// LOWERCASE LETTERS
// ============================================================


// ------------------------------------------------------------
// a
// ------------------------------------------------------------

static const uint8_t font_a[7] = {
    0b00000,
    0b01110,
    0b00001,
    0b01111,
    0b10001,
    0b10011,
    0b01101
};


// ------------------------------------------------------------
// b
// ------------------------------------------------------------

static const uint8_t font_b[7] = {
    0b10000,
    0b10000,
    0b10110,
    0b11001,
    0b10001,
    0b10001,
    0b11110
};


// ------------------------------------------------------------
// c
// ------------------------------------------------------------

static const uint8_t font_c[7] = {
    0b00000,
    0b01110,
    0b10001,
    0b10000,
    0b10000,
    0b10001,
    0b01110
};


// ------------------------------------------------------------
// d
// ------------------------------------------------------------

static const uint8_t font_d[7] = {
    0b00001,
    0b00001,
    0b01101,
    0b10011,
    0b10001,
    0b10001,
    0b01111
};


// ------------------------------------------------------------
// e
// ------------------------------------------------------------

static const uint8_t font_e[7] = {
    0b00000,
    0b01110,
    0b10001,
    0b11111,
    0b10000,
    0b10001,
    0b01110
};


// ------------------------------------------------------------
// f
// ------------------------------------------------------------

static const uint8_t font_f[7] = {
    0b00110,
    0b01001,
    0b01000,
    0b11100,
    0b01000,
    0b01000,
    0b01000
};


// ------------------------------------------------------------
// g
// ------------------------------------------------------------

static const uint8_t font_g[7] = {
    0b00000,
    0b01111,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b01110
};


// ------------------------------------------------------------
// h
// ------------------------------------------------------------

static const uint8_t font_h[7] = {
    0b10000,
    0b10000,
    0b10110,
    0b11001,
    0b10001,
    0b10001,
    0b10001
};


// ------------------------------------------------------------
// i
// ------------------------------------------------------------

static const uint8_t font_i[7] = {
    0b00100,
    0b00000,
    0b01100,
    0b00100,
    0b00100,
    0b00100,
    0b01110
};


// ------------------------------------------------------------
// j
// ------------------------------------------------------------

static const uint8_t font_j[7] = {
    0b00010,
    0b00000,
    0b00110,
    0b00010,
    0b00010,
    0b10010,
    0b01100
};


// ------------------------------------------------------------
// k
// ------------------------------------------------------------

static const uint8_t font_k[7] = {
    0b10000,
    0b10000,
    0b10010,
    0b10100,
    0b11000,
    0b10100,
    0b10010
};


// ------------------------------------------------------------
// l
// ------------------------------------------------------------

static const uint8_t font_l[7] = {
    0b11000,
    0b01000,
    0b01000,
    0b01000,
    0b01000,
    0b01000,
    0b11100
};


// ------------------------------------------------------------
// m
// ------------------------------------------------------------

static const uint8_t font_m[7] = {
    0b00000,
    0b11011,
    0b10101,
    0b10101,
    0b10101,
    0b10101,
    0b10101
};


// ------------------------------------------------------------
// n
// ------------------------------------------------------------

static const uint8_t font_n[7] = {
    0b00000,
    0b10110,
    0b11001,
    0b10001,
    0b10001,
    0b10001,
    0b10001
};


// ------------------------------------------------------------
// o
// ------------------------------------------------------------

static const uint8_t font_o[7] = {
    0b00000,
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110
};


// ------------------------------------------------------------
// p
// ------------------------------------------------------------

static const uint8_t font_p[7] = {
    0b00000,
    0b11110,
    0b10001,
    0b10001,
    0b11110,
    0b10000,
    0b10000
};


// ------------------------------------------------------------
// q
// ------------------------------------------------------------

static const uint8_t font_q[7] = {
    0b00000,
    0b01111,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b00001
};


// ------------------------------------------------------------
// r
// ------------------------------------------------------------

static const uint8_t font_r[7] = {
    0b00000,
    0b10110,
    0b11001,
    0b10000,
    0b10000,
    0b10000,
    0b10000
};


// ------------------------------------------------------------
// s
// ------------------------------------------------------------

static const uint8_t font_s[7] = {
    0b00000,
    0b01111,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b11110
};


// ------------------------------------------------------------
// t
// ------------------------------------------------------------

static const uint8_t font_t[7] = {
    0b01000,
    0b01000,
    0b11100,
    0b01000,
    0b01000,
    0b01001,
    0b00110
};


// ------------------------------------------------------------
// u
// ------------------------------------------------------------

static const uint8_t font_u[7] = {
    0b00000,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10011,
    0b01101
};


// ------------------------------------------------------------
// v
// ------------------------------------------------------------

static const uint8_t font_v[7] = {
    0b00000,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01010,
    0b00100
};


// ------------------------------------------------------------
// w
// ------------------------------------------------------------

static const uint8_t font_w[7] = {
    0b00000,
    0b10001,
    0b10001,
    0b10101,
    0b10101,
    0b10101,
    0b01010
};


// ------------------------------------------------------------
// x
// ------------------------------------------------------------

static const uint8_t font_x[7] = {
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b01010,
    0b10001,
    0b10001
};


// ------------------------------------------------------------
// y
// ------------------------------------------------------------

static const uint8_t font_y[7] = {
    0b10001,
    0b10001,
    0b10001,
    0b01110,
    0b00100,
    0b01000,
    0b10000
};


// ------------------------------------------------------------
// z
// ------------------------------------------------------------

static const uint8_t font_z[7] = {
    0b00000,
    0b11111,
    0b00010,
    0b00100,
    0b01000,
    0b10000,
    0b11111
};


// ============================================================
// UPPERCASE LETTERS
// ============================================================

// We keep these because MonkeyOS currently uses uppercase
// letters in its startup messages.


// ------------------------------------------------------------
// H
// ------------------------------------------------------------

static const uint8_t font_H[7] = {
    0b10001,
    0b10001,
    0b10001,
    0b11111,
    0b10001,
    0b10001,
    0b10001
};


// ------------------------------------------------------------
// M
// ------------------------------------------------------------

static const uint8_t font_M[7] = {
    0b10001,
    0b11011,
    0b10101,
    0b10101,
    0b10001,
    0b10001,
    0b10001
};


// ------------------------------------------------------------
// O
// ------------------------------------------------------------

static const uint8_t font_O[7] = {
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110
};


// ------------------------------------------------------------
// N
// ------------------------------------------------------------

static const uint8_t font_N[7] = {
    0b10001,
    0b11001,
    0b11001,
    0b10101,
    0b10011,
    0b10011,
    0b10001
};


// ------------------------------------------------------------
// K
// ------------------------------------------------------------

static const uint8_t font_K[7] = {
    0b10001,
    0b10010,
    0b10100,
    0b11000,
    0b10100,
    0b10010,
    0b10001
};


// ------------------------------------------------------------
// E
// ------------------------------------------------------------

static const uint8_t font_E[7] = {
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b10000,
    0b10000,
    0b11111
};


// ------------------------------------------------------------
// S
// ------------------------------------------------------------

static const uint8_t font_S[7] = {
    0b01111,
    0b10000,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b11110
};


// ============================================================
// NUMBERS
// ============================================================

static const uint8_t font_0[7] = {
    0b01110,
    0b10001,
    0b10011,
    0b10101,
    0b11001,
    0b10001,
    0b01110
};

static const uint8_t font_1[7] = {
    0b00100,
    0b01100,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b01110
};

static const uint8_t font_2[7] = {
    0b01110,
    0b10001,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b11111
};

static const uint8_t font_3[7] = {
    0b11110,
    0b00001,
    0b00001,
    0b01110,
    0b00001,
    0b00001,
    0b11110
};

static const uint8_t font_4[7] = {
    0b00010,
    0b00110,
    0b01010,
    0b10010,
    0b11111,
    0b00010,
    0b00010
};

static const uint8_t font_5[7] = {
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b00001,
    0b00001,
    0b11110
};

static const uint8_t font_6[7] = {
    0b00110,
    0b01000,
    0b10000,
    0b11110,
    0b10001,
    0b10001,
    0b01110
};

static const uint8_t font_7[7] = {
    0b11111,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b01000,
    0b01000
};

static const uint8_t font_8[7] = {
    0b01110,
    0b10001,
    0b10001,
    0b01110,
    0b10001,
    0b10001,
    0b01110
};

static const uint8_t font_9[7] = {
    0b01110,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b00010,
    0b01100
};


// ============================================================
// EXCLAMATION MARK
// ============================================================

static const uint8_t font_exclamation[7] = {
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00000,
    0b00100
};


// ============================================================
// GET CHARACTER
// ============================================================
//
// Convert a character into its bitmap.
//
// Example:
//
//     get_char('a')
//          ↓
//     font_a
//
// If a character is not supported:
//
//     NULL
//

static const uint8_t *get_char(char c)
{
    switch (c)
    {
        // ----------------------------------------------------
        // LOWERCASE LETTERS
        // ----------------------------------------------------

        case 'a': return font_a;
        case 'b': return font_b;
        case 'c': return font_c;
        case 'd': return font_d;
        case 'e': return font_e;
        case 'f': return font_f;
        case 'g': return font_g;
        case 'h': return font_h;
        case 'i': return font_i;
        case 'j': return font_j;
        case 'k': return font_k;
        case 'l': return font_l;
        case 'm': return font_m;
        case 'n': return font_n;
        case 'o': return font_o;
        case 'p': return font_p;
        case 'q': return font_q;
        case 'r': return font_r;
        case 's': return font_s;
        case 't': return font_t;
        case 'u': return font_u;
        case 'v': return font_v;
        case 'w': return font_w;
        case 'x': return font_x;
        case 'y': return font_y;
        case 'z': return font_z;


        // ----------------------------------------------------
        // UPPERCASE LETTERS
        // ----------------------------------------------------

        case 'H': return font_H;
        case 'M': return font_M;
        case 'N': return font_N;
        case 'K': return font_K;
        case 'E': return font_E;
        case 'O': return font_O;
        case 'S': return font_S;


        // ----------------------------------------------------
        // NUMBERS
        // ----------------------------------------------------

        case '0': return font_0;
        case '1': return font_1;
        case '2': return font_2;
        case '3': return font_3;
        case '4': return font_4;
        case '5': return font_5;
        case '6': return font_6;
        case '7': return font_7;
        case '8': return font_8;
        case '9': return font_9;


        // ----------------------------------------------------
        // EXCLAMATION MARK
        // ----------------------------------------------------

        case '!': return font_exclamation;


        // ----------------------------------------------------
        // UNKNOWN CHARACTER
        // ----------------------------------------------------

        default:
            return NULL;
    }
}


// ============================================================
// DRAW CHARACTER
// ============================================================
//
// Convert the 5 × 7 bitmap into actual framebuffer pixels.
//
// scale = 4:
//
//     1 bitmap pixel
//          ↓
//     4 × 4 screen pixels
//

static void draw_char(struct limine_framebuffer *framebuffer,
                      char c,
                      uint32_t x,
                      uint32_t y,
                      uint32_t color,
                      uint32_t scale)
{
    // Find the bitmap for this character.

    const uint8_t *bitmap = get_char(c);


    // Space is simply empty.

    if (c == ' ')
    {
        return;
    }


    // Character is not supported.

    if (bitmap == NULL)
    {
        return;
    }


    // Go through every row.

    for (uint32_t row = 0; row < 7; row++)
    {
        // Go through every column.

        for (uint32_t col = 0; col < 5; col++)
        {
            // Check whether this bitmap pixel is ON.

            if (bitmap[row] & (1 << (4 - col)))
            {
                // Convert one bitmap pixel into a larger
                // block of actual screen pixels.

                for (uint32_t dy = 0; dy < scale; dy++)
                {
                    for (uint32_t dx = 0; dx < scale; dx++)
                    {
                        put_pixel(
                            framebuffer,
                            x + col * scale + dx,
                            y + row * scale + dy,
                            color
                        );
                    }
                }
            }
        }
    }
}


// ============================================================
// CONSOLE CURSOR
// ============================================================

static uint32_t cursor_x = 50;
static uint32_t cursor_y = 50;


// ============================================================
// CHARACTER SIZE AND SPACING
// ============================================================
//
// Bitmap:
//
//     5 × 7
//
// Scale:
//
//     4
//
// Actual character:
//
//     20 × 28 pixels
//
// We reserve:
//
//     24 pixels horizontally
//     36 pixels vertically
//

static const uint32_t CHAR_WIDTH = 24;
static const uint32_t CHAR_HEIGHT = 36;


// ============================================================
// CONSOLE SCROLL
// ============================================================
//
// Move framebuffer content upward when we reach the bottom.
//

static void console_scroll(struct limine_framebuffer *framebuffer)
{
    uint32_t pitch = framebuffer->pitch;


    // Move the framebuffer contents upward by one text line.

    memmove(
        framebuffer->address,

        (uint8_t *)framebuffer->address +
            pitch * CHAR_HEIGHT,

        pitch * (framebuffer->height - CHAR_HEIGHT)
    );


    // Clear the new empty area at the bottom.

    memset(
        (uint8_t *)framebuffer->address +
            pitch * (framebuffer->height - CHAR_HEIGHT),

        0,

        pitch * CHAR_HEIGHT
    );


    // Move cursor upward because the screen moved upward.

    cursor_y -= CHAR_HEIGHT;
}
// ============================================================
// CLEAR CHARACTER AREA
// ============================================================
//
// Erase one character cell from the framebuffer.
//
// We use this for Backspace.
//
// The character cell is:
//
//     CHAR_WIDTH  ×  CHAR_HEIGHT
//
// We fill the entire cell with black.
//

static void clear_character_area(
    struct limine_framebuffer *framebuffer,
    uint32_t x,
    uint32_t y
)
{
    for (uint32_t py = 0; py < CHAR_HEIGHT; py++)
    {
        for (uint32_t px = 0; px < CHAR_WIDTH; px++)
        {
            put_pixel(
                framebuffer,
                x + px,
                y + py,
                0x000000
            );
        }
    }
}
// ============================================================
// CONSOLE BACKSPACE
// ============================================================
//
// Backspace removes the character immediately before
// the current cursor position.
//
// Example:
//
//     Hello|
//
// After Backspace:
//
//     Hell|
//
// The cursor moves backwards by one character cell
// and that cell is cleared.
//

static void console_backspace(
    struct limine_framebuffer *framebuffer
)
{
    // --------------------------------------------------------
    // If we are at the very beginning of the console,
    // there is nothing to delete.
    // --------------------------------------------------------

    if (cursor_x == 50 && cursor_y == 50)
    {
        return;
    }


    // --------------------------------------------------------
    // NORMAL CASE
    // --------------------------------------------------------
    //
    // There is a previous character on the same line.
    //

    if (cursor_x > 50)
    {
        // Move cursor one character backwards.

        cursor_x -= CHAR_WIDTH;


        // Erase that character.

        clear_character_area(
            framebuffer,
            cursor_x,
            cursor_y
        );

        return;
    }


    // --------------------------------------------------------
    // BEGINNING OF LINE
    // --------------------------------------------------------
    //
    // If cursor_x == 50, we are at the beginning of a line.
    //
    // Move to the previous line.
    //

    if (cursor_y > 50)
    {
        cursor_y -= CHAR_HEIGHT;


        // Calculate how many character columns fit.

        uint32_t columns =
            (framebuffer->width - 50) / CHAR_WIDTH;


        // Move cursor to the last character position.

        if (columns > 0)
        {
            cursor_x =
                50 + (columns - 1) * CHAR_WIDTH;
        }


        // Erase the character at that position.

        clear_character_area(
            framebuffer,
            cursor_x,
            cursor_y
        );
    }
}


// ============================================================
// CONSOLE PUTCHAR
// ============================================================
//
// Print ONE character.
//
// Handles:
//
//     - newline
//     - wrapping
//     - drawing
//     - cursor movement
//     - scrolling
//

static void console_putchar(struct limine_framebuffer *framebuffer,
                            char c)
{
    // --------------------------------------------------------
    // NEWLINE
    // --------------------------------------------------------

    if (c == '\n')
    {
        // Return to beginning of line.

        cursor_x = 50;

        // Move down.

        cursor_y += CHAR_HEIGHT;
    }

    else
    {
        // ----------------------------------------------------
        // AUTOMATIC LINE WRAPPING
        // ----------------------------------------------------

        if (cursor_x + CHAR_WIDTH > framebuffer->width)
        {
            cursor_x = 50;

            cursor_y += CHAR_HEIGHT;
        }


        // ----------------------------------------------------
        // DRAW CHARACTER
        // ----------------------------------------------------

        draw_char(
            framebuffer,
            c,
            cursor_x,
            cursor_y,
            0xFFFFFF,
            4
        );


        // ----------------------------------------------------
        // MOVE CURSOR
        // ----------------------------------------------------

        cursor_x += CHAR_WIDTH;
    }


    // --------------------------------------------------------
    // CHECK SCREEN BOTTOM
    // --------------------------------------------------------

    if (cursor_y + CHAR_HEIGHT > framebuffer->height)
    {
        console_scroll(framebuffer);
    }
}


// ============================================================
// CONSOLE WRITE
// ============================================================
//
// Print an entire C string.
//

static void console_write(struct limine_framebuffer *framebuffer,
                          const char *string)
{
    while (*string)
    {
        // Let console_putchar() handle every character.

        console_putchar(
            framebuffer,
            *string
        );

        // Move to next character.

        string++;
    }
}


// ============================================================
// KERNEL ENTRY POINT
// ============================================================

void kmain(void)
{
    // --------------------------------------------------------
    // CHECK LIMINE BASE REVISION
    // --------------------------------------------------------

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        hcf();
    }


    // --------------------------------------------------------
    // CHECK FRAMEBUFFER
    // --------------------------------------------------------

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }


    // --------------------------------------------------------
    // GET FIRST FRAMEBUFFER
    // --------------------------------------------------------

    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];


    // ========================================================
    // STARTUP MESSAGE
    // ========================================================

    console_write(
        framebuffer,
        "Hello\n"
    );

    console_write(
        framebuffer,
        "MonkeyOS!\n"
    );


// ========================================================
// KEYBOARD POLLING LOOP
// ========================================================

for (;;)
{
    // ----------------------------------------------------
    // GET SCANCODE
    // ----------------------------------------------------

    uint8_t scancode = keyboard_poll();


    // No key available.

    if (scancode == 0)
    {
        continue;
    }


    // ----------------------------------------------------
    // BACKSPACE
    // ----------------------------------------------------
    //
    // Backspace scancode:
    //
    //     0x0E
    //
    // It is handled separately because it is an
    // editing operation rather than a printable
    // character.
    //

    if (scancode == 0x0E)
    {
        console_backspace(framebuffer);

        continue;
    }


    // ----------------------------------------------------
    // CONVERT SCANCODE TO CHARACTER
    // ----------------------------------------------------

    char character =
        keyboard_scancode_to_ascii(scancode);


    // ----------------------------------------------------
    // PRINT CHARACTER
    // ----------------------------------------------------

    if (character != '\0')
    {
        console_putchar(
            framebuffer,
            character
        );
    }
}
}