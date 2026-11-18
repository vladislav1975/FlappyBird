/**
 * @file main.cpp
 * @brief Главный файл игры Flappy Bird для ESP32 с использованием ESP-IDF,
 *        FreeRTOS и библиотеки графики LovyanGFX.
 */

// ============================================================================
// 1. ПОДКЛЮЧЕНИЕ БИБЛИОТЕК (INCLUDES)
// ============================================================================
#include <stdio.h>
#include <vector>

// Компоненты ОС реального времени FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Драйверы периферии и системные утилиты ESP-IDF
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"

// Пользовательские заголовочные файлы
#include "display_config.hpp"
#include "sprites.h"

// ============================================================================
// 2. МАКРОСЫ И КОНСТАНТЫ КОНФИГУРАЦИИ (DEFINES & CONSTANTS)
// ============================================================================
#define BUTTON_PIN      GPIO_NUM_0  // Пин встроенной кнопки (BOOT) на плате ESP32
#define DEBOUNCE_MS     50          // Время задержки аппаратного антидребезга (мс)
#define TARGET_FPS      30          // Целевая частота кадров в секунду
#define MAX_LIVES       3           // Максимальное количество жизней игрока

// Размеры дисплея в альбомной (горизонтальной) ориентации после поворота на 90°
const int SCREEN_WIDTH_R  = 160;
const int SCREEN_HEIGHT_R = 128;

// Физические константы игрового процесса
const float GRAVITY       = 0.2f;   // Ускорение свободного падения (пикселей/кадр²)
const float JUMP_FORCE    = -3.0f;  // Начальный импульс прыжка вверх (ось Y инвертирована)
const int   BIRD_SIZE     = 20;     // Линейный размер спрайта птички (ширина/высота в пикселях)

// Константы генерации и движения препятствий (труб)
const int      TUBE_COUNT      = 4;     // Количество одновременно отображаемых труб на экране
const int      TUBE_SPACING    = 90;    // Горизонтальное расстояние между трубами
const int      GAP_CLEARENCE   = 10;    // Минимальный запас прохода в зазоре
const float    SPEED           = 2.3f;  // Скорость движения труб справа налево (пикс/кадр)
const uint32_t INVULNERABLE_MS = 1500;  // Длительность неуязвимости после получения урона (мс)

// ============================================================================
// 3. ПОЛЬЗОВАТЕЛЬСКИЕ ТИПЫ ДАННЫХ И СТРУКТУРЫ (TYPES & STRUCTS)
// ============================================================================

/**
 * @brief Возможные состояния конечного автомата (FSM) игровой логики.
 */
enum GameState {
  STATE_MENU,    // Стартовый экран ожидания начала игры
  STATE_PLAYING, // Активный игровой процесс
  STATE_GAMEOVER // Экран поражения с отображением результатов
};

/**
 * @brief Структура данных, описывающая состояние игрока (птички).
 */
struct Player {
  float    y               = (float)SCREEN_HEIGHT_R / 2; // Текущая вертикальная координата
  float    velocity        = 0.0f;                       // Текущая вертикальная скорость
  uint8_t  size            = BIRD_SIZE;                  // Размер хитбокса (квадрат)
  uint8_t  x               = 20;                         // Фиксированная горизонтальная позиция
  uint8_t  lives           = MAX_LIVES;                          // Текущий запас жизней
  TickType_t invuln_end_time = 0;                        // Системное время (в тиках RTOS), до которого действует неуязвимость
};

/**
 * @brief Структура данных, описывающая препятствие (трубу).
 */
struct Obstacle {
  float x          = (float)SCREEN_WIDTH_R; // Текущая координата X левого края трубы
  int   width      = 14;                    // Ширина трубы в пикселях
  int   gap_y      = 44;                    // Координата Y верхней границы прохода
  int   gap_height = 55;                    // Высота свободного окна для пролёта
  float speed      = SPEED;                 // Скорость движения препятствия
};

// ============================================================================
// 4. ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ И ОБЪЕКТЫ (GLOBALS)
// ============================================================================
static const char *TAG = "Main Game"; // Тег для логирования через ESP_LOG

// Графические объекты LovyanGFX
LGFX        lcd;     // Экземпляр физического дисплея ST7735
LGFX_Sprite canvas(&lcd); // Виртуальный холст в оперативной памяти (двойная буферизация)

// Дескрипторы (хэндлы) примитивов FreeRTOS
TimerHandle_t  debounce_timer     = NULL; // Таймер программного подавления дребезга кнопки
TaskHandle_t   game_task_handle   = NULL; // Хэндл главной игровой задачи для механизма Task Notifications

// Игровые сущности и переменные состояния
GameState      current_state      = STATE_MENU; // Текущее состояние игры
int            score              = 0;          // Текущий игровой счет
Player         bird;                            // Экземпляр управляемого игрока
std::vector<Obstacle> p;                        // Динамический массив препятствий (труб)
uint8_t        bg_number          = 0;          // Индекс выбранного фонового изображения

// ============================================================================
// 5. ПРОТОТИПЫ ФУНКЦИЙ (FORWARD DECLARATIONS)
// ============================================================================
// Инициализация оборудования и прерываний
void init_pins(void);
static void btn_isr_handler(void *arg);
void timer_callback(TimerHandle_t xTimer);

// Игровая логика, физика и обработка коллизий
void reset_game(void);
bool check_collision(Player p, Obstacle o);
void take_damage(void);

// Главный цикл приложения (задача FreeRTOS)
void game_loop_task(void *pvParameters);

// ============================================================================
// 6. ТОЧКА ВХОДА В ПРИЛОЖЕНИЕ (APP_MAIN)
// ============================================================================
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "App starting...");

  // 1. Инициализация аппаратных линий ввода-вывода (кнопка, подсветка)
  init_pins();

  // 2. Инициализация физического дисплея
  lcd.init();
  lcd.setRotation(1);     // Поворот экрана на 90 градусов (альбомный режим 160x128)
  lcd.clear(TFT_BLUE);    // Первичная тестовая заливка экрана

  // 3. Настройка виртуального холста (спрайта) для плавной отрисовки без мерцания
  canvas.setColorDepth(8); // 8-битный цвет (RGB332) для экономии памяти ОЗУ
  canvas.createSprite(SCREEN_WIDTH_R, SCREEN_HEIGHT_R);
  
  // Предварительное создание массива труб и сброс параметров игры для корректного старта меню
  p.resize(TUBE_COUNT);
  reset_game(); 
  current_state = STATE_MENU; // Принудительно устанавливаем состояние главного меню

  // 4. Создание программного таймера FreeRTOS для фильтрации дребезга кнопки
  debounce_timer = xTimerCreate(
      "debounce_timer",           // Текстовое имя таймера для отладки
      pdMS_TO_TICKS(DEBOUNCE_MS), // Период срабатывания, переведенный в тики ОС
      pdFALSE,                    // Однократный режим срабатывания (auto-reload выключен)
      (void *)1,                  // Идентификатор таймера
      timer_callback              // Callback-функция, вызываемая по истечении времени
  );

  // 5. Установка службы обработки прерываний GPIO и привязка прерывания к пину кнопки
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, btn_isr_handler, NULL);

  // 6. Создание и запуск главной игровой задачи на Ядре 1 (Core 1)
  xTaskCreatePinnedToCore(
      game_loop_task,     // Функция, реализующая задачу
      "game_loop",        // Имя задачи
      4096,               // Размер стека задачи (в байтах)
      NULL,               // Указатель на параметры задачи
      2,                  // Приоритет задачи
      &game_task_handle,  // Сохранение дескриптора задачи
      1                   // Привязка к ядру процессора (Core 1)
  );
}

// ============================================================================
// 7. РЕАЛИЗАЦИЯ ФУНКЦИЙ И ЗАДАЧ (IMPLEMENTATION)
// ============================================================================

/**
 * @brief Настраивает порты ввода-вывода GPIO для управляющей кнопки и подсветки экрана.
 */
void init_pins(void) {
  // Конфигурация пина кнопки: вход с подтяжкой к питанию, прерывание по спаду (нажатие)
  gpio_config_t btn_config = {
      .pin_bit_mask = (1ULL << BUTTON_PIN),
      .mode         = GPIO_MODE_INPUT,
      .pull_up_en   = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type    = GPIO_INTR_NEGEDGE
  };

  // Конфигурация пина подсветки дисплея: цифровой выход
  gpio_config_t bk_pin_config = {
      .pin_bit_mask = (1ULL << PIN_NUM_BCKL),
      .mode         = GPIO_MODE_OUTPUT,
      .pull_up_en   = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type    = GPIO_INTR_DISABLE
  };

  gpio_config(&bk_pin_config);
  gpio_config(&btn_config);

  // Включение подсветки дисплея (высокий уровень)
  gpio_set_level((gpio_num_t)PIN_NUM_BCKL, 1);
}

/**
 * @brief Обработчик аппаратного прерывания от кнопки (выполняется в IRAM).
 * @note  Перезапускает таймер антидребезга при каждом скачке напряжения на пине.
 */
static void IRAM_ATTR btn_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTimerResetFromISR(debounce_timer, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Callback-функция таймера антидребезга. Вызывается по истечении DEBOUNCE_MS.
 */
void timer_callback(TimerHandle_t xTimer) {
  // Проверяем, удерживается ли кнопка в нажатом состоянии (логический ноль)
  if (gpio_get_level(BUTTON_PIN) == 0) {
    // Отправляем прямое уведомление (Task Notification) в главную игровую задачу
    xTaskNotifyGive(game_task_handle);
  }
}

/**
 * @brief Сбрасывает состояние игрока, препятствий и счета для начала новой игры.
 */
void reset_game(void) {
    bird.y             = (float)SCREEN_HEIGHT_R / 2;
    bird.velocity      = 0.0f;
    bird.lives         = MAX_LIVES;
    bird.invuln_end_time = 0;
    score              = 0;
    
    // Инициализация координат и проходов для каждой трубы
    for (int i = 0; i < TUBE_COUNT; i++) {
        p[i].x          = (float)(SCREEN_WIDTH_R + (i * TUBE_SPACING));
        p[i].gap_height = BIRD_SIZE + GAP_CLEARENCE * 2 + (int)(esp_random() % BIRD_SIZE);

        int min_y = 10;
        int max_y = SCREEN_HEIGHT_R - 10 - p[i].gap_height;
        if (max_y < min_y) max_y = min_y;
        p[i].gap_y = min_y + (esp_random() % (max_y - min_y + 1));
    }
    
    // Случайный выбор фонового паттерна
    bg_number     = static_cast<uint8_t>(esp_random() % 4);
    current_state = STATE_PLAYING;
}

/**
 * @brief Проверяет пересечение хитбокса птички с препятствием (алгоритм AABB).
 * @param p Структура игрока.
 * @param o Структура препятствия.
 * @return true в случае столкновения, false если путь свободен.
 */
bool check_collision(Player p, Obstacle o) {
  // Проверка перекрытия по горизонтальной оси X
  bool overlap_x = ((int)p.x < (int)o.x + o.width) && ((int)p.x + p.size > (int)o.x);

  if (overlap_x) {
    // Проверка выхода за пределы безопасного вертикального прохода (gap)
    if ((int)p.y < o.gap_y) {
      return true; // Столкновение с верхней трубой
    }
    if ((int)p.y + p.size > o.gap_y + o.gap_height) {
      return true; // Столкновение с нижней трубой
    }
  }

  return false;
}

/**
 * @brief Обрабатывает получение урона игроком (потеря жизни, отскок, неуязвимость).
 */
void take_damage(void) {
  TickType_t now = xTaskGetTickCount();
  
  if (bird.y > (SCREEN_HEIGHT_R - BIRD_SIZE)) {
      bird.y = SCREEN_HEIGHT_R - BIRD_SIZE;
  }
  
  // Если птичка еще неуязвима, игнорируем урон
  if ((int32_t)(xTaskGetTickCount() - bird.invuln_end_time) < 0) return;
  
  bird.velocity      = JUMP_FORCE * 0.8f;
  bird.lives--;
  bird.invuln_end_time = now + pdMS_TO_TICKS(INVULNERABLE_MS);
  
  //if (bird.velocity >= 0) bird.velocity = 0;
  
  // Если жизни закончились — переходим на экран поражения
  if (bird.lives <= 0) {
      current_state = STATE_GAMEOVER;  
  }
}

/**
 * @brief Главная задача игрового цикла FreeRTOS.
 * @note  Обеспечивает стабильную частоту кадров (30 FPS) посредством vTaskDelayUntil.
 */
void game_loop_task(void *pvParameters) {
  ESP_LOGI(TAG, "Game loop started.");

  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frame_delay = pdMS_TO_TICKS(1000 / TARGET_FPS); // ~33 мс на кадр

  while (1) {
    // --------------------------------------------------------------------
    // ЭТАП 1: ЧТЕНИЕ ВВОДА (ОБРАБОТКА УВЕДОМЛЕНИЙ ОТ КНОПКИ)
    // --------------------------------------------------------------------
    uint8_t btn_pressed = ulTaskNotifyTake(pdTRUE, 0); // Неблокирующий опрос

    // --------------------------------------------------------------------
    // ЭТАП 2: МАШИНА СОСТОЯНИЙ (ЛОГИКА И РЕНДЕРИНГ)
    // --------------------------------------------------------------------
    if (current_state == STATE_MENU) {
      // --- СОСТОЯНИЕ: ГЛАВНОЕ МЕНЮ ---
      canvas.clear(TFT_BLACK);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(TFT_YELLOW);
      canvas.drawString("FLAPPY CUBE", 45, 40);
      canvas.setTextColor(TFT_WHITE);
      canvas.drawString("Press Button to Start", 20, 70);
      
      // Запуск игры по нажатию кнопки
      if (btn_pressed > 0) {
        reset_game();
      }

    } else if (current_state == STATE_PLAYING) {
      // --- СОСТОЯНИЕ: ИГРОВОЙ ПРОЦЕСС ---
      
      // 1. Обработка прыжка при нажатии кнопки
      if (btn_pressed > 0) {
        bird.velocity = JUMP_FORCE;
      }

      // 2. Расчёт физики птички (применение гравитации)
      bird.velocity += GRAVITY;
      bird.y        += bird.velocity; 
      
      // Проверка столкновения с границами экрана
      if (bird.y > SCREEN_HEIGHT_R - bird.size) take_damage();
      if (bird.y < 0) {
        bird.y        = 0;
        bird.velocity = 0.0f;
      }
      
      // 3. Движение препятствий, подсчет очков и респаун труб
      for (size_t i = 0; i < p.size(); i++) {
        p[i].x -= SPEED;
      
        // Начисление очков при успешном прохождении трубы
        if (p[i].x + p[i].width >= bird.x &&
            p[i].x + p[i].width < bird.x + p[i].speed) {
          score++;
        } 
        
        // Перенос ушедшей за левый край трубы вправо
        if (p[i].x < -p[i].width) {
            float max_x = 0;
            for (size_t j = 0; j < p.size(); j++) {
                if (p[j].x > max_x) {
                    max_x = p[j].x;
                }
            }
            p[i].x = max_x + TUBE_SPACING;
            
            p[i].gap_height = BIRD_SIZE + GAP_CLEARENCE * 2 + (int)(esp_random() % BIRD_SIZE);

            int min_y = 10;
            int max_y = SCREEN_HEIGHT_R - 10 - p[i].gap_height;
            if (max_y < min_y) max_y = min_y;
            p[i].gap_y = min_y + (esp_random() % (max_y - min_y + 1));
        }
        
        // Проверка коллизии птички с текущей трубой
        if (check_collision(bird, p[i])) {
          take_damage();
        }
      }
      
      // 4. Рендеринг игровых объектов на виртуальный холст
      
      // Рендер фона с эффектом параллакса
      static float bg_x = 0.0f;
      canvas.setSwapBytes(true);
      canvas.pushImage(static_cast<int32_t>(bg_x), 0, BG_WIDTH, BG_HEIGHT, bg[bg_number]);
      canvas.pushImage(static_cast<int32_t>(bg_x + SCREEN_WIDTH_R), 0, BG_WIDTH, BG_HEIGHT, bg[bg_number]);
            
      bg_x -= SPEED * 0.3f;
      if (bg_x < -SCREEN_WIDTH_R) bg_x += SCREEN_WIDTH_R;
      
      // Рендер труб (препятствий)
      for (size_t i = 0; i < p.size(); i++) {
          // Верхняя труба          
          for (uint8_t j = 0; j < (int)(p[i].gap_y/4 - 1); j++){
            canvas.pushImage((int)p[i].x, j*4, 14, 4, pipe_fragment, 0xFFFF);
          }
          canvas.pushImage((int)p[i].x - 1, p[i].gap_y - 7, 16, 8, pipe_cap, 0xFFFF);
                    
          // Нижняя труба
          int bottom_tube_y = p[i].gap_y + p[i].gap_height;
          for (uint8_t j = 0; j < (int)((SCREEN_HEIGHT_R - bottom_tube_y)/4 + 1); j++){
            canvas.pushImage((int)p[i].x, bottom_tube_y + j*4, 14, 4, pipe_fragment, 0xFFFF);
          }
          canvas.pushImage((int)p[i].x - 1, bottom_tube_y-1, 16, 8, pipe_cap, 0xFFFF);
      }
      
      // Расчёт угла наклона спрайта птички в зависимости от вертикальной скорости
      float angle = bird.velocity * 5.0f;
      if (angle < -30.0f) angle = -30.0f;
      if (angle > 60.0f)  angle = 60.0f;
      
      // Эффект мигания при неуязвимости
      bool is_invulnerable = ((int32_t)(xTaskGetTickCount() - bird.invuln_end_time) < 0);
      static uint8_t blink_counter = 0;
      blink_counter++; 
      
      if (!is_invulnerable || (blink_counter % 4) < 2) {
        
        canvas.pushImageRotateZoomWithAA(
            bird.x + static_cast<int>(BIRD_SIZE / 2),
            (int)bird.y + static_cast<int>(BIRD_SIZE / 2),
            (float)BIRD_SIZE / 2,
            (float)BIRD_SIZE / 2,
            angle,
            1.0f,
            1.0f,
            BIRD_SIZE,
            BIRD_SIZE,
            flappy_bird_20x20,
            (uint16_t)0xFFFF
        );
      }
      
      // Вывод оставшихся жизней (сердечки)
      for (uint8_t i = 0; i < bird.lives; i++) {
          canvas.pushImage(2 + i * 15, 3, 15, 15, heart_life_15x15, (uint16_t)0xFFFF);
      }
      canvas.setSwapBytes(false);
      
      // Вывод текущего счета
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(TFT_WHITE);
      canvas.drawString("Score:", 12 + 15 * MAX_LIVES, 5);
      canvas.setFont(&fonts::FreeMonoBold9pt7b);
      canvas.drawNumber(score, 50 + 15 * MAX_LIVES, 1);
      
      

    } else if (current_state == STATE_GAMEOVER) {
      // --- СОСТОЯНИЕ: ЭКРАН ПОРАЖЕНИЯ ---
      canvas.clear(TFT_BLACK);
      canvas.setFont(&fonts::Font0);
      canvas.setTextColor(TFT_RED);
      canvas.drawString("GAME OVER", 50, 30);
      canvas.setTextColor(TFT_WHITE);
      canvas.drawString("Score: ", 50, 60);
      canvas.drawNumber(score, 95, 60);
      canvas.drawString("Press Button to Retry", 20, 90); 
      
      // Перезапуск игры по нажатию кнопки
      if (btn_pressed > 0) {
        reset_game();
      }
    }
    
    // --------------------------------------------------------------------
    // ЭТАП 3: ВЫВОД БУФЕРА НА ДИСПЛЕЙ И СТАБИЛИЗАЦИЯ ЧАСТОТЫ КАДРОВ
    // --------------------------------------------------------------------
    canvas.pushSprite(0, 0); // Мгновенный перенос готового кадра из ОЗУ на физический экран
    
    // Задержка выполнения задачи для поддержания стабильного FPS
    vTaskDelayUntil(&last_wake_time, frame_delay);
  }
}
