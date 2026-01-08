# Инструкция по выгрузке на GitHub

Репозиторий уже инициализирован и файлы подготовлены. Следуйте этим шагам для выгрузки на GitHub:

## Шаг 1: Настройка Git (если еще не настроено)

Если git еще не настроен, выполните:

```bash
git config --global user.email "ваш@email.com"
git config --global user.name "Ваше Имя"
```

## Шаг 2: Создание репозитория на GitHub

1. Зайдите на https://github.com
2. Нажмите кнопку **"New repository"** (или перейдите по прямой ссылке: https://github.com/new)
3. Заполните:
   - **Repository name:** `i2c-pressure-sensor` (или любое другое название)
   - **Description:** `I2C Pressure Sensor 0-10 bar examples and practical notes for ESP32`
   - Выберите **Public** или **Private**
   - **НЕ** создавайте README, .gitignore или лицензию (они уже есть)
4. Нажмите **"Create repository"**

## Шаг 3: Подключение удаленного репозитория

После создания репозитория GitHub покажет инструкции. Выполните команды (замените `USERNAME` на ваш GitHub username):

```bash
cd C:\AI\Dev\11_pressure_i2c
git remote add origin https://github.com/USERNAME/i2c-pressure-sensor.git
```

Или если используете SSH:

```bash
git remote add origin git@github.com:USERNAME/i2c-pressure-sensor.git
```

## Шаг 4: Создание первого коммита

Если git config еще не настроен, сначала настройте:

```bash
git config user.email "ваш@email.com"
git config user.name "Ваше Имя"
```

Затем создайте коммит:

```bash
git add .
git commit -m "Initial commit: I2C pressure sensor examples and documentation"
```

## Шаг 5: Выгрузка на GitHub

```bash
git branch -M main
git push -u origin main
```

Если появится запрос авторизации:
- Для HTTPS: используйте Personal Access Token (не пароль)
- Для SSH: убедитесь что SSH ключ добавлен в GitHub

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

