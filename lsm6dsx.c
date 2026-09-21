/**
 ******************************************************************************
 * @file    lsm6dsx.c
 * @brief   Реализация драйвера LSM6DSx (см. lsm6dsx.h).
 * @author  Mechanic
 * @date    19.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#include "lsm6dsx.h"
#include <string.h>

#if LSM6DSX_ENABLE_LOGGER
#define LOGGER_ENABLE_LSM6DSX
#include "logger.h"
#include "logger_codes.h"
#define LSM6DSX_LOG(code, src, val)  LOGGER_Log((code), (src), (val))
#else
#define LSM6DSX_LOG(code, src, val)  ((void)0)
#endif

/* ------------------------------------------------------------------------ */
/*  Регистровая карта (общая для всего семейства LSM6DSx)                   */
/* ------------------------------------------------------------------------ */

#define LSM6DSX_REG_CTRL1_XL        0x10U
#define LSM6DSX_REG_CTRL2_G         0x11U
#define LSM6DSX_REG_CTRL3_C         0x12U
#define LSM6DSX_REG_WHO_AM_I        0x0FU
#define LSM6DSX_REG_STATUS_REG      0x1EU
#define LSM6DSX_REG_OUTX_L_G        0x22U
#define LSM6DSX_REG_OUTX_L_XL       0x28U

#define LSM6DSX_CTRL3_C_BDU         (1U << 6)
#define LSM6DSX_CTRL3_C_SIM         (1U << 3)   /* 1 = SPI 3-wire            */
#define LSM6DSX_CTRL3_C_IF_INC      (1U << 2)

#define LSM6DSX_SPI_READ_BIT        0x80U

/* ------------------------------------------------------------------------ */
/*  Таблица известных моделей: WHO_AM_I -> модель                           */
/*  У части моделей значение WHO_AM_I совпадает (см. lsm6dsx.h, раздел      */
/*  "автоопределение модели") - для автопоиска это означает, что вернётся   */
/*  первая модель таблицы с данным WHO_AM_I.                                */
/* ------------------------------------------------------------------------ */

typedef struct
{
    LSM6DSX_Model_t model;
    uint8_t         whoami;
} lsm6dsx_model_entry_t;

static const lsm6dsx_model_entry_t s_model_table[] =
{
    { LSM6DSX_MODEL_LSM6DS3,      0x69U },
    { LSM6DSX_MODEL_LSM6DS3TR_C,  0x6AU },
    { LSM6DSX_MODEL_LSM6DSL,      0x6AU },
    { LSM6DSX_MODEL_LSM6DSM,      0x6AU },
    { LSM6DSX_MODEL_LSM6DSO,      0x6CU },
    { LSM6DSX_MODEL_LSM6DSOX,     0x6CU },
    { LSM6DSX_MODEL_LSM6DSO32,    0x6CU },
    { LSM6DSX_MODEL_LSM6DSR,      0x6BU },
    { LSM6DSX_MODEL_LSM6DSRX,     0x6BU },
    { LSM6DSX_MODEL_ISM330DHCX,   0x6BU },
};
#define LSM6DSX_MODEL_TABLE_LEN  (sizeof(s_model_table) / sizeof(s_model_table[0]))

/* ------------------------------------------------------------------------ */
/*  Статический пул хэндлов (без malloc)                                    */
/* ------------------------------------------------------------------------ */

static LSM6DSX_Handle_t s_pool[LSM6DSX_MAX_DEVICES];

/* ------------------------------------------------------------------------ */
/*  Шина: SPI4 / SPI3 / I2C за общими внутренними функциями                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Опускает линию CS (активирует SPI-датчик).
 * @param  cfg  конфигурация датчика (SPI-хэндл, порт/пин CS)
 */
static inline void lsm6dsx_cs_low(const LSM6DSX_Config_t *cfg)
{
    HAL_GPIO_WritePin(cfg->cs_port, cfg->cs_pin, GPIO_PIN_RESET);
}

/**
 * @brief  Поднимает линию CS (деактивирует SPI-датчик).
 * @param  cfg  конфигурация датчика (SPI-хэндл, порт/пин CS)
 */
static inline void lsm6dsx_cs_high(const LSM6DSX_Config_t *cfg)
{
    HAL_GPIO_WritePin(cfg->cs_port, cfg->cs_pin, GPIO_PIN_SET);
}

/** Переключение направления передачи для SPI 3-wire (half-duplex, 1 линия
 *  данных) - управляется битом BIDIOE регистра CR1 SPI-периферии напрямую,
 *  т.к. соответствующие HAL-макросы не экспортируются публично во всех
 *  версиях HAL. Бит одинаково расположен во всех классических SPI STM32. */
/**
 * @brief  Переключает линию SPI 3-wire в направление передачи (TX).
 * @param  hspi  хэндл SPI-периферии
 */
static inline void lsm6dsx_spi3_set_tx(SPI_HandleTypeDef *hspi)
{
    SET_BIT(hspi->Instance->CR1, SPI_CR1_BIDIOE);
}

/**
 * @brief  Переключает линию SPI 3-wire в направление приёма (RX).
 * @param  hspi  хэндл SPI-периферии
 */
static inline void lsm6dsx_spi3_set_rx(SPI_HandleTypeDef *hspi)
{
    CLEAR_BIT(hspi->Instance->CR1, SPI_CR1_BIDIOE);
}

/**
 * @brief  Читает len байт регистров начиная с reg по шине, выбранной в конфигурации.
 * @param  h    хэндл датчика
 * @param  reg  адрес первого регистра
 * @param  buf  буфер приёма, len байт
 * @param  len  число байт для чтения
 * @return HAL_OK либо код ошибки шины
 */
static HAL_StatusTypeDef lsm6dsx_read_regs(LSM6DSX_Handle_t *h, uint8_t reg, uint8_t *buf, uint16_t len)
{
    const LSM6DSX_Config_t *cfg = &h->config;
    HAL_StatusTypeDef status;

    switch (cfg->interface)
    {
    case LSM6DSX_IF_SPI4:
    {
        uint8_t addr = reg | LSM6DSX_SPI_READ_BIT;
        lsm6dsx_cs_low(cfg);
        status = HAL_SPI_Transmit(cfg->hspi, &addr, 1U, LSM6DSX_BUS_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            status = HAL_SPI_Receive(cfg->hspi, buf, len, LSM6DSX_BUS_TIMEOUT_MS);
        }
        lsm6dsx_cs_high(cfg);
        break;
    }
    case LSM6DSX_IF_SPI3:
    {
        uint8_t addr = reg | LSM6DSX_SPI_READ_BIT;
        lsm6dsx_cs_low(cfg);
        lsm6dsx_spi3_set_tx(cfg->hspi);
        status = HAL_SPI_Transmit(cfg->hspi, &addr, 1U, LSM6DSX_BUS_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            lsm6dsx_spi3_set_rx(cfg->hspi);
            status = HAL_SPI_Receive(cfg->hspi, buf, len, LSM6DSX_BUS_TIMEOUT_MS);
        }
        lsm6dsx_cs_high(cfg);
        break;
    }
    case LSM6DSX_IF_I2C:
    default:
        status = HAL_I2C_Mem_Read(cfg->hi2c, (uint16_t)(cfg->i2c_address << 1), reg,
                                   I2C_MEMADD_SIZE_8BIT, buf, len, LSM6DSX_BUS_TIMEOUT_MS);
        break;
    }

    if (status != HAL_OK)
    {
        LSM6DSX_LOG(LOG_CODE_LSM6DSX_BUS_ERROR, h->index, (int32_t)status);
    }
    return status;
}

/**
 * @brief  Записывает один регистр по шине, выбранной в конфигурации.
 * @param  h      хэндл датчика
 * @param  reg    адрес регистра
 * @param  value  записываемое значение
 * @return HAL_OK либо код ошибки шины
 */
static HAL_StatusTypeDef lsm6dsx_write_reg(LSM6DSX_Handle_t *h, uint8_t reg, uint8_t value)
{
    const LSM6DSX_Config_t *cfg = &h->config;
    uint8_t frame[2] = { reg, value };
    HAL_StatusTypeDef status;

    switch (cfg->interface)
    {
    case LSM6DSX_IF_SPI4:
        lsm6dsx_cs_low(cfg);
        status = HAL_SPI_Transmit(cfg->hspi, frame, 2U, LSM6DSX_BUS_TIMEOUT_MS);
        lsm6dsx_cs_high(cfg);
        break;
    case LSM6DSX_IF_SPI3:
        lsm6dsx_cs_low(cfg);
        lsm6dsx_spi3_set_tx(cfg->hspi);
        status = HAL_SPI_Transmit(cfg->hspi, frame, 2U, LSM6DSX_BUS_TIMEOUT_MS);
        lsm6dsx_cs_high(cfg);
        break;
    case LSM6DSX_IF_I2C:
    default:
        status = HAL_I2C_Mem_Write(cfg->hi2c, (uint16_t)(cfg->i2c_address << 1), reg,
                                    I2C_MEMADD_SIZE_8BIT, &value, 1U, LSM6DSX_BUS_TIMEOUT_MS);
        break;
    }

    if (status != HAL_OK)
    {
        LSM6DSX_LOG(LOG_CODE_LSM6DSX_BUS_ERROR, h->index, (int32_t)status);
    }
    return status;
}

/* ------------------------------------------------------------------------ */
/*  Поиск/выделение слота пула (идемпотентность Init)                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Сравнивает два конфига на совпадение "физического" идентификатора
 *         датчика: для SPI - интерфейс + хэндл SPI + пин CS, для I2C - хэндл
 *         I2C + адрес.
 * @param  a  первая конфигурация
 * @param  b  вторая конфигурация
 * @return 1, если совпадают; иначе 0
 */
static uint8_t lsm6dsx_same_device(const LSM6DSX_Config_t *a, const LSM6DSX_Config_t *b)
{
    if (a->interface != b->interface)
    {
        return 0U;
    }
    if (a->interface == LSM6DSX_IF_I2C)
    {
        return (a->hi2c == b->hi2c) && (a->i2c_address == b->i2c_address);
    }
    return (a->hspi == b->hspi) && (a->cs_port == b->cs_port) && (a->cs_pin == b->cs_pin);
}

/**
 * @brief  Ищет в пуле уже занятый слот с тем же физическим датчиком, иначе
 *         возвращает первый свободный слот (для идемпотентности Init).
 * @param  config  конфигурация регистрируемого датчика
 * @return указатель на слот пула, либо NULL, если пул исчерпан
 */
static LSM6DSX_Handle_t *lsm6dsx_find_or_alloc_slot(const LSM6DSX_Config_t *config)
{
    LSM6DSX_Handle_t *free_slot = NULL;

    for (uint32_t i = 0U; i < LSM6DSX_MAX_DEVICES; i++)
    {
        if (s_pool[i].used != 0U)
        {
            if (lsm6dsx_same_device(&s_pool[i].config, config) != 0U)
            {
                return &s_pool[i];
            }
        }
        else if (free_slot == NULL)
        {
            free_slot = &s_pool[i];
        }
    }

    if (free_slot == NULL)
    {
        LSM6DSX_LOG(LOG_CODE_LSM6DSX_POOL_EXHAUSTED, 0U, 0);
    }
    return free_slot;
}

/* ------------------------------------------------------------------------ */
/*  Масштабирование сырых отсчётов в физические единицы                    */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Чувствительность акселерометра (g на младший значащий разряд) -
 *         справочные значения из даташитов LSM6DSO-family, общие для
 *         семейства с точностью, достаточной для прикладных задач опроса
 *         (не метрология).
 * @param  fs  диапазон измерения акселерометра
 * @return чувствительность, g/LSB
 */
static float lsm6dsx_xl_sensitivity(LSM6DSX_XLFullScale_t fs)
{
    switch (fs)
    {
    case LSM6DSX_XL_FS_2G:  return 0.061e-3f;
    case LSM6DSX_XL_FS_4G:  return 0.122e-3f;
    case LSM6DSX_XL_FS_8G:  return 0.244e-3f;
    case LSM6DSX_XL_FS_16G: return 0.488e-3f;
    default:                return 0.061e-3f;
    }
}

/**
 * @brief  Чувствительность гироскопа (dps на младший значащий разряд).
 * @param  fs  диапазон измерения гироскопа
 * @return чувствительность, dps/LSB
 */
static float lsm6dsx_g_sensitivity(LSM6DSX_GFullScale_t fs)
{
    switch (fs)
    {
    case LSM6DSX_G_FS_125DPS:  return 4.375e-3f;
    case LSM6DSX_G_FS_250DPS:  return 8.75e-3f;
    case LSM6DSX_G_FS_500DPS:  return 17.5e-3f;
    case LSM6DSX_G_FS_1000DPS: return 35.0e-3f;
    case LSM6DSX_G_FS_2000DPS: return 70.0e-3f;
    default:                   return 8.75e-3f;
    }
}

/* ------------------------------------------------------------------------ */
/*  Фильтрация                                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Приводит уровень фильтрации (0..LSM6DSX_FILTER_LEVEL_MAX) к
 *         параметрам конкретного фильтра и сбрасывает накопленное состояние -
 *         вызывается и из Init(), и из SetFilter().
 * @param  h      хэндл датчика
 * @param  type   тип фильтра
 * @param  level  уровень фильтрации, 0..LSM6DSX_FILTER_LEVEL_MAX
 */
static void lsm6dsx_filter_reset(LSM6DSX_Handle_t *h, LSM6DSX_FilterType_t type, uint8_t level)
{
    h->config.filter_type  = type;
    h->config.filter_level = level;

    /* level=0 -> alpha=1 (новый отсчёт полностью замещает старый, фильтр не
     * влияет на сигнал); level=MAX -> alpha минимальна (макс. сглаживание). */
    h->ema_alpha = 1.0f - ((float)level / (float)(LSM6DSX_FILTER_LEVEL_MAX + 1U));
    h->ema_valid = 0U;
    memset(h->ema_state, 0, sizeof(h->ema_state));

    /* level=0 -> окно 2 (минимально осмысленное усреднение); level=MAX ->
     * окно LSM6DSX_FILTER_MAX_WINDOW. */
    h->ma_window = (uint16_t)(2U + ((uint32_t)level * (LSM6DSX_FILTER_MAX_WINDOW - 2U))
                                    / LSM6DSX_FILTER_LEVEL_MAX);
    h->ma_write_index = 0U;
    h->ma_count = 0U;
    memset(h->ma_buffer, 0, sizeof(h->ma_buffer));
}

/**
 * @brief  Применяет активный фильтр к 6 каналам (3 акселерометра + 3
 *         гироскопа) на месте, в values[0..5]. Быстрая операция: EMA - 6
 *         умножений-сложений, MOVING_AVG - 6 вставок в кольцевой буфер +
 *         скользящая сумма.
 * @param  h       хэндл датчика
 * @param  values  массив [6] значений каналов, изменяется на месте
 */
static void lsm6dsx_filter_apply(LSM6DSX_Handle_t *h, float values[6])
{
    if (h->config.filter_type == LSM6DSX_FILTER_EMA)
    {
        for (uint8_t i = 0U; i < 6U; i++)
        {
            if (h->ema_valid == 0U)
            {
                h->ema_state[i] = values[i];
            }
            else
            {
                h->ema_state[i] += h->ema_alpha * (values[i] - h->ema_state[i]);
            }
            values[i] = h->ema_state[i];
        }
        h->ema_valid = 1U;
    }
    else if (h->config.filter_type == LSM6DSX_FILTER_MOVING_AVG)
    {
        uint16_t window = h->ma_window;
        for (uint8_t i = 0U; i < 6U; i++)
        {
            h->ma_buffer[i][h->ma_write_index] = values[i];
        }
        h->ma_write_index = (uint16_t)((h->ma_write_index + 1U) % window);
        if (h->ma_count < window)
        {
            h->ma_count++;
        }

        for (uint8_t i = 0U; i < 6U; i++)
        {
            float sum = 0.0f;
            for (uint16_t k = 0U; k < h->ma_count; k++)
            {
                sum += h->ma_buffer[i][k];
            }
            values[i] = sum / (float)h->ma_count;
        }
    }
    /* LSM6DSX_FILTER_NONE - values остаются без изменений. */
}

/* ------------------------------------------------------------------------ */
/*  Инициализация                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Кодирует ODR в 4-битный код регистра (значения enum совпадают с ним).
 * @param  odr  частота обновления
 * @return 4-битный код ODR
 */
static uint8_t lsm6dsx_encode_odr(LSM6DSX_Odr_t odr)
{
    return (uint8_t)odr; /* значения enum совпадают с 4-битным кодом ODR */
}

/**
 * @brief  Записывает CTRL1_XL (ODR_XL[7:4] | FS_XL[3:2]) и CTRL2_G (ODR_G[7:4] |
 *         FS_G[3:2] | FS_125[0]) из сохранённой в хэндле конфигурации.
 * @param  h  хэндл датчика
 * @return HAL_OK либо код ошибки шины
 */
static HAL_StatusTypeDef lsm6dsx_apply_odr_fs(LSM6DSX_Handle_t *h)
{
    const LSM6DSX_Config_t *cfg = &h->config;
    uint8_t ctrl1_xl = (uint8_t)((lsm6dsx_encode_odr(cfg->xl_odr) << 4)
                                  | ((uint8_t)cfg->xl_full_scale << 2));
    uint8_t fs_g_code = (cfg->g_full_scale == LSM6DSX_G_FS_125DPS)
                             ? 0U : (uint8_t)cfg->g_full_scale;
    uint8_t fs125_bit = (cfg->g_full_scale == LSM6DSX_G_FS_125DPS) ? 1U : 0U;
    uint8_t ctrl2_g = (uint8_t)((lsm6dsx_encode_odr(cfg->g_odr) << 4)
                                 | (fs_g_code << 2) | (fs125_bit << 1));

    HAL_StatusTypeDef status = lsm6dsx_write_reg(h, LSM6DSX_REG_CTRL1_XL, ctrl1_xl);
    if (status == HAL_OK)
    {
        status = lsm6dsx_write_reg(h, LSM6DSX_REG_CTRL2_G, ctrl2_g);
    }
    return status;
}

LSM6DSX_Handle_t *LSM6DSX_Init(const LSM6DSX_Config_t *config)
{
    if ((config == NULL) || (config->filter_level > LSM6DSX_FILTER_LEVEL_MAX))
    {
        return NULL;
    }
    if ((config->interface != LSM6DSX_IF_I2C) && (config->hspi == NULL))
    {
        return NULL;
    }
    if ((config->interface == LSM6DSX_IF_I2C) && (config->hi2c == NULL))
    {
        return NULL;
    }

    LSM6DSX_Handle_t *h = lsm6dsx_find_or_alloc_slot(config);
    if (h == NULL)
    {
        return NULL; /* пул исчерпан, событие уже залогировано */
    }

    h->config = *config;
    h->index  = (uint8_t)(h - s_pool);

    if (config->interface != LSM6DSX_IF_I2C)
    {
        lsm6dsx_cs_high(&h->config); /* CS неактивен по умолчанию */
    }

    /* BDU=1 (защита от разрыва чтения MSB/LSB), IF_INC=1 (авто-инкремент
     * адреса для burst-чтения), SIM=1 только для 3-wire SPI. */
    uint8_t ctrl3_c = LSM6DSX_CTRL3_C_BDU | LSM6DSX_CTRL3_C_IF_INC;
    if (config->interface == LSM6DSX_IF_SPI3)
    {
        ctrl3_c |= LSM6DSX_CTRL3_C_SIM;
        lsm6dsx_spi3_set_tx(h->config.hspi); /* стартовое направление - TX */
    }
    if (lsm6dsx_write_reg(h, LSM6DSX_REG_CTRL3_C, ctrl3_c) != HAL_OK)
    {
        h->used = 0U;
        return NULL;
    }

    if (lsm6dsx_read_regs(h, LSM6DSX_REG_WHO_AM_I, &h->whoami, 1U) != HAL_OK)
    {
        h->used = 0U;
        return NULL;
    }

    if (config->model == LSM6DSX_MODEL_AUTO)
    {
        h->detected_model = LSM6DSX_MODEL_UNKNOWN;
        for (uint32_t i = 0U; i < LSM6DSX_MODEL_TABLE_LEN; i++)
        {
            if (s_model_table[i].whoami == h->whoami)
            {
                h->detected_model = s_model_table[i].model;
                break;
            }
        }
        if (h->detected_model == LSM6DSX_MODEL_UNKNOWN)
        {
            LSM6DSX_LOG(LOG_CODE_LSM6DSX_INIT_FAIL_WHOAMI, h->index, (int32_t)h->whoami);
            h->used = 0U;
            return NULL;
        }
    }
    else
    {
        uint8_t expected_found = 0U;
        for (uint32_t i = 0U; i < LSM6DSX_MODEL_TABLE_LEN; i++)
        {
            if ((s_model_table[i].model == config->model) && (s_model_table[i].whoami == h->whoami))
            {
                expected_found = 1U;
                break;
            }
        }
        if (expected_found == 0U)
        {
            LSM6DSX_LOG(LOG_CODE_LSM6DSX_INIT_FAIL_WHOAMI, h->index, (int32_t)h->whoami);
            h->used = 0U;
            return NULL;
        }
        h->detected_model = config->model;
    }

    h->xl_sensitivity = lsm6dsx_xl_sensitivity(config->xl_full_scale);
    h->g_sensitivity  = lsm6dsx_g_sensitivity(config->g_full_scale);

    if (lsm6dsx_apply_odr_fs(h) != HAL_OK)
    {
        h->used = 0U;
        return NULL;
    }

    lsm6dsx_filter_reset(h, config->filter_type, config->filter_level);

    h->used = 1U;
    LSM6DSX_LOG(LOG_CODE_LSM6DSX_INIT_OK, h->index, (int32_t)h->detected_model);

    return h;
}

/* ------------------------------------------------------------------------ */
/*  Операционные функции                                                    */
/* ------------------------------------------------------------------------ */

HAL_StatusTypeDef LSM6DSX_ReadAccel(LSM6DSX_Handle_t *h, float *x_g, float *y_g, float *z_g)
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }

    uint8_t raw[6];
    HAL_StatusTypeDef status = lsm6dsx_read_regs(h, LSM6DSX_REG_OUTX_L_XL, raw, 6U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* Собираем 6 каналов вместе (accel в [0..2], gyro-слоты фильтра [3..5]
     * оставляем нулевыми - фильтруем только реально прочитанные каналы). */
    float values[6] = { 0 };
    for (uint8_t i = 0U; i < 3U; i++)
    {
        int16_t raw16 = (int16_t)((uint16_t)raw[2U * i] | ((uint16_t)raw[(2U * i) + 1U] << 8));
        values[i] = (float)raw16 * h->xl_sensitivity;
    }

    if (h->config.filter_type != LSM6DSX_FILTER_NONE)
    {
        lsm6dsx_filter_apply(h, values);
    }

    if (x_g != NULL) { *x_g = values[0]; }
    if (y_g != NULL) { *y_g = values[1]; }
    if (z_g != NULL) { *z_g = values[2]; }
    return HAL_OK;
}

HAL_StatusTypeDef LSM6DSX_ReadGyro(LSM6DSX_Handle_t *h, float *x_dps, float *y_dps, float *z_dps)
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }

    uint8_t raw[6];
    HAL_StatusTypeDef status = lsm6dsx_read_regs(h, LSM6DSX_REG_OUTX_L_G, raw, 6U);
    if (status != HAL_OK)
    {
        return status;
    }

    float values[6] = { 0 };
    for (uint8_t i = 0U; i < 3U; i++)
    {
        int16_t raw16 = (int16_t)((uint16_t)raw[2U * i] | ((uint16_t)raw[(2U * i) + 1U] << 8));
        values[3U + i] = (float)raw16 * h->g_sensitivity;
    }

    if (h->config.filter_type != LSM6DSX_FILTER_NONE)
    {
        lsm6dsx_filter_apply(h, values);
    }

    if (x_dps != NULL) { *x_dps = values[3]; }
    if (y_dps != NULL) { *y_dps = values[4]; }
    if (z_dps != NULL) { *z_dps = values[5]; }
    return HAL_OK;
}

HAL_StatusTypeDef LSM6DSX_ReadRaw(LSM6DSX_Handle_t *h, int16_t accel_raw[3], int16_t gyro_raw[3])
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }

    if (accel_raw != NULL)
    {
        uint8_t raw[6];
        HAL_StatusTypeDef status = lsm6dsx_read_regs(h, LSM6DSX_REG_OUTX_L_XL, raw, 6U);
        if (status != HAL_OK)
        {
            return status;
        }
        for (uint8_t i = 0U; i < 3U; i++)
        {
            accel_raw[i] = (int16_t)((uint16_t)raw[2U * i] | ((uint16_t)raw[(2U * i) + 1U] << 8));
        }
    }

    if (gyro_raw != NULL)
    {
        uint8_t raw[6];
        HAL_StatusTypeDef status = lsm6dsx_read_regs(h, LSM6DSX_REG_OUTX_L_G, raw, 6U);
        if (status != HAL_OK)
        {
            return status;
        }
        for (uint8_t i = 0U; i < 3U; i++)
        {
            gyro_raw[i] = (int16_t)((uint16_t)raw[2U * i] | ((uint16_t)raw[(2U * i) + 1U] << 8));
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef LSM6DSX_IsDataReady(LSM6DSX_Handle_t *h, uint8_t *accel_ready, uint8_t *gyro_ready)
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }

    uint8_t status_reg;
    HAL_StatusTypeDef status = lsm6dsx_read_regs(h, LSM6DSX_REG_STATUS_REG, &status_reg, 1U);
    if (status != HAL_OK)
    {
        return status;
    }

    if (accel_ready != NULL) { *accel_ready = (status_reg & 0x01U) ? 1U : 0U; }
    if (gyro_ready  != NULL) { *gyro_ready  = (status_reg & 0x02U) ? 1U : 0U; }
    return HAL_OK;
}

HAL_StatusTypeDef LSM6DSX_SetFilter(LSM6DSX_Handle_t *h, LSM6DSX_FilterType_t type, uint8_t level)
{
    if ((h == NULL) || (level > LSM6DSX_FILTER_LEVEL_MAX))
    {
        return HAL_ERROR;
    }
    lsm6dsx_filter_reset(h, type, level);
    return HAL_OK;
}

HAL_StatusTypeDef LSM6DSX_PowerDown(LSM6DSX_Handle_t *h)
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = lsm6dsx_write_reg(h, LSM6DSX_REG_CTRL1_XL, 0x00U);
    if (status == HAL_OK)
    {
        status = lsm6dsx_write_reg(h, LSM6DSX_REG_CTRL2_G, 0x00U);
    }
    return status;
}

HAL_StatusTypeDef LSM6DSX_Restart(LSM6DSX_Handle_t *h)
{
    if (h == NULL)
    {
        return HAL_ERROR;
    }
    return lsm6dsx_apply_odr_fs(h);
}
