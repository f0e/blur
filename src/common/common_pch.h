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
#include <set>
#include <unordered_set>
#include <ranges>

// libs
#include <nlohmann/json.hpp>

#include <boost/process.hpp>
#include <boost/asio.hpp>
#ifdef _WIN32
#	include <boost/process/v1/windows.hpp>
#endif

#ifdef _WIN32
#	include <conio.h>
#	include <shobjidl.h>
#	include <Windows.h>
#elif defined(__APPLE__)
#	include <mach-o/dyld.h>
#endif

// blur
#include "blur.h"
#include "utils.h"
