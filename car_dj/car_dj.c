#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <furi_ble/ams_client.h>

// Shared with the Remote app, so a phone paired there reconnects here without pairing again
#define KEYS_PATH        EXT_PATH("apps_data/hid_ble/.bt_hid.keys")
#define SKIP_COOLDOWN_MS 5000
#define VOLUME_UP_LIMIT  4

typedef struct {
    FuriMutex* mutex;
    AmsInfo info;
    bool connected;
    uint32_t last_skip;
    int8_t volume_steps;
    uint8_t turn;
    uint32_t scroll;
    FuriString* title;
    FuriString* message;
    uint32_t message_until;
} CarDj;

typedef enum {
    EventInput,
    EventTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} Event;

static void flash(CarDj* app, const char* text) {
    furi_string_set(app->message, text);
    app->message_until = furi_get_tick() + 1500;
}

static uint32_t cooldown_left(CarDj* app) {
    if(!app->last_skip) return 0;
    uint32_t since = furi_get_tick() - app->last_skip;
    return since >= SKIP_COOLDOWN_MS ? 0 : SKIP_COOLDOWN_MS - since;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    CarDj* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);

    bool ready = app->connected &&
                 (app->info.state == AmsStateReady || app->info.state == AmsStateCommand);
    if(!ready) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignBottom, "Car DJ");
        canvas_set_font(canvas, FontSecondary);
        char hint[64];
        snprintf(
            hint,
            sizeof(hint),
            app->connected ? "Connecting to music..." :
                             "Pair the phone:\nSettings > Bluetooth\n> Control %s",
            furi_hal_version_get_name_ptr());
        elements_multiline_text_aligned(canvas, 64, 24, AlignCenter, AlignTop, hint);
        furi_mutex_release(app->mutex);
        return;
    }

    // Whose turn, and the song
    canvas_set_font(canvas, FontSecondary);
    char turn[16];
    snprintf(turn, sizeof(turn), "Turn: %u", app->turn + 1);
    canvas_draw_str(canvas, 2, 9, turn);
    bool playing = app->info.playback[0] == '1';
    canvas_draw_str_aligned(
        canvas, 126, 9, AlignRight, AlignBottom, playing ? "Playing" : "Paused");
    canvas_set_font(canvas, FontPrimary);
    if(furi_string_empty(app->title)) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignBottom, "No song");
    } else {
        elements_scrollable_text_line(canvas, 2, 24, 124, app->title, app->scroll, false);
    }

    // Big controls
    canvas_draw_rframe(canvas, 1, 30, 38, 22, 3);
    canvas_draw_rbox(canvas, 45, 30, 38, 22, 3);
    canvas_draw_rframe(canvas, 89, 30, 38, 22, 3);
    uint32_t wait = cooldown_left(app);
    char left[8], right[8];
    if(wait) {
        snprintf(left, sizeof(left), "%lu", (wait + 999) / 1000);
        snprintf(right, sizeof(right), "%lu", (wait + 999) / 1000);
    } else {
        snprintf(left, sizeof(left), "<<");
        snprintf(right, sizeof(right), ">>");
    }
    canvas_draw_str_aligned(canvas, 20, 41, AlignCenter, AlignCenter, left);
    canvas_draw_str_aligned(canvas, 108, 41, AlignCenter, AlignCenter, right);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, playing ? "||" : ">");
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    if(furi_get_tick() < app->message_until) {
        canvas_draw_str_aligned(
            canvas, 64, 63, AlignCenter, AlignBottom, furi_string_get_cstr(app->message));
    } else {
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Up/Down: volume");
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input, void* ctx) {
    FuriMessageQueue* queue = ctx;
    Event event = {.type = EventInput, .input = *input};
    furi_message_queue_put(queue, &event, 0);
}

static void tick_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    Event event = {.type = EventTick};
    furi_message_queue_put(queue, &event, 0);
}

static void bt_status_callback(BtStatus status, void* ctx) {
    CarDj* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->connected = (status == BtStatusConnected);
    furi_mutex_release(app->mutex);
}

static void skip(CarDj* app, AmsCommand command) {
    if(cooldown_left(app)) {
        flash(app, "Wait a moment");
        return;
    }
    if(ams_client_command(command)) {
        app->last_skip = furi_get_tick();
        app->turn ^= 1;
    }
}

static void handle_key(CarDj* app, InputEvent* input) {
    if(input->type != InputTypeShort) return;
    switch(input->key) {
    case InputKeyOk:
        ams_client_command(AmsCommandTogglePlayPause);
        break;
    case InputKeyLeft:
        skip(app, AmsCommandPreviousTrack);
        break;
    case InputKeyRight:
        skip(app, AmsCommandNextTrack);
        break;
    case InputKeyUp:
        if(app->volume_steps >= VOLUME_UP_LIMIT) {
            flash(app, "That's loud enough");
        } else if(ams_client_command(AmsCommandVolumeUp)) {
            app->volume_steps++;
        }
        break;
    case InputKeyDown:
        if(ams_client_command(AmsCommandVolumeDown) && app->volume_steps > -20) {
            app->volume_steps--;
        }
        break;
    default:
        break;
    }
}

int32_t car_dj_app(void* p) {
    UNUSED(p);
    CarDj* app = malloc(sizeof(CarDj));
    memset(app, 0, sizeof(CarDj));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->title = furi_string_alloc();
    app->message = furi_string_alloc();

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(Event));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    Bt* bt = furi_record_open(RECORD_BT);
    bt_disconnect(bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(bt, KEYS_PATH);
    FuriHalBleProfileBase* profile = bt_profile_start(bt, ble_profile_hid, NULL);
    furi_check(profile);
    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(bt, bt_status_callback, app);

    FuriTimer* timer = furi_timer_alloc(tick_callback, FuriTimerTypePeriodic, queue);
    furi_timer_start(timer, 250);

    Event event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.type == EventTick) {
            AmsInfo fresh;
            ams_client_get(&fresh);
            if(strcmp(fresh.title, app->info.title) != 0) {
                furi_string_set(app->title, fresh.title);
                app->scroll = 0;
            }
            app->info = fresh;
            app->scroll++;
        } else if(event.input.key == InputKeyBack) {
            // Back must be held, so a stray press doesn't drop the kids out of the app
            if(event.input.type == InputTypeLong) {
                running = false;
            } else if(event.input.type == InputTypeShort) {
                flash(app, "Hold Back to exit");
            }
        } else {
            handle_key(app, &event.input);
        }
        furi_mutex_release(app->mutex);
        view_port_update(view_port);
    }

    furi_timer_stop(timer);
    furi_timer_free(timer);
    bt_set_status_changed_callback(bt, NULL, NULL);
    bt_disconnect(bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(bt);
    furi_check(bt_profile_restore_default(bt));
    furi_record_close(RECORD_BT);

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_string_free(app->title);
    furi_string_free(app->message);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
