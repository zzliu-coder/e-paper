// Local PAPER standby: no cloud/weather/assistant application dependency.
#include "standby_screen/standby_screen.h"
#include "display/lv_adapter_display.h"
#include "power_policy.h"
#include "inkdesk_app.h"
#include "rescue_fonts.h"
namespace {lv_obj_t* overlay=nullptr;}
lv_obj_t* StandbyScreen::Create(){auto*s=lv_obj_create(nullptr);return s;}
void StandbyScreen::Show(){
    if(overlay)return;
    overlay=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay,480,800);lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);lv_obj_set_style_bg_color(overlay,lv_color_white(),0);
    auto*l=lv_label_create(overlay);lv_obj_set_style_text_font(l,&paper_rescue_22,0);lv_obj_set_style_text_color(l,lv_color_black(),0);lv_label_set_text(l,"纸间 / 待机\n\n按电源键唤醒");lv_obj_center(l);
    lv_obj_add_event_cb(overlay,[](lv_event_t*){overlay=nullptr;},LV_EVENT_DELETE,nullptr);
    if(auto*d=LVAdapterDisplay::Instance())d->BeginStandbyEnterPaint();
    PowerPolicy::GetInstance().RequestReevaluate();
}
void StandbyScreen::Dismiss(){if(!overlay)return;ExitEpdSleep();lv_obj_delete(overlay);overlay=nullptr;PowerPolicy::GetInstance().OnStandbyOverlayDismissed();inkdesk_app::Open();}
bool StandbyScreen::IsActive(){return overlay!=nullptr;}
bool StandbyScreen::IsPaintReady(){return overlay!=nullptr;}
void StandbyScreen::EnterEpdSleep(){if(overlay)if(auto*d=LVAdapterDisplay::Instance())d->ParkEpdForStandby();}
void StandbyScreen::ExitEpdSleep(){if(auto*d=LVAdapterDisplay::Instance())d->WakeEpdFromStandby();}
bool StandbyScreen::HandleBootClick(){return IsActive();}
bool StandbyScreen::HandleBootLongPress(){if(!overlay)return false;lv_async_call([](void*){Dismiss();},nullptr);return true;}
void StandbyScreen::EnsureWeatherCached(){}
