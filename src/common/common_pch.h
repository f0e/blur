#pragma once

// std
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <array>
#include <vector>
#include <random>
#include <stack>
#include <filesystem>
#include <numeric>
#include <functional>
#include <codecvt>
#include <locale>
#include <regex>
#include <unordered_set>

#ifdef _WIN32
#	include <conio.h>
#	include <shobjidl.h>
#	include <Windows.h>
#elif defined(__APPLE__)
#	include <mach-o/dyld.h>
#endif

// libs
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <nlohmann/json.hpp>

#include <boost/process.hpp>
#include <boost/asio.hpp>

// blur
#include "blur.h"
#include "helpers.h"

// resources
#include "../resources/resource.h"
