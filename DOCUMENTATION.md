# Анализатор следов текстовых редакторов

## Описание

Программа анализирует историю открытых файлов в **Блокноте Windows (Notepad)** и **Notepad++**,
извлекает пути к файлам и сохраняет их в SQLite-базу данных (`activity_local.db`) со статистикой.

## Как это работает

### 1. Поиск данных Блокнота (Notepad)
- Читает бинарные файлы `.bin` из папки `TabState`:
  ```
  %USERPROFILE%\AppData\Local\Packages\
  Microsoft.WindowsNotepad_8wekyb3d8bbwe\LocalState\TabState
  ```
- Путь определяется динамически через переменную окружения `%USERPROFILE%`
- Парсит UTF-16LE строки внутри `.bin` файлов, извлекая пути к открытым файлам

### 2. Поиск данных Notepad++
- Читает `session.xml` и `config.xml` из текущей директории
- Извлекает атрибуты `filename` и `name` с путями к файлам
- Использует FILETIME-штампы из XML

### 3. Обработка данных
- Удаление дубликатов
- Сохранение в SQLite БД (`activity_local.db`)
- Вывод статистики: по дням, по часам, по расширениям файлов

## Структура проекта

```
C++_NOTEPADE_ANALIZER/
├── local_analyzer.cpp   # Исходный код программы
├── COMPILE.md           # Инструкция по компиляции
├── DOCUMENTATION.md     # Документация (этот файл)
├── activity_local.db    # База данных (создаётся при запуске)
├── session.xml          # (опционально) сессия Notepad++
├── config.xml           # (опционально) конфиг Notepad++
└── CMakeLists.txt       # (опционально) файл сборки CMake
```

## Структура таблицы SQLite

```sql
CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    app TEXT,              -- "Notepad" или "Notepad++"
    path TEXT,             -- полный путь к файлу
    timestamp INTEGER,     -- Unix timestamp
    extension TEXT,        -- расширение файла
    date_str TEXT          -- дата в формате YYYY-MM-DD HH:MM:SS
);
```

## Пример вывода

```
=== Анализ следов текстовых редакторов (локальная версия) ===

[Шаг 1] Поиск данных Блокнота...
Сканирование директории: C:\Users\user\AppData\Local\Packages\...
  Обработка файла: 1.bin
    Найдено: C:\Projects\main.cpp
Найдено файлов Блокнота: 5

[Шаг 2] Поиск данных Notepad++...
Чтение session.xml: ...
  Найдено: D:\Docs\readme.md
Найдено файлов Notepad++: 3

=== Результаты ===
[Notepad] C:\Projects\main.cpp [2025-12-01 14:30:00]

=== Временная шкала активности (по дням) ===
2025-12-01: 3 файлов

=== Временная шкала активности (по часам) ===
14:00 - 2 файлов

=== Статистика по типам файлов ===
.cpp: 2 (40.0%)
.md: 1 (20.0%)
```
