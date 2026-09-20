// Archived experiment: excluded from product sources; historical runtime required.
#include "paper/runtime.hpp"
#include "paper/mono_filter.hpp"
namespace paper {
void Runtime::grayTestPage(){
    title("字体清晰度对照");
    const int mode=monoTestMode_=="gray4"?3:monoTestMode_=="dots"?2:0;
    const char* name=mode==3?"真四灰阶 · 实验":mode==2?"网点模拟 · 黑白驱动":"原始黑白";
    text({24,96,432,30},name,22,500);
    text({24,134,432,28},"常规字重 400；不改变阅读设置",18,400);
    if(grayTestPage_==4){
        text({24,190,432,30},"色块：黑 / 深灰 / 浅灰 / 白",22,400);
        for(int i=0;i<4;++i){Rect b{24+i*108,250,96,160};canvas_.rect(b,i,true);if(mode!=3)monoFilter(canvas_,b,mode);canvas_.rect(b,0,false);}
        text({24,458,432,70},"先检查四块分明、白底干净。\n正常后点换字号，异常选黑白。",22,400);
    }else{
        const int sizes[]={18,22,26,32};const int px=sizes[grayTestPage_];
        text({24,180,432,30},"字号 "+std::to_string(px)+" / 常规不加粗",22,400);
        // Cache only this lab's current-size coverage master. Switching software
        // filters must not reopen two full font indexes on every tap.
        if(!monoTestMaster_||monoTestMasterPx_!=px){
            auto next=std::make_unique<Canvas>();
            next->rect({24,392,432,162},0,true);
            for(bool inverse:{false,true}){
                const int y=inverse?442:266;
                auto st=next->text(fonts_,FontSpec{px,400,true},"清晨微光 Aa 012345",{32,y,416,44},px+10,inverse);
                if(!st){if(textError_.empty())textError_=st.message;return;}
            }
            monoTestMaster_=std::move(next);monoTestMasterPx_=px;
        }
        for(int y=240;y<554;++y)for(int x=24;x<456;++x)canvas_.pixel(x,y,monoTestMaster_->pixel(x,y));
        for(bool inverse:{false,true}){
            const int y=inverse?442:266;
            if(mode!=3)monoFilter(canvas_,{32,y,416,44},mode,inverse);
            FontSpec label{18,400,false};label.uiOnly=true;
            auto st=canvas_.text(fonts_,label,inverse?"黑底白字 / 常规":"白底黑字 / 常规",{32,y-26,416,24},24,inverse);
            if(!st&&textError_.empty())textError_=st.message;
        }
    }
    text({24,568,432,30},"同字形三方案；真灰阶每次清屏",18,400);
    button({24,616,138,54},"原始黑白","gray-test","mono",mode==0);
    button({170,616,138,54},"真四灰阶","gray-test","gray4",mode==3);
    button({316,616,140,54},"网点模拟","gray-test","dots",mode==2);
    button({24,682,138,54},"换字号","gray-test-next");
    button({170,682,138,54},"清屏比较","refresh");
    button({316,682,140,54},"退出测试","home");
    footnote("灰阶仅实验 · 日常阅读保留黑白");
}
}
