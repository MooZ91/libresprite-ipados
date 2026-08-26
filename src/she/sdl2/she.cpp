// SHE library
// Copyright (C) 2021-2026 LibreSprite contributors
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "she/she.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "base/concurrent_queue.h"
#include "base/exception.h"
#include "base/string.h"
#include "she/sdl2/sdl2_display.h"
#include "she/sdl2/sdl2_surface.h"
#include "she/common/system.h"
#include "she/logger.h"

#undef HAVE_STDINT_H
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_hints.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_syswm.h>
#else
#include <SDL.h>
#include <SDL_hints.h>
#include <SDL_image.h>
#include <SDL_syswm.h>
#endif

#include <cmath>
#include <iostream>
#include <cassert>
#include <list>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

#define SDL_HINT_WINDOWS_DPI_AWARENESS "SDL_WINDOWS_DPI_AWARENESS"

float penPressure = 0;

namespace ui {
  float get_pen_pressure() {
    return penPressure;
  }
}

#if defined(__APPLE__) && TARGET_OS_IPHONE
namespace {
  SDL_TouchID pencilTouchId = 0;

  // Our SDL build reports Apple Pencil on a touch device of its own, named
  // "pencil" (see ios/patches/sdl2-ios-pencil.patch); stock SDL cannot tell a
  // pencil from a finger or a resting palm. The device only comes into
  // existence once the first pencil touch arrives, so keep looking until then.
  bool isPencilTouch(SDL_TouchID id) {
    if (!id)
      return false;

    if (!pencilTouchId) {
      for (int i = 0, n = SDL_GetNumTouchDevices(); i < n; ++i) {
        const char* name = SDL_GetTouchName(i);
        if (name && SDL_strcmp(name, "pencil") == 0) {
          pencilTouchId = SDL_GetTouchDevice(i);
          break;
        }
      }
    }

    return pencilTouchId && id == pencilTouchId;
  }

  // Apple Pencil hover reaches SDL as a motion of the *real* mouse
  // (SDL_MouseMotionEvent::which == 0), while a finger or a pencil in
  // contact drives the touch-emulated one (SDL_TOUCH_MOUSEID). Both share a
  // single cursor position, and UIKit fires a hover motion as the pencil
  // lifts that still carries the point the stroke started at. Fed to the
  // still-open tool loop that became the stroke's last point, closing every
  // pencil stroke with a straight line back to its own beginning; it also
  // left SDL's cursor -- and with it the button-up event SDL builds from it
  // -- on the wrong pixel.
  //
  // So while the screen is being touched, hover is ignored, and the release
  // reports the touch's own last position instead of the shared cursor.
  int activeTouches = 0;
  int lastTouchX = 0;
  int lastTouchY = 0;
}
#endif

namespace {

// SDL synthesises the mouse events for a touch and queues them *before* the
// touch's own finger event, so a mouse event on its own carries neither the
// pressure of the touch that caused it nor any way to tell a pencil from a
// finger. Peek at the finger event that follows it.
bool peekTouchPressure(Uint32 fingerEventType, float& pressure)
{
  SDL_Event peeked;
  if (SDL_PeepEvents(&peeked, 1, SDL_PEEKEVENT,
                     fingerEventType, fingerEventType) != 1)
    return false;

#if defined(__APPLE__) && TARGET_OS_IPHONE
  // Only a pencil carries a meaningful pressure. A finger reports a fully
  // pressed touch, which taken as pen pressure would pin the pointer type to
  // Pen and hide the brush preview for good.
  pressure = isPencilTouch(peeked.tfinger.touchId)
    ? std::max<>(peeked.tfinger.pressure, 0.0001f)
    : 0.0f;
#else
  pressure = std::max<>(peeked.tfinger.pressure, 0.0001f);
#endif
  return true;
}

} // anonymous namespace

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
extern "C" {
    void onPointerEvent(int isPen, float pressure) {
	if (isPen) {
	    penPressure = pressure > 0 ? pressure : 0.0001;
	} else {
	    penPressure = 0;
	}
    }
}
extern bool cfginit();
#endif

static she::System* g_instance = nullptr;
static std::unordered_map<int, she::Event::MouseButton> mouseButtonMapping = {
  {SDL_BUTTON_LEFT, she::Event::LeftButton},
  {SDL_BUTTON_MIDDLE, she::Event::MiddleButton},
  {SDL_BUTTON_RIGHT, she::Event::RightButton}
};
static she::KeyScancode lastScancode;
static int lastScancodeSDL;
struct Modifier {
  const int sheModifier;
  int ascii;
  bool isPressed = false;
  Modifier(int sheModifier) : sheModifier(sheModifier) {}
};

static std::unordered_map<int, Modifier*> reverseKeyCodeMapping;

static std::unordered_map<SDL_Keycode, Modifier> keyCodeMapping = {
  {SDLK_UNKNOWN, she::kKeyNil},
  {SDL_Keycode(13), she::kKeyEnter},
  {SDLK_PERIOD, she::kKeyStop},
  {SDLK_a, she::kKeyA},
  {SDLK_b, she::kKeyB},
  {SDLK_c, she::kKeyC},
  {SDLK_d, she::kKeyD},
  {SDLK_e, she::kKeyE},
  {SDLK_f, she::kKeyF},
  {SDLK_g, she::kKeyG},
  {SDLK_h, she::kKeyH},
  {SDLK_i, she::kKeyI},
  {SDLK_j, she::kKeyJ},
  {SDLK_k, she::kKeyK},
  {SDLK_l, she::kKeyL},
  {SDLK_m, she::kKeyM},
  {SDLK_n, she::kKeyN},
  {SDLK_o, she::kKeyO},
  {SDLK_p, she::kKeyP},
  {SDLK_q, she::kKeyQ},
  {SDLK_r, she::kKeyR},
  {SDLK_s, she::kKeyS},
  {SDLK_t, she::kKeyT},
  {SDLK_u, she::kKeyU},
  {SDLK_v, she::kKeyV},
  {SDLK_w, she::kKeyW},
  {SDLK_x, she::kKeyX},
  {SDLK_y, she::kKeyY},
  {SDLK_z, she::kKeyZ},
  {SDLK_0, she::kKey0},
  {SDLK_1, she::kKey1},
  {SDLK_2, she::kKey2},
  {SDLK_3, she::kKey3},
  {SDLK_4, she::kKey4},
  {SDLK_5, she::kKey5},
  {SDLK_6, she::kKey6},
  {SDLK_7, she::kKey7},
  {SDLK_8, she::kKey8},
  {SDLK_9, she::kKey9},
  {SDLK_KP_0, she::kKey0Pad},
  {SDLK_KP_1, she::kKey1Pad},
  {SDLK_KP_2, she::kKey2Pad},
  {SDLK_KP_3, she::kKey3Pad},
  {SDLK_KP_4, she::kKey4Pad},
  {SDLK_KP_5, she::kKey5Pad},
  {SDLK_KP_6, she::kKey6Pad},
  {SDLK_KP_7, she::kKey7Pad},
  {SDLK_KP_8, she::kKey8Pad},
  {SDLK_KP_9, she::kKey9Pad},
  {SDLK_F1, she::kKeyF1},
  {SDLK_F2, she::kKeyF2},
  {SDLK_F3, she::kKeyF3},
  {SDLK_F4, she::kKeyF4},
  {SDLK_F5, she::kKeyF5},
  {SDLK_F6, she::kKeyF6},
  {SDLK_F7, she::kKeyF7},
  {SDLK_F8, she::kKeyF8},
  {SDLK_F9, she::kKeyF9},
  {SDLK_F10, she::kKeyF10},
  {SDLK_F11, she::kKeyF11},
  {SDLK_F12, she::kKeyF12},
  {SDLK_ESCAPE, she::kKeyEsc},
  {SDLK_QUOTE, she::kKeyTilde},
  {SDLK_MINUS, she::kKeyMinus},
  {SDLK_EQUALS, she::kKeyEquals},
  {SDLK_BACKSPACE, she::kKeyBackspace},
  {SDLK_TAB, she::kKeyTab},
  {SDLK_LEFTBRACKET, she::kKeyOpenbrace},
  {SDLK_RIGHTBRACKET, she::kKeyClosebrace},
  {SDLK_KP_ENTER, she::kKeyEnter},
  {SDLK_COLON, she::kKeyColon},
  {SDLK_QUOTE, she::kKeyQuote},
  {SDLK_BACKSLASH, she::kKeyBackslash},
  // {SDLK_BACKSLASH2, she::kKeyBackslash2},
  {SDLK_COMMA, she::kKeyComma},
  {SDLK_STOP, she::kKeyStop},
  {SDLK_SLASH, she::kKeySlash},
  {SDLK_SPACE, she::kKeySpace},
  {SDLK_INSERT, she::kKeyInsert},
  {SDLK_DELETE, she::kKeyDel},
  {SDLK_HOME, she::kKeyHome},
  {SDLK_END, she::kKeyEnd},
  {SDLK_PAGEUP, she::kKeyPageUp},
  {SDLK_PAGEDOWN, she::kKeyPageDown},
  {SDLK_LEFT, she::kKeyLeft},
  {SDLK_RIGHT, she::kKeyRight},
  {SDLK_UP, she::kKeyUp},
  {SDLK_DOWN, she::kKeyDown},
  {SDLK_KP_DIVIDE, she::kKeySlashPad},
  {SDLK_ASTERISK, she::kKeyAsterisk},
  {SDLK_KP_MINUS, she::kKeyMinusPad},
  {SDLK_KP_PLUS, she::kKeyPlusPad},
  // {SDLK_KP_DEL, she::kKeyDelPad},
  {SDLK_KP_PERIOD, she::kKeyDelPad},
  {SDLK_KP_ENTER, she::kKeyEnterPad},
  {SDLK_PRINTSCREEN, she::kKeyPrtscr},
  {SDLK_PAUSE, she::kKeyPause},
  // {SDLK_ABNTC1, she::kKeyAbntC1},
  // {SDLK_YEN, she::kKeyYen},
  // {SDLK_KANA, she::kKeyKana},
  // {SDLK_CONVERT, she::kKeyConvert},
  // {SDLK_NOCONVERT, she::kKeyNoconvert},
  {SDLK_AT, she::kKeyAt},
  // {SDLK_CIRCUMFLEX, she::kKeyCircumflex},
  // {SDLK_COLON2, she::kKeyColon2},
  // {SDLK_KANJI, she::kKeyKanji},
  {SDLK_KP_EQUALS, she::kKeyEqualsPad},
  {SDLK_BACKQUOTE, she::kKeyBackquote},
  {SDLK_SEMICOLON, she::kKeySemicolon},
  // {SDLK_COMMAND, she::kKeyCommand},
  // {SDLK_UNKNOWN1, she::kKeyUnknown1},
  // {SDLK_UNKNOWN2, she::kKeyUnknown2},
  // {SDLK_UNKNOWN3, she::kKeyUnknown3},
  // {SDLK_UNKNOWN4, she::kKeyUnknown4},
  // {SDLK_UNKNOWN5, she::kKeyUnknown5},
  // {SDLK_UNKNOWN6, she::kKeyUnknown6},
  // {SDLK_UNKNOWN7, she::kKeyUnknown7},
  // {SDLK_UNKNOWN8, she::kKeyUnknown8},
  {SDLK_LSHIFT, she::kKeyLShift},
  {SDLK_RSHIFT, she::kKeyRShift},
  {SDLK_LCTRL, she::kKeyLControl},
  {SDLK_RCTRL, she::kKeyRControl},
  {SDLK_LALT, she::kKeyAlt},
  {SDLK_RALT, she::kKeyAltGr},
  {SDL_Keycode(1073742051), she::kKeyLWin},
  // {SDLK_RWIN, she::kKeyRWin},
  {SDLK_MENU, she::kKeyMenu},
  {SDLK_SCROLLLOCK, she::kKeyScrLock},
  {SDLK_NUMLOCKCLEAR, she::kKeyNumLock},
  {SDLK_CAPSLOCK, she::kKeyCapsLock},
};

std::unordered_map<SDL_Keycode, Modifier> modifiers = {
  {SDLK_SPACE, she::kKeySpaceModifier},

  {SDLK_LALT, she::kKeyAltModifier},
  {SDLK_RALT, she::kKeyAltModifier},

  {SDLK_LCTRL, she::kKeyCtrlModifier},
  {SDLK_RCTRL, she::kKeyCtrlModifier},

  {SDLK_LGUI, she::kKeyCmdModifier},
  {SDLK_RGUI, she::kKeyCmdModifier},

  {SDLK_LSHIFT, she::kKeyShiftModifier},
  {SDLK_RSHIFT, she::kKeyShiftModifier}
};

she::KeyModifiers getSheModifiers() {
  int mod = 0;
  for (auto& entry : modifiers) {
    if (entry.second.isPressed)
      mod |= entry.second.sheModifier;
  }
  return (she::KeyModifiers) mod;
}

#ifdef __EMSCRIPTEN__
EM_JS(int, get_canvas_width, (), { return canvas.clientWidth; });
EM_JS(int, get_canvas_height, (), { return canvas.clientHeight; });
static int oldWidth, oldHeight;

static void addEventListener(const std::string& name, void (*function)(void*), void* data = nullptr) {
  EM_ASM({
    canvas.addEventListener(UTF8ToString($0), (event) => {
      window.event = event;
      dynCall('vi', $1, [$2]);
    });
  }, name.c_str(), function, data);
}

static void cancelEvent(void*) {
  EM_ASM({
    event.stopPropagation();
    event.preventDefault();
  });
}

static bool wrapped;
static void patchEventListeners() {
  if (wrapped)
    return;
  wrapped = EM_ASM_INT({
    let handle = 0;
    JSEvents.eventHandlers.forEach(handler => {
	if (handler.eventTypeString == 'keydown' || handler.eventTypeString == 'keyup') {
          handle = handler.callbackfunc;
	  let tmp = getWasmTableEntry(handle);
	  function wrapper(...args) {
	    let ret = tmp(...args);
	    let keyCode = GROWABLE_HEAP_I32()[(args[1] >> 2) + 9];
	    return ret && keyCode != 86;
	  }
	  if (tmp.name != wrapper.name)
	    wasmTableMirror[handle] = wrapper;
	}
    });
    return !!handle;
  });
}

#endif

static std::deque<she::Event> keybuffer;
static bool display_has_mouse = false;
namespace {

// Pointer events arrive in the windowing system's coordinates, which on a
// HiDPI display are not the surface's pixels. Scale them the way the surface
// itself is scaled, so a touch lands where it looks like it landed.
// A second finger on the screen means the user is pinching, not drawing. SDL
// keeps synthesising mouse events from the first finger throughout, so the
// stroke has to be closed and the rest of them dropped until the hand lifts.
bool gestureActive = false;

int toSurfaceCoord(int v)
{
  return (int)(v * she::unique_display->pointToPixel() / she::unique_display->scale());
}

} // anonymous namespace

namespace she {
  void log(const std::string& text) {
#if defined(ANDROID)
    SDL_Log("%s", text.c_str());
#endif
  }

  namespace sdl {
    bool isMaximized;
    bool isMinimized;
    extern std::unordered_map<int, SDL2Display*> windowIdToDisplay;
  }

  class SDL2EventQueue : public EventQueue {
  public:
    PointerType pointerType = PointerType::Mouse;
    std::chrono::steady_clock::time_point lastUpTime = std::chrono::steady_clock::now();

    SDL2EventQueue() {
#if defined(__EMSCRIPTEN__)
        EM_ASM(
            const onPointerEvent = Module.cwrap("onPointerEvent", "", ["number", "number"]);
	    const listener = event => {
		onPointerEvent((event.pointerType == "pen")|0, event.pressure);
	    };
            Module.canvas.addEventListener("pointerdown", listener);
	    Module.canvas.addEventListener("pointermove", listener);
	    Module.canvas.addEventListener("pointerup", listener);
            );
#endif
      if (reverseKeyCodeMapping.empty()) {
        for (auto& entry : keyCodeMapping) {
          reverseKeyCodeMapping[entry.second.sheModifier] = &entry.second;
          entry.second.ascii = entry.first;
        }
      }
    }

    void forceFlip() {
      for (auto& entry : sdl::windowIdToDisplay) {
        entry.second->flip({
            0,
            0,
            entry.second->width(),
            entry.second->height()
          });
        entry.second->present();
      }
    }

    void refresh() {
      if (!m_events.empty())
	return;
      Event event;
      while (true) {
	event.setType(Event::None);
	getEventInternal(event, false);
	if (event.type() == Event::None) {
	  return;
	}
	m_events.push(event);
      }
    }

    void getEvent(Event& event, bool) override {
      event.setType(Event::None);
      if (m_events.try_pop(event))
        return;
      if (she::instance()->isGfxThread())
	getEventInternal(event, false);
    }

    void getEventInternal(Event& event, bool) {
      SDL_Event sdlEvent;
      while (SDL_PollEvent(&sdlEvent)) {
        switch (sdlEvent.type) {
        case SDL_APP_DIDENTERFOREGROUND:
          SDL2Surface::textureGen++;
          forceFlip();
          continue;

	case SDL_SYSWMEVENT:
#if defined(EASYTAB_H)
#if defined(_WIN32)
	  {
	    auto& win = sdlEvent.syswm.msg->msg.win;
	    if (EasyTab_HandleEvent(win.hwnd, win.msg, win.lParam, win.wParam) == EASYTAB_OK) {
		penPressure = std::max<>(EasyTab->Pressure, 0.0001f);
	    }
	  }
#endif
#if defined(__linux__)
	    if (EasyTab_HandleEvent(&sdlEvent.syswm.msg->msg.x11.event) == EASYTAB_OK) {
		penPressure = std::max(EasyTab->Pressure, 0.0001f);
	    }
#endif
#endif
	    continue;

        case SDL_WINDOWEVENT:
          switch (sdlEvent.window.event) {
          case SDL_WINDOWEVENT_EXPOSED:
            forceFlip();
            continue;
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            continue;

          case SDL_WINDOWEVENT_MAXIMIZED:
            sdl::isMaximized = true;
            sdl::isMinimized = false;
            std::cout << "Maximized" << std::endl;
            continue;

          case SDL_WINDOWEVENT_MINIMIZED:
            sdl::isMaximized = false;
            sdl::isMinimized = true;
            std::cout << "Minimized" << std::endl;
            continue;

          case SDL_WINDOWEVENT_RESTORED:
            sdl::isMaximized = false;
            sdl::isMinimized = false;
            std::cout << "Restored" << std::endl;
            continue;

          case SDL_WINDOWEVENT_RESIZED: {
	    #ifdef __EMSCRIPTEN__
	    continue;
	    #else
            auto display = sdl::windowIdToDisplay[sdlEvent.window.windowID];
            int newWidth = sdlEvent.window.data1;
            int newHeight = sdlEvent.window.data2;
#if defined(__APPLE__) && TARGET_OS_IPHONE
            // data1/data2 come in points, but on iOS the display is measured
            // in pixels with the screen density folded into its scale (see
            // SDL2Display's constructor). Mixing the two units here would
            // resize the surface to a fraction of the screen on every
            // rotation, so ask SDL for the pixel size instead.
            SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(sdlEvent.window.windowID),
                                      &newWidth, &newHeight);
#endif
            display->setWidth(newWidth);
            display->setHeight(newHeight);
            display->recreateSurface();
            event.setType(Event::ResizeDisplay);
            event.setDisplay(display);
            return;
	    #endif
          }

          case SDL_WINDOWEVENT_LEAVE: {
            if (display_has_mouse) {
              display_has_mouse = false;

              Event ev;
              ev.setType(Event::MouseLeave);
              m_events.push(ev);
              break;
            }
          }

          default:
            std::cout << "Unknown windowevent: " << (int) sdlEvent.window.event << std::endl;
            continue;
          }
          continue;

        case SDL_MOUSEMOTION:
          if (gestureActive)
            continue;
#if defined(__APPLE__) && TARGET_OS_IPHONE
          if (sdlEvent.motion.which == SDL_TOUCH_MOUSEID) {
            lastTouchX = sdlEvent.motion.x;
            lastTouchY = sdlEvent.motion.y;
          }
          else if (activeTouches > 0) {
            // Hover on top of a stroke: see the note by activeTouches.
            continue;
          }
#endif
          if (!display_has_mouse) {
            display_has_mouse = true;
            Event ev;
            ev.setType(Event::MouseEnter);
            m_events.push(ev);
          }

          event.setType(Event::MouseMove);
          event.setModifiers(getSheModifiers());
          event.setPosition({
              toSurfaceCoord(sdlEvent.motion.x),
              toSurfaceCoord(sdlEvent.motion.y)
            });

	  {
	      float pressure;
	      if (peekTouchPressure(SDL_FINGERMOTION, pressure))
		  penPressure = pressure;
	  }


	  event.setPressure(penPressure);
	  event.setPointerType(pointerType);
          return;

        case SDL_FINGERDOWN:
#if defined(__APPLE__) && TARGET_OS_IPHONE
          ++activeTouches;
#endif
          if (!gestureActive &&
              SDL_GetNumTouchFingers(sdlEvent.tfinger.touchId) >= 2) {
            gestureActive = true;
            // The first finger has been drawing up to now. Release the button
            // so the tool loop closes instead of being left half-way through
            // a stroke while the pinch runs.
            {
              int x = 0, y = 0;
#if defined(__APPLE__) && TARGET_OS_IPHONE
              x = lastTouchX;
              y = lastTouchY;
#else
              SDL_GetMouseState(&x, &y);
#endif
              Event ev;
              ev.setType(Event::MouseUp);
              ev.setPosition({toSurfaceCoord(x), toSurfaceCoord(y)});
              ev.setButton(Event::LeftButton);
              ev.setModifiers(getSheModifiers());
              m_events.push(ev);
            }
          }
          // fallthrough
        case SDL_FINGERMOTION:
#if defined(__APPLE__) && TARGET_OS_IPHONE
          // Only a pencil carries a meaningful pressure. A finger reports a
          // fully pressed touch, which taken as pen pressure would pin the
          // pointer type to Pen and hide the brush preview for good.
          penPressure = isPencilTouch(sdlEvent.tfinger.touchId)
            ? std::max<>(sdlEvent.tfinger.pressure, 0.0001f)
            : 0.0f;
#else
          penPressure = std::max<>(sdlEvent.tfinger.pressure, 0.0001f);
#endif
          continue;

        case SDL_FINGERUP:
#if defined(__APPLE__) && TARGET_OS_IPHONE
          if (activeTouches > 0)
            --activeTouches;
#endif
          // SDL still counts the finger that is lifting, so 1 here means the
          // screen is about to be empty. Waiting for that keeps the finger
          // left behind by a pinch from carrying on as a stroke.
          if (gestureActive &&
              SDL_GetNumTouchFingers(sdlEvent.tfinger.touchId) <= 1)
            gestureActive = false;
          // Nothing used to clear this outside the emscripten path, so once a
          // pen had touched the screen every later stroke stayed in pen mode
          // at whatever pressure was last seen, and the brush preview -- which
          // hides itself while a pen is down -- never came back.
          penPressure = 0.0f;
          continue;

        case SDL_MOUSEWHEEL:
          event.setType(Event::MouseWheel);
          event.setModifiers(getSheModifiers());
          event.setWheelDelta({-sdlEvent.wheel.x, -sdlEvent.wheel.y});
          int x, y;
          SDL_GetMouseState(&x, &y);
          event.setPosition({
              toSurfaceCoord(x),
              toSurfaceCoord(y)
            });
          return;

        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEBUTTONDOWN: {
          if (gestureActive)
            continue;
          auto type = sdlEvent.type == SDL_MOUSEBUTTONDOWN ? Event::MouseDown : Event::MouseUp;
          int buttonX = sdlEvent.button.x;
          int buttonY = sdlEvent.button.y;
#if defined(__APPLE__) && TARGET_OS_IPHONE
          // SDL fills these in from the cursor position it shares with
          // hover, and the hover motion that fires as the pencil lifts is
          // stale, so the release would land on the point the stroke began
          // at. The press is trustworthy the other way around: SDL sets the
          // cursor from the touch itself immediately before sending it. It
          // may not *post* a motion event for that -- when hover had already
          // moved the cursor to the same pixel, SDL drops it as a no-op --
          // which is why the press is what seeds the remembered position
          // rather than a motion event.
          if (sdlEvent.button.which == SDL_TOUCH_MOUSEID) {
            if (sdlEvent.type == SDL_MOUSEBUTTONDOWN) {
              lastTouchX = buttonX;
              lastTouchY = buttonY;
            }
            else {
              buttonX = lastTouchX;
              buttonY = lastTouchY;
            }
          }
#endif
          event.setType(type);
          event.setPosition({
              toSurfaceCoord(buttonX),
              toSurfaceCoord(buttonY)
            });
          event.setButton(mouseButtonMapping[sdlEvent.button.button]);
          event.setModifiers(getSheModifiers());

#if defined(__APPLE__) && TARGET_OS_IPHONE
	  if (sdlEvent.type == SDL_MOUSEBUTTONDOWN) {
	    // The press is queued ahead of its own SDL_FINGERDOWN, so at this
	    // point penPressure is still whatever the previous stroke left --
	    // zero, once its release had cleared it. Deciding the pointer type
	    // from that left the first stroke of a session on Mouse, and ui
	    // reads a Mouse pointer's pressure as a flat 1.0, so the pencil
	    // drew with no pressure at all until a stroke had been finished.
	    float pressure;
	    if (peekTouchPressure(SDL_FINGERDOWN, pressure))
	      penPressure = pressure;
	  }
#endif

	  if (penPressure > 0.0f) {
	    pointerType = PointerType::Pen;
	    event.setPressure(penPressure);
	  } else {
	    // Assigning the type before reporting it matters: reporting the
	    // old one first left it latched to Pen for every later event, once
	    // a pen release had set it.
	    pointerType = PointerType::Mouse;
	    event.setPressure(sdlEvent.type == SDL_MOUSEBUTTONDOWN ? 1.0f : 0.0f);
	  }
	  event.setPointerType(pointerType);

	  auto now = std::chrono::steady_clock::now();
	  auto delta = now - lastUpTime;
          if (sdlEvent.type == SDL_MOUSEBUTTONUP) {
	    using namespace std::chrono_literals;
	    if (delta < 200ms) {
	      m_events.push(event);
	      event.setType(Event::MouseDoubleClick);
	      event.setPosition(event.position());
	      event.setButton(event.button());
	    }
	    lastUpTime = now;
          }

          return;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          Event event;
          bool isPressed = sdlEvent.type == SDL_KEYDOWN;
          auto modifierIt = modifiers.find((SDL_Keycode) sdlEvent.key.keysym.sym);
          if (modifierIt != modifiers.end()) {
            modifierIt->second.isPressed = sdlEvent.type == SDL_KEYDOWN;
          }

          auto it = keyCodeMapping.find((SDL_Keycode) sdlEvent.key.keysym.sym);

          if (it == keyCodeMapping.end()) {
            std::cout << "Unknown scancode: " << sdlEvent.key.keysym.sym << std::endl;
            continue;
          }

          event.setType(isPressed ? Event::KeyDown : Event::KeyUp);
          auto modifiers = getSheModifiers();
          event.setModifiers(modifiers);
          it->second.isPressed = isPressed;
          auto scancode = static_cast<she::KeyScancode>(it->second.sheModifier);
          event.setScancode(scancode);
          if (isPressed) {
            lastScancode = scancode;
            lastScancodeSDL = sdlEvent.key.keysym.scancode;
          }
          if (sdlEvent.key.repeat) {
            event.setRepeat(sdlEvent.key.repeat);
          }
          keybuffer.push_back(event);
          if (modifiers & (she::kKeyCtrlModifier | she::kKeyCmdModifier)) {
            SDL_StopTextInput();
            break;
          } else if (!SDL_IsTextInputActive()) {
            SDL_StartTextInput();
          }
          continue;
        }

        case SDL_DROPFILE: {
          std::string file(sdlEvent.drop.file);
          event.setType(Event::DropFiles);
          event.setFiles({file});
          SDL_free(sdlEvent.drop.file);
          return;
        }

          // CloseDisplay,
          // ResizeDisplay,
          // MouseEnter,
          // MouseLeave,
          // TouchMagnify,
        case SDL_MULTIGESTURE:
          // dDist is the change in the distance between the fingers, given as
          // a fraction of the window, which is already the relative delta that
          // TouchMagnify wants. The factor only sets how far a pinch travels
          // before the zoom follows; it was picked by feel on an iPad.
          if (sdlEvent.mgesture.numFingers == 2 &&
              std::fabs(sdlEvent.mgesture.dDist) > 0.0001f) {
            event.setType(Event::TouchMagnify);
            event.setMagnification(sdlEvent.mgesture.dDist * 6.0);
            // x and y are normalised to the window, so they need the surface's
            // size rather than the point-to-pixel conversion the others use.
            event.setPosition({
                int(sdlEvent.mgesture.x * unique_display->width() /
                    unique_display->scale()),
                int(sdlEvent.mgesture.y * unique_display->height() /
                    unique_display->scale())
              });
            event.setModifiers(getSheModifiers());
            return;
          }
          continue;

        case SDL_QUIT:
          event.setType(Event::CloseDisplay);
          return;

        case SDL_TEXTEDITING:
          continue;

        case SDL_TEXTINPUT: {
          keybuffer.clear();
          std::string textString = sdlEvent.text.text;
          base::utf8_const_iterator begin{textString.begin()};
          base::utf8_const_iterator end{textString.end()};
          Event event;
          event.setModifiers(getSheModifiers());
          for (auto it = begin; it != end; ++it) {
            event.setType(Event::KeyDown);
            event.setUnicodeChar(*it);
            if (lastScancodeSDL > SDL_SCANCODE_UNKNOWN && lastScancodeSDL < SDL_SCANCODE_RETURN) {
              event.setScancode(lastScancode);
              lastScancodeSDL = SDL_SCANCODE_UNKNOWN;
            }
            keybuffer.push_back(event);
            event.setType(Event::KeyUp);
            keybuffer.push_back(event);
          }

          break;
        }

        case SDL_KEYMAPCHANGED:
          continue;

        default:
          std::cout << "Unknown event: " << sdlEvent.type << std::endl;
          continue;
        }
      }

      if (!keybuffer.empty()) {
        event = keybuffer.front();
        keybuffer.pop_front();
        return;
      }
    }

    void queueEvent(const Event& event) override {
      m_events.push(event);
    }

  private:
    base::concurrent_queue<Event> m_events;
  };

  EventQueue* EventQueue::instance() {
    static SDL2EventQueue g_queue;
    return &g_queue;
  }

  class SDL2System : public CommonSystem {
  public:
    SDL2System() {
      g_instance = this;
    }

    ~SDL2System() {
      shutdown = true;
      sleeping = false;
      if (mainThread.joinable())
	mainThread.join();
      IMG_Quit();
      SDL_Quit();
      g_instance = nullptr;
    }

    bool shutdown{false};
    std::thread mainThread;
    std::thread::id mainThreadId;
    std::thread::id gfxThreadId;
    std::vector<std::function<void()>> gfxQueue;
    std::atomic<bool> sleeping{false};

    bool isGfxThread() override {
      return std::this_thread::get_id() == gfxThreadId;
    }

    bool isMainThread() override {
      return std::this_thread::get_id() == mainThreadId;
    }

    void gfx(std::function<void()>&& func, bool sleep) override {
      if (isGfxThread()) {
	func();
	return;
      }
      gfxQueue.emplace_back(std::move(func));
      if (sleep)
	this->sleep();
    }

    using Timestamp = std::chrono::high_resolution_clock::time_point;
    Timestamp start = std::chrono::high_resolution_clock::now();

    void sleep() override {
      using namespace std::chrono_literals;
      if (shutdown)
	return;

      if (mainThreadId == gfxThreadId) {
	refresh();
	auto now = std::chrono::high_resolution_clock::now();

	// If the dispatching of messages was faster than 10 milliseconds,
	// it means that the process is not using a lot of CPU, so we can
	// wait the difference to cover those 10 milliseconds
	// sleeping. With this code we can avoid 100% CPU usage.
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
	start = now;

	if (elapsed < 15ms)
	  std::this_thread::sleep_for(15ms - elapsed);
      } else if (isMainThread()) {
	sleeping = true;
	while (sleeping) {
	  using namespace std::chrono_literals;
	  std::this_thread::sleep_for(10ms);
	}
      }
    }

    std::function<int()> m_func;
    int run(std::function<int()>&& func) override {
      gfxThreadId = std::this_thread::get_id();
      #ifndef EMSCRIPTEN
      mainThreadId = gfxThreadId;
      return func();
      #elif defined(EMSCRIPTEN) && !defined(__EMSCRIPTEN__)
      m_func = std::move(func);
      // like emscripten, but not really
      mainThread = std::thread{[this]{
        mainThreadId = std::this_thread::get_id();
	m_func();
      }};
      while (!shutdown)(
	refresh();
      }
      #else
      m_func = std::move(func);

      addEventListener("dragenter", cancelEvent);
      addEventListener("dragover", cancelEvent);
      addEventListener("drop", [](void*){
	EM_ASM({
	  event.stopPropagation();
	  event.preventDefault();
	  let files = event.dataTransfer?.files;
	  if (files?.length) {
	    for (let i = 0; i < files.length; ++i) {
	      let fr = new FileReader();
	      fr.onload = ((fr, name)=>{
		  try{ FS.mkdir('/tmp'); }catch(ex){}
		  FS.writeFile('/tmp/' + name, new Uint8Array(fr.result));
		  canvas.dispatchEvent(new CustomEvent("readFile", {detail:{path:'/tmp/' + name}}));
		}).bind(null, fr, files[i].name);
	      fr.readAsArrayBuffer(files[i]);
	    }
	  }
        });
      });

      addEventListener("readFile", [](void*){
	auto str = (char*) EM_ASM_PTR({return stringToNewUTF8(event.detail.path)});
	std::string path = str;
	free(str);
	Event event;
	event.setType(Event::DropFiles);
	event.setFiles({path});
	static_cast<SDL2EventQueue*>(EventQueue::instance())->queueEvent(event);
      });

      emscripten_set_main_loop([]{
	  if (!cfginit())
	      return;
	  auto sys = static_cast<SDL2System*>(g_instance);
	  sys->mainThread = std::thread{[]{
	      auto sys = static_cast<SDL2System*>(g_instance);
	      sys->mainThreadId = std::this_thread::get_id();
	      sys->m_func();
	  }};
	  emscripten_cancel_main_loop();
	  emscripten_set_main_loop([]{
	      patchEventListeners();
	      static_cast<SDL2System*>(g_instance)->refresh();
	  }, 0, true);
      }, 0, true);
      #endif
      return 0;
    }

    void refresh() {
      if (!sleeping) {
	static_cast<SDL2EventQueue*>(EventQueue::instance())->refresh();
	return;
      }
      #ifdef __EMSCRIPTEN__
      auto width = get_canvas_width();
      auto height = get_canvas_height();
      if (width && height && (oldWidth != width || oldHeight != height) && !sdl::windowIdToDisplay.empty()) {
	oldWidth = width;
	oldHeight = height;
	for (auto& entry : sdl::windowIdToDisplay) {
	  auto display = entry.second;
	  SDL_SetWindowSize(display->m_window, width, height);
	  display->setWidth(width);
	  display->setHeight(height);
	  display->recreateSurface();
	  Event event;
	  event.setType(Event::ResizeDisplay);
	  event.setDisplay(display);
	  static_cast<SDL2EventQueue*>(EventQueue::instance())->queueEvent(event);
	}
      }
      #endif
      int frames = 5;
      do {
	for (auto it = gfxQueue.begin(); it != gfxQueue.end(); ++it) {
	  (*it)();
	}
	gfxQueue.clear();
	sleeping = false;
	for (auto& entry : sdl::windowIdToDisplay)
	  entry.second->present();
	static_cast<SDL2EventQueue*>(EventQueue::instance())->refresh();
      } while (sleeping && --frames);
    }

    void activateApp() override {
      // Do nothing
    }

    void finishLaunching() override {
      // Do nothing
    }

    Capabilities capabilities() const override {
      return (Capabilities)(int(Capabilities::CanResizeDisplay) | int(Capabilities::GpuAccelerationSwitch));
    }

    EventQueue* eventQueue() override { // TODO remove this function
      return EventQueue::instance();
    }

    bool gpuAcceleration() const override {
      return SDL2Display::gpu;
    }

    void setGpuAcceleration(bool state) override {
      if (!unique_display)
        SDL2Display::gpu = state;
    }

    gfx::Size defaultNewDisplaySize() override {
      return gfx::Size(0, 0);
    }

    Display* defaultDisplay() override {
      return unique_display;
    }

    Display* createDisplay(int width, int height, int scale) override {
      //LOG("Creating display %dx%d (scale = %d)\n", width, height, scale);
      return new SDL2Display(width, height, scale);
    }

    Surface* createSurface(int width, int height) override {
      return new SDL2Surface(width, height, SDL2Surface::DeleteAndDestroy);
    }

    Surface* createRgbaSurface(int width, int height) override {
      return new SDL2Surface(width, height, 32, SDL2Surface::DeleteAndDestroy);
    }

    std::vector<uint8_t> encodeSurfaceAsPNG(Surface* s) override {
      auto surface = static_cast<SDL2Surface*>(s);
      std::vector<uint8_t> data;
      data.resize(surface->width() * surface->height() * 4 + 1024);
      std::shared_ptr<SDL_RWops> rops{
        SDL_RWFromMem(data.data(), data.size()),
        [](auto *rops){ rops->close(rops); }
      };
      if (IMG_SavePNG_RW(static_cast<SDL_Surface*>(surface->nativeHandle()), rops.get(), 0) != 0)
        return {};
      data.resize(SDL_RWtell(rops.get()));
      return data;
    }

    Surface* loadSurface(const char* filename) override {
      SDL_Surface* bmp = IMG_Load(filename);
      if (!bmp)
	throw std::runtime_error(std::string{"Error loading image "} + filename);
      return new SDL2Surface(bmp, SDL2Surface::DeleteAndDestroy);
    }

    Surface* loadRgbaSurface(const char* filename) override {
      SDL_Surface* bmp = IMG_Load(filename);
      if (!bmp)
	throw std::runtime_error(std::string{"Error loading image "} + filename);
      if (bmp->format->BitsPerPixel < 32) {
        auto copy = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_RGBA8888, 0);
        SDL_FreeSurface(bmp);
        bmp = copy;
      }
      return new SDL2Surface(bmp, SDL2Surface::DeleteAndDestroy);
    }

  };

  System* create_system() {
    return new SDL2System();
  }

  System* instance()
  {
    return g_instance;
  }

  void error_message(const char* msg)
  {
    if (g_instance && g_instance->logger())
      g_instance->logger()->logError(msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PACKAGE, msg, nullptr);
  }

  int scancode_to_ascii(KeyScancode scancode) {
    auto it = reverseKeyCodeMapping.find(scancode);
    if (it == reverseKeyCodeMapping.end())
      return 0;
    return it->second->ascii;
  }

  bool is_key_pressed(KeyScancode scancode) {
    auto it = reverseKeyCodeMapping.find(scancode);
    if (it != reverseKeyCodeMapping.end()) {
      return it->second->isPressed;
    }
    return false;
  }

  void set_input_rect(const gfx::Rect& rect) {
    if (rect.isEmpty()) {
      SDL_StopTextInput();
      return;
    }
    // The rect arrives in surface coordinates. SDL measures the window in
    // its own units, which on a HiDPI display are neither those nor pixels.
    // Handing it surface coordinates makes iOS think the field sits far below
    // the keyboard, and it scrolls the whole view off the top of the screen.
    const double f = unique_display
      ? unique_display->scale() / unique_display->pointToPixel()
      : 1.0;
    SDL_Rect sdlRect{
      (int)(rect.x * f),
      (int)(rect.y * f),
      (int)(rect.w * f),
      (int)(rect.h * f)
    };
    SDL_SetTextInputRect(&sdlRect);
    SDL_StartTextInput();
  }

  void clear_keyboard_buffer() {
    keybuffer.clear();
  }

} // namespace she

// It must be defined by the user program code.
extern int app_main(int argc, char* argv[]);

int main(const int argc, char* argv[]) {
  #ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
  #endif
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

  // If the requested DPI awareness is not available on the currently running OS,
  // SDL will try to request the best available match.
  // https://wiki.libsdl.org/SDL2/SDL_HINT_WINDOWS_DPI_AWARENESS
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::cerr << "Critical: Could not initialize SDL2. Aborting." << std::endl;
    return -1;
  }
  if (!IMG_Init(-1)) {
    std::cerr << "Critical: Could not initialize SDL2_image (" << IMG_GetError() << "). Aborting." << std::endl;
    return -2;
  }
  SDL_EventState(SDL_FINGERMOTION, SDL_ENABLE);
  return app_main(argc, argv);
}
