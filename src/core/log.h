#pragma once

namespace ml::Log
{
    // printf-style, to ModActivator\ModActivator.log beside the plugin. Defined in
    // activator.cpp; nothing is written until the log is opened at startup.
    void Write(const char* level, const char* fmt, ...);
}

#define LOG(...)     ::ml::Log::Write("info ", __VA_ARGS__)
#define LOG_OK(...)  ::ml::Log::Write("ok   ", __VA_ARGS__)
#define LOG_ERR(...) ::ml::Log::Write("error", __VA_ARGS__)
