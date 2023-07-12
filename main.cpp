#include <Windows.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#define TIMER_ID 1

const char *g_szClassName = "myWindowClass";
static HWND hEdit = NULL;


// Глобальные переменные для многопоточной обработки данных из COM-порта
std::mutex g_mutex;
std::condition_variable g_cv;
std::string g_message;
bool g_isReading = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
        {
            // Создаем текстовый контрол для вывода текста
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            SendMessage(hEdit, EM_LIMITTEXT, 0, 0);
            break;
        }
        case WM_TIMER:
            if (wParam == TIMER_ID)
            {
                // Очищаем содержимое текстового контрола
                SetWindowText(hEdit, "");
            }
            break;
        case WM_SIZE:
        {
            // Позиционируем текстовый контрол на всю площадь окна
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            SetWindowPos(hEdit, NULL, 0, 0, rcClient.right, rcClient.bottom, SWP_NOZORDER);
            break;
        }
        case WM_SETFOCUS:
        {
            SetFocus(hEdit);
            break;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        default:
        {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
    return 0;
}

// Функция для чтения данных из COM-порта в отдельном потоке
void ReadFromComPort()
{
    // Открываем COM-порт
    HANDLE hComm = CreateFile("COM7", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hComm != INVALID_HANDLE_VALUE)
    {
        DCB dcbSerialParams = {};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (GetCommState(hComm, &dcbSerialParams))
        {
            dcbSerialParams.BaudRate = CBR_115200;
            dcbSerialParams.ByteSize = 8;
            dcbSerialParams.Parity = NOPARITY;
            dcbSerialParams.StopBits = ONESTOPBIT;

            if (SetCommState(hComm, &dcbSerialParams))
            {
                DWORD dwErrors = 0;
                COMSTAT comStat = {};
                DWORD dwBytesRead = 0;

                while (true)
                {
                    ClearCommError(hComm, &dwErrors, &comStat);
                    if (comStat.cbInQue > 0)
                    {
                        char buffer[comStat.cbInQue] = {};
                        memset(buffer, 0, sizeof(buffer));
                        if (ReadFile(hComm, buffer, comStat.cbInQue, &dwBytesRead, NULL))
                        {
                            if (dwBytesRead > 0)
                            {
                                std::lock_guard<std::mutex> lock(g_mutex);
                                g_message.append(buffer, dwBytesRead);
                                //g_message = std::string(buffer, dwBytesRead);
                                g_isReading = true;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        // Ждем 10 мс, чтобы не блокировать поток
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
            }
        }

        CloseHandle(hComm);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Регистрируем класс окна
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_szClassName;
    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Создаем окно
    HWND hwnd = CreateWindowEx(0, g_szClassName, "My Window", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hInstance, NULL);
    if (hwnd == NULL)
    {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Отображаем окно
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Запускаем поток чтения данных из COM-порта
    std::thread comThread(ReadFromComPort);

    // Обрабатываем сообщения в основном потоке
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Обновляем текстовый контрол в основном потоке, если есть новое сообщение из COM-порта
        if (g_isReading)
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            SetWindowText(hEdit, "");
            SendMessage(hEdit, EM_SETSEL, -1, -1);
            SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)g_message.c_str()); // выводим данные на экран
            g_message.clear();
            g_isReading = false;
            SetTimer(hwnd, TIMER_ID, 10000, NULL);
        }
    }

    // Ожидаем завершения потока чтения данных из COM-порта
    comThread.join();

    return 0;
}