#pragma once
#include "utils.h"

#ifdef __APPLE__
    #define LOG(M,...) { printf(M"\n", ##__VA_ARGS__); LogRemote::I.log(M, ##__VA_ARGS__); }
    #define LOGW
#elif __ANDROID__
    #define LOG(...) { ((void)__android_log_print(ANDROID_LOG_INFO, "NativeCPP", __VA_ARGS__)); LogRemote::I.log(__VA_ARGS__); }
    #define LOGW
#else
    #define LOG(M,...) { printf(M"\n", ##__VA_ARGS__); LogRemote::I.log(M, ##__VA_ARGS__); }
    #define LOGW(M,...) { wprintf(M"\n", ##__VA_ARGS__); LogRemote::I.log(M, ##__VA_ARGS__); }
#endif

class LogRemote
{
public:
    static LogRemote I;
    bool m_running = false;
    bool m_error = false;
    std::thread m_thread;
    std::mutex m_mutex;
    BlockingQueue<std::string> m_mq;
    // Store messages until the file is open
    std::vector<std::string> m_tmp;
#if WITH_CURL
    CURL *curl = nullptr;
    CURLcode res;
#endif
    std::string readBuffer;
    std::string m_url;
    int m_session;
    std::ofstream m_logfile;
   
    void start();
    void stop();
    void net_init();
    std::string net_request(std::string cmd, std::string data = "");
    void net_close();
    void file_init();
    void file_close();
    void log(const char* format, ...);
    void log(const wchar_t* format, ...);
    ~LogRemote();
};
