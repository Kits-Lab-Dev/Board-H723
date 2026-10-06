#include <stdio.h>
#include <string.h>
#include "lcd_ltdc.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "sddisk.h"
#include "sdram.h"

// LTDC и DMA2D — через регистры: модули HAL для них в проекте не включены (.ioc их не
// использует), а регистров немного. Описание — RM0468, разделы LTDC и DMA2D.

#define TASK_STACK_SIZE 512
#define FB_SIZE (LCD_WIDTH * LCD_HEIGHT * 2) // RGB565 — 2 байта на пиксель, 768 КБ
#define CACHE_LINE 32

static StackType_t taskStack[TASK_STACK_SIZE];
static StaticTask_t taskTcb;

static uint16_t *frontBuffer; // его сейчас показывает LTDC
static uint16_t *backBuffer;  // в нём рисуем

// Выводы LTDC на J1/J2: порт, маска выводов, номер альтернативной функции
static const struct
{
    GPIO_TypeDef *port;
    uint16_t pins;
    uint8_t af;
} ltdcPins[] = {
    {GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_9 | GPIO_PIN_15, GPIO_AF14_LTDC},
    {GPIOA, GPIO_PIN_3, GPIO_AF9_LTDC},
    {GPIOA, GPIO_PIN_8, GPIO_AF13_LTDC},
    {GPIOA, GPIO_PIN_10, GPIO_AF12_LTDC},
    {GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_AF9_LTDC},
    {GPIOB, GPIO_PIN_5, GPIO_AF3_LTDC},
    {GPIOB, GPIO_PIN_10, GPIO_AF14_LTDC},
    {GPIOC, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_AF14_LTDC},
    {GPIOC, GPIO_PIN_9 | GPIO_PIN_10, GPIO_AF10_LTDC},
    {GPIOD, GPIO_PIN_2, GPIO_AF9_LTDC},
    {GPIOD, GPIO_PIN_3, GPIO_AF14_LTDC},
    {GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, GPIO_AF14_LTDC},
    {GPIOG, GPIO_PIN_7 | GPIO_PIN_13, GPIO_AF14_LTDC},
};

// Буфер в SDRAM, выровненный по строке кэша (32 байта). Память после чужих sdramFree может
// лежать в кэше «грязной» — инвалидируем, иначе вытеснение строк кэша затрёт то, что пишет
// DMA2D. Выравнивание — чтобы инвалидация не задела соседние данные в той же строке кэша.
// Исходный указатель sdramMalloc хранится перед буфером — для lcdFreeImage.
static void *allocFrame(size_t size)
{
    size = (size + CACHE_LINE - 1) & ~(CACHE_LINE - 1);
    uint8_t *raw = sdramMalloc(size + 2 * CACHE_LINE);
    if (!raw)
        return NULL;
    uint8_t *p = (uint8_t *)(((uint32_t)raw + sizeof(void *) + CACHE_LINE - 1) & ~(CACHE_LINE - 1));
    ((void **)p)[-1] = raw;
    SCB_InvalidateDCache_by_Addr(p, size);
    return p;
}

void lcdFreeImage(uint16_t *image)
{
    if (image)
        sdramFree(((void **)image)[-1]);
}

// ---------- DMA2D ----------

static bool dma2dRun(uint32_t mode)
{
    DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCEIF;
    DMA2D->CR = mode | DMA2D_CR_START;
    TickType_t t0 = xTaskGetTickCount();
    while (!(DMA2D->ISR & (DMA2D_ISR_TCIF | DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)))
    {
        if (xTaskGetTickCount() - t0 > pdMS_TO_TICKS(100))
        {
            DMA2D->CR |= DMA2D_CR_ABORT;
            return false;
        }
    }
    return (DMA2D->ISR & DMA2D_ISR_TCIF) != 0;
}

// Обрезать прямоугольник по экрану; false — ничего не видно
static bool clip(int *x, int *y, int *w, int *h, int *skipX, int *skipY)
{
    *skipX = *x < 0 ? -*x : 0;
    *skipY = *y < 0 ? -*y : 0;
    *x += *skipX;
    *y += *skipY;
    *w -= *skipX;
    *h -= *skipY;
    if (*x + *w > LCD_WIDTH)
        *w = LCD_WIDTH - *x;
    if (*y + *h > LCD_HEIGHT)
        *h = LCD_HEIGHT - *y;
    return *w > 0 && *h > 0;
}

void lcdFillRect(int x, int y, int w, int h, uint16_t color)
{
    int sx, sy;
    if (!clip(&x, &y, &w, &h, &sx, &sy))
        return;
    // Регистр → память: DMA2D сам заливает прямоугольник цветом
    DMA2D->OPFCCR = 2; // выход RGB565
    DMA2D->OCOLR = color;
    DMA2D->OMAR = (uint32_t)&backBuffer[y * LCD_WIDTH + x];
    DMA2D->OOR = LCD_WIDTH - w; // сколько пикселей пропустить до следующей строки
    DMA2D->NLR = ((uint32_t)w << 16) | h;
    dma2dRun(3 << DMA2D_CR_MODE_Pos);
}

void lcdFill(uint16_t color)
{
    lcdFillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

// Память → память: копия прямоугольника; у источника свой шаг строки (stride, пикселей)
static void blit(int x, int y, int w, int h, const uint16_t *src, int stride)
{
    int sx, sy;
    if (!clip(&x, &y, &w, &h, &sx, &sy))
        return;
    DMA2D->FGMAR = (uint32_t)&src[sy * stride + sx];
    DMA2D->FGOR = stride - w;
    DMA2D->FGPFCCR = 2; // вход RGB565
    DMA2D->OPFCCR = 2;
    DMA2D->OMAR = (uint32_t)&backBuffer[y * LCD_WIDTH + x];
    DMA2D->OOR = LCD_WIDTH - w;
    DMA2D->NLR = ((uint32_t)w << 16) | h;
    dma2dRun(0);
}

void lcdDrawImage(int x, int y, int w, int h, const uint16_t *image)
{
    blit(x, y, w, h, image, w);
}

void lcdRestore(int x, int y, int w, int h, const uint16_t *screen)
{
    blit(x, y, w, h, &screen[y * LCD_WIDTH + x], LCD_WIDTH);
}

// ---------- LTDC ----------

void lcdSwap(void)
{
    uint16_t *t = frontBuffer;
    frontBuffer = backBuffer;
    backBuffer = t;
    // Новый адрес вступит в силу в начале следующего кадра (VBR — reload on vertical
    // blanking); до этого старый задний буфер ещё на экране, и рисовать в него нельзя
    LTDC_Layer1->CFBAR = (uint32_t)frontBuffer;
    LTDC->SRCR = LTDC_SRCR_VBR;
    while (LTDC->SRCR & LTDC_SRCR_VBR)
        vTaskDelay(1);
}

bool lcdInit(void)
{
    frontBuffer = allocFrame(FB_SIZE);
    backBuffer = allocFrame(FB_SIZE);
    if (!frontBuffer || !backBuffer)
        return false;

    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_VERY_HIGH};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    for (unsigned i = 0; i < sizeof(ltdcPins) / sizeof(ltdcPins[0]); i++)
    {
        gpio.Pin = ltdcPins[i].pins;
        gpio.Alternate = ltdcPins[i].af;
        HAL_GPIO_Init(ltdcPins[i].port, &gpio);
    }

    // Такт пикселя — PLL3R (PeriphCommonClock_Config в main.c, 25 МГц)
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_LTDC_FORCE_RESET();
    __HAL_RCC_LTDC_RELEASE_RESET();
    __HAL_RCC_DMA2D_CLK_ENABLE();

    // Развёртка: в регистрах — накопленные суммы (ширина импульса, + порог, + видимая
    // часть, + задний порог), каждая минус 1
    uint32_t ahbp = LCD_HSYNC + LCD_HBP - 1, avbp = LCD_VSYNC + LCD_VBP - 1;
    LTDC->SSCR = ((LCD_HSYNC - 1) << 16) | (LCD_VSYNC - 1);
    LTDC->BPCR = (ahbp << 16) | avbp;
    LTDC->AWCR = ((ahbp + LCD_WIDTH) << 16) | (avbp + LCD_HEIGHT);
    LTDC->TWCR = ((ahbp + LCD_WIDTH + LCD_HFP) << 16) | (avbp + LCD_HEIGHT + LCD_VFP);
    LTDC->BCCR = 0; // цвет фона под слоем — чёрный
    // Полярности HSYNC, VSYNC, DE — активный 0, такт пикселя не инвертирован: все биты 0
    LTDC->GCR = 0;

    // Слой 1 на весь экран, RGB565, без прозрачности
    LTDC_Layer1->WHPCR = ((ahbp + LCD_WIDTH) << 16) | (ahbp + 1);
    LTDC_Layer1->WVPCR = ((avbp + LCD_HEIGHT) << 16) | (avbp + 1);
    LTDC_Layer1->PFCR = 2; // RGB565
    LTDC_Layer1->CACR = 255;
    LTDC_Layer1->DCCR = 0;
    LTDC_Layer1->BFCR = (4 << 8) | 5; // смешивание по постоянной альфе
    LTDC_Layer1->CFBAR = (uint32_t)frontBuffer;
    // Шаг строки в байтах и длина строки + 7 (так требует LTDC в STM32H7)
    LTDC_Layer1->CFBLR = ((LCD_WIDTH * 2) << 16) | (LCD_WIDTH * 2 + 7);
    LTDC_Layer1->CFBLNR = LCD_HEIGHT;
    LTDC_Layer1->CR = LTDC_LxCR_LEN;
    LTDC->SRCR = LTDC_SRCR_IMR; // применить настройки слоя сразу

    // Оба буфера — чёрные. lcdSwap() здесь не подходит: он ждёт кадровый импульс,
    // а LTDC ещё выключен
    lcdFill(0);
    uint16_t *t = frontBuffer;
    frontBuffer = backBuffer;
    backBuffer = t;
    lcdFill(0);
    LTDC_Layer1->CFBAR = (uint32_t)frontBuffer;
    LTDC->SRCR = LTDC_SRCR_IMR;
    LTDC->GCR |= LTDC_GCR_LTDCEN;
    return true;
}

// ---------- BMP ----------

typedef struct __attribute__((packed))
{
    uint16_t type; // "BM"
    uint32_t size;
    uint32_t reserved;
    uint32_t offset; // начало пикселей
    uint32_t infoSize;
    int32_t width;
    int32_t height; // > 0 — строки снизу вверх
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
} BmpHeader;

// Прочитать size байт с позиции pos блоками по 32 КБ: драйвер SD передаёт их одной
// командой, это в разы быстрее мелких чтений через stdio
static bool readAt(FIL *f, uint32_t pos, void *buf, uint32_t size)
{
    if (f_lseek(f, pos) != FR_OK)
        return false;
    uint8_t *p = buf;
    while (size > 0)
    {
        UINT n = size < 32768 ? size : 32768, got;
        if (f_read(f, p, n, &got) != FR_OK || got != n)
            return false;
        p += n;
        size -= n;
    }
    return true;
}

uint16_t *lcdLoadBMP(const char *path, int *w, int *h)
{
    static FIL f; // FIL с буфером сектора — больше 0,5 КБ, стек задачи маленький
    if (f_open(&f, path, FA_READ) != FR_OK)
        return NULL;
    BmpHeader hd;
    uint16_t *image = NULL;
    uint8_t *raw = NULL;
    if (!readAt(&f, 0, &hd, sizeof(hd)) || hd.type != 0x4D42 || hd.bpp != 24 ||
        hd.compression != 0 || hd.width <= 0 || hd.height == 0)
    {
        ERR("lcd_ltdc: %s: need BMP 24 bit, no compression\n", path);
        goto out;
    }
    int width = hd.width, height = hd.height > 0 ? hd.height : -hd.height;
    uint32_t rowSize = (width * 3 + 3) & ~3; // строки BMP выровнены по 4 байта
    raw = sdramMalloc(rowSize * height + CACHE_LINE);
    image = allocFrame(width * height * 2);
    if (!raw || !image || !readAt(&f, hd.offset, raw, rowSize * height))
    {
        ERR("lcd_ltdc: %s: read failed\n", path);
        lcdFreeImage(image);
        image = NULL;
        goto out;
    }
    // Пиксели записал процессор через кэш — выгрузить их в SDRAM, иначе DMA2D их не увидит
    SCB_CleanDCache_by_Addr((uint32_t *)((uint32_t)raw & ~(CACHE_LINE - 1)), rowSize * height + CACHE_LINE);

    // DMA2D переводит строку RGB888 → RGB565; по строке за раз, чтобы перевернуть
    // картинку (BMP хранит строки снизу вверх) и пропустить выравнивание строк
    for (int y = 0; y < height; y++)
    {
        int srcRow = hd.height > 0 ? height - 1 - y : y;
        DMA2D->FGMAR = (uint32_t)(raw + srcRow * rowSize);
        DMA2D->FGOR = 0;
        DMA2D->FGPFCCR = 1; // вход RGB888 (в памяти B, G, R — как в BMP)
        DMA2D->OPFCCR = 2;  // выход RGB565
        DMA2D->OMAR = (uint32_t)&image[y * width];
        DMA2D->OOR = 0;
        DMA2D->NLR = ((uint32_t)width << 16) | 1;
        if (!dma2dRun(1 << DMA2D_CR_MODE_Pos))
        {
            ERR("lcd_ltdc: DMA2D error\n");
            lcdFreeImage(image);
            image = NULL;
            goto out;
        }
    }
    *w = width;
    *h = height;
out:
    if (raw)
        sdramFree(raw);
    f_close(&f);
    return image;
}

// ---------- Пример ----------

// Фон на весь экран: картинка с карты по центру или 8 цветных полос
static uint16_t *makeBackground(void)
{
    static const uint16_t colors[] = {
        LCD_RGB(255, 255, 255), LCD_RGB(255, 255, 0), LCD_RGB(0, 255, 255), LCD_RGB(0, 255, 0),
        LCD_RGB(255, 0, 255), LCD_RGB(255, 0, 0), LCD_RGB(0, 0, 255), LCD_RGB(0, 0, 0)};
    uint16_t *bg = allocFrame(FB_SIZE);
    if (!bg)
        return NULL;

    for (int i = 0; i < 10 && !sdDiskIsMounted(); i++)
        vTaskDelay(pdMS_TO_TICKS(500));
    int w = 0, h = 0;
    TickType_t t0 = xTaskGetTickCount();
    uint16_t *image = sdDiskIsMounted() ? lcdLoadBMP(LCD_LTDC_IMAGE, &w, &h) : NULL;

    uint16_t *saved = backBuffer; // рисуем фон теми же функциями, подменив буфер
    backBuffer = bg;
    if (image)
    {
        INF("lcd_ltdc: %s %dx%d loaded in %lu ms\n", LCD_LTDC_IMAGE, w, h, xTaskGetTickCount() - t0);
        lcdFill(0);
        lcdDrawImage((LCD_WIDTH - w) / 2, (LCD_HEIGHT - h) / 2, w, h, image);
        lcdFreeImage(image);
    }
    else
    {
        INF("lcd_ltdc: no %s, color bars\n", LCD_LTDC_IMAGE);
        for (int i = 0; i < 8; i++)
            lcdFillRect(i * LCD_WIDTH / 8, 0, LCD_WIDTH / 8, LCD_HEIGHT, colors[i]);
    }
    backBuffer = saved;
    return bg;
}

static void lcdTask(void *arg)
{
    (void)arg;
    if (!lcdInit())
    {
        ERR("lcd_ltdc: no SDRAM for frame buffers\n");
        vTaskDelete(NULL);
    }
    PLL3_ClocksTypeDef pll3;
    HAL_RCCEx_GetPLL3ClockFreq(&pll3);
    uint32_t total = (LCD_HSYNC + LCD_HBP + LCD_WIDTH + LCD_HFP) * (LCD_VSYNC + LCD_VBP + LCD_HEIGHT + LCD_VFP);
    INF("lcd_ltdc: %dx%d RGB565, pixel clock %lu kHz, refresh %lu Hz\n", LCD_WIDTH, LCD_HEIGHT,
        pll3.PLL3_R_Frequency / 1000, pll3.PLL3_R_Frequency / total);

    uint16_t *bg = makeBackground();
    if (!bg)
    {
        ERR("lcd_ltdc: no SDRAM for background\n");
        vTaskDelete(NULL);
    }
    // Фон — в оба буфера целиком, один раз
    lcdRestore(0, 0, LCD_WIDTH, LCD_HEIGHT, bg);
    lcdSwap();
    lcdRestore(0, 0, LCD_WIDTH, LCD_HEIGHT, bg);

    // Дальше перерисовываем только то, что изменилось. В заднем буфере квадрат стоит там,
    // где его нарисовали два кадра назад (буферы чередуются), — это место закрываем фоном.
    const int size = 100;
    int x = 0, y = 0, dx = 6, dy = 4;
    int oldX[2], oldY[2];
    bool drawn[2] = {false, false};
    int cur = 0; // номер заднего буфера
    // Время рисования — по счётчику тактов ядра (DWT): тик FreeRTOS (1 мс) слишком грубый
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t frames = 0, drawCycles = 0;
    TickType_t report = xTaskGetTickCount();
    for (;;)
    {
        uint32_t c0 = DWT->CYCCNT;
        if (drawn[cur])
            lcdRestore(oldX[cur], oldY[cur], size, size, bg);
        lcdFillRect(x, y, size, size, LCD_RGB(255, 80, 100));
        oldX[cur] = x;
        oldY[cur] = y;
        drawn[cur] = true;
        drawCycles += DWT->CYCCNT - c0;
        lcdSwap();
        cur ^= 1;
        frames++;

        x += dx;
        y += dy;
        if (x < 0 || x + size > LCD_WIDTH)
            dx = -dx, x += 2 * dx;
        if (y < 0 || y + size > LCD_HEIGHT)
            dy = -dy, y += 2 * dy;

        TickType_t now = xTaskGetTickCount();
        if (now - report >= pdMS_TO_TICKS(5000))
        {
            INF("lcd_ltdc: %lu fps, draw %lu us/frame\n", frames * 1000 / (now - report),
                drawCycles / frames / (SystemCoreClock / 1000000));
            frames = drawCycles = 0;
            report = now;
        }
    }
}

void lcd_ltdc_start(void)
{
    xTaskCreateStatic(lcdTask, "lcd", TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, taskStack, &taskTcb);
    INF("lcd_ltdc: started\n");
}
