#pragma execution_character_set("utf-8")
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#define DEBUGMOD 1
#ifdef DEBUGMOD
#define DEBUG(fmt, ...) g_print(g_strdup_printf("%d: %s",__LINE__, fmt), __VA_ARGS__)
#define DEBUGLINE() g_print(g_strdup_printf("DEBUGLINE : %d\n",__LINE__))
#else
#define DEBUG(fmt, ...) g_print("")
#define DEBUGLINE() g_print("")
#endif

/* httpŠm”F—p */
int httptest(char *url)
{
    CURL *curl;
    CURLcode res;
    long status;
    int ret = 0;
    DEBUGLINE();
    curl = curl_easy_init();
    if(!curl) {
        g_printerr("init failed!\n");
        return -1;
    }
    DEBUGLINE();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    DEBUGLINE();
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    DEBUGLINE();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NSPlayer/WebABC");
    DEBUGLINE();
    res = curl_easy_perform(curl);
    DEBUGLINE();
    if(res != CURLE_OK && res != CURLE_COULDNT_CONNECT) {
      DEBUGLINE();
        g_printerr("curl error %d\n", res);
        return -1;
    }
    DEBUGLINE();
    if(res == CURLE_COULDNT_CONNECT){
        return -1;
    }
    DEBUGLINE();
    res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if(res != CURLE_OK) {
      ret = -1;
    }
    DEBUGLINE();
    if(status == 200 || status == 503){
      ret = status;
    }
    DEBUGLINE();
    curl_easy_cleanup(curl);
    DEBUGLINE();
    return ret;
}