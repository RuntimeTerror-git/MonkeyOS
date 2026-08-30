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
//
// If the bootloader does not support this revision,
// we will stop the kernel later in kmain().

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);


// ============================================================
// FRAMEBUFFER REQUEST
// ============================================================

// We are asking Limine to give our kernel information about
// a framebuffer.
//
// A framebuffer is an area of memory that represents the
// pixels displayed on the screen.
//
// Once Limine gives us this information, we can directly
// write pixels into that memory.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};


// ============================================================
// LIMINE REQUEST START / END MARKERS
// ============================================================

// These markers tell Limine where our requests begin and end.
//
// Limine looks for these special sections when loading
// our kernel and uses them to find the requests we made.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;


// ============================================================
// MEMORY FUNCTIONS
// ============================================================

// GCC and Clang may generate calls to functions such as
// memcpy(), memset(), memmove(), and memcmp().
//
// Because MonkeyOS is a freestanding kernel, we cannot rely
// on the normal C standard library being available.
//
// Therefore, we implement these functions ourselves.


// ------------------------------------------------------------
// memcpy()
// ------------------------------------------------------------

// Copy 'n' bytes from src to dest.
//
// memcpy() assumes that the source and destination memory
// regions do not overlap.

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

// Fill 'n' bytes of memory with the value 'c'.

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

// Copy 'n' bytes from src to dest.
//
// Unlike memcpy(), memmove() is safe when the source and
// destination memory regions overlap.

void *memmove(void *dest,
              const void *src,
              size_t n)
{
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    // If destination is before source, copy forwards.

    if ((uintptr_t)src > (uintptr_t)dest) {

        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    }

    // If destination is after source, copy backwards.
    //
    // Copying backwards prevents us from overwriting source
    // data that we still need.

    else if ((uintptr_t)src < (uintptr_t)dest) {

        for (size_t i = n; i > 0; i--) {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}


// ------------------------------------------------------------
// memcmp()
// ------------------------------------------------------------

// Compare two blocks of memory.
//
// Returns:
//   0  -> memory blocks are equal
//  -1  -> s1 is smaller
//   1  -> s1 is greater

int memcmp(const void *s1,
           const void *s2,
           size_t n)
{
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {

        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}


// ============================================================
// HALT AND CATCH FIRE
// ============================================================

// This function permanently stops the CPU from doing useful
// work.
//
// We use it when something goes wrong or when our kernel has
// finished its current job.

static void hcf(void)
{
    for (;;) {

        // HLT tells the CPU to halt until an interrupt occurs.

        asm ("hlt");
    }
}

// ============================================================
// I/O PORT INPUT
// ============================================================

// Read one byte from an x86 hardware I/O port.
//
// The keyboard controller uses I/O ports to communicate
// with the CPU.
//
// We will use this function to read keyboard data.

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
// The traditional PC keyboard controller uses:
//
//     0x64 -> Keyboard controller STATUS register
//     0x60 -> Keyboard DATA register
//
// We use these ports to communicate with the keyboard.
//
// ============================================================

#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64


// ============================================================
// KEYBOARD POLLING
// ============================================================
//
// We are NOT using keyboard interrupts yet.
//
// Instead, the CPU repeatedly checks port 0x64.
//
// This is called POLLING.
//
// ------------------------------------------------------------
//
// Status register (0x64):
//
// Bit 0 = Output Buffer Status
//
//     0 -> No data available
//     1 -> Data available
//
// ------------------------------------------------------------
//
// A normal PS/2 keyboard produces:
//
//     KEY PRESS
//         ↓
//     MAKE CODE
//
//     KEY RELEASE
//         ↓
//     BREAK CODE
//
// For example, the A key:
//
//     Press A:
//         0x1E
//
//     Release A:
//         0x9E
//
// Notice:
//
//     0x9E has bit 7 set.
//
// Therefore:
//
//     bit 7 = 0 → MAKE CODE
//     bit 7 = 1 → BREAK CODE
//
// We only want MAKE codes right now.
//
// ============================================================

static uint8_t keyboard_poll(void)
{
    // --------------------------------------------------------
    // STEP 1: Check keyboard controller status
    // --------------------------------------------------------

    uint8_t status = inb(KEYBOARD_STATUS_PORT);


    // --------------------------------------------------------
    // STEP 2: Check whether data is available
    // --------------------------------------------------------
    //
    // Bit 0 tells us whether the output buffer contains data.
    //
    //     0 -> nothing available
    //     1 -> data available
    //
    // If there is no data, return 0.
    //
    // The caller will then do:
    //
    //     if (scancode == 0)
    //         continue;
    //
    // --------------------------------------------------------

    if ((status & 0x01) == 0)
    {
        return 0;
    }


    // --------------------------------------------------------
    // STEP 3: Read the scancode
    // --------------------------------------------------------
    //
    // The actual keyboard data is available at port 0x60.

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);


    // --------------------------------------------------------
    // STEP 4: Ignore BREAK / KEY RELEASE codes
    // --------------------------------------------------------
    //
    // In PS/2 Set 1 scancodes:
    //
    //     bit 7 = 1
    //
    // means that this is a key-release code.
    //
    // Example:
    //
    //     0x1E -> A pressed
    //     0x9E -> A released
    //
    // We only want:
    //
    //     0x1E
    //
    // So ignore:
    //
    //     0x9E
    //
    // --------------------------------------------------------

    if (scancode & 0x80)
    {
        // This is a BREAK code.
        //
        // Do not send it to the keyboard handler.

        return 0;
    }


    // --------------------------------------------------------
    // STEP 5: Return the MAKE code
    // --------------------------------------------------------
    //
    // At this point:
    //
    //     data was available
    //     ↓
    //     scancode was read
    //     ↓
    //     bit 7 is 0
    //     ↓
    //     this is a key press
    //
    // Return it to the keyboard polling loop.

    return scancode;
}

// ============================================================
// KEYBOARD SCANCODE TO ASCII
// ============================================================

// The keyboard sends scancodes, not ASCII characters.
//
// Example:
//
//     0x1E -> A key
//     0x30 -> B key
//     0x2E -> C key
//
// This function converts those scancodes into characters.

static char keyboard_scancode_to_ascii(uint8_t scancode)
{
    switch (scancode)
    {
        // Numbers
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

        // QWERTY row
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

        // Home row
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';

        // Bottom row
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';

        // Space
        case 0x39: return ' ';

        // Unknown key
        default:
            return '\0';
    }
}

// ============================================================
// FRAMEBUFFER
// ============================================================

// This function draws ONE pixel on the screen.
//
// framebuffer -> information provided by Limine
// x           -> horizontal position
// y           -> vertical position
// color       -> color of the pixel
//
// The framebuffer is essentially a large area of memory
// containing the pixels of our screen.

static void put_pixel(struct limine_framebuffer *framebuffer,
                       uint32_t x,
                       uint32_t y,
                       uint32_t color)
{
    // framebuffer->address points to the beginning of the
    // framebuffer memory.

    volatile uint32_t *fb = framebuffer->address;


    // framebuffer->pitch tells us how many BYTES one complete
    // row of pixels occupies.
    //
    // Each pixel is 4 bytes (32 bits).
    //
    // Therefore pitch / 4 gives us the number of pixels
    // in one row.

    fb[y * (framebuffer->pitch / 4) + x] = color;
}


// ============================================================
// FONT DATA
// ============================================================

// Our font is a small handmade bitmap font.
//
// Each character is:
//
//     5 pixels wide
//     7 pixels high
//
// A '1' means that the pixel should be drawn.
// A '0' means that the pixel should remain empty.
//
// draw_char() later converts these bitmap patterns
// into actual framebuffer pixels.


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


// ------------------------------------------------------------
// !
// ------------------------------------------------------------

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

// This function maps a character to its bitmap.
//
// Example:
//
//     get_char('H')
//          ↓
//     returns font_H
//
// If we don't have a bitmap for a character,
// we return NULL.

static const uint8_t *get_char(char c)
{
    switch (c)
    {
        case 'H':
            return font_H;

        case 'e':
            return font_e;

        case 'l':
            return font_l;

        case 'o':
            return font_o;

        case 'M':
            return font_M;

        case 'N':
            return font_N;

        case 'K':
            return font_K;

        case 'E':
            return font_E;

        case 'n':
            return font_n;

        case 'k':
            return font_k;

        case 'y':
            return font_y;

        case 'O':
            return font_O;

        case 'S':
            return font_S;

        case '!':
            return font_exclamation;

        default:
            return NULL;
    }
}


// ============================================================
// DRAW CHARACTER
// ============================================================

// This function converts our 5 x 7 bitmap into real pixels.
//
// With scale = 4:
//
//     1 bitmap pixel
//          ↓
//     becomes a 4 x 4 block of screen pixels.

static void draw_char(struct limine_framebuffer *framebuffer,
                      char c,
                      uint32_t x,
                      uint32_t y,
                      uint32_t color,
                      uint32_t scale)
{
    // Get the bitmap belonging to this character.

    const uint8_t *bitmap = get_char(c);


    // A space does not have a bitmap.
    //
    // It simply represents empty space.

    if (c == ' ') {
        return;
    }


    // If the character isn't in our font,
    // there is nothing to draw.

    if (bitmap == NULL) {
        return;
    }


    // Our font has 7 rows.

    for (uint32_t row = 0; row < 7; row++)
    {
        // Each row contains 5 pixels.

        for (uint32_t col = 0; col < 5; col++)
        {
            // Check whether this bitmap pixel is ON.

            if (bitmap[row] & (1 << (4 - col)))
            {
                // Scale the bitmap pixel into a larger
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

// The console needs to remember where the next character
// should be drawn.
//
// We start at:
//
//     x = 50
//     y = 50
//

static uint32_t cursor_x = 50;
static uint32_t cursor_y = 50;


// ============================================================
// CHARACTER SIZE AND SPACING
// ============================================================

// Our bitmap is:
//
//     5 pixels wide
//     7 pixels high
//
// With scale = 4:
//
//     width  = 5 × 4 = 20 pixels
//     height = 7 × 4 = 28 pixels
//
// We reserve:
//
//     CHAR_WIDTH  = 24
//     CHAR_HEIGHT = 36
//
// This gives us some spacing between characters and lines.

static const uint32_t CHAR_WIDTH = 24;
static const uint32_t CHAR_HEIGHT = 36;


// ============================================================
// CONSOLE SCROLL
// ============================================================
//
// NEW FEATURE:
//
// When the cursor reaches the bottom of the screen,
// we move the entire framebuffer upward by one text line.
//
// Example:
//
// BEFORE:
//
//     Line 1
//     Line 2
//     Line 3
//     Line 4
//
// AFTER SCROLL:
//
//     Line 2
//     Line 3
//     Line 4
//     Line 5
//
// Line 1 has disappeared and a new empty area is available
// at the bottom.
//

static void console_scroll(struct limine_framebuffer *framebuffer)
{
    // --------------------------------------------------------
    // GET FRAMEBUFFER PITCH
    // --------------------------------------------------------
    //
    // pitch = number of BYTES used by one complete row.
    //

    uint32_t pitch = framebuffer->pitch;


    // --------------------------------------------------------
    // MOVE SCREEN CONTENT UP
    // --------------------------------------------------------
    //
    // We skip the first CHAR_HEIGHT rows.
    //
    // Data from:
    //
    //     row CHAR_HEIGHT
    //
    // is moved to:
    //
    //     row 0
    //
    // memmove() is used because source and destination overlap.
    //

    memmove(
        framebuffer->address,

        (uint8_t *)framebuffer->address +
            pitch * CHAR_HEIGHT,

        pitch * (framebuffer->height - CHAR_HEIGHT)
    );


    // --------------------------------------------------------
    // CLEAR BOTTOM AREA
    // --------------------------------------------------------
    //
    // After moving the screen upward, the bottom
    // CHAR_HEIGHT rows contain old data.
    //
    // Clear that area with zero.
    //
    // Zero represents black in our current framebuffer setup.
    //

    memset(
        (uint8_t *)framebuffer->address +
            pitch * (framebuffer->height - CHAR_HEIGHT),

        0,

        pitch * CHAR_HEIGHT
    );


    // --------------------------------------------------------
    // MOVE CURSOR UP
    // --------------------------------------------------------
    //
    // The screen moved upward by CHAR_HEIGHT pixels.
    //
    // Therefore the cursor must also move upward by
    // CHAR_HEIGHT pixels.
    //

    cursor_y -= CHAR_HEIGHT;
}


// ============================================================
// CONSOLE PUTCHAR
// ============================================================
//
// Prints ONE character.
//
// It handles:
//
//     1. Newline
//     2. Automatic line wrapping
//     3. Drawing the character
//     4. Moving the cursor
//     5. Screen scrolling
//

static void console_putchar(struct limine_framebuffer *framebuffer,
                            char c)
{
    // --------------------------------------------------------
    // HANDLE NEWLINE
    // --------------------------------------------------------
    //
    // '\n' means:
    //
    //     "Move to the beginning of the next line."
    //
    // It does not draw a visible character.
    //

    if (c == '\n')
    {
        // Return to the left side.

        cursor_x = 50;

        // Move down one complete text line.

        cursor_y += CHAR_HEIGHT;
    }

    else
    {
        // ----------------------------------------------------
        // AUTOMATIC LINE WRAPPING
        // ----------------------------------------------------
        //
        // Check whether the next character will fit.

        if (cursor_x + CHAR_WIDTH > framebuffer->width)
        {
            // Start from the left again.

            cursor_x = 50;

            // Move down one line.

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
            0xFFFFFF,     // White
            4             // Scale
        );


        // ----------------------------------------------------
        // MOVE CURSOR
        // ----------------------------------------------------
        //
        // Move right so that the next character is placed
        // after the current character.
        //

        cursor_x += CHAR_WIDTH;
    }


    // --------------------------------------------------------
    // CHECK BOTTOM OF SCREEN
    // --------------------------------------------------------
    //
    // This check happens for BOTH:
    //
    //     normal characters
    //
    // and:
    //
    //     newline characters
    //
    // If the cursor has reached the bottom,
    // scroll the screen upward.
    //

    if (cursor_y + CHAR_HEIGHT > framebuffer->height)
    {
        console_scroll(framebuffer);
    }
}


// ============================================================
// CONSOLE WRITE
// ============================================================
//
// Prints an entire C string.
//
// Instead of:
//
//     console_putchar(framebuffer, 'H');
//     console_putchar(framebuffer, 'e');
//     console_putchar(framebuffer, 'l');
//     console_putchar(framebuffer, 'l');
//     console_putchar(framebuffer, 'o');
//
// we can now simply write:
//
//     console_write(framebuffer, "Hello");
//
// This function also implements WORD-AWARE WRAPPING.
//
// This means we try not to split a word between two lines.
//

static void console_write(struct limine_framebuffer *framebuffer,
                          const char *string)
{
    // Continue until we reach the end of the C string.
//
// C strings end with:
//
//     '\0'
//

    while (*string)
    {
        // ----------------------------------------------------
        // HANDLE NEWLINE
        // ----------------------------------------------------
        //
        // Let console_putchar() handle '\n'.

        if (*string == '\n')
        {
            console_putchar(
                framebuffer,
                *string
            );

            string++;

            continue;
        }


        // ----------------------------------------------------
        // FIND NEXT WORD
        // ----------------------------------------------------
        //
        // 'word' points to the beginning of the word.
        //
        // Example:
        //
        //     "Hello MonkeyOS!"
        //      ^
        //      word

        const char *word = string;


        // Count the characters in this word.

        uint32_t word_length = 0;


        // Continue until we find:
        //
        //     space
        //     newline
        //     end of string
        //

        while (word[word_length] != ' ' &&
               word[word_length] != '\n' &&
               word[word_length] != '\0')
        {
            word_length++;
        }


        // ----------------------------------------------------
        // CALCULATE WORD WIDTH
        // ----------------------------------------------------
        //
        // Each character occupies CHAR_WIDTH pixels.
        //
        // Therefore:
        //
        //     word width =
        //     word length × CHAR_WIDTH
        //

        uint32_t word_width =
            word_length * CHAR_WIDTH;


        // ----------------------------------------------------
        // CHECK WHETHER WORD FITS
        // ----------------------------------------------------
        //
        // If the complete word doesn't fit,
        // move to the next line before printing it.
        //

        if (cursor_x + word_width > framebuffer->width)
        {
            // Start from the left.

            cursor_x = 50;

            // Move down one line.

            cursor_y += CHAR_HEIGHT;


            // ------------------------------------------------
            // CHECK BOTTOM AFTER WORD WRAP
            // ------------------------------------------------
            //
            // Word wrapping itself can move the cursor below
            // the bottom of the screen.
            //

            if (cursor_y + CHAR_HEIGHT > framebuffer->height)
            {
                console_scroll(framebuffer);
            }
        }


        // ----------------------------------------------------
        // PRINT THE WORD
        // ----------------------------------------------------
        //
        // Print every character belonging to this word.

        for (uint32_t i = 0; i < word_length; i++)
        {
            console_putchar(
                framebuffer,
                word[i]
            );
        }


        // ----------------------------------------------------
        // HANDLE SPACE AFTER WORD
        // ----------------------------------------------------
        //
        // If there is a space after the word,
        // print that space and move to the next word.
        //

        if (string[word_length] == ' ')
        {
            console_putchar(
                framebuffer,
                ' '
            );

            string += word_length + 1;
        }

        else
        {
            // No space.
            //
            // Move directly to the next character.

            string += word_length;
        }
    }
}


// ============================================================
// KERNEL ENTRY POINT
// ============================================================
//
// This is where MonkeyOS starts executing.
//
// Limine loads our kernel and eventually transfers control
// to kmain().
//

void kmain(void)
{
    // --------------------------------------------------------
    // CHECK LIMINE BASE REVISION
    // --------------------------------------------------------

    // Make sure the bootloader supports the base revision
    // requested by our kernel.

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        hcf();
    }


    // --------------------------------------------------------
    // CHECK FRAMEBUFFER
    // --------------------------------------------------------

    // Make sure Limine successfully provided a framebuffer.

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }


    // --------------------------------------------------------
    // GET FIRST FRAMEBUFFER
    // --------------------------------------------------------

    // Limine can provide more than one framebuffer.
    //
    // For now, we simply use the first one.

    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];


    // ========================================================
    // TEST OUR CONSOLE
    // ========================================================

    // Print a short message.

    console_write(
        framebuffer,
        "Hello\n"
    );

    console_write(
        framebuffer,
        "MonkeyOS!\n"
    );


    // --------------------------------------------------------
    // TEST WORD-AWARE WRAPPING
    // --------------------------------------------------------
    //
    // This long string allows us to test our word wrapping.
    //
    // Words should not be unnecessarily split between lines.

    console_write(
        framebuffer,
        "Hello MonkeyOS! Hello MonkeyOS! Hello MonkeyOS! "
        "Hello MonkeyOS! Hello MonkeyOS! Hello MonkeyOS! "
        "Hello MonkeyOS! Hello MonkeyOS! Hello MonkeyOS! "
        "Hello MonkeyOS!"
    );


    // --------------------------------------------------------
    // TEST SCROLLING
    // --------------------------------------------------------
    //
    // We print many lines so that the framebuffer becomes full.
    //
    // Once the bottom is reached, console_scroll() should move
    // the old content upward.
    //
    // You can remove this test later.

    console_write(
        framebuffer,
        "\n"
        "Line 1\n"
        "Line 2\n"
        "Line 3\n"
        "Line 4\n"
        "Line 5\n"
        "Line 6\n"
        "Line 7\n"
        "Line 8\n"
        "Line 9\n"
        "Line 10\n"
        "Line 11\n"
        "Line 12\n"
        "Line 13\n"
        "Line 14\n"
        "Line 15\n"
        "Line 16\n"
        "Line 17\n"
        "Line 18\n"
        "Line 19\n"
        "Line 20\n"
        "Line 21\n"
        "Line 22\n"
        "Line 23\n"
        "Line 24\n"
    );


    // ============================================================
// KEYBOARD POLLING LOOP
// ============================================================

// The kernel must stay running so that it can continuously
// check the keyboard.
//
// This is called polling.
//
// The CPU repeatedly asks:
//
//     "Did the keyboard send anything?"

for (;;)
{
    // Try to read a keyboard scancode.

    uint8_t scancode = keyboard_poll();

    // If no data is available, keep checking.

    if (scancode == 0)
    {
        continue;
    }

    // --------------------------------------------------------
    // TEMPORARY TEST
    // --------------------------------------------------------
    //
    // We are NOT converting the scancode into a character yet.
    //
    // For now, simply print '!' whenever a key is detected.
    //
    // This lets us verify that:
    //
    // Keyboard
    //     ↓
    // Port 0x60
    //     ↓
    // MonkeyOS
    //
    // is working.

    // Convert the scancode into an ASCII character.

char character = keyboard_scancode_to_ascii(scancode);


// Only print characters that we recognize.

if (character != '\0')
{
    console_putchar(
        framebuffer,
        character
    );
}
}

    
}