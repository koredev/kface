#include <pebble.h>

// Main Window
static Window *s_main_window;

// Date
static TextLayer *s_date_layer;
static GFont s_date_font;

// Time
static TextLayer *s_time_layer;
static GFont s_time_font;

// Weather
static TextLayer *s_weather_layer;
static GFont s_weather_font;

// Battery
static int s_battery_level;
static Layer *s_battery_layer;

static void update_time() {
    // Get a tm structure
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);
    
    // Write the current hours and minutes into a buffer
    // Need static buffer so it persists across multiple calls to update_time
    // This buffer is required by TextLayer to be long-lived as long as the text is displayed
    static char s_date_buffer[16];
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
    
    static char s_time_buffer[8];
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

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {   
    // Store incoming information
    static char temperature_buffer[8];
    static char conditions_buffer[32];
    static char weather_layer_buffer[32];
    
    // Read tuples for data
    Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);
    
    // If all data is available, use it
    if(temp_tuple && conditions_tuple) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", (int)temp_tuple->value->int32);
        snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", conditions_tuple->value->cstring);
    }
    
    // Assemble full string and display
    snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "%s, %s", temperature_buffer, conditions_buffer);
    text_layer_set_text(s_weather_layer, weather_layer_buffer);
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

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    layer_mark_dirty(s_battery_layer);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    
    // 10x6
    int gone = 13 - (s_battery_level * 13) / 100;
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(2, 0, 2, 2));
    graphics_draw_rect(ctx, GRect(0, 2, bounds.size.w, bounds.size.h));
    
    if (15-gone >= 2)
    graphics_fill_rect(ctx, GRect(1, 3+gone, bounds.size.w-2, 15-gone), 0, GCornerNone);
}

static void main_window_load(Window *window) {
    // Get information about the Window
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    
    // DATE
    s_date_layer = text_layer_create(GRect(0, bounds.size.h/4 - 12, bounds.size.w, 25));
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_24));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, s_date_font);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
    
    // TIME
    // Create the TextLayer with specific bounds
    s_time_layer = text_layer_create(GRect(0, bounds.size.h/2 - 25, bounds.size.w, 50));
    // Improve the layout to be more like a watchface
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_48));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
    
    // WEATHER
    s_weather_layer = text_layer_create(GRect(12, 3*bounds.size.h/4, bounds.size.w/3, 25));
    s_weather_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_12));
    text_layer_set_background_color(s_weather_layer, GColorClear);
    text_layer_set_text_color(s_weather_layer, GColorWhite);
    text_layer_set_font(s_weather_layer, s_weather_font);
    text_layer_set_text_alignment(s_weather_layer, GTextAlignmentLeft);
    text_layer_set_text(s_weather_layer, "Loading...");
    layer_add_child(window_layer, text_layer_get_layer(s_weather_layer));
    
    // BATTERY ICON
    s_battery_layer = layer_create(GRect(bounds.size.w/2, 3*bounds.size.h/4 + 2, 6, 17));
    layer_set_update_proc(s_battery_layer, battery_update_proc);
    layer_add_child(window_layer, s_battery_layer);
    
    // BATTERY TEXT
}

static void main_window_unload(Window *window) {
    // Destroy TextLayer
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_weather_layer);
    
    // Unload GFont
    fonts_unload_custom_font(s_date_font);
    fonts_unload_custom_font(s_time_font);
    fonts_unload_custom_font(s_weather_font);
    
    // Destroy layers
    layer_destroy(s_battery_layer);
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
    
    // Battery callbacks
    battery_state_service_subscribe(battery_callback);
    
    // Ensure battery level is displayed from the start
    battery_callback(battery_state_service_peek());
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