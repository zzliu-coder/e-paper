#pragma once
inline void(*delayHook)(int)=nullptr;
inline void(*queuedTask)(void*)=nullptr;
inline void* queuedArg=nullptr;
inline void vTaskDelay(int ms){if(delayHook)delayHook(ms);}
inline void vTaskDelete(void*){}
inline int xTaskCreate(void(*f)(void*),const char*,int,void*arg,int,void*){queuedTask=f;queuedArg=arg;return 1;}
