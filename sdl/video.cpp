#include "video.h"
#include "ttf.h"
#include "menu.h"
#include "util.h"

#include "port.h"
#include "snes9x.h"
#include "gfx.h"
#include "ppu.h"

#include <SDL.h>
#include <libintl.h>

#define _(s) gettext(s)

#ifdef _WIN32
#define STBI_WINDOWS_UTF8
#endif
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static SDL_Surface *screen = NULL;
static uint32 screenWidth = 0, screenHeight = 0;
static size_t renderOffset = 0;
const uint32 videoFlag = SDL_HWSURFACE |
#ifdef SDL_TRIPLEBUF
    SDL_TRIPLEBUF;
#else
    SDL_DOUBLEBUF;
#endif

SVideoSettings VideoSettings;

static char logMsg[256] = {};
static uint64_t logDeadline = 0ULL;
static TTF::Font *ttf_font;
static VideoImageData bg;
static bool clearCache = false;

static bool freezed = false;
static const int freezeWidth = SNES_WIDTH / 2, freezeHeight = SNES_HEIGHT / 2;
static uint8_t freezeBuffer[freezeWidth * freezeHeight * 3];

static void predrawMenu();

void S9xExtraDisplayUsage() {

}

void S9xParseDisplayArg(char **, int &, int) {

}

void S9xSetTitle(const char *title) {
    SDL_WM_SetCaption(title, NULL);
}

void VideoCustomDisplayString(const char *string, int linesFromBottom, int pixelsFromLeft, bool allowWrap, int type) {
    VideoOutputStringPixel(pixelsFromLeft, screenHeight - linesFromBottom * 9, string, allowWrap, true);
}

void S9xInitDisplay(int argc, char **argv) {
    SDL_ShowCursor(0);
    VideoSetOriginResolution();

    const char *font_files = _("/usr/share/fonts/dejavu/DejaVuSansMono.ttf|/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    int pos = 0;
    if (ttf_font) delete ttf_font;
    ttf_font = new TTF::Font(12, 0, screen);
    while (pos >= 0) {
        char font_filename[MAXPATHLEN];
        const char *delim = strchr(font_files + pos, '|');
        if (delim == NULL) {
            strcpy(font_filename, font_files + pos);
            pos = -1;
        } else {
            strncpy(font_filename, font_files + pos, delim - font_files - pos);
            font_filename[delim - font_files - pos] = 0;
            pos = delim - font_files + 1;
        }
        ttf_font->add(font_filename);
    }

    S9xGraphicsInit();
    // S9xCustomDisplayString = &VideoCustomDisplayString;
    MenuSetPreDrawFunc(&predrawMenu);

    bg = VideoLoadImageFile("bg.png");
}

void S9xDeinitDisplay() {
    VideoFreeImage(&bg);

    S9xGraphicsDeinit();
    delete ttf_font;
}

void S9xTextMode() {
}

void S9xGraphicsMode() {
}

void S9xSetPalette() {

}

bool8 S9xInitUpdate() {
    return 1;
}

bool8 S9xDeinitUpdate(int width, int height) {
    if (logMsg[0]) {
        VideoOutputStringPixel(16, 16, logMsg, true, true);
        if (GetTicks() >= logDeadline) {
            logMsg[0] = 0;
        }
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    SDL_Flip(screen);
    if (VideoSettings.Fullscreen) {
        width = (width + 7) & ~7;
        height = (height + 7) & ~7;
        if (width!=screenWidth || height!=screenHeight) {
            renderOffset = 0;
            screenWidth = width;
            screenHeight = height;
            screen = SDL_SetVideoMode(width, height, 16, videoFlag);
            clearCache = true;
        }
    }
    if (clearCache) {
        clearCache = false;
        VideoClear();
        SDL_Flip(screen);
        VideoClear();
#ifdef SDL_TRIPLEBUF
        SDL_Flip(screen);
        VideoClear();
#endif
        GFX.Pitch = screen->pitch;
    }
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    GFX.Screen = (uint16*)screen->pixels + renderOffset;
    return 1;
}

bool8 S9xContinueUpdate(int, int) {
    return 1;
}

void S9xSyncSpeed() {
    if (Settings.SoundSync)
    {
        return;
    }

    if (Settings.DumpStreams)
        return;

    if (Settings.HighSpeedSeek > 0)
        Settings.HighSpeedSeek--;

    if (Settings.TurboMode)
    {
        if ((++IPPU.FrameSkip >= Settings.TurboSkipFrames) && !Settings.HighSpeedSeek)
        {
            IPPU.FrameSkip = 0;
            IPPU.SkippedFrames = 0;
            IPPU.RenderThisFrame = TRUE;
        }
        else
        {
            IPPU.SkippedFrames++;
            IPPU.RenderThisFrame = FALSE;
        }

        return;
    }

    static uint64_t next1 = 0ULL;
    uint64_t now = GetTicks();

    // If there is no known "next" frame, initialize it now.
    if (next1 == 0ULL)
    {
        next1 = now + 1000ULL;
    }

    // If we're on AUTO_FRAMERATE, we'll display frames always only if there's excess time.
    // Otherwise we'll display the defined amount of frames.
    unsigned limit = (Settings.SkipFrames == AUTO_FRAMERATE) ? (next1 < now ? 10 : 1) : Settings.SkipFrames;

    IPPU.RenderThisFrame = (++IPPU.SkippedFrames >= limit) ? TRUE : FALSE;

    if (IPPU.RenderThisFrame)
        IPPU.SkippedFrames = 0;
    else {
        // If we were behind the schedule, check how much it is.
        if (next1 < now) {
            uint64_t lag = now - next1;
            if (lag >= 500000ULL) {
                // More than a half-second behind means probably pause.
                // The next line prevents the magic fast-forward effect.
                next1 = now;
            }
        }
    }

    // Delay until we're completed this frame.
    // Can't use setitimer because the sound code already could be using it. We don't actually need it either.
    uint64_t deadline = now + Settings.FrameTime; // saving 1 frame time, giving all cpu time to logic layer
    if (next1 > deadline)
        usleep(next1 - deadline);

    // Calculate the timestamp of the next frame.
    next1 += Settings.FrameTime;
}

void SetInfoDlgColor(unsigned char, unsigned char, unsigned char) {
}

void VideoSetOriginResolution() {
    if (screen && (SDL_MUSTLOCK(screen))) SDL_UnlockSurface(screen);
    screenWidth = 320;
    screenHeight = 240;
    screen = SDL_SetVideoMode(screenWidth, screenHeight, 16, videoFlag);
    VideoClear();
    SDL_Flip(screen);
    VideoClear();
#ifdef SDL_TRIPLEBUF
    SDL_Flip(screen);
    VideoClear();
#endif

    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);

    int w = (SNES_WIDTH + 7u) & ~7u;
    int h = (SNES_HEIGHT + 7u) & ~7u;
    renderOffset = (w < 320 ? (320 - w) / 2 : 0) + (h < 240 ? ((240 - h) / 2 * 320) : 0);
    GFX.Screen = (uint16*)screen->pixels + renderOffset;
    GFX.Pitch = screen->pitch;
}

void VideoOutputString(int x, int y, const char *text, bool allowWrap, bool shadow) {
    ttf_font ? ttf_font->render(screen, x, y, text, allowWrap, shadow)
        : VideoOutputStringPixel(x, y, text, allowWrap, shadow);
}

void VideoOutputStringPixel(int x, int y, const char *text, bool allowWrap, bool shadow) {
    static const uint8_t font8x8data[128][8] = {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x3E, 0x41, 0x55, 0x41, 0x55, 0x49, 0x3E },
        { 0x00, 0x3E, 0x7F, 0x6B, 0x7F, 0x6B, 0x77, 0x3E },
        { 0x00, 0x22, 0x77, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 },
        { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08 },
        { 0x00, 0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C },
        { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x08, 0x1C },
        { 0x00, 0x00, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00 },
        { 0xFF, 0xFF, 0xE3, 0xC1, 0xC1, 0xC1, 0xE3, 0xFF },
        { 0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00 },
        { 0xFF, 0xFF, 0xE3, 0xDD, 0xDD, 0xDD, 0xE3, 0xFF },
        { 0x00, 0x0F, 0x03, 0x05, 0x39, 0x48, 0x48, 0x30 },
        { 0x00, 0x08, 0x3E, 0x08, 0x1C, 0x22, 0x22, 0x1C },
        { 0x00, 0x18, 0x14, 0x10, 0x10, 0x30, 0x70, 0x60 },
        { 0x00, 0x0F, 0x19, 0x11, 0x13, 0x37, 0x76, 0x60 },
        { 0x00, 0x08, 0x2A, 0x1C, 0x77, 0x1C, 0x2A, 0x08 },
        { 0x00, 0x60, 0x78, 0x7E, 0x7F, 0x7E, 0x78, 0x60 },
        { 0x00, 0x03, 0x0F, 0x3F, 0x7F, 0x3F, 0x0F, 0x03 },
        { 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x08 },
        { 0x00, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x66 },
        { 0x00, 0x3F, 0x65, 0x65, 0x3D, 0x05, 0x05, 0x05 },
        { 0x00, 0x0C, 0x32, 0x48, 0x24, 0x12, 0x4C, 0x30 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F },
        { 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x3E },
        { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x1C, 0x1C, 0x1C },
        { 0x00, 0x1C, 0x1C, 0x1C, 0x7F, 0x3E, 0x1C, 0x08 },
        { 0x00, 0x08, 0x0C, 0x7E, 0x7F, 0x7E, 0x0C, 0x08 },
        { 0x00, 0x08, 0x18, 0x3F, 0x7F, 0x3F, 0x18, 0x08 },
        { 0x00, 0x00, 0x00, 0x70, 0x70, 0x70, 0x7F, 0x7F },
        { 0x00, 0x00, 0x14, 0x22, 0x7F, 0x22, 0x14, 0x00 },
        { 0x00, 0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x7F, 0x7F },
        { 0x00, 0x7F, 0x7F, 0x3E, 0x3E, 0x1C, 0x1C, 0x08 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18 },
        { 0x00, 0x36, 0x36, 0x14, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36 },
        { 0x00, 0x08, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x08 },
        { 0x00, 0x60, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x06 },
        { 0x00, 0x3C, 0x66, 0x3C, 0x28, 0x65, 0x66, 0x3F },
        { 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00 },
        { 0x00, 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60 },
        { 0x00, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06 },
        { 0x00, 0x00, 0x36, 0x1C, 0x7F, 0x1C, 0x36, 0x00 },
        { 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x60 },
        { 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60 },
        { 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00 },
        { 0x00, 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C },
        { 0x00, 0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E },
        { 0x00, 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E },
        { 0x00, 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C },
        { 0x00, 0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C },
        { 0x00, 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C },
        { 0x00, 0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C },
        { 0x00, 0x7E, 0x66, 0x0C, 0x0C, 0x18, 0x18, 0x18 },
        { 0x00, 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C },
        { 0x00, 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C },
        { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00 },
        { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30 },
        { 0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06 },
        { 0x00, 0x00, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x00 },
        { 0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60 },
        { 0x00, 0x3C, 0x66, 0x06, 0x1C, 0x18, 0x00, 0x18 },
        { 0x00, 0x38, 0x44, 0x5C, 0x58, 0x42, 0x3C, 0x00 },
        { 0x00, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66 },
        { 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C },
        { 0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C },
        { 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C },
        { 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E },
        { 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60 },
        { 0x00, 0x3C, 0x66, 0x60, 0x60, 0x6E, 0x66, 0x3C },
        { 0x00, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66 },
        { 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C },
        { 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38 },
        { 0x00, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66 },
        { 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E },
        { 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63 },
        { 0x00, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63 },
        { 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C },
        { 0x00, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60 },
        { 0x00, 0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x06 },
        { 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66 },
        { 0x00, 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C },
        { 0x00, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x18 },
        { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E },
        { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18 },
        { 0x00, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63 },
        { 0x00, 0x63, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x63 },
        { 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18 },
        { 0x00, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E },
        { 0x00, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E },
        { 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00 },
        { 0x00, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78 },
        { 0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F },
        { 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E },
        { 0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x7C },
        { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C },
        { 0x00, 0x06, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3E },
        { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C },
        { 0x00, 0x1C, 0x36, 0x30, 0x30, 0x7C, 0x30, 0x30 },
        { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C },
        { 0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66 },
        { 0x00, 0x00, 0x18, 0x00, 0x18, 0x18, 0x18, 0x3C },
        { 0x00, 0x0C, 0x00, 0x0C, 0x0C, 0x6C, 0x6C, 0x38 },
        { 0x00, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66 },
        { 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },
        { 0x00, 0x00, 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x6B },
        { 0x00, 0x00, 0x00, 0x7C, 0x7E, 0x66, 0x66, 0x66 },
        { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C },
        { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 },
        { 0x00, 0x00, 0x3C, 0x6C, 0x6C, 0x3C, 0x0D, 0x0F },
        { 0x00, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x60, 0x60 },
        { 0x00, 0x00, 0x00, 0x3E, 0x40, 0x3C, 0x02, 0x7C },
        { 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18 },
        { 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E },
        { 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x3C, 0x18 },
        { 0x00, 0x00, 0x00, 0x63, 0x6B, 0x6B, 0x6B, 0x3E },
        { 0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66 },
        { 0x00, 0x00, 0x00, 0x66, 0x66, 0x3E, 0x06, 0x3C },
        { 0x00, 0x00, 0x00, 0x3C, 0x0C, 0x18, 0x30, 0x3C },
        { 0x00, 0x0E, 0x18, 0x18, 0x30, 0x18, 0x18, 0x0E },
        { 0x00, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18 },
        { 0x00, 0x70, 0x18, 0x18, 0x0C, 0x18, 0x18, 0x70 },
        { 0x00, 0x00, 0x00, 0x3A, 0x6C, 0x00, 0x00, 0x00 },
        { 0x00, 0x08, 0x1C, 0x36, 0x63, 0x41, 0x41, 0x7F }
    };
    uint32_t wrapx = screenWidth - 8;
    while (*text) {
        int pos = 0;
        uint8_t c = *text++;
        if (c > 0x7F) continue;
        uint16_t *ptr = (uint16_t *)screen->pixels + x + y*screenWidth;
        const unsigned char *dataptr = font8x8data[c];
        for (int l = 0; l < 8; l++) {
            unsigned char data = *dataptr++;
            if (shadow) {
                if (data & 0x80u) {
                    ptr[pos + 0] = 0xFFFFu;
                    ptr[pos + 1 + screenWidth] = 0;
                }
                if (data & 0x40u) {
                    ptr[pos + 1] = 0xFFFFu;
                    ptr[pos + 2 + screenWidth] = 0;
                }
                if (data & 0x20u) {
                    ptr[pos + 2] = 0xFFFFu;
                    ptr[pos + 3 + screenWidth] = 0;
                }
                if (data & 0x10u) {
                    ptr[pos + 3] = 0xFFFFu;
                    ptr[pos + 4 + screenWidth] = 0;
                }
                if (data & 0x08u) {
                    ptr[pos + 4] = 0xFFFFu;
                    ptr[pos + 5 + screenWidth] = 0;
                }
                if (data & 0x04u) {
                    ptr[pos + 5] = 0xFFFFu;
                    ptr[pos + 6 + screenWidth] = 0;
                }
                if (data & 0x02u) {
                    ptr[pos + 6] = 0xFFFFu;
                    ptr[pos + 7 + screenWidth] = 0;
                }
                if (data & 0x01u) {
                    ptr[pos + 7] = 0xFFFFu;
                    ptr[pos + 8 + screenWidth] = 0;
                }
            } else {
                if (data & 0x80u) ptr[pos + 0] = 0xFFFFu;
                if (data & 0x40u) ptr[pos + 1] = 0xFFFFu;
                if (data & 0x20u) ptr[pos + 2] = 0xFFFFu;
                if (data & 0x10u) ptr[pos + 3] = 0xFFFFu;
                if (data & 0x08u) ptr[pos + 4] = 0xFFFFu;
                if (data & 0x04u) ptr[pos + 5] = 0xFFFFu;
                if (data & 0x02u) ptr[pos + 6] = 0xFFFFu;
                if (data & 0x01u) ptr[pos + 7] = 0xFFFFu;
            }
            pos += screenWidth;
        }
        x += 8;
        if (x > wrapx) {
            if (!allowWrap) break;
            x = 0;
            y += 12;
        }
    }
}

void VideoSetLogMessage(const char *msg, uint32_t msecs) {
    logDeadline = GetTicks() + msecs * 1000ULL;
    strncpy(logMsg, msg, 255);
    logMsg[255] = 0;
}

void VideoClear() {
    memset(screen->pixels, 0, screen->pitch * screen->h);
}

void VideoClearCache() {
    clearCache = true;
}

void VideoFlip() {
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    SDL_Flip(screen);
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    GFX.Screen = (uint16*)screen->pixels;
}

void VideoFreeze() {
    uint16_t *ptr = (uint16_t*)screen->pixels;
    uint8_t *out = freezeBuffer;
    int pitch = screen->pitch;
    for (int j = freezeHeight; j; --j) {
        uint16_t *inbuf = ptr;
        for (int i = freezeWidth; i; --i) {
            uint16_t c = *inbuf;
            out[0] = (c >> 11) * 0xFF / 0x1F;
            out[1] = ((c >> 5) & 0x3F) * 0xFF / 0x3F;
            out[2] = (c & 0x1F) * 0xFF / 0x1F;
            inbuf += 2;
            out += 3;
        }
        ptr += pitch;
    }
    freezed = true;
}

void VideoUnfreeze() {
    freezed = false;
}

VideoImageData VideoLoadImageFile(const char *filename) {
    int x, y, n;
    uint8_t *image = stbi_load(filename, &x, &y, &n, STBI_rgb);
    if (image == NULL) {
        return VideoImageData { NULL };
    }
    uint16_t *buf = new uint16_t[x * y];
    uint8_t *ptr = image;
    uint16_t *out = buf;
    for (int j = y; j; --j) {
        for (int i = x; i; --i, ptr += 3) {
            *out++ = (ptr[2] >> 3) | ((ptr[1] >> 2) << 5) | ((ptr[0] >> 3) << 11);
        }
    }
    stbi_image_free(image);
    return VideoImageData { buf, x, y };
}

void VideoFreeImage(VideoImageData *data) {
    delete[] data->buffer;
}

void VideoDrawImage(int x, int y, VideoImageData *data) {
    uint16_t *__restrict__ ptr = data->buffer;
    if (!ptr) return;
    int width = data->w;
    int linebytes = data->w * 2;
    int pitch = screen->pitch / screen->format->BytesPerPixel;
    uint16_t *__restrict__ out = (uint16_t*)screen->pixels + y * pitch + x;
    for (int j = data->h; j; --j) {
        memmove(out, ptr, linebytes);
        out += pitch;
        ptr += width;
    }
}

void VideoTakeScreenshot(const char *filename) {
    bool fr = freezed;
    if (!fr) VideoFreeze();
    stbi_write_png(filename, freezeWidth, freezeHeight, STBI_rgb, freezeBuffer, 0);
    if (!fr) VideoUnfreeze();
}

static void predrawMenu() {
    VideoDrawImage(0, 0, &bg);
    VideoOutputString(110, 10, "snes9x " VERSION, false, true);
    VideoOutputString(90, 22, "RG350 port by soarqin", false, true);
    VideoOutputString(64, 34, "Built on " __DATE__ " at " __TIME__, false, true);
}
