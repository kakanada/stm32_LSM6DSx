/**
 ******************************************************************************
 * @file    lsm6dsx.h
 * @brief   Драйвер семейства 6-осевых MEMS датчиков ST LSM6DSx (акселерометр +
 *          гироскоп): LSM6DS3, LSM6DS3TR-C, LSM6DSL, LSM6DSM, LSM6DSO,
 *          LSM6DSOX, LSM6DSO32, LSM6DSR, LSM6DSRX, ISM330DHCX.
 * @author  Mechanic
 * @date    19.09.2026
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 Mechanic.
 *            Свободное некоммерческое использование и модификация. Условия
 *            распространения - см. LICENSE / README.md в составе проекта.
 ******************************************************************************
 */

#ifndef LSM6DSX_H
#define LSM6DSX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"   /* CubeMX: SPI_HandleTypeDef, I2C_HandleTypeDef, GPIO */

/* ------------------------------------------------------------------------ */
/*  Конфигурация модуля (define-ы, переопределяются до включения заголовка) */
/* ------------------------------------------------------------------------ */

/** Максимальное число одновременно зарегистрированных датчиков. */
#ifndef LSM6DSX_MAX_DEVICES
#define LSM6DSX_MAX_DEVICES            4U
#endif

/** Таймаут одной транзакции по SPI/I2C, мс. Короткий - операционные функции
 *  обязаны быть быстрыми, а не блокироваться на неисправной шине. */
#ifndef LSM6DSX_BUS_TIMEOUT_MS
#define LSM6DSX_BUS_TIMEOUT_MS          5U
#endif

/** Максимальный размер окна фильтра LSM6DSX_FILTER_MOVING_AVG (на каждую из
 *  6 осей хранится буфер такого размера типа float - т.е. память на
 *  экземпляр растёт как 6 * LSM6DSX_FILTER_MAX_WINDOW * sizeof(float)). */
#ifndef LSM6DSX_FILTER_MAX_WINDOW
#define LSM6DSX_FILTER_MAX_WINDOW      16U
#endif

/** Верхняя граница шкалы уровня фильтрации (0..LSM6DSX_FILTER_LEVEL_MAX). */
#define LSM6DSX_FILTER_LEVEL_MAX       10U

/** Включает интеграцию с библиотекой stm32_logger (LOGGER_Log/LOGGER_Mark).
 *  По умолчанию выключено - модуль не тянет за собой чужую зависимость,
 *  пока пользователь явно не определит этот макрос до включения заголовка. */
#ifndef LSM6DSX_ENABLE_LOGGER
#define LSM6DSX_ENABLE_LOGGER          0
#endif

/* ------------------------------------------------------------------------ */
/*  Модель датчика                                                          */
/* ------------------------------------------------------------------------ */

typedef enum
{
    LSM6DSX_MODEL_AUTO = 0,     /**< только как ВХОДНОЕ значение config.model:
                                     определить модель автоматически по
                                     WHO_AM_I */
    LSM6DSX_MODEL_LSM6DS3,
    LSM6DSX_MODEL_LSM6DS3TR_C,
    LSM6DSX_MODEL_LSM6DSL,
    LSM6DSX_MODEL_LSM6DSM,
    LSM6DSX_MODEL_LSM6DSO,
    LSM6DSX_MODEL_LSM6DSOX,
    LSM6DSX_MODEL_LSM6DSO32,
    LSM6DSX_MODEL_LSM6DSR,
    LSM6DSX_MODEL_LSM6DSRX,
    LSM6DSX_MODEL_ISM330DHCX,
    LSM6DSX_MODEL_UNKNOWN       /**< только как ВЫХОДНОЕ значение
                                     detected_model: WHO_AM_I прочитан, но не
                                     совпал ни с одной известной моделью */
} LSM6DSX_Model_t;

/* ------------------------------------------------------------------------ */
/*  Интерфейс подключения                                                   */
/* ------------------------------------------------------------------------ */

typedef enum
{
    LSM6DSX_IF_SPI4 = 0,    /**< SPI, полный дуплекс (4 провода)            */
    LSM6DSX_IF_SPI3,        /**< SPI, 1 линия данных (3 провода, half-duplex) */
    LSM6DSX_IF_I2C          /**< I2C                                        */
} LSM6DSX_Interface_t;

/* ------------------------------------------------------------------------ */
/*  Диапазоны и частоты опроса                                              */
/* ------------------------------------------------------------------------ */

/** Диапазон акселерометра. Значения совпадают с кодом бит FS_XL[1:0]
 *  регистра CTRL1_XL - см. lsm6dsx.c. */
typedef enum
{
    LSM6DSX_XL_FS_2G = 0,
    LSM6DSX_XL_FS_16G,
    LSM6DSX_XL_FS_4G,
    LSM6DSX_XL_FS_8G
} LSM6DSX_XLFullScale_t;

/** Диапазон гироскопа. Значения совпадают с кодом бит FS_G[1:0] регистра
 *  CTRL2_G, кроме LSM6DSX_G_FS_125DPS - отдельный бит FS_125. */
typedef enum
{
    LSM6DSX_G_FS_250DPS = 0,
    LSM6DSX_G_FS_500DPS,
    LSM6DSX_G_FS_1000DPS,
    LSM6DSX_G_FS_2000DPS,
    LSM6DSX_G_FS_125DPS
} LSM6DSX_GFullScale_t;

/** Частота опроса. Общая для CTRL1_XL[7:4] (ODR_XL) и CTRL2_G[7:4] (ODR_G) -
 *  акселерометр и гироскоп настраиваются каждый своим полем config,
 *  используя один и тот же перечень значений. */
typedef enum
{
    LSM6DSX_ODR_POWER_DOWN = 0,
    LSM6DSX_ODR_12_5_HZ,
    LSM6DSX_ODR_26_HZ,
    LSM6DSX_ODR_52_HZ,
    LSM6DSX_ODR_104_HZ,
    LSM6DSX_ODR_208_HZ,
    LSM6DSX_ODR_416_HZ,
    LSM6DSX_ODR_833_HZ,
    LSM6DSX_ODR_1666_HZ,
    LSM6DSX_ODR_3332_HZ,
    LSM6DSX_ODR_6664_HZ
} LSM6DSX_Odr_t;

/* ------------------------------------------------------------------------ */
/*  Фильтрация                                                              */
/* ------------------------------------------------------------------------ */

typedef enum
{
    LSM6DSX_FILTER_NONE = 0,   /**< отключена - минимальная задержка         */
    LSM6DSX_FILTER_EMA,        /**< экспоненциальное сглаживание, O(1)       */
    LSM6DSX_FILTER_MOVING_AVG  /**< скользящее среднее по окну               */
} LSM6DSX_FilterType_t;

/* ------------------------------------------------------------------------ */
/*  Конфигурация одного экземпляра - заполняется пользователем              */
/* ------------------------------------------------------------------------ */

typedef struct
{
    /** Интерфейс подключения датчика. */
    LSM6DSX_Interface_t interface;

    /** SPI-хэндл (LSM6DSX_IF_SPI4/LSM6DSX_IF_SPI3). Для SPI3 периферия
     *  должна быть настроена в CubeMX с Direction = "1 Line" (half-duplex),
     *  модуль сам переключает CR1.BIDIOE между приёмом и передачей.
     *  Не используется при LSM6DSX_IF_I2C. */
    SPI_HandleTypeDef *hspi;

    /** GPIO chip-select для обоих режимов SPI (для SPI3 линия CS всё равно
     *  нужна - она задаёт границы регистровой транзакции). Не используется
     *  при LSM6DSX_IF_I2C. */
    GPIO_TypeDef *cs_port;
    uint16_t      cs_pin;

    /** I2C-хэндл. Используется только при LSM6DSX_IF_I2C. */
    I2C_HandleTypeDef *hi2c;

    /** 7-битный адрес на шине I2C: 0x6A, если SDO/SA0 подтянут к GND, либо
     *  0x6B, если к VDD. Используется только при LSM6DSX_IF_I2C. */
    uint8_t i2c_address;

    /** Модель датчика: LSM6DSX_MODEL_AUTO для автоопределения по WHO_AM_I,
     *  либо конкретное значение, если модель известна заранее (быстрее -
     *  Init() не тратит время на перебор таблицы, и работает предсказуемо
     *  для моделей с неуникальным WHO_AM_I). */
    LSM6DSX_Model_t model;

    /** Диапазон и частота опроса акселерометра. */
    LSM6DSX_XLFullScale_t xl_full_scale;
    LSM6DSX_Odr_t         xl_odr;

    /** Диапазон и частота опроса гироскопа. */
    LSM6DSX_GFullScale_t  g_full_scale;
    LSM6DSX_Odr_t         g_odr;

    /** Тип и уровень (0..LSM6DSX_FILTER_LEVEL_MAX) фильтрации показаний
     *  ReadAccel/ReadGyro. Игнорируется, если filter_type == FILTER_NONE. */
    LSM6DSX_FilterType_t filter_type;
    uint8_t               filter_level;
} LSM6DSX_Config_t;

/* ------------------------------------------------------------------------ */
/*  Хэндл экземпляра - главная структура API                                */
/* ------------------------------------------------------------------------ */
/*
 * LSM6DSX_Init() возвращает указатель НА ЭТУ структуру (память статическая,
 * живёт всё время работы программы - хранить у себя полученный указатель,
 * освобождать не нужно).
 */
typedef struct
{
    /* ---- Публичные поля (можно читать снаружи) ---- */
    LSM6DSX_Config_t config;           /**< копия того, что передали в Init */
    LSM6DSX_Model_t  detected_model;   /**< фактически определённая модель:
                                             результат автопоиска либо просто
                                             копия config.model при ручном
                                             выборе */
    uint8_t          whoami;           /**< сырое значение регистра WHO_AM_I */
    uint8_t          index;            /**< позиция в пуле (для справки)    */

    /* ---- Внутреннее состояние - не трогать напрямую, только через API ---- */
    uint8_t  used;                     /* слот занят (бухгалтерия пула)     */
    float    xl_sensitivity;           /* g на младший разряд АЦП           */
    float    g_sensitivity;            /* dps на младший разряд АЦП         */

    /* Состояние фильтра EMA: 0=X,1=Y,2=Z акселерометра, 3..5 - гироскоп. */
    float    ema_state[6];
    uint8_t  ema_valid;                /* 0 - ещё не было ни одного отсчёта */
    float    ema_alpha;                /* вес нового отсчёта, (0..1]        */

    /* Состояние фильтра "скользящее среднее": кольцевой буфер на канал. */
    float    ma_buffer[6][LSM6DSX_FILTER_MAX_WINDOW];
    uint16_t ma_write_index;
    uint16_t ma_count;                 /* сколько отсчётов реально накоплено */
    uint16_t ma_window;                /* текущий размер окна (по level)     */
} LSM6DSX_Handle_t;

/* ------------------------------------------------------------------------ */
/*  Регистрация экземпляра (единственная блокирующая функция модуля)        */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Регистрирует датчик: настраивает шину/CS, определяет или проверяет
 *         модель по WHO_AM_I, записывает регистры конфигурации (BDU,
 *         авто-инкремент адреса, режим SPI, ODR/FS акселерометра и
 *         гироскопа), инициализирует состояние фильтра. Повторный вызов с
 *         тем же "физическим" идентификатором (интерфейс + шина + CS/адрес)
 *         идемпотентен - вернёт указатель на ТОТ ЖЕ хэндл, применив новый
 *         config поверх него.
 * @param  config  заполненная конфигурация
 * @retval указатель на хэндл, либо NULL при ошибке (config некорректен,
 *         WHO_AM_I не совпал с ожидаемым/ни с одним известным значением,
 *         ошибка шины, либо исчерпан LSM6DSX_MAX_DEVICES)
 */
LSM6DSX_Handle_t *LSM6DSX_Init(const LSM6DSX_Config_t *config);

/* ------------------------------------------------------------------------ */
/*  Операционные функции - быстрые, неблокирующие                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Читает акселерометр (одна короткая транзакция по шине, 6 байт) и
 *         обновляет фильтр, если он включён в конфигурации.
 * @param  h     хэндл датчика
 * @param  x_g, y_g, z_g  выходные значения, g (9.81 м/с^2); NULL - не писать
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_ReadAccel(LSM6DSX_Handle_t *h, float *x_g, float *y_g, float *z_g);

/**
 * @brief  Читает гироскоп (одна короткая транзакция по шине, 6 байт) и
 *         обновляет фильтр, если он включён в конфигурации.
 * @param  h     хэндл датчика
 * @param  x_dps, y_dps, z_dps  выходные значения, градус/с; NULL - не писать
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_ReadGyro(LSM6DSX_Handle_t *h, float *x_dps, float *y_dps, float *z_dps);

/**
 * @brief  Читает оба сенсора в сыром виде (без масштабирования и без
 *         фильтрации) - самый быстрый вариант опроса, если физические
 *         единицы не нужны на этом уровне приложения.
 * @param  h           хэндл датчика
 * @param  accel_raw   массив [3] X/Y/Z, LSB акселерометра; NULL - пропустить
 * @param  gyro_raw    массив [3] X/Y/Z, LSB гироскопа; NULL - пропустить
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_ReadRaw(LSM6DSX_Handle_t *h, int16_t accel_raw[3], int16_t gyro_raw[3]);

/**
 * @brief  Читает STATUS_REG - готовность новых данных.
 * @param  h             хэндл датчика
 * @param  accel_ready   1, если есть новый отсчёт акселерометра; может быть NULL
 * @param  gyro_ready    1, если есть новый отсчёт гироскопа; может быть NULL
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_IsDataReady(LSM6DSX_Handle_t *h, uint8_t *accel_ready, uint8_t *gyro_ready);

/**
 * @brief  Меняет тип/уровень фильтрации во время работы и сбрасывает
 *         внутреннее состояние фильтра (буфер/накопитель EMA) - следующий
 *         отсчёт после вызова становится точкой отсчёта заново.
 * @param  h      хэндл датчика
 * @param  type   новый тип фильтра
 * @param  level  0..LSM6DSX_FILTER_LEVEL_MAX
 * @retval HAL_OK; HAL_ERROR, если h == NULL или level вне диапазона
 */
HAL_StatusTypeDef LSM6DSX_SetFilter(LSM6DSX_Handle_t *h, LSM6DSX_FilterType_t type, uint8_t level);

/**
 * @brief  Переводит акселерометр и гироскоп в режим power-down (ODR = 0).
 *         Конфигурация в хэндле не теряется - см. LSM6DSX_Restart().
 * @param  h  хэндл датчика
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_PowerDown(LSM6DSX_Handle_t *h);

/**
 * @brief  Восстанавливает ODR акселерометра/гироскопа из сохранённой в
 *         хэндле конфигурации после LSM6DSX_PowerDown().
 * @param  h  хэндл датчика
 * @retval HAL_OK; HAL_ERROR, если h == NULL; иной HAL_StatusTypeDef - ошибка шины
 */
HAL_StatusTypeDef LSM6DSX_Restart(LSM6DSX_Handle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* LSM6DSX_H */
