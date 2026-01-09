# Настройка доступа к GitHub для Cursor IDE

Чтобы я мог самостоятельно работать с GitHub из Cursor IDE, нужно настроить один из вариантов:

## Вариант 1: GitHub CLI (gh) - Рекомендуется ⭐

Самый удобный способ - установить GitHub CLI и авторизоваться:

### Установка GitHub CLI

1. Скачайте с https://cli.github.com/
2. Или через winget:
```powershell
winget install --id GitHub.cli
```

### Авторизация

```powershell
gh auth login
```

Выберите:
- GitHub.com
- HTTPS
- Authenticate Git with your GitHub credentials? → Yes
- Login with a web browser → Yes

После этого я смогу:
- Создавать репозитории через `gh repo create`
- Делать push/pull
- Работать с issues и PR

---

## Вариант 2: Personal Access Token в переменной окружения

### Создание токена

1. Перейдите на https://github.com/settings/tokens
2. Нажмите "Generate new token (classic)"
3. Название: `Cursor IDE Access`
4. Права: **repo** (полный доступ к репозиториям)
5. Скопируйте токен

### Сохранение токена

**Windows (PowerShell):**

```powershell
# Для текущей сессии
$env:GITHUB_TOKEN = "ваш_токен_здесь"

# Постоянно (для текущего пользователя)
[System.Environment]::SetEnvironmentVariable('GITHUB_TOKEN', 'ваш_токен_здесь', 'User')
```

**Или через GUI:**
1. Win + R → `sysdm.cpl` → Advanced → Environment Variables
2. User variables → New
3. Variable name: `GITHUB_TOKEN`
4. Variable value: ваш токен

После перезапуска Cursor IDE токен будет доступен.

---

## Вариант 3: Git Credential Manager

Windows обычно уже имеет Git Credential Manager. Настройка:

```powershell
git config --global credential.helper manager-core
```

При первом `git push` он попросит авторизоваться через браузер и сохранит credentials.

---

## Вариант 4: SSH ключи

### Генерация SSH ключа

```powershell
ssh-keygen -t ed25519 -C "shururik@gmail.com"
```

### Добавление в GitHub

1. Скопируйте публичный ключ:
```powershell
cat ~/.ssh/id_ed25519.pub
```

2. Добавьте на https://github.com/settings/keys
3. Настройте git:
```powershell
git config --global url."git@github.com:".insteadOf "https://github.com/"
```

---

## Проверка настройки

После настройки проверьте:

```powershell
# Для GitHub CLI
gh auth status

# Для токена
if ($env:GITHUB_TOKEN) { Write-Host "Token OK" } else { Write-Host "Token missing" }

# Для git credentials
git config --global credential.helper
```

---

## Рекомендация

**Лучший вариант:** GitHub CLI (`gh`) - он безопасно хранит токен и предоставляет удобный API для всех операций.

После настройки я смогу:
- ✅ Создавать репозитории автоматически
- ✅ Делать push/pull без запросов
- ✅ Работать с issues, PR, releases
- ✅ Управлять настройками репозиториев

