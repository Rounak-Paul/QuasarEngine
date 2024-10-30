#pragma once
#include <qspch.h>
#include <Core/System.h>

#define MAX_MESSAGE_CODES 16384

namespace Quasar
{
    typedef struct event_context {
        // 128 bytes
        union {
            i64 i64[2];
            u64 u64[2];
            f64 f64[2];

            i32 i32[4];
            u32 u32[4];
            f32 f32[4];

            i16 i16[8];
            u16 u16[8];

            i8 i8[16];
            u8 u8[16];

            char c[16];
        } data;
    } event_context;

    typedef b8 (*PFN_on_event)(u16 code, void* sender, void* listener_inst, event_context data);

    typedef struct registered_event {
        void* listener;
        PFN_on_event callback;
    } registered_event;

    typedef struct event_code_entry {
        std::vector<registered_event> events;
    } event_code_entry;

    // State structure.
    typedef struct event_system_state {
        // Lookup table for event codes.
        event_code_entry registered[MAX_MESSAGE_CODES];
    } event_system_state;

    class QS_API Event : public System {
        public:
        Event() {};
        ~Event() = default;

        virtual b8 init(void* config) override;
        virtual void shutdown() override;

        b8 Register(u16 code, void* listener, PFN_on_event on_event);
        b8 Unregister(u16 code, void* listener, PFN_on_event on_event);
        b8 Execute(u16 code, void* sender, event_context context);

        private:
        event_system_state event_state;
    };

    // System internal event codes. Application should use codes beyond 255.
    typedef enum system_event_code {
        // Shuts the application down on the next frame.
        EVENT_CODE_APPLICATION_QUIT = 0x01,

        // Resized/resolution changed from the OS.
        /* Context usage:
        * u16 width = data.data.u16[0];
        * u16 height = data.data.u16[1];
        */
        EVENT_CODE_RESIZED = 0x02,

        // Change the render mode for debugging purposes.
        /* Context usage:
        * i32 mode = context.data.i32[0];
        */
        EVENT_CODE_SET_RENDER_MODE = 0x0F,

        EVENT_CODE_DEBUG0 = 0x10,
        EVENT_CODE_DEBUG1 = 0x11,
        EVENT_CODE_DEBUG2 = 0x12,
        EVENT_CODE_DEBUG3 = 0x13,
        EVENT_CODE_DEBUG4 = 0x14,

        /** @brief The hovered-over object id, if there is one.
         * Context usage:
         * i32 id = context.data.u32[0]; - will be INVALID ID if nothing is hovered over.
         */
        EVENT_CODE_OBJECT_HOVER_ID_CHANGED = 0x15,

        /** 
         * @brief An event fired by the renderer backend to indicate when any render targets
         * associated with the default window resources need to be refreshed (i.e. a window resize)
         */
        EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED = 0x16,

        /**
         * @brief An event fired when a watched file has been written to.
         * Context usage:
         * u32 watch_id = context.data.u32[0];
         */
        EVENT_CODE_WATCHED_FILE_WRITTEN = 0X17,

        /**
         * @brief An event fired when a watched file has been removed.
         * Context usage:
         * u32 watch_id = context.data.u32[0];
         */
        EVENT_CODE_WATCHED_FILE_DELETED = 0x18,

        /**
         * @brief An event fired when window is in focus.
         * Context usage:
         * i32 in_focus = context.data.i32[0];
         */
        EVENT_CODE_WINDOW_FOCUS_CHANGED = 0x19,

        EVENT_CODE_MAX = 0xFF
    } system_event_code;
} // namespace Quasar