#include "pch.h"
#include "log.h"
#include "app.h"

LogRemote LogRemote::I;

void LogRemote::start()
{
#if WITH_CURL
    if (m_running || m_error)
        return; // already running

    m_running = true;
    m_thread = std::thread([&] {
    BT_SetTerminate();
        net_init();
        auto session_string = net_request("/start");
        m_session = atoi(session_string.c_str());
        while (m_running && !m_error)
        {
            auto m = m_mq.Get();
            auto escaped = curl_easy_escape(curl, m.c_str(), (int)m.size());
            auto data = std::make_unique<char[]>(m.size() + 64);
            int sz = snprintf(data.get(), m.size() + 64, "session=%d&m=%s", m_session, escaped);
            curl_free(escaped);
            net_request("/log", std::string(data.get(), sz));
        }
        net_close();
        LOG("NET thread loop exit");
    });
#endif //CURL
}

void LogRemote::stop()
{
    m_running = false;
    m_mq.UnlockGetters();
    if (m_thread.joinable())
        m_thread.join();
}

void LogRemote::net_init()
{
#if WITH_CURL
    if (!(curl = curl_easy_init()))
        return;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &this->readBuffer);
    //curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_data_handler);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
#ifdef __ANDROID__
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
#endif //CURL
}
std::string LogRemote::net_request(std::string cmd, std::string data /*= ""*/)
{
    readBuffer.clear();
#if WITH_CURL
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    auto url = m_url + cmd;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    res = curl_easy_perform(curl);
    if (res != CURLcode::CURLE_OK)
    {
        LOG("NET error, closed");
        m_running = false;
        m_error = true;
    }
#endif //CURL
    return readBuffer;
}
void LogRemote::net_close()
{
#if WITH_CURL
    if (curl)
        curl_easy_cleanup(curl);
    curl = nullptr;
#endif //CURL
    m_running = false;
}
void LogRemote::file_init()
{
    std::lock_guard<std::mutex> _lock(m_mutex);
    if (!m_logfile.is_open())
        m_logfile.open("log.txt");
    if (m_logfile.is_open() && !m_tmp.empty())
    {
        for (auto const& s : m_tmp)
            m_logfile.write(s.data(), s.size());
        m_logfile.flush();
        m_tmp.clear();
    }
}
void LogRemote::file_close()
{
    std::lock_guard<std::mutex> _lock(m_mutex);
    if (!m_logfile.is_open())
        m_logfile.close();
}
void LogRemote::log(const char* format, ...)
{
    std::lock_guard<std::mutex> _lock(m_mutex);
    static char buffer[4096];
    va_list arglist;
    va_start(arglist, format);
    int n = vsnprintf(buffer, sizeof(buffer), format, arglist);
    va_end(arglist);
    m_mq.Post(std::string(buffer, n));
    auto line = std::string(buffer, n) + "\n";
    if (m_logfile.is_open())
    {
        m_logfile.write(line.data(), line.size());
        m_logfile.flush();
    }
    else
    {
        m_tmp.push_back(line);
    }
#if _WIN32
    OutputDebugStringA(("DBG: " + line).c_str());
#endif
}
void LogRemote::log(const wchar_t* format, ...)
{
    std::lock_guard<std::mutex> _lock(m_mutex);

    static wchar_t buffer[4096];
    va_list arglist;
    va_start(arglist, format);
    int n = vswprintf(buffer, sizeof(buffer)/sizeof(wchar_t), format, arglist);
    va_end(arglist);

    std::wstring string_to_convert(buffer, n);

    //setup converter
//     using convert_type = std::codecvt_utf8<wchar_t>;
//     std::wstring_convert<convert_type, wchar_t> converter;

    mbstate_t st = {};
    std::string converted;
    converted.resize(string_to_convert.size());
    const wchar_t * wptr = string_to_convert.c_str();
    std::wcsrtombs((char*)converted.data(), &wptr, converted.capacity(), &st);

    //use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
    //std::string converted_str = converter.to_bytes(string_to_convert);

    m_mq.Post(std::string(converted));
    auto line = converted + "\n";
    if (m_logfile.is_open())
    {
        m_logfile.write(line.data(), line.size());
        m_logfile.flush();
    }
    else
    {
        m_tmp.push_back(line);
    }
#if _WIN32
    auto line_console = L"DBG: " + std::wstring(buffer, n) + L"\n";
    OutputDebugStringW(line_console.c_str());
#endif
}
LogRemote::~LogRemote()
{
    stop();
}
