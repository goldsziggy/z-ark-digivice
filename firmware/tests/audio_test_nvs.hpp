#pragma once
#include "nvs.h"
#include <vector>
struct AudioTestNvs {
    std::vector<unsigned char> bytes;
    bool present=false,readFail=false,writeFail=false,commitFail=false,corruptAfterCommit=false;
    esp_err_t openError=ESP_OK;
    unsigned opens=0,closes=0,sets=0,commits=0,reads=0;
};
extern AudioTestNvs audioTestNvs;
