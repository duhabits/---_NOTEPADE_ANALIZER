# Анализатор следов текстовых редакторов

Извлекает историю открытых файлов из **Блокнота (Notepad)** и **Notepad++** и сохраняет данные в SQLite-базу со статистикой по дням, часам и расширениям.

## Как это работает

1. **Блокнот** — парсит бинарные `.bin` файлы из `%USERPROFILE%\AppData\Local\Packages\Microsoft.WindowsNotepad_8wekyb3d8bbwe\LocalState\TabState`, извлекая UTF-16LE пути к открытым файлам.
2. **Notepad++** — читает `session.xml` и `config.xml` из `%APPDATA%\Notepad++` и директории пользователя `shafi`, извлекая атрибуты `filename` / `name`.
3. Данные сохраняются в `activity_local.db` (SQLite) с удалением дубликатов.
4. Выводятся активность по дням, по часам и статистика по расширениям.

## Структура проекта

```
├── local_analyzer.cpp   # Исходный код
├── activity_local.db    # База данных (создаётся при запуске)
├── COMPILE.md           # Инструкция по компиляции
├── DOCUMENTATION.md     # Подробная документация
└── README.md            # Этот файл
```

## Требования

- Компилятор C++17 (MSVC, GCC, Clang)
- SQLite3 (dev-пакет / sqlite3.lib)
- Windows (для анализа Блокнота), частично работает на Linux/macOS

## Сборка и запуск

```bash
# CMake
mkdir build && cd build && cmake .. && cmake --build .

# MSVC
cl /EHsc /std:c++17 /I include local_analyzer.cpp /link lib\sqlite3.lib

# GCC (Linux/macOS)
g++ -std=c++17 -o local_analyzer local_analyzer.cpp -lsqlite3
```

Подробнее — [COMPILE.md](COMPILE.md).

## Использование

```bash
./local_analyzer
```

Программа создаст `activity_local.db` в текущей директории.

## Структура БД

```sql
CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    app TEXT,              -- "Notepad" или "Notepad++"
    path TEXT,             -- полный путь к файлу
    filename TEXT,         -- имя файла
    timestamp INTEGER,     -- Unix timestamp
    extension TEXT,        -- расширение
    date_str TEXT          -- дата (YYYY-MM-DD HH:MM:SS)
);
```

## Пример вывода

```
[Notepad] main.cpp [2025-12-01 14:30:00]  (C:\Projects\main.cpp)
[Notepad++] readme.md [2025-12-01 15:00:00]  (D:\Docs\readme.md)

=== Временная шкала активности (по дням) ===
2025-12-01: 5 файлов

=== Статистика по типам файлов ===
.cpp: 2 (40.0%)
.md:  1 (20.0%)
```
