# Компиляция программы

## Требования

- Компилятор с поддержкой C++17 (MSVC, GCC, Clang)
- CMake 3.10+ (опционально)
- SQLite3 (dev-пакет)

## Установка SQLite3

### Windows (MSVC)
1. Скачайте `sqlite3.dll` и `sqlite3.lib` с https://www.sqlite.org/download.html
2. Разместите `sqlite3.lib` в директории `lib/` рядом с проектом
3. Разместите `sqlite3.dll` в директории с исполняемым файлом или в `PATH`
4. Скачайте `sqlite3.h` и разместите в `include/`

### Linux
```bash
sudo apt install libsqlite3-dev   # Debian/Ubuntu
sudo dnf install sqlite-devel     # Fedora
```

### macOS
```bash
brew install sqlite3
```

## Компиляция

### Через CMake (рекомендуется)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Напрямую через MSVC (Windows)

```bash
cl /EHsc /std:c++17 /I include local_analyzer.cpp /link lib\sqlite3.lib
```

### Напрямую через GCC (Linux/macOS)

```bash
g++ -std=c++17 -o local_analyzer local_analyzer.cpp -lsqlite3
```

## Запуск

```bash
./local_analyzer
```

Программа создаст `activity_local.db` в текущей директории.
