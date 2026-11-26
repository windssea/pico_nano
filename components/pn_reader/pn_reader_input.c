/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读手势与按钮的一次提交识别器，设备和PC共用。
 * English: single-commit reader gesture/button recognizer shared by device and PC.
 * 冻结：多点/读错取消，滑出按钮不再触发；两次空采样确认释放。
 * Frozen: multitouch/read errors cancel; leaving a button prevents activation; two empty samples confirm release.
 */
#include "pn_reader_input.h"
#include <stdlib.h>
/* 默认按当前激活状态栏留白确定正文区；去上下栏全屏时整屏（y=0..1215）响应。工具栏由调用方的专用命中处理。
 * Body region respects active status bar insets; bar-less full-screen mode accepts the whole 0..1215 screen. */
static int region(int x,int y,bool full_screen,bool allow_edges){
    if(x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(full_screen || allow_edges)return 4;
    int top_in=0,bot_in=0;pn_status_bar_insets(false,&top_in,&bot_in);
    return y>=top_in && y<1216-bot_in?4:-1;
}
static bool map_gesture_action(uint8_t gact,pn_reader_action_t *out){
    switch(gact){
        case PN_GACT_PREV_PAGE:*out=PN_APP_PREVIOUS;return true;
        case PN_GACT_NEXT_PAGE:*out=PN_APP_NEXT;return true;
        case PN_GACT_TOOLBAR:*out=PN_APP_TOOLS;return true;
        case PN_GACT_TOC:*out=PN_APP_TOC;return true;
        case PN_GACT_JUMP:*out=PN_APP_JUMP;return true;
        case PN_GACT_BOOKMARK:*out=PN_APP_BOOKMARK;return true;
        case PN_GACT_DICT:*out=PN_APP_DICT_SEARCH;return true;
        case PN_GACT_REFRESH:*out=PN_APP_REFRESH;return true;
        case PN_GACT_AUTO_TURN:*out=PN_APP_AUTO_TURN;return true;
        default:return false;
    }
}
/// 识别 9 方位屏幕手势（优先检测边沿手势，未绑定时平滑退回常规翻页）。
/// Recognize the 9 visual screen gestures (edge gestures first; unbound slots fall through to normal page turns).
static bool match_custom_gesture(const pn_reader_input_t *i,int dx,int dy,pn_reader_action_t *out){
    if(!i->config.use_gestures)return false;
    const pn_gesture_prefs_t *g=&i->config.gestures;
    bool horiz=abs(dx)>=64 && 2*abs(dx)>=3*abs(dy);
    bool vert=abs(dy)>=64 && 2*abs(dy)>=3*abs(dx);
    // 顶部下拉 / Top edge swipe down.
    if(vert && dy>0 && i->start_y<112 && map_gesture_action(g->slots[PN_GSLOT_TOP_DOWN],out))return true;
    // 左下/右下边缘上滑 / Bottom-left & bottom-right edge swipe up.
    if(vert && dy<0 && i->start_y>=1040){
        uint8_t act=i->start_x<342?g->slots[PN_GSLOT_BOT_LEFT_UP]:g->slots[PN_GSLOT_BOT_RIGHT_UP];
        if(map_gesture_action(act,out))return true;
    }
    // 左上/右上边缘内滑 / Left & right upper edge inward swipes.
    if(horiz && i->start_y<420){
        if(dx>0 && i->start_x<84 && map_gesture_action(g->slots[PN_GSLOT_LEFT_EDGE_IN],out))return true;
        if(dx<0 && i->start_x>=600 && map_gesture_action(g->slots[PN_GSLOT_RIGHT_EDGE_IN],out))return true;
    }
    // 左/右边缘垂直滑 / Left & right edge vertical swipes.
    if(vert && i->start_y>=260 && i->start_y<1040){
        if(i->start_x<84 && map_gesture_action(g->slots[PN_GSLOT_LEFT_EDGE_VERT],out))return true;
        if(i->start_x>=600 && map_gesture_action(g->slots[PN_GSLOT_RIGHT_EDGE_VERT],out))return true;
    }
    // 屏幕内上滑/下滑（非上下滑动翻页模式下生效）/ Center up/down swipes (active when swipe-turn is not vertical).
    bool vert_swipe=i->config.use_gestures?(g->swipe_turn==PN_SWIPE_TURN_TB):i->config.vertical_turn;
    if(vert && !vert_swipe){
        uint8_t act=dy<0?g->slots[PN_GSLOT_SWIPE_UP]:g->slots[PN_GSLOT_SWIPE_DOWN];
        if(map_gesture_action(act,out))return true;
    }
    return false;
}
void pn_reader_input_cancel(pn_reader_input_t *i){if(i){i->cancelled=true;i->blocked=true;i->long_fired=false;i->last_tap=false;i->empty_samples=0;}}
static bool feed_impl(pn_reader_input_t *i,unsigned count,int x,int y,bool valid,bool timed,uint64_t now_ms,pn_reader_action_t *out){
    if(!i || !out)return false;
    i->last_tap=false;
    if(!valid || count>1){pn_reader_input_cancel(i);return false;}
    if(!count){
        if(i->empty_samples<2)i->empty_samples++;
        if(i->empty_samples<2)return false;
        bool submit=i->down && !i->cancelled && !i->blocked && !i->long_fired;pn_reader_action_t action=PN_APP_OPEN;bool tap=false;
        if(submit){
            if(i->region==4){int dx=i->last_x-i->start_x,dy=i->last_y-i->start_y;
                if(match_custom_gesture(i,dx,dy,&action)){
                    // 自定义屏幕手势已命中 / Custom screen gesture matched.
                }else if(abs(dx)<=24 && abs(dy)<=24){
                    // 仅正文内部轻点触发阅读区点按（边沿栏留给状态栏命中）/ Plain body tap honors active tap-zone layout.
                    int top_in=0,bot_in=0;pn_status_bar_insets(i->config.full_screen,&top_in,&bot_in);
                    if(i->start_y<top_in || i->start_y>=1216-bot_in){submit=false;}
                    else{
                        tap=true;
                        if(i->config.use_gestures){
                            // 新版统一手势与触控区：7 套可视化触控区直接生效，不再被旧开关覆盖或反转。
                            // Unified gestures & tap zones: all 7 visual tap-zone layouts apply directly without legacy toggle overrides.
                            switch(i->config.gestures.tap_zone){
                                case PN_TAP_ZONE_TB:{
                                    int mid_y=top_in+(1216-top_in-bot_in)/2;
                                    action=i->start_y<mid_y?PN_APP_PREVIOUS:PN_APP_NEXT;break;
                                }
                                case PN_TAP_ZONE_THREE_COL:
                                    if(i->start_x>=228 && i->start_x<456)action=PN_APP_TOOLS;
                                    else action=(i->start_x<228)?PN_APP_PREVIOUS:PN_APP_NEXT;
                                    break;
                                case PN_TAP_ZONE_ONE_HAND_R:{
                                    int mid_y=top_in+(1216-top_in-bot_in)/2;
                                    if(i->start_x<228)action=PN_APP_PREVIOUS;
                                    else action=i->start_y<mid_y?PN_APP_PREVIOUS:PN_APP_NEXT;
                                    break;
                                }
                                case PN_TAP_ZONE_ONE_HAND_L:{
                                    int mid_y=top_in+(1216-top_in-bot_in)/2;
                                    if(i->start_x>=456)action=PN_APP_PREVIOUS;
                                    else action=i->start_y<mid_y?PN_APP_PREVIOUS:PN_APP_NEXT;
                                    break;
                                }
                                case PN_TAP_ZONE_ALL_NEXT:
                                    action=PN_APP_NEXT;break;
                                case PN_TAP_ZONE_OFF:
                                    submit=false;break;
                                default:
                                    action=(i->start_x<342)?PN_APP_PREVIOUS:PN_APP_NEXT;break;
                            }
                        }else if(i->config.no_edge_tap){submit=false;}
                        else if(i->config.vertical_turn){
                            int mid_y=i->config.full_screen?608:588;
                            action=i->start_y<mid_y?PN_APP_PREVIOUS:PN_APP_NEXT;
                        }else{
                            bool left=i->start_x<342;
                            action=(left!=i->config.left_hand)?PN_APP_PREVIOUS:PN_APP_NEXT;
                        }
                    }
                }else if(i->config.use_gestures){
                    uint8_t st=i->config.gestures.swipe_turn;
                    if(st==PN_SWIPE_TURN_OFF)submit=false;
                    else if(st==PN_SWIPE_TURN_TB){
                        if(abs(dy)>=64 && 2*abs(dy)>=3*abs(dx))action=dy<0?PN_APP_NEXT:PN_APP_PREVIOUS;
                        else submit=false;
                    }else{
                        if(abs(dx)>=64 && 2*abs(dx)>=3*abs(dy))action=dx<0?PN_APP_NEXT:PN_APP_PREVIOUS;
                        else submit=false;
                    }
                }else if(i->config.vertical_turn){
                    if(abs(dy)>=64 && 2*abs(dy)>=3*abs(dx)){if(i->config.no_swipe)submit=false;else action=dy<0?PN_APP_NEXT:PN_APP_PREVIOUS;}
                    else submit=false;
                }else{
                    if(abs(dx)>=64 && 2*abs(dx)>=3*abs(dy)){if(i->config.no_swipe)submit=false;else action=dx<0?PN_APP_NEXT:PN_APP_PREVIOUS;}
                    else submit=false;
                }
            }else submit=false;
        }
        i->down=false;i->cancelled=false;i->blocked=false;i->long_fired=false;i->last_tap=tap;
        if(submit){*out=action;return true;}return false;
    }
    i->empty_samples=0;if(i->blocked)return false;
    bool allow_edges=i->config.use_gestures;
    int hit=region(x,y,i->config.full_screen,allow_edges);
    if(!i->down){i->down=true;i->cancelled=hit<0;i->region=hit;i->start_x=x;i->start_y=y;i->down_ms=now_ms;i->long_fired=false;}
    else if(hit!=4)i->cancelled=true;
    i->last_x=x;i->last_y=y;
    if(timed && i->down && !i->cancelled && !i->long_fired && i->region==4){
        uint8_t lp_mode=i->config.use_gestures?i->config.gestures.long_press_mode:PN_LP_DICT;
        if(lp_mode!=PN_LP_OFF){
            int top_in=0,bot_in=0;pn_status_bar_insets(i->config.full_screen,&top_in,&bot_in);
            int dx=i->last_x-i->start_x,dy=i->last_y-i->start_y;
            if(i->start_y>=top_in && i->start_y<1216-bot_in && abs(dx)<=24 && abs(dy)<=24 && now_ms>=i->down_ms && (now_ms-i->down_ms)>=PN_READER_LONG_PRESS_MS){
                i->long_fired=true;i->cancelled=true;i->blocked=true;
                *out=(lp_mode==PN_LP_MENU)?PN_APP_LONG_PRESS_MENU:(lp_mode==PN_LP_HIGHLIGHT)?PN_APP_HIGHLIGHT:PN_APP_LOOKUP;return true;
            }
        }
    }
    return false;
}
bool pn_reader_input_feed(pn_reader_input_t *i,unsigned count,int x,int y,bool valid,pn_reader_action_t *out){
    return feed_impl(i,count,x,y,valid,false,0,out);
}
bool pn_reader_input_feed_timed(pn_reader_input_t *i,unsigned count,int x,int y,bool valid,uint64_t now_ms,pn_reader_action_t *out){
    return feed_impl(i,count,x,y,valid,true,now_ms,out);
}
