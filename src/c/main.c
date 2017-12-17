#include <pebble.h>

// Main Window
static Window *s_main_window;

// Bluetooth
static BitmapLayer *s_bt_connected_icon_layer, *s_bt_disconnected_icon_layer;
static GBitmap *s_bt_connected_icon_bitmap, *s_bt_disconnected_icon_bitmap;

// Date
static TextLayer *s_date_layer;
static char s_date_buffer[16];

// Time
static TextLayer *s_time_layer;
static char s_time_buffer[8];

// Status bar
static Layer *s_status_bar_layer;

// Weather
static Layer *s_weather_layer;
static BitmapLayer *s_weather_bitmap_layer;
static GBitmap *s_weather_bitmap;
static TextLayer *s_weather_text_layer;
static char temperature_buffer[8];
static uint32_t s_sunrise = 0, s_sunset = 0;

static const uint32_t day_weather_icon_table[] = {
    0, 0, RESOURCE_ID_IMAGE_WEATHER_THUNDER, 
    RESOURCE_ID_IMAGE_WEATHER_RAIN_DAY, 0, RESOURCE_ID_IMAGE_WEATHER_RAIN_DAY, 
    RESOURCE_ID_IMAGE_WEATHER_SNOW, RESOURCE_ID_IMAGE_WEATHER_MIST, RESOURCE_ID_IMAGE_WEATHER_CLEAR_DAY
};

static const uint32_t day_cloudy_weather_icon_table[] = {
    0, RESOURCE_ID_IMAGE_WEATHER_PARTLY_CLOUDY_DAY, RESOURCE_ID_IMAGE_WEATHER_CLOUDY, 
    RESOURCE_ID_IMAGE_WEATHER_CLOUDY, RESOURCE_ID_IMAGE_WEATHER_CLOUDY
};

static const uint32_t night_weather_icon_table[] = {
    0, 0, RESOURCE_ID_IMAGE_WEATHER_THUNDER, 
    RESOURCE_ID_IMAGE_WEATHER_RAIN_NIGHT, 0, RESOURCE_ID_IMAGE_WEATHER_RAIN_NIGHT, 
    RESOURCE_ID_IMAGE_WEATHER_SNOW, RESOURCE_ID_IMAGE_WEATHER_MIST, RESOURCE_ID_IMAGE_WEATHER_CLEAR_NIGHT
};

static const uint32_t night_cloudy_weather_icon_table[] = {
    0, RESOURCE_ID_IMAGE_WEATHER_PARTLY_CLOUDY_NIGHT, RESOURCE_ID_IMAGE_WEATHER_CLOUDY, 
    RESOURCE_ID_IMAGE_WEATHER_CLOUDY, RESOURCE_ID_IMAGE_WEATHER_CLOUDY
};

// Steps
static Layer *s_steps_dots_layer, *s_steps_progress_layer, *s_steps_average_layer, *s_steps_layer;
static TextLayer *s_steps_text_layer;
static char s_steps_buffer[32], s_steps_emoji[5];
static int s_steps_count = 0, s_steps_goal = 0, s_steps_average = 0;
static GColor s_steps_color_loser, s_steps_color_winner;

// Battery
static int s_battery_level;
static Layer *s_battery_layer, *s_battery_icon_layer;
static TextLayer *s_battery_text_layer;

// Fonts
static GFont s_font_48, s_font_12;

static void bluetooth_handler(bool connected) {
    // Show icon if disconnected
    layer_set_hidden(bitmap_layer_get_layer(s_bt_connected_icon_layer), !connected);
    layer_set_hidden(bitmap_layer_get_layer(s_bt_disconnected_icon_layer), connected);

    if(!connected) {
        // Issue a vibrating alert
        vibes_double_pulse();
    }
}

static void update_time() {
    // Get a tm structure
    time_t current_time = time(NULL);
    struct tm *tick_time = localtime(&current_time);
    
    // Write the current hours and minutes into a buffer
    // Need static buffer so it persists across multiple calls to update_time
    // This buffer is required by TextLayer to be long-lived as long as the text is displayed
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
    strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
    
    // Display this time on the TextLayer
    text_layer_set_text(s_date_layer, s_date_buffer);
    text_layer_set_text(s_time_layer, s_time_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
    
    // Weather update every 30 minutes
    if (tick_time->tm_min % 30 == 0) {
        // Begin dictionary
        DictionaryIterator *iter;
        app_message_outbox_begin(&iter);
        
        // Add key-value pair
        dict_write_uint8(iter, 0, 0);
        
        // Send message
        app_message_outbox_send();
    }
}

static GBitmap* lookup_weather_icon(int code, int night) {       
    if (code <= 800 && night) {
        return gbitmap_create_with_resource(night_weather_icon_table[code/100]);
    } else if (code <= 800) {
        return gbitmap_create_with_resource(day_weather_icon_table[code/100]);
    } else if (code < 900 && night) {
        return gbitmap_create_with_resource(night_cloudy_weather_icon_table[code%100]);
    } else if (code < 900) {
        return gbitmap_create_with_resource(day_cloudy_weather_icon_table[code%100]);
    } else {
        return gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_EXTREME);
    }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {    
    // Read tuples for data
    Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);
    Tuple *sunrise_tuple = dict_find(iterator, MESSAGE_KEY_SUNRISE);
    Tuple *sunset_tuple = dict_find(iterator, MESSAGE_KEY_SUNSET);
    
    s_sunrise = (uint32_t)sunrise_tuple->value->uint32;
    s_sunset = (uint32_t)sunset_tuple->value->uint32;
    
    // If all data is available, use it
    if(temp_tuple && conditions_tuple && sunrise_tuple && sunset_tuple) {
        // Set icon
        uint32_t current_time = time(NULL);
        bool night = current_time < s_sunrise || current_time > s_sunset;
        s_weather_bitmap = lookup_weather_icon(conditions_tuple->value->int32, night);
        bitmap_layer_set_bitmap(s_weather_bitmap_layer, s_weather_bitmap);
        
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", (int)temp_tuple->value->int32);
        text_layer_set_text(s_weather_text_layer, temperature_buffer);
    }    
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// Is step data available?
bool steps_data_is_available() {
    return HealthServiceAccessibilityMaskAvailable & health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
}

static void get_steps_goal() {
    const time_t start = time_start_of_today();
    const time_t end = start + SECONDS_PER_DAY;
    s_steps_goal = (int)health_service_sum_averaged(HealthMetricStepCount, start, end, HealthServiceTimeScopeDaily);
}

static void get_steps_count() {
    s_steps_count = (int)health_service_sum_today(HealthMetricStepCount);
}

static void get_steps_average() {
    const time_t start = time_start_of_today();
    const time_t end = time(NULL);
    s_steps_average = (int)health_service_sum_averaged(HealthMetricStepCount, start, end, HealthServiceTimeScopeDaily);
}

static void display_steps_count() {
    if (s_steps_count >= s_steps_average) {
        text_layer_set_text_color(s_steps_text_layer, s_steps_color_winner);
        snprintf(s_steps_emoji, sizeof(s_steps_emoji), "\U0001F60C");
    } else {
        text_layer_set_text_color(s_steps_text_layer, s_steps_color_loser);
        snprintf(s_steps_emoji, sizeof(s_steps_emoji), "\U0001F4A9");
    }
    
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%s%d", s_steps_emoji, s_steps_count);
    
    text_layer_set_text(s_steps_text_layer, s_steps_buffer);
}

static void health_handler(HealthEventType event, void *context) {
    if(event == HealthEventSignificantUpdate) {
        get_steps_goal();
    }

    if(event != HealthEventSleepUpdate) {
        get_steps_count();
        get_steps_average();
        display_steps_count();
        layer_mark_dirty(s_steps_progress_layer);
        layer_mark_dirty(s_steps_average_layer);
    }
}

static void dots_layer_update_proc(Layer *layer, GContext *ctx) {
    const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(6));
    const int num_dots = 12;
    for(int i = 0; i < num_dots; i++) {
        GPoint pos = gpoint_from_polar(inset, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(i * 360 / num_dots));
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_circle(ctx, pos, 2);
    }
}

static void progress_layer_update_proc(Layer *layer, GContext *ctx) {
    const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(2));
    graphics_context_set_fill_color(ctx, s_steps_count >= s_steps_average ? s_steps_color_winner : s_steps_color_loser);
    graphics_fill_radial(ctx, inset, GOvalScaleModeFitCircle, 12, DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360.0f * s_steps_count / s_steps_goal));
}

static void average_layer_update_proc(Layer *layer, GContext *ctx) {
    if(s_steps_average < 1) {
        return;
    }
    
    const GRect inset = grect_inset(layer_get_bounds(layer), GEdgeInsets(2));
    graphics_context_set_fill_color(ctx, GColorYellow);
    int trigangle = DEG_TO_TRIGANGLE(360.0f * s_steps_average / s_steps_goal);
    int line_width_trigangle = 1000;
    // Draw a very narrow radial (it's just a line)
    graphics_fill_radial(ctx, inset, GOvalScaleModeFitCircle, 12, trigangle - line_width_trigangle, trigangle);
}

static void battery_handler(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    
    static char battery_text_layer_buffer[8];
    snprintf(battery_text_layer_buffer, sizeof(battery_text_layer_buffer), "%d%%", s_battery_level);
    text_layer_set_text(s_battery_text_layer, battery_text_layer_buffer);
    
    layer_mark_dirty(s_battery_icon_layer);
}

static void battery_update_icon_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    int gone = 5 - (s_battery_level * 5) / 100;
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(2, 1, 2, 1));
    graphics_draw_rect(ctx, GRect(0, 2, bounds.size.w, bounds.size.h-2));
    
    if (6-gone >= 2) {
        graphics_fill_rect(ctx, GRect(2, 4+gone, bounds.size.w-4, 6-gone), 0, GCornerNone);
    }
}

static void load_fonts() {
    s_font_12 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_12));
    s_font_48 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_48));
}

static void load_bluetooth(GRect bounds, Layer *layer) {
    s_bt_connected_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH_CONNECTED);
    s_bt_connected_icon_layer = bitmap_layer_create(GRect(bounds.size.w/2 - 8, bounds.size.h/6, 16, 16));
    bitmap_layer_set_bitmap(s_bt_connected_icon_layer, s_bt_connected_icon_bitmap);
    s_bt_disconnected_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLUETOOTH_DISABLED);
    s_bt_disconnected_icon_layer = bitmap_layer_create(GRect(bounds.size.w/2 - 8, bounds.size.h/6, 16, 16));
    bitmap_layer_set_bitmap(s_bt_disconnected_icon_layer, s_bt_disconnected_icon_bitmap);
    layer_add_child(layer, bitmap_layer_get_layer(s_bt_connected_icon_layer));
    layer_add_child(layer, bitmap_layer_get_layer(s_bt_disconnected_icon_layer));
}

static void load_steps_background(GRect bounds, Layer *layer) {
    s_steps_color_loser = GColorMelon;
    s_steps_color_winner = GColorJaegerGreen;
    
    // Dots for the progress indicator
    s_steps_dots_layer = layer_create(bounds);
    layer_set_update_proc(s_steps_dots_layer, dots_layer_update_proc);
    layer_add_child(layer, s_steps_dots_layer);

    // Progress indicator
    s_steps_progress_layer = layer_create(bounds);
    layer_set_update_proc(s_steps_progress_layer, progress_layer_update_proc);
    layer_add_child(layer, s_steps_progress_layer);

    // Average indicator
    s_steps_average_layer = layer_create(bounds);
    layer_set_update_proc(s_steps_average_layer, average_layer_update_proc);
    layer_add_child(layer, s_steps_average_layer);
}

static void load_date(GRect bounds, Layer *layer) {
    s_date_layer = text_layer_create(GRect(0, bounds.size.h/4, bounds.size.w, 25));
    
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, s_font_12);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(layer, text_layer_get_layer(s_date_layer));
}

static void load_time(GRect bounds, Layer *layer) {
    s_time_layer = text_layer_create(GRect(0, bounds.size.h/2 - 32, bounds.size.w, 50));   
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_font_48);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(layer, text_layer_get_layer(s_time_layer));
}

static void load_status_bar(GRect bounds, Layer *layer) {
    s_status_bar_layer = layer_create(GRect(0, 3*bounds.size.h/4 - 18, bounds.size.w, 32));
    GRect status_bar_bounds = layer_get_bounds(s_status_bar_layer);
    
    // Weather
    s_weather_layer = layer_create(GRect(PBL_IF_ROUND_ELSE(42, 28), 0, status_bar_bounds.size.w, status_bar_bounds.size.h/2));
    GRect weather_bounds = layer_get_bounds(s_weather_layer);
    s_weather_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_NA);
    s_weather_bitmap_layer = bitmap_layer_create(GRect(0, 0, 24, weather_bounds.size.h));
    bitmap_layer_set_bitmap(s_weather_bitmap_layer, s_weather_bitmap);
    layer_add_child(s_weather_layer, bitmap_layer_get_layer(s_weather_bitmap_layer));
    
    s_weather_text_layer = text_layer_create(GRect(22, 2, weather_bounds.size.w, weather_bounds.size.h));
    text_layer_set_background_color(s_weather_text_layer, GColorClear);
    text_layer_set_text_color(s_weather_text_layer, GColorWhite);
    text_layer_set_font(s_weather_text_layer, s_font_12);
    text_layer_set_text_alignment(s_weather_text_layer, GTextAlignmentLeft);
    text_layer_set_text(s_weather_text_layer, "");
    layer_add_child(s_weather_layer, text_layer_get_layer(s_weather_text_layer));
    layer_add_child(s_status_bar_layer, s_weather_layer);
        
    // Battery
    s_battery_layer = layer_create(GRect(status_bar_bounds.size.w/2 + PBL_IF_ROUND_ELSE(12, 8), 0, status_bar_bounds.size.w/3, status_bar_bounds.size.h/2));
    GRect battery_bounds = layer_get_bounds(s_battery_layer);
    s_battery_icon_layer = layer_create(GRect(0, 2, 6, 12));
    layer_set_update_proc(s_battery_icon_layer, battery_update_icon_proc);
    layer_add_child(s_battery_layer, s_battery_icon_layer);
    
    s_battery_text_layer = text_layer_create(GRect(8, 2, battery_bounds.size.w, battery_bounds.size.h));
    text_layer_set_background_color(s_battery_text_layer, GColorClear);
    text_layer_set_text_color(s_battery_text_layer, GColorWhite);
    text_layer_set_font(s_battery_text_layer, s_font_12);
    text_layer_set_text_alignment(s_battery_text_layer, GTextAlignmentLeft);
    text_layer_set_text(s_battery_text_layer, "100%");
    layer_add_child(s_battery_layer, text_layer_get_layer(s_battery_text_layer));
    layer_add_child(s_status_bar_layer, s_battery_layer);
    
    // Steps
    if (steps_data_is_available()) {
        s_steps_layer = layer_create(GRect(status_bar_bounds.size.w/4, 16, status_bar_bounds.size.w/2, status_bar_bounds.size.h/2));
        GRect steps_bounds = layer_get_bounds(s_steps_layer);
        s_steps_text_layer = text_layer_create(GRect(0, 0, steps_bounds.size.w, steps_bounds.size.h));
        text_layer_set_background_color(s_steps_text_layer, GColorClear);
        text_layer_set_text_color(s_steps_text_layer, GColorWhite);
        text_layer_set_font(s_steps_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
        text_layer_set_text_alignment(s_steps_text_layer, GTextAlignmentCenter);
        text_layer_set_text(s_steps_text_layer, "A10000");
        layer_add_child(s_steps_layer, text_layer_get_layer(s_steps_text_layer));
        layer_add_child(s_status_bar_layer, s_steps_layer);
    }
    
    layer_add_child(layer, s_status_bar_layer);
}

static void main_window_load(Window *window) {
    // Get information about the Window
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    load_fonts();
    load_bluetooth(bounds, window_layer);
    if (steps_data_is_available()) {
        load_steps_background(bounds, window_layer);
    }
    load_date(bounds, window_layer);
    load_time(bounds, window_layer);
    load_status_bar(bounds, window_layer);
}

static void main_window_unload(Window *window) {
    // Destroy TextLayer
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_weather_text_layer);
    text_layer_destroy(s_steps_text_layer);
    text_layer_destroy(s_battery_text_layer);
    
    // Destroy bitmaps
    gbitmap_destroy(s_weather_bitmap);
    gbitmap_destroy(s_bt_connected_icon_bitmap);
    gbitmap_destroy(s_bt_disconnected_icon_bitmap);
    bitmap_layer_destroy(s_weather_bitmap_layer);
    bitmap_layer_destroy(s_bt_connected_icon_layer);
    bitmap_layer_destroy(s_bt_disconnected_icon_layer);
    
    // Unload GFont
    fonts_unload_custom_font(s_font_12);
    fonts_unload_custom_font(s_font_48);
    
    // Destroy layers
    layer_destroy(s_weather_layer);
    layer_destroy(s_steps_layer);
    layer_destroy(s_battery_layer);
    layer_destroy(s_battery_icon_layer);
    layer_destroy(s_status_bar_layer);
    layer_destroy(s_steps_dots_layer);
    layer_destroy(s_steps_progress_layer);
    layer_destroy(s_steps_average_layer);
}

static void init() {
    // Create main Window element and assign to pointer
    s_main_window = window_create();
    
    // Set handlers to manage the elements inside the Window
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    
    //Show the Window on the watch, with animated=true
    window_stack_push(s_main_window, true);
    
    // Register for Bluetooth connection updates
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bluetooth_handler
    });
    
    // Show the correct state of the BT connection from the start
    bluetooth_handler(connection_service_peek_pebble_app_connection());
    
    // Make sure the time is displayed from the start
    update_time();
    
    // Register with TickTimerService
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    
    // Set the background color
    window_set_background_color(s_main_window, GColorBlack);
    
    // AppMessage callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
    
    // Open AppMessage
    const int inbox_size = 128;
    const int outbox_size = 128;
    app_message_open(inbox_size, outbox_size);
    
    // Battery
    battery_state_service_subscribe(battery_handler);
    battery_handler(battery_state_service_peek());
    
    // Steps
    if (steps_data_is_available()) {
        health_service_events_subscribe(health_handler, NULL);
    }
}

static void deinit() {
    // Destroy Window
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}