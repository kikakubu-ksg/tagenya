#pragma execution_character_set("utf-8")
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

/* httpŠm”F—p */
int httptest(char *url)
{
    CURL *curl;
    CURLcode res;
    long status;
    int ret = 0;

    curl = curl_easy_init();
    if(!curl) {
        g_printerr("init failed!\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NSPlayer/WebABC");

    res = curl_easy_perform(curl);
    if(res != CURLE_OK && res != CURLE_COULDNT_CONNECT) {
        g_printerr("curl error %d\n", res);
        return -1;
    }
    if(res == CURLE_COULDNT_CONNECT){
        return -1;
    }
    res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if(res != CURLE_OK) {
      ret = -1;
    }

    if(status == 200 || status == 503){
      ret = status;
    }

    curl_easy_cleanup(curl);

    return ret;
}