#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;


// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    uint8_t *restrict pdest = dest;
    const uint8_t *restrict psrc = src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}


// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}


/* =========================================================
   FRAMEBUFFER
   ========================================================= */

static void put_pixel(struct limine_framebuffer *framebuffer,
                      uint32_t x,
                      uint32_t y,
                      uint32_t color)
{
    volatile uint32_t *fb = framebuffer->address;

    fb[y * (framebuffer->pitch / 4) + x] = color;
}


/* =========================================================
   5 x 7 FONT
   ========================================================= */

/*
    Each character is represented using 5 x 7 pixels.

    Example: H

    10001
    10001
    10001
    11111
    10001
    10001
    10001
*/

static const uint8_t font_H[7] = {
    0b10001,
    0b10001,
    0b10001,
    0b11111,
    0b10001,
    0b10001,
    0b10001
};

static const uint8_t font_e[7] = {
    0b00000,
    0b01110,
    0b10001,
    0b11111,
    0b10000,
    0b10001,
    0b01110
};

static const uint8_t font_l[7] = {
    0b11000,
    0b01000,
    0b01000,
    0b01000,
    0b01000,
    0b01000,
    0b11100
};

static const uint8_t font_o[7] = {
    0b00000,
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110
};

static const uint8_t font_t[7] = {
    0b00100,
    0b00100,
    0b11111,
    0b00100,
    0b00100,
    0b00101,
    0b00010
};

static const uint8_t font_h[7] = {
    0b10000,
    0b10000,
    0b10110,
    0b11001,
    0b10001,
    0b10001,
    0b10001
};

static const uint8_t font_i[7] = {
    0b00100,
    0b00000,
    0b01100,
    0b00100,
    0b00100,
    0b00100,
    0b01110
};

static const uint8_t font_s[7] = {
    0b01111,
    0b10000,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b11110
};

static const uint8_t font_M[7] = {
    0b10001,
    0b11011,
    0b10101,
    0b10101,
    0b10001,
    0b10001,
    0b10001
};

static const uint8_t font_n[7] = {
    0b00000,
    0b10110,
    0b11001,
    0b10001,
    0b10001,
    0b10001,
    0b10001
};

static const uint8_t font_k[7] = {
    0b10000,
    0b10000,
    0b10010,
    0b10100,
    0b11000,
    0b10100,
    0b10010
};

static const uint8_t font_y[7] = {
    0b10001,
    0b10001,
    0b10001,
    0b01110,
    0b00100,
    0b01000,
    0b10000
};

static const uint8_t font_O[7] = {
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110
};

static const uint8_t font_S[7] = {
    0b01111,
    0b10000,
    0b10000,
    0b01110,
    0b00001,
    0b00001,
    0b11110
};

static const uint8_t font_exclamation[7] = {
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00000,
    0b00100
};


/* =========================================================
   GET CHARACTER
   ========================================================= */

static const uint8_t *get_char(char c)
{
    switch (c)
    {
        case 'H': return font_H;
        case 'e': return font_e;
        case 'l': return font_l;
        case 'o': return font_o;

        case 't': return font_t;
        case 'h': return font_h;
        case 'i': return font_i;
        case 's': return font_s;

        case 'M': return font_M;
        case 'n': return font_n;
        case 'k': return font_k;
        case 'y': return font_y;
        case 'O': return font_O;
        case 'S': return font_S;

        case '!': return font_exclamation;

        default:
            return NULL;
    }
}


/* =========================================================
   DRAW CHARACTER
   ========================================================= */

static void draw_char(struct limine_framebuffer *framebuffer,
                      char c,
                      uint32_t x,
                      uint32_t y,
                      uint32_t color,
                      uint32_t scale)
{
    const uint8_t *bitmap = get_char(c);

    /*
        Space doesn't need to be drawn.
    */

    if (c == ' ')
        return;

    /*
        Character not found.
    */

    if (bitmap == NULL)
        return;

    /*
        Go through the 7 rows.
    */

    for (uint32_t row = 0; row < 7; row++)
    {
        /*
            Go through the 5 columns.
        */

        for (uint32_t col = 0; col < 5; col++)
        {
            /*
                Check whether this pixel is ON.
            */

            if (bitmap[row] & (1 << (4 - col)))
            {
                /*
                    Draw a scaled pixel.
                */

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


/* =========================================================
   DRAW STRING
   ========================================================= */

static void draw_string(struct limine_framebuffer *framebuffer,
                        const char *string,
                        uint32_t x,
                        uint32_t y,
                        uint32_t color,
                        uint32_t scale)
{
    while (*string)
    {
        /*
            Space between words.
        */

        if (*string == ' ')
        {
            x += 6 * scale;
        }
        else
        {
            draw_char(
                framebuffer,
                *string,
                x,
                y,
                color,
                scale
            );

            /*
                Character width = 5
                Space between characters = 1
            */

            x += 6 * scale;
        }

        string++;
    }
}


// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.

void kmain(void) {

    // Ensure the bootloader actually understands our base revision (see spec).

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }


    // Ensure we got a framebuffer.

    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }


    // Fetch the first framebuffer.

    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];


    /*
        Print:

        Hello, this is MonkeyOS!
    */

    draw_string(
        framebuffer,
        "Hello, this is MonkeyOS!",
        350,         // X position
        350,         // Y position
        0xFFFFFF,   // White
        4           // Scale
    );


    // We're done, just hang...

    hcf();
}