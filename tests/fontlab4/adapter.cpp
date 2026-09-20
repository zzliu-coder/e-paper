#include "ui/fontlab4/device.h"
#include <cassert>
#include <algorithm>
using namespace paper::lab4;
int main(int argc,char** argv){
    assert(argc==2);Device d;d.session=Session(argv[1]);lv_font_t font;
    assert(d.OpenPage(0));auto original=d.session.page()->pixels;
    auto* root=lv_obj_create(nullptr);assert(d.Paint(root,&font));
    assert(root->children.size()>=2);const auto* image=root->children.front()->image;
    assert(image&&image->header.w==480&&image->header.h==800&&image->header.stride==60);
    assert(image->data_size==48008);const uint8_t palette[]={0,0,0,255,255,255,255,255};
    assert(std::equal(palette,palette+8,image->data));
    assert(std::equal(original.begin(),original.end(),image->data+8));
    // A newly loaded page must not invalidate the previous live LVGL image source.
    assert(d.session.Load(1));assert(std::equal(original.begin(),original.end(),image->data+8));
    lv_obj_delete(root);
    root=lv_obj_create(nullptr);fontlab4_test_fail_allocation=true;
    assert(!d.Paint(root,&font));fontlab4_test_fail_allocation=false;lv_obj_delete(root);
    assert(!d.OpenPage(4096));root=lv_obj_create(nullptr);assert(d.Paint(root,&font));
    assert(!root->children.empty());for(auto* child:root->children)assert(!child->image);
    lv_obj_delete(root);assert(d.Tap(420,20,"test",0)==Action::Legacy&&!d.enabled);
    d.Enable();assert(d.enabled);root=lv_obj_create(nullptr);assert(d.Paint(root,&font));lv_obj_delete(root);
    std::puts("PASS: adapter API, I1 palette/stride/pixels, image lifetime, allocation failure and unavailable/legacy routes. LVGL API is a test stand-in; no LVGL raster or ESP-IDF build.");
}
