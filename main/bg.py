# Финальный генератор идеального пиксельного фона 160x128 для Flappy Bird
import math
import random

W, H = 160, 128
FILENAME = "game_bg.h"

def rgb888_to_565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

pixels = [0] * (W * H)

# 1. Плавный и чистый многоступенчатый градиент неба (без черного цвета!)
for y in range(H):
    f = y / H  # от 0.0 до 1.0
    
    if f < 0.35:
        # Верх неба: глубокий насыщенный ночной синий (RGB: 25, 40, 95)
        r = int(25 + 15 * (f / 0.35))
        g = int(40 + 20 * (f / 0.35))
        b = int(95 + 25 * (f / 0.35))
    elif f < 0.70:
        # Середина: переход в мягкий индиго и фиолетовый (RGB: 40, 45, 120 -> 90, 50, 110)
        f2 = (f - 0.35) / 0.35
        r = int(40 + 50 * f2)
        g = int(45 + 5 * f2)
        b = int(120 - 10 * f2)
    else:
        # Низ (горизонт): теплый закатный розово-оранжевый (RGB: 90, 50, 110 -> 210, 110, 50)
        f3 = (f - 0.70) / 0.30
        r = int(90 + 120 * f3)
        g = int(50 + 60 * f3)
        b = int(110 - 60 * f3)
        
    pixels[y * W : (y + 1) * W] = [rgb888_to_565(r, g, b)] * W

# 2. Аккуратные и красивые звезды в верхней половине неба
random.seed(777)
for _ in range(40):
    sx = random.randint(0, W - 1)
    sy = random.randint(2, int(H * 0.55))
    # Разная яркость звезд для глубины
    sh = random.choice([190, 220, 250, 255])
    pixels[sy * W + sx] = rgb888_to_565(sh, sh, sh)

# 3. Атмосферная дымка и облака на среднем плане
for cx in [35, 90, 135]:
    for cy in [55, 70, 62]:
        for oy in range(-2, 3):
            for ox in range(-14, 15):
                px, py = cx + ox, cy + oy
                if 0 <= px < W and 0 <= py < H:
                    if ox*ox / 196 + oy*oy / 6 <= 1:
                        # Полупрозрачные мягкие облака в оттенках заката
                        pixels[py * W + px] = rgb888_to_565(140, 85, 110)

# 4. Дальний туманный силуэт гор (Y: 78 - 95)
for x in range(W):
    mountain_h = 80 + int(math.sin(x * 0.04) * 5 + math.cos(x * 0.08) * 4)
    for y in range(max(0, mountain_h), 100):
        pixels[y * W + x] = rgb888_to_565(65, 50, 80)

# 5. Ближний план: детализированные руины и колонны (идеально бесшовные)
for x in range(W):
    base_h = 88
    profile = int(math.sin(x * (2 * math.pi / W)) * 8)
    
    # Архитектурный ритм руин
    col_struct = 0
    mod_x = x % 24
    if mod_x < 4:     # Колонна
        col_struct = 16
    elif mod_x == 11: # Арочный проем
        col_struct = -10
    elif 16 <= mod_x <= 19: # Уцелевшая стена
        col_struct = 6

    top_y = base_h - profile - col_struct
    
    for y in range(max(0, top_y), 100):
        # Красивая текстура каменной кладки с теневыми швами
        if (x + y) % 4 == 0 or x % 6 == 0:
            pixels[y * W + x] = rgb888_to_565(38, 30, 52) # Темные швы / трещины
        elif (x * y) % 7 == 0:
            pixels[y * W + x] = rgb888_to_565(75, 60, 95) # Светлые сколы камня
        else:
            pixels[y * W + x] = rgb888_to_565(52, 42, 70) # Основной тон блоков

# 6. Земля и сочный многослойный грунт (100 - 128 px)
# Верхний край: яркая и детальная трава
for y in range(100, 103):
    for x in range(W):
        if (x + y) % 2 == 0:
            pixels[y * W + x] = rgb888_to_565(95, 230, 70)
        else:
            pixels[y * W + x] = rgb888_to_565(50, 175, 45)

# Основная земля с текстурой и вкраплениями породы
for y in range(103, 118):
    for x in range(W):
        pattern = (x * 7 + y * 11) % 13
        if pattern == 0:
            pixels[y * W + x] = rgb888_to_565(130, 105, 80) # Камешек в грунте
        elif pattern == 1:
            pixels[y * W + x] = rgb888_to_565(70, 140, 45) # Корни травы
        else:
            pixels[y * W + x] = rgb888_to_565(125, 72, 22) # Глубокий чернозем

# Плотный нижний слой породы
for y in range(118, H):
    for x in range(W):
        if (x + y) % 3 == 0:
            pixels[y * W + x] = rgb888_to_565(70, 50, 35)
        else:
            pixels[y * W + x] = rgb888_to_565(90, 62, 40)

# Сохранение в заголовочный файл C++
with open(FILENAME, 'w') as f:
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"#define BG_WIDTH {W}\n")
    f.write(f"#define BG_HEIGHT {H}\n\n")
    f.write(f"const uint16_t game_bg_160x128[{W*H}] = {{\n")
    
    for i in range(0, len(pixels), 20):
        hex_row = ", ".join([f"0x{p:04X}" for p in pixels[i : i+20]])
        f.write(f"  {hex_row},\n")
        
    f.write("};\n")

print(f"Идеальный файл {FILENAME} успешно создан!")
