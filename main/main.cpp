/**
 * @file main.cpp
 * @brief Main application file for the Flappy Bird game running on ESP32 with
 * ESP-IDF, FreeRTOS, and the LovyanGFX graphics library.
 */

// ============================================================================
// 1. HEADER FILES
// ============================================================================
#include <stdio.h>
#include <vector>

// FreeRTOS real-time operating system headers
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// ESP-IDF peripheral drivers and system utilities
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

// Project-specific header files
#include "display_config.hpp"
#include "sprites.h"

// ============================================================================
// 2. CONFIGURATION MACROS AND CONSTANTS
// ============================================================================
#define BUTTON_PIN                                                             \
  GPIO_NUM_0 // GPIO pin connected to the ESP32 board's BOOT button
#define DEBOUNCE_MS                                                            \
  50 // Debounce interval used to filter mechanical button transitions, in
     // milliseconds
#define TARGET_FPS 30 // Target game frame rate, in frames per second
#define MAX_LIVES 5   // Maximum number of lives available to the player
#define NS "game data"

// Display dimensions in landscape orientation after the configured 90-degree
// rotation
const int SCREEN_WIDTH_R = 160;
const int SCREEN_HEIGHT_R = 128;

// Gameplay physics and player-sprite constants
const float GRAVITY = 0.2f; // Downward acceleration applied to the bird on each
                            // frame, in pixels per frame squared
const float JUMP_FORCE =
    -3.0f; // Initial upward velocity applied when the player presses the
           // button; screen Y coordinates increase downward
const int BIRD_SIZE = 20; // Bird sprite and collision-box size, in pixels

// Obstacle generation and movement constants
const int TUBE_COUNT =
    4; // Number of obstacles maintained in the game at the same time
const int TUBE_SPACING =
    90; // Horizontal distance maintained between consecutive obstacles
const int GAP_CLEARENCE = 20; // Additional clearance added above and below the
                              // bird's size when calculating the gap
const float SPEED =
    1.8f; // Obstacle movement speed from right to left, in pixels per frame
const uint32_t INVULNERABLE_MS =
    1500; // Invulnerability period after damage is taken, in milliseconds

// ============================================================================
// 3. GAME DATA TYPES AND STRUCTURES
// ============================================================================

/**
 * @brief Possible states of the game-state machine (FSM).
 *
 * The main game task checks this value on every frame and executes the matching
 * state-specific update and drawing code.
 */
enum GameState {
  STATE_MENU, // Initial screen displayed while waiting for the player to start
  STATE_PLAYING, // Active gameplay state
  STATE_GAMEOVER // Game-over screen displaying the final score
};

/**
 * @brief Runtime data for the player-controlled bird.
 *
 * Position and velocity are updated by the game loop. The invulnerability
 * deadline is stored in FreeRTOS ticks so it can be compared with the RTOS clock.
 */
struct Player {
  float y = (float)SCREEN_HEIGHT_R / 2; // Current vertical position of the bird
  float velocity =
      0.0f; // Current vertical velocity; positive values move the bird downward
  uint8_t size = BIRD_SIZE;       // Square collision-box size
  uint8_t x = 20;                 // Fixed horizontal position of the bird
  uint8_t lives = MAX_LIVES;      // Number of lives currently remaining
  TickType_t invuln_end_time = 0; // FreeRTOS tick count at which the current
                                  // invulnerability period expires
};

/**
 * @brief Runtime geometry and movement data for a pipe obstacle.
 *
 * The gap is defined by its top coordinate and height. The bird must remain
 * inside this vertical interval while overlapping the obstacle horizontally.
 */
struct Obstacle {
  float x =
      (float)SCREEN_WIDTH_R; // Current X coordinate of the obstacle's left edge
  int width = 14;            // Obstacle width in pixels
  int gap_y = 44;            // Y coordinate of the top edge of the gap
  int gap_height =
      55; // Vertical height of the opening through which the bird must fly
  float speed = SPEED; // Horizontal movement speed assigned to this obstacle
};

// ============================================================================
// 4. GLOBAL VARIABLES AND OBJECTS
// ============================================================================
static const char *TAG = "Main Game"; // Log tag used by ESP-IDF logging macros

// LovyanGFX display and off-screen drawing objects
LGFX lcd; // LovyanGFX device instance representing the physical ST7735 display
LGFX_Sprite canvas(&lcd); // Off-screen sprite buffer used to compose each frame
                          // before transferring it to the display

// FreeRTOS timer and task handles
TimerHandle_t debounce_timer =
    NULL; // Software timer used to debounce the button input
TaskHandle_t game_task_handle = NULL; // Handle of the game task that receives
                                      // button-press task notifications

// Game entities and runtime state
GameState current_state = STATE_MENU; // Current state of the game-state machine
int score = 0;                        // Current player score
Player bird;                          // Player-controlled bird instance
std::vector<Obstacle> p; // Dynamic container holding the active pipe obstacles
uint8_t bg_number =
    0; // Index of the background image currently selected for rendering
uint16_t max_score = 0; // Highest score loaded from or saved to NVS
bool is_game_finished = false; // Prevents repeatedly updating the high score after game over

// ============================================================================
// 5. FUNCTION DECLARATIONS
// ============================================================================
// Hardware and interrupt initialization
void init_pins(void);
static void btn_isr_handler(void *arg);
void timer_callback(TimerHandle_t xTimer);

// Gameplay, physics, and collision handling
void reset_game(void);
bool check_collision(Player p, Obstacle o);
void take_damage(void);
uint16_t load_max_score(void);
esp_err_t save_max_score(uint16_t);

// Main application loop, executed as a FreeRTOS task
void game_loop_task(void *pvParameters);

// ============================================================================
// 6. APPLICATION ENTRY POINT
// ============================================================================
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "App starting...");

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // 1. Initialize the GPIO pins used by the button and display backlight
  init_pins();

  // 2. Initialize the physical display
  lcd.init();
  lcd.setRotation(
      1); // Rotate the display into landscape orientation (160 × 128 pixels)
  lcd.clear(TFT_BLUE); // Clear the display to a solid color during startup

  // 3. Configure the off-screen sprite used to render complete frames without
  // visible flicker
  canvas.setColorDepth(
      8); // Use 8-bit color depth (RGB332) to reduce framebuffer memory usage
  canvas.createSprite(SCREEN_WIDTH_R, SCREEN_HEIGHT_R);

  // Allocate the obstacle container and initialize game data before showing the
  // main menu
  p.resize(TUBE_COUNT);
  reset_game();
  current_state = STATE_MENU; // Set the initial application state to the main
                              // menu after the reset routine

  // 4. Create a one-shot FreeRTOS software timer for button debouncing
  debounce_timer = xTimerCreate(
      "debounce_timer", // Timer name, shown in debugging and tracing tools
      pdMS_TO_TICKS(DEBOUNCE_MS), // Debounce interval converted from
                                  // milliseconds to FreeRTOS ticks
      pdFALSE,   // Run the timer once; automatic periodic reload is disabled
      (void *)1, // Timer identifier passed to the callback
      timer_callback // Callback invoked when the debounce interval expires
  );

  // 5. Install the GPIO interrupt service and register the button interrupt
  // handler
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, btn_isr_handler, NULL);

  // 6. Create the main game task and pin it to CPU core 1
  xTaskCreatePinnedToCore(
      game_loop_task, // Function executed by the new task
      "game_loop",    // Task name used for diagnostics
      4096,           // Task stack size, in bytes
      NULL, // Optional task argument; this application does not pass parameters
      2,    // FreeRTOS task priority
      &game_task_handle, // Output handle used to notify the game task
      1                  // CPU core on which the task is pinned
  );
}

// ============================================================================
// 7. FUNCTION AND TASK IMPLEMENTATIONS
// ============================================================================

/**
 * @brief Configure the GPIO pins used for the player button and display
 * backlight.
 */
void init_pins(void) {
  // Configure the button as a pulled-up input; a falling edge indicates a press
  gpio_config_t btn_config = {.pin_bit_mask = (1ULL << BUTTON_PIN),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_ENABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_NEGEDGE};

  // Configure the display backlight pin as a digital output
  gpio_config_t bk_pin_config = {.pin_bit_mask = (1ULL << PIN_NUM_BCKL),
                                 .mode = GPIO_MODE_OUTPUT,
                                 .pull_up_en = GPIO_PULLUP_DISABLE,
                                 .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                 .intr_type = GPIO_INTR_DISABLE};

  gpio_config(&bk_pin_config);
  gpio_config(&btn_config);

  // Drive the backlight pin high to turn on the display illumination
  gpio_set_level((gpio_num_t)PIN_NUM_BCKL, 1);
}

/**
 * @brief GPIO interrupt handler for the button; this function is placed in
 * IRAM.
 * @note  Reset the debounce timer on each detected falling edge so that the
 * callback runs after the input has settled.
 */
static void IRAM_ATTR btn_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTimerResetFromISR(debounce_timer, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Debounce timer callback, invoked after the configured
 * input-stabilization interval.
 */
void timer_callback(TimerHandle_t xTimer) {
  // Check whether the button is still held down; the active-low input reads
  // zero when pressed
  if (gpio_get_level(BUTTON_PIN) == 0) {
    // Notify the game task using a direct FreeRTOS task notification
    xTaskNotifyGive(game_task_handle);
  }
}

/**
 * @brief Reset the player, obstacles, and score to prepare a new game.
 */
void reset_game(void) {
  bird.y = (float)SCREEN_HEIGHT_R / 2;
  bird.velocity = 0.0f;
  bird.lives = MAX_LIVES;
  bird.invuln_end_time = 0;
  score = 0;

  // Initialize the position and opening of each obstacle
  for (int i = 0; i < TUBE_COUNT; i++) {
    p[i].x = (float)(SCREEN_WIDTH_R + (i * TUBE_SPACING));
    p[i].gap_height =
        BIRD_SIZE + GAP_CLEARENCE * 2 + (int)(esp_random() % BIRD_SIZE);

    int min_y = 10;
    int max_y = SCREEN_HEIGHT_R - 10 - p[i].gap_height;
    if (max_y < min_y)
      max_y = min_y;
    p[i].gap_y = min_y + (esp_random() % (max_y - min_y + 1));
  }

  // Select a background image at random
  bg_number = static_cast<uint8_t>(esp_random() % 4);

  current_state = STATE_PLAYING;
  is_game_finished = false;
}

/**
 * @brief Check the bird's axis-aligned bounding box (AABB) against a pipe
 * obstacle.
 * @param p Player state to test.
 * @param o Obstacle geometry to test.
 * @return Returns true if the bird intersects the obstacle; otherwise returns
 * false.
 */
bool check_collision(Player p, Obstacle o) {
  // Determine whether the bird and obstacle overlap on the horizontal axis
  bool overlap_x =
      ((int)p.x < (int)o.x + o.width) && ((int)p.x + p.size > (int)o.x);

  if (overlap_x) {
    // If the objects overlap horizontally, check whether the bird lies outside
    // the vertical opening
    if ((int)p.y < o.gap_y) {
      return true; // Collision with the upper pipe
    }
    if ((int)p.y + p.size > o.gap_y + o.gap_height) {
      return true; // Collision with the lower pipe
    }
  }

  return false;
}

/**
 * @brief Apply damage to the player, including the bounce response, life loss,
 * and temporary invulnerability.
 */
void take_damage(void) {
  TickType_t now = xTaskGetTickCount();

  if (bird.y > (SCREEN_HEIGHT_R - BIRD_SIZE)) {
    bird.y = SCREEN_HEIGHT_R - BIRD_SIZE;
  }

  // Ignore additional damage while the current invulnerability period is active
  if ((int32_t)(xTaskGetTickCount() - bird.invuln_end_time) < 0)
    return;

  bird.velocity = JUMP_FORCE * 0.8f;
  bird.lives--;
  bird.invuln_end_time = now + pdMS_TO_TICKS(INVULNERABLE_MS);

  // if (bird.velocity >= 0) bird.velocity = 0;

  // Switch to the game-over state when no lives remain
  if (bird.lives <= 0) {
    current_state = STATE_GAMEOVER;
  }
}

/**
 * @brief Load the maximum score from non-volatile storage (NVS).
 * @return Returns the highest score recorded in NVS, or zero if no valid
 * score is found.
 */
uint16_t load_max_score(void) {

  uint16_t max_score = 0;
  nvs_handle_t myhandle;
  esp_err_t err = nvs_open(NS, NVS_READONLY, &myhandle);
  if (err == ESP_OK) {
    err = nvs_get_u16(myhandle, "max_score", &max_score);
    nvs_close(myhandle);
  }

  return max_score;
}

/**
 * @brief Save a new maximum score to non-volatile storage (NVS).
 * @param new_max_score The new highest score to be saved.
 */
esp_err_t save_max_score(uint16_t new_max_score) {

  nvs_handle_t myhandle;
  esp_err_t err = nvs_open(NS, NVS_READWRITE, &myhandle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error open NVS: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_set_u16(myhandle, "max_score", new_max_score);
  if (err == ESP_OK) {
    err = nvs_commit(myhandle);
  } else {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  }

  if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving high score: %s", esp_err_to_name(err));
  }

  nvs_close(myhandle);
  return err;
}

/**
 * @brief Main game-loop task, responsible for updating and rendering the game.
 */
void game_loop_task(void *pvParameters) {
  ESP_LOGI(TAG, "Game loop started.");

  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frame_delay = pdMS_TO_TICKS(
      1000 / TARGET_FPS); // Approximately 33 milliseconds per frame at the
                          // configured target frame rate

  while (1) {
    // --------------------------------------------------------------------
    // STEP 1: READ INPUT (BUTTON NOTIFICATIONS)
    // --------------------------------------------------------------------
    uint8_t btn_pressed = ulTaskNotifyTake(
        pdTRUE,
        0); // Read and clear pending task notifications without blocking

    // --------------------------------------------------------------------
    // STEP 2: GAME STATE, UPDATE LOGIC, AND RENDERING
    // --------------------------------------------------------------------
    if (current_state == STATE_MENU) {
      // --- STATE: MAIN MENU ---
      canvas.clear(TFT_BLACK);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(TFT_YELLOW);
      canvas.drawString("FLAPPY CUBE", 45, 40);
      canvas.setTextColor(TFT_WHITE);
      canvas.drawString("Press Button to Start", 20, 70);

      // Start a new game when a button notification is received
      if (btn_pressed > 0) {
        reset_game();
      }

    } else if (current_state == STATE_PLAYING) {
      // --- STATE: ACTIVE GAMEPLAY ---

      // 1. Apply an upward impulse when the player presses the button
      if (btn_pressed > 0) {
        bird.velocity = JUMP_FORCE;
      }

      // 2. Update the bird's vertical velocity and position using the gravity
      // constant
      bird.velocity += GRAVITY;
      bird.y += bird.velocity;

      // Check whether the bird has reached the top or bottom screen boundary
      if (bird.y > SCREEN_HEIGHT_R - bird.size)
        take_damage();
      if (bird.y < 0) {
        bird.y = 0;
        bird.velocity = 0.0f;
      }

      // 3. Move obstacles, update the score, and recycle pipes that have left
      // the screen
      for (size_t i = 0; i < p.size(); i++) {
        p[i].x -= SPEED;

        // Award a point when the pipe passes the bird's scoring position
        if (p[i].x + p[i].width >= bird.x &&
            p[i].x + p[i].width < bird.x + p[i].speed) {
          score++;
        }

        // Move an obstacle that has left the screen to the right of the
        // remaining obstacles
        if (p[i].x < -p[i].width) {
          float max_x = 0;
          for (size_t j = 0; j < p.size(); j++) {
            if (p[j].x > max_x) {
              max_x = p[j].x;
            }
          }
          p[i].x = max_x + TUBE_SPACING;

          p[i].gap_height =
              BIRD_SIZE + GAP_CLEARENCE * 2 + (int)(esp_random() % BIRD_SIZE);

          int min_y = 10;
          int max_y = SCREEN_HEIGHT_R - 10 - p[i].gap_height;
          if (max_y < min_y)
            max_y = min_y;
          p[i].gap_y = min_y + (esp_random() % (max_y - min_y + 1));
        }

        // Check the bird for a collision with the current obstacle
        if (check_collision(bird, p[i])) {
          take_damage();
        }
      }

      // 4. Draw the game scene to the off-screen sprite buffer

      // Draw the scrolling background to create a parallax effect
      static float bg_x = 0.0f;
      canvas.setSwapBytes(true);
      canvas.pushImage(static_cast<int32_t>(bg_x), 0, BG_WIDTH, BG_HEIGHT,
                       bg[bg_number]);
      canvas.pushImage(static_cast<int32_t>(bg_x + SCREEN_WIDTH_R), 0, BG_WIDTH,
                       BG_HEIGHT, bg[bg_number]);

      bg_x -= SPEED * 0.3f;
      if (bg_x < -SCREEN_WIDTH_R)
        bg_x += SCREEN_WIDTH_R;

      // Draw all pipe obstacles
      for (size_t i = 0; i < p.size(); i++) {
        // Upper pipe: repeat the pipe body fragments up to the opening
        for (uint8_t j = 0; j < (int)(p[i].gap_y / 4 - 1); j++) {
          canvas.pushImage((int)p[i].x, j * 4, 14, 4, pipe_fragment, 0xFFFF);
        }
        canvas.pushImage((int)p[i].x - 1, p[i].gap_y - 7, 16, 8, pipe_cap,
                         0xFFFF);

        // Lower pipe: draw the body from the bottom edge of the opening to the
        // bottom of the screen
        int bottom_tube_y = p[i].gap_y + p[i].gap_height;
        for (uint8_t j = 0;
             j < (int)((SCREEN_HEIGHT_R - bottom_tube_y) / 4 + 1); j++) {
          canvas.pushImage((int)p[i].x, bottom_tube_y + j * 4, 14, 4,
                           pipe_fragment, 0xFFFF);
        }
        canvas.pushImage((int)p[i].x - 1, bottom_tube_y - 1, 16, 8, pipe_cap,
                         0xFFFF);
      }

      // Calculate the bird's rotation angle from its vertical velocity
      float angle = bird.velocity * 5.0f;
      if (angle < -30.0f)
        angle = -30.0f;
      if (angle > 60.0f)
        angle = 60.0f;

      // Blink the bird while temporary invulnerability is active
      bool is_invulnerable =
          ((int32_t)(xTaskGetTickCount() - bird.invuln_end_time) < 0);
      static uint8_t blink_counter = 0;
      blink_counter++;

      if (!is_invulnerable || (blink_counter % 4) < 2) {

        canvas.pushImageRotateZoomWithAA(
            bird.x + static_cast<int>(BIRD_SIZE / 2),
            (int)bird.y + static_cast<int>(BIRD_SIZE / 2), (float)BIRD_SIZE / 2,
            (float)BIRD_SIZE / 2, angle, 1.0f, 1.0f, BIRD_SIZE, BIRD_SIZE,
            flappy_bird_20x20, (uint16_t)0xFFFF);
      }

      // Draw one heart icon for each remaining life
      for (uint8_t i = 0; i < bird.lives; i++) {
        canvas.pushImage(2 + i * 15, 3, 15, 15, heart_life_15x15,
                         (uint16_t)0xFFFF);
      }
      canvas.setSwapBytes(false);

      // Draw the current score
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(TFT_WHITE);
      canvas.drawString("Score:", 12 + 15 * MAX_LIVES, 5);
      canvas.setFont(&fonts::FreeMonoBold9pt7b);
      canvas.drawNumber(score, 50 + 15 * MAX_LIVES, 1);

    } else if (current_state == STATE_GAMEOVER) {

      // --- STATE: GAME OVER ---
      static bool is_winner = false;
      canvas.clear(TFT_BLACK);

      if (!is_game_finished) {
        max_score = load_max_score();

        if (score > max_score) {
          save_max_score(score);
          is_winner = true;
        } else
          is_winner = false;
        is_game_finished = true;
      }

      if (is_winner) {
        canvas.setFont(&fonts::Font2);
        canvas.setTextColor(TFT_GREEN);
        canvas.drawString("WINNER!!!", 55, 5);
      }
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(TFT_RED);
      canvas.drawString("GAME OVER", 45, 25);
      canvas.setTextColor(TFT_WHITE);

      canvas.setFont(&fonts::Font0);
      canvas.drawString("Max score:", 40, 60);
      canvas.drawNumber(max_score, 110, 60);

      canvas.drawString("Your score: ", 40, 80);
      canvas.drawNumber(score, 110, 80);

      canvas.drawString("Press Button to Retry", 20, 100);

      // Start a new game when the player presses the button
      if (btn_pressed > 0) {
        reset_game();
      }
    }

    // --------------------------------------------------------------------
    // STEP 3: PRESENT THE FRAME AND MAINTAIN THE FRAME RATE
    // --------------------------------------------------------------------
    canvas.pushSprite(0, 0); // Transfer the completed frame from the sprite
                             // buffer to the physical display

    // Wait until the next scheduled frame to maintain the target frame rate
    vTaskDelayUntil(&last_wake_time, frame_delay);
  }
}
