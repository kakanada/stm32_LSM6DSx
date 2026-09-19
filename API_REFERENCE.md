# API Reference — lsm6dsx

> При расхождениях с `lsm6dsx.h` ориентируйтесь на заголовочный файл — он первичен, этот файл его
> сжатый пересказ.

## Оглавление

- [Типы](#типы)
- [Инициализация](#инициализация)
- [Операционные функции](#операционные-функции)

## Типы

### `LSM6DSX_Interface_t`

| Значение | Описание |
|---|---|
| `LSM6DSX_IF_SPI4` | SPI, полный дуплекс (4 провода) |
| `LSM6DSX_IF_SPI3` | SPI, 1 линия данных (3 провода, half-duplex) |
| `LSM6DSX_IF_I2C` | I2C |

### `LSM6DSX_Model_t`

`LSM6DSX_MODEL_AUTO` (только на вход — автоопределение), `LSM6DSX_MODEL_LSM6DS3`,
`LSM6DSX_MODEL_LSM6DS3TR_C`, `LSM6DSX_MODEL_LSM6DSL`, `LSM6DSX_MODEL_LSM6DSM`,
`LSM6DSX_MODEL_LSM6DSO`, `LSM6DSX_MODEL_LSM6DSOX`, `LSM6DSX_MODEL_LSM6DSO32`,
`LSM6DSX_MODEL_LSM6DSR`, `LSM6DSX_MODEL_LSM6DSRX`, `LSM6DSX_MODEL_ISM330DHCX`,
`LSM6DSX_MODEL_UNKNOWN` (только на выход — WHO_AM_I не совпал ни с одной моделью).

### `LSM6DSX_XLFullScale_t` / `LSM6DSX_GFullScale_t` / `LSM6DSX_Odr_t`

Перечисления диапазонов и частот опроса — см. Doxygen-комментарии в `lsm6dsx.h`, значения
соответствуют кодам регистров CTRL1_XL/CTRL2_G датчика.

### `LSM6DSX_FilterType_t`

| Значение | Когда использовать |
|---|---|
| `LSM6DSX_FILTER_NONE` | Фильтрация не нужна, важна минимальная задержка. |
| `LSM6DSX_FILTER_EMA` | Общий случай: O(1) память/время, малая задержка. |
| `LSM6DSX_FILTER_MOVING_AVG` | Нужна максимальная гладкость сигнала, задержка не критична. |

### `LSM6DSX_Config_t`

| Поле | Тип | Назначение |
|---|---|---|
| `interface` | `LSM6DSX_Interface_t` | Способ подключения. |
| `hspi` | `SPI_HandleTypeDef*` | Хэндл SPI (SPI4/SPI3). |
| `cs_port`, `cs_pin` | `GPIO_TypeDef*`, `uint16_t` | GPIO chip-select (SPI4/SPI3). |
| `hi2c` | `I2C_HandleTypeDef*` | Хэндл I2C (только I2C). |
| `i2c_address` | `uint8_t` | 7-битный адрес, `0x6A`/`0x6B` (только I2C). |
| `model` | `LSM6DSX_Model_t` | `LSM6DSX_MODEL_AUTO` либо конкретная модель. |
| `xl_full_scale`, `xl_odr` | — | Диапазон/частота акселерометра. |
| `g_full_scale`, `g_odr` | — | Диапазон/частота гироскопа. |
| `filter_type`, `filter_level` | `LSM6DSX_FilterType_t`, `uint8_t` | Тип и уровень (0..`LSM6DSX_FILTER_LEVEL_MAX`) фильтрации. |

### `LSM6DSX_Handle_t`

| Поле | Доступ | Назначение |
|---|---|---|
| `config` | публичное | Копия конфигурации, переданной в `Init`. |
| `detected_model` | публичное | Фактически определённая (или скопированная) модель. |
| `whoami` | публичное | Сырое значение регистра WHO_AM_I. |
| `index` | публичное | Позиция в статическом пуле (для справки). |
| прочие поля | внутренние | Не трогать напрямую, только через API. |

## Инициализация

### `LSM6DSX_Init`

```c
LSM6DSX_Handle_t *LSM6DSX_Init(const LSM6DSX_Config_t *config);
```

Единственная блокирующая функция модуля. Настраивает шину/CS, определяет или проверяет модель по
WHO_AM_I, записывает регистры конфигурации, инициализирует фильтр. Идемпотентна: повторный вызов с
тем же физическим идентификатором (интерфейс + шина + CS/адрес) возвращает тот же хэндл.

- **Возврат:** указатель на хэндл; `NULL` при некорректном `config`, несовпадении WHO_AM_I, ошибке
  шины либо исчерпанном `LSM6DSX_MAX_DEVICES`.

## Операционные функции

Все функции ниже — быстрые, неблокирующие (одна короткая транзакция по шине с таймаутом
`LSM6DSX_BUS_TIMEOUT_MS`), без циклов ожидания.

| Функция | Параметры | Возврат | Назначение |
|---|---|---|---|
| `LSM6DSX_ReadAccel` | `h`, `x_g*`, `y_g*`, `z_g*` | `HAL_StatusTypeDef` | Акселерометр, g, с фильтрацией. |
| `LSM6DSX_ReadGyro` | `h`, `x_dps*`, `y_dps*`, `z_dps*` | `HAL_StatusTypeDef` | Гироскоп, °/с, с фильтрацией. |
| `LSM6DSX_ReadRaw` | `h`, `accel_raw[3]`, `gyro_raw[3]` | `HAL_StatusTypeDef` | Сырые LSB, без масштаба и фильтра. |
| `LSM6DSX_IsDataReady` | `h`, `accel_ready*`, `gyro_ready*` | `HAL_StatusTypeDef` | Флаги готовности новых данных (STATUS_REG). |
| `LSM6DSX_SetFilter` | `h`, `type`, `level` | `HAL_StatusTypeDef` | Смена фильтра на лету, сброс его состояния. |
| `LSM6DSX_PowerDown` | `h` | `HAL_StatusTypeDef` | ODR акселерометра и гироскопа = 0. |
| `LSM6DSX_Restart` | `h` | `HAL_StatusTypeDef` | Восстановление ODR из конфигурации хэндла. |

Каждый указатель-выход может быть `NULL`, если конкретное значение не нужно вызывающему коду — оно
просто не записывается. `HAL_ERROR` возвращается всеми функциями при `h == NULL`; остальные
значения `HAL_StatusTypeDef` — как есть от вызова HAL SPI/I2C.
