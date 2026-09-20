#include "bluetooth_service.hpp"
#include "power_policy.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "selftest.h"
#include <cassert>
#include <iostream>
static void rx(const std::string&s){paper_bluetooth::Observe((const uint8_t*)s.data(),s.size());}
static void mode(const char*s){assert(paper_bluetooth::Command("mode",s));assert(queuedTask);queuedTask(queuedArg);}
int main(){
    delayHook=[](int ms){if(ms==1800){rx("SET MODE 1\r\n");assert(paper_bluetooth::Snapshot().busy);}};
    mode("1");rx("ERROR\r\n");assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    delayHook=nullptr;
    mode("2");rx("SET MODE 2\r");assert(paper_bluetooth::Snapshot().mode==2);
    fakeTime+=119000000;assert(paper_bluetooth::Snapshot().railHeld);
    fakeTime+=2000000;selftest::mockBusy=true;assert(paper_bluetooth::Snapshot().railHeld);
    selftest::mockBusy=false;assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    mode("2");rx("SET MODE 2\nCONNECT SUCCESS\r\n");
    fakeTime+=200000000;auto s=paper_bluetooth::Snapshot();assert(s.connected&&s.connectionKnown&&s.railHeld&&s.railOn);
    rx("DISCONNECT\r\n");assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    mode("3");rx("SET MO");rx("DE 3\r");fakeTime+=300000000;
    s=paper_bluetooth::Snapshot();assert(s.railHeld&&!s.connected&&!s.connectionKnown&&s.rxBytes>0&&s.lastRxMs>0);
    for(int i=0;i<10;++i)rx("UNKNOWN RESPONSE\r\n");
    s=paper_bluetooth::Snapshot();assert(s.recentReplies.size()==6&&!s.connectionKnown);
    assert(paper_bluetooth::Command("stop"));assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    mode("3");rx("SET MODE 3\rERROR\r");assert(!paper_bluetooth::Snapshot().railHeld);
    mode("2");fakeTime+=16000000;assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    mode("2");rx(std::string(600,'X')+"CONNECT SUCCESS\n");assert(!paper_bluetooth::Snapshot().connected);
    assert(paper_bluetooth::Command("stop"));assert(powerRefs==0);
    // A local recording must acquire power and receive its own mode-one ACK.
    mode("2");rx("SET MODE 2\r\nSET MODE 1\r\n");
    assert(!paper_bluetooth::BeginLocalAudio());assert(powerRefs==1);
    assert(paper_bluetooth::Command("stop"));assert(powerRefs==0);
    mode("2");rx("SET MODE 1\r\nERROR\r\n");
    assert(!paper_bluetooth::BeginLocalAudio());assert(powerRefs==1);
    assert(!paper_bluetooth::Snapshot().railHeld&&powerRefs==0);
    delayHook=[](int ms){if(ms==2500)rx("SET MODE 1\r\n");};
    assert(paper_bluetooth::BeginLocalAudio());assert(powerRefs==1);
    assert(!paper_bluetooth::Command("mode","3"));
    assert(!paper_bluetooth::BeginLocalAudio());
    fakeTime+=300000000;assert(paper_bluetooth::Snapshot().railHeld);
    paper_bluetooth::EndLocalAudio();assert(powerRefs==0);
    paper_bluetooth::EndLocalAudio();assert(powerRefs==0);
    delayHook=[](int ms){if(ms==2500)rx("ERROR\r\n");};
    assert(!paper_bluetooth::BeginLocalAudio());assert(powerRefs==0);
    delayHook=nullptr;
    assert(!paper_bluetooth::BeginLocalAudio());assert(powerRefs==0);
    mode("3");rx("SET MODE 3\n");
    assert(!paper_bluetooth::BeginLocalAudio());assert(powerRefs==1);
    assert(paper_bluetooth::Command("stop"));assert(powerRefs==0);
    std::cout<<"PASS: Bluetooth RX framing, bounded diagnostics, truthful unknown, receive lease, errors/timeout/stop release\n";
}
