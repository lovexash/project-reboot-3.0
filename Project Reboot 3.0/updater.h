#pragma once

#define CURL_STATICLIB
#include <string>
#include <Windows.h>
#include <curl/curl.h>
#include "log.h"

namespace Updater
{
    static size_t WriteResponse(void* data, size_t size, size_t nmemb, std::string* out)
    {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }

    // Parse __DATE__ string (e.g. "Jun 19 2026") to YYYYMMDD integer.
    static int ParseBuildDate()
    {
        static const char* months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        char mon[4]{};
        int day = 0, year = 0;
        if (sscanf_s(__DATE__, "%3s %d %d", mon, (unsigned)sizeof(mon), &day, &year) != 3)
            return 0;
        int m = 1;
        for (int i = 0; i < 12; i++)
            if (!strncmp(mon, months[i], 3)) { m = i + 1; break; }
        return year * 10000 + m * 100 + day;
    }

    inline void CheckForUpdates()
    {
        CURL* curl = curl_easy_init();
        if (!curl)
        {
            LOG_WARN(LogInit, "Updater: failed to init curl.");
            return;
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL,
            "https://api.github.com/repos/lovexash/project-reboot-3.0/commits?per_page=1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponse);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ProjectReboot3.0-Updater/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            LOG_WARN(LogInit, "Updater: request failed: {}", curl_easy_strerror(res));
            return;
        }

        // Extract latest commit SHA.
        auto shaPos = response.find("\"sha\":\"");
        if (shaPos == std::string::npos)
        {
            LOG_WARN(LogInit, "Updater: unexpected API response.");
            return;
        }
        shaPos += 7;
        std::string sha = response.substr(shaPos, response.find('"', shaPos) - shaPos).substr(0, 7);

        // Extract commit date ("date":"YYYY-MM-DD...").
        auto datePos = response.find("\"date\":\"");
        if (datePos == std::string::npos)
        {
            LOG_INFO(LogInit, "Updater: latest commit {}", sha);
            return;
        }
        datePos += 8;
        std::string dateStr = response.substr(datePos, 10); // "YYYY-MM-DD"

        LOG_INFO(LogInit, "Updater: latest repo commit {} ({})", sha, dateStr);
        LOG_INFO(LogInit, "Updater: this build compiled on {}", __DATE__);

        int repoYear = 0, repoMonth = 0, repoDay = 0;
        if (sscanf_s(dateStr.c_str(), "%d-%d-%d", &repoYear, &repoMonth, &repoDay) == 3)
        {
            int repoDate = repoYear * 10000 + repoMonth * 100 + repoDay;
            int buildDate = ParseBuildDate();

            if (repoDate > buildDate)
            {
                LOG_WARN(LogInit,
                    "Updater: new commits available since this build! Update at: "
                    "https://github.com/lovexash/project-reboot-3.0");
                MessageBoxA(nullptr,
                    "A newer version of Project Reboot 3.0 is available!\n\n"
                    "Update at: https://github.com/lovexash/project-reboot-3.0",
                    "Project Reboot 3.0 - Update Available",
                    MB_ICONINFORMATION | MB_OK);
            }
            else
            {
                LOG_INFO(LogInit, "Updater: Project Reboot 3.0 is up to date.");
            }
        }
    }

    inline DWORD WINAPI UpdaterThread(LPVOID)
    {
        CheckForUpdates();
        return 0;
    }
}
