// Copyright (c) Vitaliy Filippov, 2019+
// License: VNPL-1.1 (see README.md for details)

#pragma once
#include <stdint.h>
#include <string>

std::string base64_encode(const std::string &in);
std::string base64_decode(const std::string &in);
uint64_t parse_size(std::string size_str);
uint64_t stoull_full(const std::string & str, int base = 0);
std::string format_size(uint64_t size, bool nobytes = false);
