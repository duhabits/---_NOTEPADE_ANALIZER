#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <cstdlib>
#include <set>
#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

#include <sqlite3.h>

namespace fs = std::filesystem;

// Файловые константы
static fs::path getTabStateDir()
{
    const char *userProfile = std::getenv("USERPROFILE");
    if (userProfile)
    {
        return fs::path(userProfile) / "AppData" / "Local" / "Packages" /
               "Microsoft.WindowsNotepad_8wekyb3d8bbwe" / "LocalState" / "TabState";
    }
    // fallback
    return fs::path("C:") / "Users" / "Default" / "AppData" / "Local" / "Packages" /
           "Microsoft.WindowsNotepad_8wekyb3d8bbwe" / "LocalState" / "TabState";
}

constexpr auto SESSION_FILE = "session.xml";
constexpr auto CONFIG_FILE = "config.xml";
constexpr auto DATABASE_FILE = "activity_local.db";
constexpr auto APP_NOTEPAD = "Notepad";
constexpr auto APP_NOTEPAD_PP = "Notepad++";
constexpr auto BIN_EXT = ".bin";

// Структура для хранения информации о файле
struct FileInfo

{
    std::string app_name;
    std::string file_path;
    std::string filename;
    std::time_t timestamp;
    std::string extension;

    // Оператор сравнения для удаления дубликатов
    bool operator<(const FileInfo &other) const
    {
        if (file_path != other.file_path)
            return file_path < other.file_path;
        if (app_name != other.app_name)
            return app_name < other.app_name;
        return timestamp < other.timestamp;
    }

    bool operator==(const FileInfo &other) const
    {
        return file_path == other.file_path && app_name == other.app_name;
    }
};

static fs::path getNotepadPlusPlusDir()
{
    const char *userProfile = std::getenv("USERPROFILE");
    if (userProfile)
    {
        return fs::path(userProfile) / "AppData" / "Roaming" / "Notepad++";
    }
    return fs::path("C:") / "Users" / "Default" / "AppData" / "Roaming" / "Notepad++";
}

// Получить расширение файла из пути
std::string getExtension(const std::string &path)
{
    size_t pos = path.find_last_of('.');
    if (pos != std::string::npos && pos < path.length() - 1)
    {
        return path.substr(pos + 1);
    }
    return "";
}

// Преобразовать UTF-16LE строку в UTF-8
std::string utf16leToUtf8(const std::vector<char> &data, size_t start, size_t len)
{
    std::string result;
    for (size_t i = start; i + 1 < start + len && i + 1 < data.size(); i += 2)
    {
        unsigned char lo = static_cast<unsigned char>(data[i]);
        unsigned char hi = static_cast<unsigned char>(data[i + 1]);
        if (lo == 0 && hi == 0)
            break;
        uint32_t cp = lo | (static_cast<uint32_t>(hi) << 8);

        if (cp < 0x80)
        {
            result += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0xD800 || cp > 0xDFFF)
        {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < start + len && i + 3 < data.size())
        {
            unsigned char lo2 = static_cast<unsigned char>(data[i + 2]);
            unsigned char hi2 = static_cast<unsigned char>(data[i + 3]);
            uint32_t cp2 = lo2 | (static_cast<uint32_t>(hi2) << 8);
            if (cp2 >= 0xDC00 && cp2 <= 0xDFFF)
            {
                uint32_t sp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
                result += static_cast<char>(0xF0 | (sp >> 18));
                result += static_cast<char>(0x80 | ((sp >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((sp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (sp & 0x3F));
                i += 2;
            }
        }
    }
    return result;
}

// Получить время модификации файла
std::time_t getFileModificationTime(const fs::path &path)
{
    try
    {
#ifdef _WIN32
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return std::time(nullptr);

        FILETIME ft;
        if (!GetFileTime(hFile, nullptr, nullptr, &ft))
        {
            CloseHandle(hFile);
            return std::time(nullptr);
        }
        CloseHandle(hFile);

        // FILETIME to Unix timestamp
        uint64_t filetime = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return static_cast<std::time_t>((filetime / 10000000) - 11644473600LL);
#else
        auto ftime = fs::last_write_time(path);
        auto sctp = std::chrono::system_clock::now();
        return std::chrono::system_clock::to_time_t(sctp);
#endif
    }
    catch (...)
    {
        return std::time(nullptr);
    }
}

// Конвертировать Windows FILETIME (100-наносекундные интервалы с 1 января 1601) в Unix timestamp
std::time_t fileTimeToUnixTime(uint32_t low, uint32_t high)
{
    uint64_t filetime = ((uint64_t)high << 32) | low;
    // FILETIME to Unix timestamp: subtract 11644473600 seconds (difference between 1601 and 1970)
    return (std::time_t)((filetime / 10000000) - 11644473600LL);
}

// Удалить дубликаты из вектора
std::vector<FileInfo> removeDuplicates(std::vector<FileInfo> files)
{
    std::set<FileInfo> uniqueFiles(files.begin(), files.end());
    return std::vector<FileInfo>(uniqueFiles.begin(), uniqueFiles.end());
}

// Парсинг бинарных файлов Блокнота из текущей директории
std::vector<FileInfo> parseNotepadBinFiles(const fs::path &tabStateDir)
{
    std::vector<FileInfo> results;

    if (!fs::exists(tabStateDir))
    {
        std::cerr << "Директория TabState не найдена: " << tabStateDir << std::endl;
        return results;
    }

    std::cout << "Сканирование директории: " << tabStateDir << std::endl;

    for (const auto &entry : fs::directory_iterator(tabStateDir))
    {
        if (entry.path().extension() == BIN_EXT)
        {
            std::cout << "  Обработка файла: " << entry.path().filename() << std::endl;

            std::ifstream file(entry.path(), std::ios::binary);
            if (!file)
                continue;

            std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
            file.close();

            // Ищем пути в формате UTF-16LE (C:, D:, и т.д.)
            for (size_t i = 0; i + 4 < buffer.size(); ++i)
            {
                // Проверяем на букву диска в UTF-16LE
                if ((buffer[i] >= 'A' && buffer[i] <= 'Z') &&
                    buffer[i + 2] == ':' && buffer[i + 1] == 0)
                {

                    // Нашли потенциальный путь, извлекаем его
                    size_t start = i;
                    size_t end = start;

                    // Ищем конец строки (нулевой байт в начале UTF-16LE пары)
                    while (end + 1 < buffer.size())
                    {
                        if (buffer[end] == 0)
                            break;
                        if (buffer[end] == '\\' && buffer[end + 1] == 0 &&
                            end + 3 < buffer.size() &&
                            buffer[end + 2] == '\\' && buffer[end + 3] == 0)
                        {
                            break;
                        }
                        end += 2;
                    }

                    std::string path = utf16leToUtf8(buffer, start, end - start);

                    // Проверяем, что путь выглядит как настоящий путь к файлу
                    if (path.length() > 4 && path.find("\\") != std::string::npos)
                    {
                        FileInfo info;
                        info.app_name = APP_NOTEPAD;
                        info.file_path = path;
                        info.filename = fs::path(path).filename().string();
                        info.timestamp = getFileModificationTime(entry.path());
                        info.extension = getExtension(path);
                        results.push_back(info);
                        std::cout << "    Найдено: " << path << std::endl;
                    }
                }
            }
        }
    }

    return results;
}

// Извлечь атрибут из XML строки
std::string extractXmlAttribute(const std::string &line, const std::string &attrName)
{
    std::string pattern = attrName + "=\"";
    size_t pos = line.find(pattern);
    if (pos == std::string::npos)
        return "";

    size_t start = pos + pattern.length();
    size_t end = line.find("\"", start);
    if (end == std::string::npos)
        return "";

    return line.substr(start, end - start);
}

// Парсинг session.xml от Notepad++ из текущей директории
std::vector<FileInfo> parseNotepadPlusPlusSession(const fs::path &sessionPath)
{
    std::vector<FileInfo> results;

    if (!fs::exists(sessionPath))
    {
        std::cerr << "Файл session.xml не найден: " << sessionPath << std::endl;
        return results;
    }

    std::cout << "Чтение session.xml: " << sessionPath << std::endl;

    std::time_t xmlTime = getFileModificationTime(sessionPath);

    std::ifstream file(sessionPath);
    if (!file)
    {
        std::cerr << "Не удалось открыть session.xml" << std::endl;
        return results;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Ищем теги <File ... filename="...">
        if (line.find("<File ") != std::string::npos)
        {
            std::string filePath = extractXmlAttribute(line, "filename");
            std::string timestampLow = extractXmlAttribute(line, "originalFileLastModifTimestamp");
            std::string timestampHigh = extractXmlAttribute(line, "originalFileLastModifTimestampHigh");

            if (!filePath.empty() && filePath.length() > 2 && filePath[1] == ':')
            {
                FileInfo info;
                info.app_name = APP_NOTEPAD_PP;
                info.file_path = filePath;
                info.filename = fs::path(filePath).filename().string();
                info.extension = getExtension(filePath);

                // Используем timestamp из XML если доступен
                if (!timestampLow.empty() && !timestampHigh.empty())
                {
                    try
                    {
                        uint32_t low = std::stoul(timestampLow);
                        uint32_t high = std::stoul(timestampHigh);
                        info.timestamp = fileTimeToUnixTime(low, high);
                        std::cout << "  Найдено: " << filePath << " (timestamp из XML)" << std::endl;
                    }
                    catch (...)
                    {
                        info.timestamp = xmlTime;
                        std::cout << "  Найдено: " << filePath << " (timestamp файла)" << std::endl;
                    }
                }
                else
                {
                    info.timestamp = xmlTime;
                    std::cout << "  Найдено: " << filePath << " (timestamp файла)" << std::endl;
                }

                results.push_back(info);
            }
        }
    }

    file.close();
    return results;
}

// Парсинг config.xml от Notepad++ из текущей директории
std::vector<FileInfo> parseNotepadPlusPlusConfig(const fs::path &configPath)
{
    std::vector<FileInfo> results;

    if (!fs::exists(configPath))
    {
        std::cerr << "Файл config.xml не найден: " << configPath << std::endl;
        return results;
    }

    std::cout << "Чтение config.xml: " << configPath << std::endl;

    std::time_t xmlTime = getFileModificationTime(configPath);

    std::ifstream file(configPath);
    if (!file)
    {
        std::cerr << "Не удалось открыть config.xml" << std::endl;
        return results;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Ищем теги <File name="...">
        std::string filePath = extractXmlAttribute(line, "name");
        if (!filePath.empty())
        {
            // Проверяем, что путь начинается с буквы диска
            if (filePath.length() > 2 && filePath[1] == ':')
            {
                FileInfo info;
                info.app_name = APP_NOTEPAD_PP;
                info.file_path = filePath;
                info.filename = fs::path(filePath).filename().string();
                info.timestamp = xmlTime;
                info.extension = getExtension(filePath);
                results.push_back(info);
                std::cout << "  Найдено: " << filePath << std::endl;
            }
        }
    }

    file.close();
    return results;
}

// Создать таблицу в SQLite
bool createTable(sqlite3 *db)
{
    const char *sql = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY,
            app TEXT,
            path TEXT,
            filename TEXT,
            timestamp INTEGER,
            extension TEXT,
            date_str TEXT
        )
    )";

    char *errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK)
    {
        std::cerr << "Ошибка создания таблицы: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

// Преобразовать timestamp в строку даты
std::string timestampToDateStr(std::time_t ts)
{
    char buf[64];
    struct tm t;
    localtime_s(&t, &ts);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return buf;
}

// Вставить данные в SQLite
bool insertFileInfo(sqlite3 *db, const FileInfo &info)
{
    const char *sql = "INSERT INTO files (app, path, filename, timestamp, extension, date_str) VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Ошибка подготовки запроса" << std::endl;
        return false;
    }

    std::string dateStr = timestampToDateStr(info.timestamp);

    sqlite3_bind_text(stmt, 1, info.app_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, info.file_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, info.filename.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(info.timestamp));
    sqlite3_bind_text(stmt, 5, info.extension.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, dateStr.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        std::cerr << "Ошибка вставки данных" << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
#else
    setlocale(LC_ALL, "ru_RU.UTF-8");
#endif

    std::cout << "=== Анализ следов текстовых редакторов (локальная версия) ===" << std::endl;
    std::cout << "Рабочая директория: " << fs::current_path() << std::endl;

    // Шаг 1: Найти данные Блокнота из локальной папки TabState
    std::cout << "\n[Шаг 1] Поиск данных Блокнота..." << std::endl;
    fs::path notepadDir = getTabStateDir();

    std::vector<FileInfo> notepadFiles = parseNotepadBinFiles(notepadDir);
    std::cout << "Найдено файлов Блокнота: " << notepadFiles.size() << std::endl;

    // Шаг 2: Найти данные Notepad++ из session.xml и config.xml
    std::cout << "\n[Шаг 2] Поиск данных Notepad++..." << std::endl;

    std::vector<FileInfo> notepadPlusPlusFiles;

    // Поиск в папке Notepad++ (%APPDATA%/Notepad++)
    fs::path nppDir = getNotepadPlusPlusDir();
    fs::path nppSessionRoaming = nppDir / SESSION_FILE;
    fs::path nppConfigRoaming = nppDir / CONFIG_FILE;

    std::vector<FileInfo> sessionRoamingFiles = parseNotepadPlusPlusSession(nppSessionRoaming);
    notepadPlusPlusFiles.insert(notepadPlusPlusFiles.end(), sessionRoamingFiles.begin(), sessionRoamingFiles.end());

    std::vector<FileInfo> configRoamingFiles = parseNotepadPlusPlusConfig(nppConfigRoaming);
    notepadPlusPlusFiles.insert(notepadPlusPlusFiles.end(), configRoamingFiles.begin(), configRoamingFiles.end());

    // Поиск в папке Notepad++ для пользователя shafi
    fs::path nppShafiDir = fs::path("C:") / "Users" / "shafi" / "AppData" / "Roaming" / "Notepad++";
    fs::path nppSessionShafi = nppShafiDir / SESSION_FILE;
    fs::path nppConfigShafi = nppShafiDir / CONFIG_FILE;

    std::vector<FileInfo> sessionShafiFiles = parseNotepadPlusPlusSession(nppSessionShafi);
    notepadPlusPlusFiles.insert(notepadPlusPlusFiles.end(), sessionShafiFiles.begin(), sessionShafiFiles.end());

    std::vector<FileInfo> configShafiFiles = parseNotepadPlusPlusConfig(nppConfigShafi);
    notepadPlusPlusFiles.insert(notepadPlusPlusFiles.end(), configShafiFiles.begin(), configShafiFiles.end());

    std::cout << "Найдено файлов Notepad++: " << notepadPlusPlusFiles.size() << std::endl;

    // Шаг 3: Удалить дубликаты
    std::cout << "\n[Шаг 3] Удаление дубликатов..." << std::endl;
    notepadFiles = removeDuplicates(notepadFiles);
    notepadPlusPlusFiles = removeDuplicates(notepadPlusPlusFiles);
    std::cout << "После удаления дубликатов - Блокнот: " << notepadFiles.size()
              << ", Notepad++: " << notepadPlusPlusFiles.size() << std::endl;

    // Шаг 4: Работа с SQLite
    std::cout << "\n[Шаг 4] Создание базы данных SQLite..." << std::endl;

    sqlite3 *db;
    if (sqlite3_open(DATABASE_FILE, &db) != SQLITE_OK)
    {
        std::cerr << "Не удалось открыть базу данных: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    // Удаляем старую таблицу и создаём заново (на случай изменения схемы)
    char *errMsg = nullptr;
    sqlite3_exec(db, "DROP TABLE IF EXISTS files", nullptr, nullptr, &errMsg);

    if (!createTable(db))
    {
        sqlite3_close(db);
        return 1;
    }

    // Вставка данных из Блокнота
    std::cout << "Вставка данных Блокнота..." << std::endl;
    for (const auto &info : notepadFiles)
    {
        if (!insertFileInfo(db, info))
        {
            std::cerr << "Ошибка вставки: " << info.file_path << std::endl;
        }
    }

    // Вставка данных из Notepad++
    std::cout << "Вставка данных Notepad++..." << std::endl;
    for (const auto &info : notepadPlusPlusFiles)
    {
        if (!insertFileInfo(db, info))
        {
            std::cerr << "Ошибка вставки: " << info.file_path << std::endl;
        }
    }

    // Вывод результатов
    std::cout << "\n=== Результаты ===" << std::endl;

    const char *query = "SELECT app, path, filename, timestamp, extension, date_str FROM files";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK)
    {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *app = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            const char *path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            const char *fname = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            const char *dateStr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));

            std::cout << "[" << (app ? app : "N/A") << "] "
                      << (fname ? fname : "N/A")
                      << " [" << (dateStr ? dateStr : "N/A") << "]"
                      << "  (" << (path ? path : "N/A") << ")" << std::endl;
            count++;
        }

        if (count == 0)
        {
            std::cout << "(нет данных)" << std::endl;
        }

        sqlite3_finalize(stmt);
    }

    // Статистика
    std::cout << "\n=== Временная шкала активности (по дням) ===" << std::endl;

    const char *dayQuery = "SELECT date(datetime(timestamp, 'unixepoch')) as day, COUNT(*) as cnt FROM files GROUP BY day ORDER BY day";
    if (sqlite3_prepare_v2(db, dayQuery, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *day = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            int cnt = sqlite3_column_int(stmt, 1);
            std::cout << (day ? day : "N/A") << ": " << cnt << " файлов" << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    std::cout << "\n=== Временная шкала активности (по часам) ===" << std::endl;

    const char *hourQuery = "SELECT strftime('%H', datetime(timestamp, 'unixepoch')) as hour, COUNT(*) as cnt FROM files GROUP BY hour ORDER BY hour";
    if (sqlite3_prepare_v2(db, hourQuery, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *hour = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            int cnt = sqlite3_column_int(stmt, 1);
            std::cout << (hour ? hour : "N/A") << ":00 - " << cnt << " файлов" << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    int totalFiles = 0;
    const char *countQuery = "SELECT COUNT(*) FROM files";
    if (sqlite3_prepare_v2(db, countQuery, -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            totalFiles = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    std::cout << "\n=== Статистика по типам файлов ===" << std::endl;

    const char *extQuery = "SELECT extension, COUNT(*) as cnt FROM files GROUP BY extension ORDER BY cnt DESC";
    if (sqlite3_prepare_v2(db, extQuery, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *ext = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            int cnt = sqlite3_column_int(stmt, 1);
            int pctWhole = totalFiles > 0 ? (cnt * 1000 / totalFiles) : 0;
            std::cout << "." << (ext && ext[0] ? ext : "(без расширения)")
                      << ": " << cnt << " (" << (pctWhole / 10) << "." << (pctWhole % 10) << "%)" << std::endl;
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    std::cout << "\nБаза данных сохранена в: " << DATABASE_FILE << std::endl;
    std::cout << "Готово!" << std::endl;

    return 0;
}
