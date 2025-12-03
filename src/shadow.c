#include <pebble.h>
#include "config.h"

#define STR_SIZE 20
#define REDRAW_INTERVAL 15

static Window *window;
static TextLayer *time_text_layer;
static TextLayer *date_text_layer;
static GBitmap *world_bitmap;
static Layer *canvas;
static GBitmap *image;
static int redraw_counter;
static int width;
static int height;

static void draw_earth() {
  // ##### calculate the time
  int now = time(NULL);
  float day_of_year; // value from 0 to 1 of progress through a year
  float time_of_day; // value from 0 to 1 of progress through a day
  // approx number of leap years since epoch
  // = now / SECONDS_IN_YEAR * .24; (.24 = average rate of leap years)
  int leap_years = (int)((float)now / 131487192.0);
  // day_of_year is an estimate, but should be correct to within one day
  day_of_year = now - (((int)((float)now / 31556926.0) * 365 + leap_years) * 86400);
  day_of_year = day_of_year / 86400.0;
  time_of_day = day_of_year - (int)day_of_year;
  day_of_year = day_of_year / 365.0;
  // ##### calculate the position of the sun
  // left to right of world goes from 0 to 65536
  int sun_x = (int)((float)TRIG_MAX_ANGLE * (1.0 - time_of_day));
  // bottom to top of world goes from -32768 to 32768
  // 0.2164 is march 20, the 79th day of the year, the march equinox
  // Earth's inclination is 23.4 degrees, so sun should vary 23.4/90=.26 up and down
  int sun_y = -sin_lookup((day_of_year - 0.2164) * TRIG_MAX_ANGLE) * .26 * .25;
  // ##### draw the bitmap
  int x, y;
  for(x = 0; x < width; x++) {
    int x_angle = (int)((float)TRIG_MAX_ANGLE * (float)x / (float)(width));
    for(y = 0; y < height; y++) {
      int y_angle = (int)((float)TRIG_MAX_ANGLE * (float)y / (float)(height * 2)) - TRIG_MAX_ANGLE/4;
      // spherical law of cosines
      float angle = ((float)sin_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)sin_lookup(y_angle)/(float)TRIG_MAX_RATIO);
      angle = angle + ((float)cos_lookup(sun_y)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(y_angle)/(float)TRIG_MAX_RATIO) * ((float)cos_lookup(sun_x - x_angle)/(float)TRIG_MAX_RATIO);
#ifdef PBL_BW
      int byte = y * gbitmap_get_bytes_per_row(world_bitmap) + (int)(x / 8);
      if ((angle < 0) ^ (0x1 & (((char *)gbitmap_get_data(world_bitmap))[byte] >> (7 - x % 8)))) {
        // white pixel
        ((char *)gbitmap_get_data(image))[byte] = ((char *)gbitmap_get_data(image))[byte] | (0x1 << (7 - x % 8));
      } else {
        // black pixel
        ((char *)gbitmap_get_data(image))[byte] = ((char *)gbitmap_get_data(image))[byte] & ~(0x1 << (7 - x % 8));
      }
#else
      int byte = y * gbitmap_get_bytes_per_row(world_bitmap) + x;
      if (angle < 0) { // dark pixel
        gbitmap_get_data(world_bitmap)[byte] = gbitmap_get_data(world_bitmap)[width*height + byte];
      } else { // light pixel
        gbitmap_get_data(world_bitmap)[byte] = gbitmap_get_data(world_bitmap)[width*height*2 + byte];
      }
#endif
    }
  }
  layer_mark_dirty(canvas);
}

static void draw_watch(struct Layer *layer, GContext *ctx) {
  graphics_draw_bitmap_in_rect(ctx, image, gbitmap_get_bounds(image));
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  static char time_text[6]; // "00:00\0"
  static char date_text[12]; // "Xxx, Xxx 00\0"

  strftime(date_text, sizeof(date_text), "%a, %b %e", tick_time);
  text_layer_set_text(date_text_layer, date_text);

  if (clock_is_24h_style()) {
    strftime(time_text, sizeof(time_text), "%R", tick_time);
  } else {
    strftime(time_text, sizeof(time_text), "%I:%M", tick_time);
    if (time_text[0] == '0') {
        memmove(time_text, &time_text[1], sizeof(time_text) - 1);
    }
  }
  text_layer_set_text(time_text_layer, time_text);
 
  redraw_counter++;
  if (redraw_counter >= REDRAW_INTERVAL) {
    draw_earth();
    redraw_counter = 0;
  }
}

static void window_load(Window *window) {
#ifdef BLACK_ON_WHITE
  GColor background_color = GColorWhite;
  GColor foreground_color = GColorBlack;
#else
  GColor background_color = GColorBlack;
  GColor foreground_color = GColorWhite;
#endif
  window_set_background_color(window, background_color);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Time text
  time_text_layer = text_layer_create(GRect(0, bounds.size.w / 2, bounds.size.w, 96));
  text_layer_set_background_color(time_text_layer, background_color);
  text_layer_set_text_color(time_text_layer, foreground_color);
  text_layer_set_font(time_text_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text(time_text_layer, "");
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  // Date text
  date_text_layer = text_layer_create(GRect(0, bounds.size.w / 2 + 58, bounds.size.w, 38));
  text_layer_set_background_color(date_text_layer, background_color);
  text_layer_set_text_color(date_text_layer, foreground_color);
  text_layer_set_font(date_text_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  text_layer_set_text(date_text_layer, "");
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));

  canvas = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
  layer_set_update_proc(canvas, draw_watch);
  layer_add_child(window_layer, canvas);
#ifdef PBL_BW
  image = gbitmap_create_with_resource(RESOURCE_ID_WORLD);
#else
  image = gbitmap_create_as_sub_bitmap(world_bitmap, GRect(0, 0, width, height));
#endif
  draw_earth();
}

static void window_unload(Window *window) {
  text_layer_destroy(time_text_layer);
  text_layer_destroy(date_text_layer);
  layer_destroy(canvas);
  gbitmap_destroy(image);
}

static void init(void) {
  redraw_counter = 0;

  world_bitmap = gbitmap_create_with_resource(RESOURCE_ID_WORLD);
  width = gbitmap_get_bounds(world_bitmap).size.w;
  height = width / 2;

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  
  window_stack_push(window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(window);
  gbitmap_destroy(world_bitmap);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
