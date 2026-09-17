#pragma once
namespace paper {
inline constexpr const char* kDesignVersion="paper-1.0";
namespace token {
inline constexpr int Margin=20, Gap=12, Padding=16, Radius=12, Stroke=1;
inline constexpr int Caption=20, InverseCaption=22, Body=25, Heading=28, Title=30;
inline constexpr int MinTouch=48, TextExtra=12, FullAfterFast=8;
inline constexpr int ReaderTop=130, ReaderBottom=560, ReaderWidth=432, ReaderGap=13;
}
enum class State { Normal, Selected, Disabled, Busy, Error };
enum class Tone { Paper, Ink, Subtle, Mid, Strong };
inline constexpr int Density(Tone t) {
    return t==Tone::Ink?100:t==Tone::Subtle?25:t==Tone::Mid?50:t==Tone::Strong?75:0;
}
inline constexpr bool NeedsFull(bool painted,int oldPage,int page,int oldTheme,int theme,unsigned fast,bool force) {
    return force||!painted||oldPage!=page||oldTheme!=theme||fast>=token::FullAfterFast;
}
}
