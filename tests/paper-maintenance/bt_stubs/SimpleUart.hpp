#pragma once
#include <string>
class SimpleUart { public: static SimpleUart& getInstance(){static SimpleUart u;return u;} bool isInitialized(){return true;} bool sendString(const std::string&){return true;} };
