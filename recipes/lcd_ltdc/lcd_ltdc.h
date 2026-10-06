#ifndef LCD_LTDC_H
#define LCD_LTDC_H

#include <stdbool.h>
#include <stdint.h>

// Дисплей RGB 800×480 на разъёмах J1/J2 через LTDC. Тайминги — из документации панели.
#define LCD_WIDTH 800
#define LCD_HEIGHT 480
#define LCD_HSYNC 1  // ширина импульса HSYNC, такты пикселя
#define LCD_HBP 46   // задний порог строки
#define LCD_HFP 210  // передний порог строки
#define LCD_VSYNC 1  // ширина импульса VSYNC, строки
#define LCD_VBP 46   // задний порог кадра
#define LCD_VFP 22   // передний порог кадра

// Картинка-фон с SD-карты: BMP 24 бита без сжатия. Нет файла — цветные полосы.
#define LCD_LTDC_IMAGE "/SD/lcd.bmp"

// Цвет RGB565 из компонент 0–255
#define LCD_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Ждёт SD-карту, загружает фон, гоняет по экрану квадрат, раз в 5 с печатает FPS
void lcd_ltdc_start(void);

// Выводы, такт, LTDC и два кадровых буфера в SDRAM. Рисовать можно сразу после вызова.
bool lcdInit(void);

// Рисование — в задний буфер через DMA2D; на экран он попадает после lcdSwap()
void lcdFill(uint16_t color);
void lcdFillRect(int x, int y, int w, int h, uint16_t color);
// Скопировать картинку RGB565 (w×h, строки подряд) в позицию x, y
void lcdDrawImage(int x, int y, int w, int h, const uint16_t *image);
// Скопировать прямоугольник из картинки размером с экран (фона) в то же место экрана
void lcdRestore(int x, int y, int w, int h, const uint16_t *screen);

// Показать задний буфер со следующего кадра и дождаться этого (без разрывов картинки)
void lcdSwap(void);

// Загрузить BMP 24 бита в RGB565 (память в SDRAM). NULL — ошибка.
uint16_t *lcdLoadBMP(const char *path, int *w, int *h);
// Освободить картинку из lcdLoadBMP
void lcdFreeImage(uint16_t *image);

#endif
