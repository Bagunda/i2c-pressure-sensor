# Инструкция по выгрузке на GitHub

Репозиторий уже инициализирован, коммит создан, файлы готовы к выгрузке.

## Быстрый способ (рекомендуется)

### Вариант 1: Автоматический скрипт (требуется токен)

1. Создайте Personal Access Token на https://github.com/settings/tokens
   - Нажмите "Generate new token (classic)"
   - Выберите права: **repo** (полный доступ к репозиториям)
   - Скопируйте токен

2. Запустите скрипт:
```powershell
cd C:\AI\Dev\11_pressure_i2c
.\create_github_repo.ps1 -Token YOUR_TOKEN_HERE
```

Скрипт автоматически:
- Создаст репозиторий `i2c-pressure-sensor` на GitHub
- Добавит remote
- Выгрузит код

### Вариант 2: Вручную через веб-интерфейс

1. Создайте репозиторий на https://github.com/new:
   - **Repository name:** `i2c-pressure-sensor`
   - **Description:** `I2C Pressure Sensor 0-10 bar examples and practical notes for ESP32`
   - Выберите **Public** или **Private**
   - **НЕ** создавайте README, .gitignore или лицензию

2. Выполните команды:
```powershell
cd C:\AI\Dev\11_pressure_i2c
git remote add origin https://github.com/shururik/i2c-pressure-sensor.git
git branch -M main
git push -u origin main
```

При запросе авторизации используйте Personal Access Token (не пароль).

## Готово!

После успешной выгрузки репозиторий будет доступен по адресу:
`https://github.com/USERNAME/i2c-pressure-sensor`

---

## Структура репозитория

```
i2c-pressure-sensor/
├── README.md                          # Главный README
├── .gitignore                         # Игнорируемые файлы
├── examples/
│   ├── direct_connection/            # Прямое подключение к ESP32
│   │   ├── Minimal_for_all.ino
│   │   └── README.md
│   └── pca9548a_multiplexer/         # Работа через PCA9548A
│       ├── pressure_i2c_pca9548a_min.ino
│       └── README.md
└── docs/
    └── manufacturer/                  # Документация производителя
        ├── README.md
        └── [файлы от производителя]
```

