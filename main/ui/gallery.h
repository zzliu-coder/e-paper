#pragma once
#include "components.h"
namespace paper {
// Showcase controls are isolated sample states. Never operate real hardware.
inline void Gallery(Frame& f,int section,int navigate,int refresh,int toggle,int sample,bool selected){
    const char* tabs[]={"文字","线框","网点","状态"};
    for(int i=0;i<4;++i)Button(f,{20+i*112,68,104,52},tabs[i],navigate,i,i==section,22);
    if(section==0){
        Text(f,20,134,440,"同字同号 · 黑白轮廓对照",22);
        int y=178;
        for(int size:{18,20,22,25,28,30}){
            char label[24];std::snprintf(label,sizeof(label),"%dpx",size);Text(f,20,y+14,58,label,18);
            Panel(f,{80,y,190,64});Panel(f,{278,y,182,64},true);
            Text(f,92,y+10,168,"阅读 82",size);Text(f,290,y+10,158,"阅读 82",size,false);y+=76;
        }
    }else if(section==1){
        Text(f,20,134,440,"圆角 / 线宽 · 原生像素",22);
        const int radii[]={0,8,12,16};
        for(int i=0;i<4;++i){int y=184+i*108;char label[32];std::snprintf(label,sizeof(label),"R%d / 1px",radii[i]);
            Panel(f,{20,y,214,92},false,radii[i],1);Text(f,36,y+28,184,label,22);
            Panel(f,{246,y,214,92},false,radii[i],2);std::snprintf(label,sizeof(label),"R%d / 2px",radii[i]);Text(f,262,y+28,180,label,22);
        }
        Dashed(f,{20,636,440,48},1,6,4);Text(f,36,640,408,"虚线 6/4 · 辅助信息与分隔",20);
    }else if(section==2){
        Text(f,20,134,440,"空间网点灰 · 单像素仍为黑白",22);
        const Tone tones[]={Tone::Subtle,Tone::Mid,Tone::Strong};
        for(int i=0;i<3;++i){int y=182+i*132;char label[32];std::snprintf(label,sizeof(label),"%d%%",Density(tones[i]));
            Text(f,20,y+20,84,label,25);Panel(f,{114,y,346,110});Texture(f,{130,y+16,314,78},tones[i]);
        }
        Message(f,{20,592,440,120},"正文保持高对比","网点用于装饰底纹，不用于小字");
    }else{
        Text(f,20,134,440,"公共组件 · 状态与操作反馈",22);
        Button(f,{20,180,214,60},"普通按钮",sample,0);
        Button(f,{246,180,214,60},selected?"已选中":"点击选择",toggle,0,selected);
        Button(f,{20,252,214,60},"不可用",sample,0,false,25,State::Disabled);
        Button(f,{246,252,214,60},"处理中",sample,0,false,25,State::Busy);
        SettingRow(f,{20,326,440,96},"示例开关",selected?"开启 · 仅本页状态":"关闭 · 仅本页状态",toggle);
        StatusBar(f,{20,436,440,36},"网络未连接");
        Progress(f,{20,486,214,18},45);Progress(f,{246,486,214,18},-1);
        Text(f,20,510,440,"进度已知 / 进度未知",18);
        Message(f,{20,554,440,132},"暂时没有内容","连接后重试 · 错误与空状态组件");
    }
    Button(f,{20,732,214,52},"全刷对照",refresh,0,false,22);
    Button(f,{246,732,214,52},"局刷对照",refresh,1,false,22);
}
}
