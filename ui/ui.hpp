#pragma once
#include "engine.hpp"
#include "screen.hpp"
#include "lvgl.h"
#include <functional>
#include <map>
#include <set>
namespace tabnamp {
class Ui {
    Engine& engine_;lv_display_t* display_;lv_obj_t* root_;lv_obj_t *player_,*eq_,*list_,*time_,*title_,*status_,*volume_,*seek_,*rows_[9],*eqs_[10],*eq_names_[10],*bars_[24],*peaks_[24],*info_;
    lv_obj_t *overlay_=nullptr,*entry_=nullptr,*keyboard_=nullptr;Action pending_=Action::Add;
    lv_obj_t* entry_submit_=nullptr;
    bool landscape_=true,was_on_=true,browser_mode_=false;
    size_t directory_first_=0;lv_obj_t* directory_rows_[9]{};lv_obj_t* directory_title_=nullptr;size_t first_=0;int selected_=0;uint64_t now_=0,next_spectrum_=0;
    lv_font_t* font_=nullptr;lv_font_t* digital_=nullptr;
    std::string previous_title_,previous_status_,previous_time_,previous_info_,previous_directory_path_;
    std::array<std::string,9> previous_rows_,previous_directory_rows_;
    std::map<std::string,lv_obj_t*> controls_;
    std::map<std::string,std::string> keymap_;lv_group_t* focus_=nullptr;bool playlist_focus_=false,multi_select_=false;std::set<int> selections_;
    bool caps_lock_=false;int dead_key_=0,previous_brightness_=-1,pending_orientation_=-1,drag_from_=-1;lv_point_t drag_start_{};bool dragging_=false;uint64_t suppress_click_=0;
    EqSettings curve_settings_;uint32_t curve_rate_=0;
    lv_obj_t* preamp_=nullptr;lv_obj_t* curve_=nullptr;std::array<lv_point_precise_t,48> curve_points_{};
    lv_obj_t* panel(const char* title);lv_obj_t* label(lv_obj_t* parent,const char* text,int x,int y,int width=0);
    lv_obj_t* button(lv_obj_t* parent,const char* title,int x,int y,int width,std::function<void()> callback);
    void prompt(const char* title,Action action,const std::string& value);void close_dialog();void confirm(const char* message,std::function<void()> callback);
    void layout();void details();void browser();void menu();void preferences();void configure_keyboard();void select_row(int row);void drag(lv_event_t* event,int row);void post(Command command);
    void configure_entry(lv_obj_t* field);void submit_entry();
public:
    Screen screen;
    std::function<void(bool)> rotate;
    std::function<void(int)> backlight;
    std::function<void()> wifi;
    std::function<void()> retry_sd;
    Ui(Engine& engine,lv_display_t* display);
    void build();void refresh(uint64_t now);void release_assets();
    void credentials(const std::string& status,std::function<void(std::string,std::string)> connect,std::function<void()> scan);
    bool input(uint8_t token,bool down,uint64_t now);
    bool key(int key,bool ctrl,bool shift,bool down,uint8_t token,uint64_t now);
    void orientation(bool landscape);void request_orientation(bool landscape);void message(const std::string& text);
    lv_obj_t* control(const std::string& name)const;
    bool dialog_open()const{return overlay_!=nullptr;}
    bool caps_lock()const{return caps_lock_;}
    int keyboard_layout()const{return engine_.snapshot().settings.keyboard_layout;}
    bool landscape()const{return landscape_;}
};
}
