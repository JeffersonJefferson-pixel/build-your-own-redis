#pragma once

#include <string>
#include "common.h"

void out_nil(std::string &out);
void out_str(std::string &out, const char *s, size_t size);
void out_str(std::string &out, const std::string &val);
void out_int(std::string &out, int64_t val);
void out_dbl(std::string &out, double val);
void out_err(std::string &out, int32_t code, const std::string &msg);
void out_arr(std::string &out, uint32_t n);