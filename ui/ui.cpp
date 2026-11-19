#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include "cJSON.h"
#include "hid.hpp"
namespace tabnamp {
static void insert_character(lv_obj_t* field,uint32_t cp){
    char text[5]{};
    if(cp<0x80)text[0]=char(cp);
    else if(cp<0x800){text[0]=char(0xc0|(cp>>6));text[1]=char(0x80|(cp&63));}
    else if(cp<0x10000){if(cp>=0xd800&&cp<=0xdfff)return;text[0]=char(0xe0|(cp>>12));text[1]=char(0x80|((cp>>6)&63));text[2]=char(0x80|(cp&63));}
    else if(cp<=0x10ffff){text[0]=char(0xf0|(cp>>18));text[1]=char(0x80|((cp>>12)&63));text[2]=char(0x80|((cp>>6)&63));text[3]=char(0x80|(cp&63));}
    lv_textarea_add_text(field,text);
}
static void classic_slider(lv_obj_t* slider){
    for(auto part:{LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB})lv_obj_set_style_radius(slider,0,part);
    lv_obj_set_style_bg_color(slider,lv_color_hex(0x12131b),LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,lv_color_hex(0x9cbd4e),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,lv_color_hex(0xb6c1cc),LV_PART_KNOB);
    lv_obj_set_style_border_width(slider,2,LV_PART_KNOB);lv_obj_set_style_border_color(slider,lv_color_hex(0x454c58),LV_PART_KNOB);
}
static uint32_t foreground=0x67ff43,background=0x303044;
static bool skin_color(cJSON* root,const char* name,uint32_t& value){auto* field=cJSON_GetObjectItemCaseSensitive(root,name);if(!cJSON_IsString(field)||strlen(field->valuestring)!=7||field->valuestring[0]!='#')return false;char* end=nullptr;auto v=strtoul(field->valuestring+1,&end,16);if(*end)return false;value=v;return true;}
static lv_color_t green(){return lv_color_hex(foreground);}
// LVGL label setters invalidate and rebuild text even when it is unchanged.
static void text_if_changed(lv_obj_t* label,const char* text){if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}
// Dotted labels mutate their text buffer, so keep their original input separately.
static void text_if_changed(lv_obj_t* label,const std::string& text,std::string& previous){if(previous!=text){lv_label_set_text(label,text.c_str());previous=text;}}
static void background_if_changed(lv_obj_t* obj,uint32_t color){auto value=lv_color_hex(color);lv_style_value_t current{};if(lv_obj_get_local_style_prop(obj,LV_STYLE_BG_COLOR,&current,0)!=LV_STYLE_RES_FOUND||!lv_color_eq(current.color,value))lv_obj_set_style_bg_color(obj,value,0);}
Ui::Ui(Engine& engine,lv_display_t* display):engine_(engine),display_(display),root_(lv_screen_active()){}
void Ui::release_assets(){if(font_)lv_binfont_destroy(font_);if(digital_)lv_binfont_destroy(digital_);font_=digital_=nullptr;if(focus_)lv_group_delete(focus_);focus_=nullptr;}
void Ui::post(Command c){if(!engine_.post(std::move(c)))message("Fila de comandos cheia; tente novamente");}
lv_obj_t* Ui::label(lv_obj_t* parent,const char* text,int x,int y,int width){auto* o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_style_text_color(o,green(),0);if(width){lv_obj_set_width(o,width);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);}return o;}
lv_obj_t* Ui::button(lv_obj_t* parent,const char* text,int x,int y,int width,std::function<void()> callback){
    auto* b=lv_button_create(parent);if(focus_)lv_group_add_obj(focus_,b);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,48);lv_obj_set_style_radius(b,6,0);lv_obj_set_style_bg_color(b,lv_color_hex(0x8997a9),0);lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,lv_color_hex(0xd1d6dc),0);
    auto* l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);lv_obj_set_style_text_color(l,lv_color_hex(0x101522),0);
    auto* fn=new std::function<void()>(std::move(callback));
    lv_obj_add_event_cb(b,[](lv_event_t* e){auto callback=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));callback();},LV_EVENT_CLICKED,fn);
    lv_obj_add_event_cb(b,[](lv_event_t* e){delete static_cast<std::function<void()>*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,fn);
    return b;
}
lv_obj_t* Ui::panel(const char* title){auto* p=lv_obj_create(root_);lv_obj_set_style_radius(p,0,0);lv_obj_set_style_bg_color(p,lv_color_hex(background),0);lv_obj_set_style_border_color(p,lv_color_hex(0x9399aa),0);lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_pad_all(p,10,0);lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);auto* t=label(p,title,0,0);lv_obj_set_style_text_color(t,lv_color_hex(0xd0c6a0),0);return p;}
void Ui::build(){
    focus_=lv_group_create();lv_group_set_default(focus_);
    std::string keymap;if(read_text(engine_.root()+"/config/keymap.json",keymap,16384)){auto* o=cJSON_ParseWithLengthOpts(keymap.c_str(),keymap.size()+1,nullptr,true);auto* v=cJSON_GetObjectItemCaseSensitive(o,"schema_version");auto* bindings=cJSON_GetObjectItemCaseSensitive(o,"bindings");if(cJSON_IsNumber(v)&&v->valueint==1&&cJSON_IsObject(bindings)){for(auto* item=bindings->child;item;item=item->next)if(item->string&&strlen(item->string)<=40&&cJSON_IsString(item)&&strlen(item->valuestring)<=40)keymap_[item->string]=item->valuestring;}cJSON_Delete(o);}
    auto initial=engine_.snapshot();landscape_=initial.settings.landscape;
    std::string manifest;if(read_text(engine_.root()+"/skins/classic/manifest.json",manifest,4096)){auto* o=cJSON_ParseWithLengthOpts(manifest.c_str(),manifest.size()+1,nullptr,true);auto* version=cJSON_GetObjectItemCaseSensitive(o,"schema_version");if(cJSON_IsNumber(version)&&version->valueint==1){skin_color(o,"foreground",foreground);skin_color(o,"background",background);}cJSON_Delete(o);}
    font_=lv_binfont_create(("S:"+engine_.root()+"/skins/classic/fonts/text20.bin").c_str());
    digital_=lv_binfont_create(("S:"+engine_.root()+"/skins/classic/fonts/text32.bin").c_str());
    if(font_)font_->fallback=&lv_font_montserrat_20;
    if(digital_)digital_->fallback=&lv_font_montserrat_32;
    lv_obj_set_style_bg_color(root_,lv_color_hex(0x14141e),0);lv_obj_set_style_text_font(root_,font_?font_:&lv_font_montserrat_20,0);lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    player_=panel("TABNAMP  /  PLAYER");eq_=panel("TABNAMP  /  EQUALIZADOR");list_=panel("TABNAMP  /  PLAYLIST");
    time_=label(player_,"00:00",8,32);lv_obj_set_style_text_font(time_,digital_?digital_:&lv_font_montserrat_32,0);
    lv_obj_add_flag(time_,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(time_,[](lv_event_t* e){static_cast<Ui*>(lv_event_get_user_data(e))->post({Action::Remaining});},LV_EVENT_CLICKED,this);
    title_=label(player_,"Adicione uma faixa",170,38,405);lv_label_set_long_mode(title_,LV_LABEL_LONG_SCROLL_CIRCULAR);
    controls_["details"]=title_;
    lv_obj_add_flag(title_,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(title_,[](lv_event_t* e){static_cast<Ui*>(lv_event_get_user_data(e))->details();},LV_EVENT_CLICKED,this);
    status_=label(player_,"Pronto",8,80,575);
    for(int i=0;i<24;i++){
        bars_[i]=lv_obj_create(player_);lv_obj_set_style_radius(bars_[i],0,0);lv_obj_set_style_border_width(bars_[i],0,0);lv_obj_set_style_bg_color(bars_[i],green(),0);lv_obj_remove_flag(bars_[i],LV_OBJ_FLAG_SCROLLABLE);
        peaks_[i]=lv_obj_create(player_);lv_obj_set_style_bg_color(peaks_[i],lv_color_hex(0xf4db63),0);lv_obj_set_style_border_width(peaks_[i],0,0);lv_obj_set_style_radius(peaks_[i],0,0);
    }
    seek_=lv_slider_create(player_);lv_obj_set_pos(seek_,16,170);lv_obj_set_size(seek_,554,12);lv_slider_set_range(seek_,0,1000);lv_obj_set_ext_click_area(seek_,16);
    lv_obj_add_event_cb(seek_,[](lv_event_t* e){auto* ui=static_cast<Ui*>(lv_event_get_user_data(e));auto s=ui->engine_.snapshot();if(s.media.duration_ms>0)ui->post({Action::Seek,int(s.media.duration_ms*lv_slider_get_value(ui->seek_)/1000-s.position_ms)});},LV_EVENT_RELEASED,this);
    struct Transport{const char* id;const char* text;Action action;int width;};int x=4;
    for(auto c:{Transport{"previous",LV_SYMBOL_PREV,Action::Previous,60},{"play",LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE,Action::PlayPause,96},{"stop",LV_SYMBOL_STOP,Action::Stop,60},{"next",LV_SYMBOL_NEXT,Action::Next,60},{"mute","M",Action::Mute,48},{"shuffle","S",Action::Shuffle,48},{"repeat","R",Action::Repeat,48}}){controls_[c.id]=button(player_,c.text,x,204,c.width,[this,c]{post({c.action});});x+=c.width+8;}
    controls_["rotate"]=button(player_,"Tela",4,264,88,[this]{preferences();});
    controls_["wifi"]=button(player_,"Wi-Fi",100,264,88,[this]{if(wifi)wifi();else message("Rede do computador usada no simulador");});
    volume_=lv_slider_create(player_);lv_obj_set_pos(volume_,218,281);lv_obj_set_size(volume_,350,14);lv_obj_set_ext_click_area(volume_,16);lv_slider_set_range(volume_,0,100);lv_slider_set_value(volume_,initial.settings.volume,LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_,[](lv_event_t* e){auto* ui=static_cast<Ui*>(lv_event_get_user_data(e));ui->post({Action::Volume,int(lv_slider_get_value(ui->volume_))});},LV_EVENT_VALUE_CHANGED,this);
    controls_["eq"]=button(eq_,"ON/OFF",4,30,100,[this]{post({Action::Bypass});});
    controls_["presets"]=button(eq_,"Presets",114,30,110,[this]{prompt("Preset: Flat, Rock, Pop, Jazz, Classical, Bass Boost, Voice",Action::Preset,"Flat");});
    auto* pre=lv_slider_create(eq_);preamp_=pre;lv_obj_set_pos(pre,326,49);lv_obj_set_size(pre,230,12);lv_obj_set_ext_click_area(pre,16);lv_slider_set_range(pre,-12,12);lv_slider_set_value(pre,int(initial.settings.eq.preamp),LV_ANIM_OFF);label(eq_,"PRE",250,38);
    lv_obj_add_event_cb(pre,[](lv_event_t* e){static_cast<Ui*>(lv_event_get_user_data(e))->post({Action::Preamp,int(lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e))))});},LV_EVENT_VALUE_CHANGED,this);
    for(int i=0;i<10;i++){
        eqs_[i]=lv_slider_create(eq_);lv_obj_set_size(eqs_[i],12,172);lv_slider_set_range(eqs_[i],-12,12);lv_slider_set_value(eqs_[i],int(initial.settings.eq.db[i]),LV_ANIM_OFF);lv_obj_set_ext_click_area(eqs_[i],17);lv_obj_set_style_bg_color(eqs_[i],lv_color_hex(0xb7be53),LV_PART_INDICATOR);
        auto* context=new std::pair<Ui*,int>(this,i);
        lv_obj_add_event_cb(eqs_[i],[](lv_event_t* e){auto* p=static_cast<std::pair<Ui*,int>*>(lv_event_get_user_data(e));p->first->post({Action::Band,int(lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)))),p->second});},LV_EVENT_VALUE_CHANGED,context);
        lv_obj_add_event_cb(eqs_[i],[](lv_event_t* e){delete static_cast<std::pair<Ui*,int>*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,context);
        char name[16];snprintf(name,sizeof(name),i>=4?"%.0fk":"%.0f",double(frequencies[i]/(i>=4?1000:1)));eq_names_[i]=label(eq_,name,0,278);
    }
    label(eq_,"-12 / 0 / +12 dB  |  headroom automatico",8,308);
    for(int i=0;i<9;i++){
        rows_[i]=button(list_,"",4,42+i*46,570,[this,i]{select_row(i);});controls_["row"+std::to_string(i)]=rows_[i];lv_obj_set_height(rows_[i],44);lv_obj_set_style_bg_color(rows_[i],lv_color_hex(0x080b08),0);lv_obj_set_style_border_width(rows_[i],0,0);
        auto* l=lv_obj_get_child(rows_[i],0);lv_obj_set_width(l,550);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(l,green(),0);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_LEFT,0);
        auto* context=new std::pair<Ui*,int>(this,i);lv_obj_add_event_cb(rows_[i],[](lv_event_t* e){auto* p=static_cast<std::pair<Ui*,int>*>(lv_event_get_user_data(e));p->first->drag(e,p->second);},LV_EVENT_ALL,context);lv_obj_add_event_cb(rows_[i],[](lv_event_t* e){delete static_cast<std::pair<Ui*,int>*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,context);
    }
    curve_=lv_line_create(eq_);lv_obj_set_pos(curve_,370,0);lv_obj_set_style_line_width(curve_,2,0);lv_obj_set_style_line_color(curve_,lv_color_hex(0xe9d567),0);lv_line_set_points(curve_,curve_points_.data(),curve_points_.size());
    info_=label(list_,"0 faixas",4,0,410);
    controls_["page_prev"]=button(list_,LV_SYMBOL_UP,4,0,48,[this]{size_t n=landscape_?9:6;first_=first_>=n?first_-n:0;});
    controls_["page_next"]=button(list_,LV_SYMBOL_DOWN,60,0,48,[this]{auto s=engine_.snapshot();size_t n=landscape_?9:6;if(first_+n<s.count)first_+=n;});
    controls_["add"]=button(list_,"Arquivos",4,0,185,[this]{browser();});
    controls_["scan"]=button(list_,"Pasta",198,0,96,[this]{prompt("Adicionar pasta (varredura recursiva)",Action::Scan,engine_.root()+"/music");});
    controls_["open"]=button(list_,"Abrir lista",302,0,140,[this]{auto action=[this]{prompt("Abrir M3U/M3U8/PLS local ou HTTP(S)",Action::OpenPlaylist,engine_.root()+"/playlists/fila.m3u8");};if(engine_.snapshot().dirty)confirm("Substituir playlist com alteracoes nao salvas?",action);else action();});
    controls_["save"]=button(list_,"Salvar",450,0,120,[this]{prompt("Salvar playlist como M3U8",Action::SavePlaylist,engine_.root()+"/playlists/fila.m3u8");});
    controls_["remove"]=button(list_,"Remover",4,0,120,[this]{if(multi_select_&&!selections_.empty()){std::string ids;for(int i:selections_)ids+=std::to_string(i)+" ";post({Action::RemoveSelected,0,0,ids});selections_.clear();}else post({Action::Remove,selected_});});
    controls_["up"]=button(list_,"Mover " LV_SYMBOL_UP,132,0,130,[this]{post({Action::MoveUp,selected_});selected_=std::max(0,selected_-1);});
    controls_["down"]=button(list_,"Mover " LV_SYMBOL_DOWN,270,0,130,[this]{post({Action::MoveDown,selected_});selected_++;});
    controls_["more"]=button(list_,"Mais",408,0,100,[this]{menu();});
    controls_["search"]=button(list_,"J",516,0,54,[this]{prompt("Buscar titulo (diferencia maiusculas)",Action::Search,"");});
    for(auto* slider:{volume_,seek_,pre}){classic_slider(slider);lv_group_add_obj(focus_,slider);}
    for(auto* slider:eqs_){classic_slider(slider);lv_group_add_obj(focus_,slider);}
    layout();screen=Screen(0);
}
void Ui::layout(){
    int w=landscape_?1280:720,h=landscape_?720:1280,left=landscape_?624:704;
    lv_obj_set_pos(player_,8,8);lv_obj_set_size(player_,left,342);lv_obj_set_pos(eq_,8,358);lv_obj_set_size(eq_,left,354);
    lv_obj_set_pos(list_,landscape_?640:8,landscape_?8:720);lv_obj_set_size(list_,landscape_?632:704,landscape_?704:552);
    int step=landscape_?56:64;for(int i=0;i<10;i++){lv_obj_set_pos(eqs_[i],24+i*step,94);lv_obj_set_pos(eq_names_[i],10+i*step,278);}
    int bottom=landscape_?674:522;
    int row_count=landscape_?9:6;
    for(int i=0;i<9;i++){if(i<row_count)lv_obj_remove_flag(rows_[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(rows_[i],LV_OBJ_FLAG_HIDDEN);}
    lv_obj_set_pos(info_,4,bottom-162);lv_obj_set_pos(controls_["page_prev"],458,bottom-172);lv_obj_set_pos(controls_["page_next"],516,bottom-172);
    for(auto* name:{"add","scan","open","save"})lv_obj_set_y(controls_[name],bottom-112);
    for(auto* name:{"remove","up","down","more","search"})lv_obj_set_y(controls_[name],bottom-56);
    for(int i=0;i<24;i++){lv_obj_set_pos(bars_[i],8+i*24,157);lv_obj_set_size(bars_[i],18,1);lv_obj_set_pos(peaks_[i],8+i*24,156);lv_obj_set_size(peaks_[i],18,2);}
    if(overlay_){if(entry_||browser_mode_){lv_obj_set_pos(overlay_,16,16);lv_obj_set_size(overlay_,w-32,h-32);if(entry_)lv_obj_set_width(entry_,w-72);if(keyboard_)lv_obj_set_height(keyboard_,std::max(h/3,engine_.snapshot().settings.keyboard_layout==1?280:240));
        if(browser_mode_){lv_obj_set_width(directory_title_,w-90);for(auto* row:directory_rows_){lv_obj_set_width(row,w-100);auto* l=lv_obj_get_child(row,0);lv_obj_set_width(l,w-130);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);}}
        for(uint32_t child=0;child<lv_obj_get_child_count(overlay_);child++){auto* obj=lv_obj_get_child(overlay_,child);if(lv_obj_check_type(obj,&lv_textarea_class))lv_obj_set_width(obj,w-90);}}else{lv_obj_set_size(overlay_,std::min(900,w-32),std::min(int(lv_obj_get_height(overlay_)),h-32));lv_obj_center(overlay_);}}
}
void Ui::orientation(bool value){landscape_=value;if(rotate)rotate(value);layout();post({Action::Landscape,value?1:0});}
void Ui::browser(){
    close_dialog();browser_mode_=true;directory_first_=0;post({Action::Browse,0,0,engine_.root()+"/music"});
    previous_directory_path_.clear();previous_directory_rows_.fill("");
    overlay_=lv_obj_create(root_);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);lv_obj_set_pos(overlay_,16,16);lv_obj_set_size(overlay_,landscape_?1248:688,landscape_?688:1248);
    directory_title_=label(overlay_,"Carregando pasta...",4,4,landscape_?1140:610);
    for(int i=0;i<9;i++)directory_rows_[i]=button(overlay_,"",4,48+i*52,landscape_?1140:610,[this,i]{
        size_t total;std::string path;auto rows=engine_.directory_rows(directory_first_+i,1,total,path);if(rows.empty())return;
        if(rows[0].directory){directory_first_=0;post({Action::Browse,0,0,rows[0].path});}else{auto path=rows[0].path;auto dot=path.find_last_of('.');auto ext=dot==std::string::npos?"":path.substr(dot);if(ext==".m3u"||ext==".m3u8"||ext==".pls"){if(engine_.snapshot().dirty)confirm("Substituir playlist nao salva?",[this,path]{post({Action::OpenPlaylist,0,0,path});});else{post({Action::OpenPlaylist,0,0,path});close_dialog();}}else{post({Action::Add,0,0,path});close_dialog();}}
    });
    layout();
    button(overlay_,"Anterior",4,540,130,[this]{directory_first_=directory_first_>=9?directory_first_-9:0;});
    button(overlay_,"Proxima",144,540,130,[this]{size_t total;std::string path;engine_.directory_rows(0,0,total,path);if(directory_first_+9<total)directory_first_+=9;});
    button(overlay_,"+ Pasta",284,540,150,[this]{size_t total;std::string path;engine_.directory_rows(0,0,total,path);post({Action::Scan,0,0,path});close_dialog();});
    button(overlay_,"SD",4,590,130,[this]{directory_first_=0;post({Action::Browse,0,0,engine_.root().substr(0,engine_.root().find_last_of('/'))});});
    button(overlay_,"USB",144,590,130,[this]{directory_first_=0;post({Action::Browse,0,0,"/usb"});});
    button(overlay_,"Fechar",444,540,130,[this]{close_dialog();});
}
void Ui::menu(){
    close_dialog();overlay_=lv_obj_create(root_);lv_obj_set_size(overlay_,landscape_?900:670,landscape_?650:1200);lv_obj_center(overlay_);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);lv_obj_set_flex_flow(overlay_,LV_FLEX_FLOW_COLUMN);
    button(overlay_,"Adicionar URL",0,0,420,[this]{prompt("URL de audio ou HLS",Action::Add,"https://");});
    for(auto operation:{std::pair<const char*,Action>("Ordenar por titulo",Action::Sort),{"Ordenar por artista",Action::SortArtist},{"Ordenar por album",Action::SortAlbum}})button(overlay_,operation.first,0,0,420,[this,operation]{post({operation.second});close_dialog();});
    controls_["selection"]=button(overlay_,multi_select_?"Sair da selecao multipla":"Selecao multipla",0,0,420,[this]{multi_select_=!multi_select_;selections_.clear();close_dialog();});
    button(overlay_,"Salvar preset de EQ",0,0,420,[this]{prompt("Nome do preset com os dez ganhos atuais",Action::SavePreset,"Meu preset");});
    button(overlay_,"Interromper indexacao",0,0,420,[this]{post({Action::CancelScan});close_dialog();});
    button(overlay_,"Limpar fila",0,0,420,[this]{confirm("Limpar a playlist atual?",[this]{post({Action::Clear});});});
    button(overlay_,"Ajuda",0,0,420,[this]{message("Espaco: play/pausa | Z/B: anterior/proxima\nM: mute | S: shuffle | R: repeat | P: playlist\nArraste uma linha para reordenar. Mais: selecao multipla.\nCtrl+O: playlist | Ctrl+L: URL | Ctrl+S: salvar\nTela: orientacao, brilho, balance e saida\nPrimeiro toque/tecla somente acorda a tela.");});
    button(overlay_,"Fechar",0,0,420,[this]{close_dialog();});
}
void Ui::configure_keyboard(){
    lv_obj_set_style_text_font(keyboard_,font_?font_:&lv_font_montserrat_20,LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard_,6,LV_PART_ITEMS);
    if(engine_.snapshot().settings.keyboard_layout!=1)return;
    static const char* lower[]={"1","2","3","4","5","6","7","8","9","0","\n","q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l","ç","\n","z","x","c","v","b","n","m",".","/","-","\n","á","é","í","ó","ú","ã","õ","â","ê","ô","\n","ABC",LV_SYMBOL_BACKSPACE," ",LV_SYMBOL_NEW_LINE,LV_SYMBOL_OK,""};
    static const char* upper[]={"1","2","3","4","5","6","7","8","9","0","\n","Q","W","E","R","T","Y","U","I","O","P","\n","A","S","D","F","G","H","J","K","L","Ç","\n","Z","X","C","V","B","N","M",".","/","-","\n","Á","É","Í","Ó","Ú","Ã","Õ","Â","Ê","Ô","\n","abc",LV_SYMBOL_BACKSPACE," ",LV_SYMBOL_NEW_LINE,LV_SYMBOL_OK,""};
    static std::array<lv_buttonmatrix_ctrl_t,55> controls=[](){std::array<lv_buttonmatrix_ctrl_t,55> out{};out.fill(lv_buttonmatrix_ctrl_t(1));out[52]=lv_buttonmatrix_ctrl_t(3);return out;}();
    lv_keyboard_set_map(keyboard_,LV_KEYBOARD_MODE_TEXT_LOWER,lower,controls.data());lv_keyboard_set_map(keyboard_,LV_KEYBOARD_MODE_TEXT_UPPER,upper,controls.data());lv_obj_set_height(keyboard_,landscape_?280:420);
}
void Ui::credentials(const std::string& status,std::function<void(std::string,std::string)> connect,std::function<void()> scan){
    close_dialog();overlay_=lv_obj_create(root_);lv_obj_set_pos(overlay_,16,16);lv_obj_set_size(overlay_,landscape_?1248:688,landscape_?688:1248);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);
    label(overlay_,status.c_str(),4,4,landscape_?1150:620);
    entry_=lv_textarea_create(overlay_);lv_obj_set_pos(entry_,4,180);lv_obj_set_size(entry_,landscape_?1100:610,52);lv_textarea_set_one_line(entry_,true);lv_textarea_set_placeholder_text(entry_,"SSID");lv_textarea_set_max_length(entry_,32);
    auto* pass=lv_textarea_create(overlay_);lv_obj_set_pos(pass,4,244);lv_obj_set_size(pass,landscape_?1100:610,52);lv_textarea_set_one_line(pass,true);lv_textarea_set_password_mode(pass,true);lv_textarea_set_placeholder_text(pass,"Senha (armazenada no SD removivel)");lv_textarea_set_max_length(pass,64);
    keyboard_=lv_keyboard_create(overlay_);configure_keyboard();lv_obj_set_width(keyboard_,lv_pct(100));lv_obj_set_height(keyboard_,landscape_?220:400);lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);lv_keyboard_set_textarea(keyboard_,entry_);configure_keyboard();
    for(auto* field:{entry_,pass})lv_obj_add_event_cb(field,[](lv_event_t* e){auto* u=static_cast<Ui*>(lv_event_get_user_data(e));u->entry_=static_cast<lv_obj_t*>(lv_event_get_target(e));lv_keyboard_set_textarea(u->keyboard_,u->entry_);},LV_EVENT_FOCUSED,this);
    auto* ssid=entry_;
    button(overlay_,"Conectar",4,316,140,[this,ssid,pass,connect]{auto s=std::string(lv_textarea_get_text(ssid)),p=std::string(lv_textarea_get_text(pass));connect(s,p);close_dialog();});
    button(overlay_,"Buscar redes",156,316,180,[this,scan]{scan();close_dialog();});button(overlay_,"Fechar",348,316,130,[this]{close_dialog();});
}
void Ui::request_orientation(bool value){pending_orientation_=value?1:0;}
void Ui::select_row(int row){
    if(suppress_click_&&now_<=suppress_click_){suppress_click_=0;return;}playlist_focus_=true;selected_=int(first_)+row;if(selected_>=int(engine_.snapshot().count))return;
    if(multi_select_){if(selections_.count(selected_))selections_.erase(selected_);else selections_.insert(selected_);}else post({Action::Select,selected_});
}
void Ui::drag(lv_event_t* event,int row){
    auto code=lv_event_get_code(event);auto* input=lv_indev_active();if(!input)return;lv_point_t point;lv_indev_get_point(input,&point);
    if(code==LV_EVENT_PRESSED){drag_from_=int(first_)+row;drag_start_=point;dragging_=false;}
    if(code==LV_EVENT_PRESSING&&std::abs(point.y-drag_start_.y)>20)dragging_=true;
    if(code==LV_EVENT_RELEASED&&dragging_){for(int i=0;i<(landscape_?9:6);i++){lv_area_t box;lv_obj_get_coords(rows_[i],&box);if(point.y>=box.y1&&point.y<=box.y2){post({Action::MoveTo,drag_from_,int(first_)+i});selected_=int(first_)+i;break;}}suppress_click_=now_+150;dragging_=false;selections_.clear();}
}
void Ui::preferences(){
    close_dialog();overlay_=lv_obj_create(root_);lv_obj_set_pos(overlay_,16,16);lv_obj_set_size(overlay_,landscape_?1248:688,landscape_?688:1248);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);lv_obj_set_flex_flow(overlay_,LV_FLEX_FLOW_COLUMN);auto s=engine_.snapshot();
    label(overlay_,"TELA E AUDIO",0,0);
    auto* orientation=lv_dropdown_create(overlay_);lv_dropdown_set_options(orientation,"Automatico (IMU)\nTravar paisagem\nTravar retrato");lv_dropdown_set_selected(orientation,s.settings.orientation_mode);lv_obj_set_width(orientation,lv_pct(100));
    lv_obj_add_event_cb(orientation,[](lv_event_t* e){auto* u=static_cast<Ui*>(lv_event_get_user_data(e));int mode=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));u->post({Action::OrientationMode,mode});if(mode)u->orientation(mode==1);},LV_EVENT_VALUE_CHANGED,this);
    auto add_slider=[this](const char* name,int lo,int hi,int value,Action action){label(overlay_,name,0,0,lv_pct(100));auto* slider=lv_slider_create(overlay_);lv_obj_set_size(slider,lv_pct(90),18);lv_obj_set_ext_click_area(slider,14);lv_obj_set_style_margin_top(slider,12,0);lv_obj_set_style_margin_bottom(slider,12,0);lv_slider_set_range(slider,lo,hi);lv_slider_set_value(slider,value,LV_ANIM_OFF);classic_slider(slider);auto* context=new std::pair<Ui*,Action>(this,action);lv_obj_add_event_cb(slider,[](lv_event_t* e){auto* p=static_cast<std::pair<Ui*,Action>*>(lv_event_get_user_data(e));p->first->post({p->second,int(lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e))))});},LV_EVENT_VALUE_CHANGED,context);lv_obj_add_event_cb(slider,[](lv_event_t* e){delete static_cast<std::pair<Ui*,Action>*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,context);};
    add_slider("Brilho",1,100,s.settings.brightness,Action::Brightness);add_slider("Balance: esquerda -100 / centro 0 / direita +100",-100,100,s.settings.balance,Action::Balance);
    auto* keymap=lv_dropdown_create(overlay_);lv_dropdown_set_options(keymap,"Teclado US\nTeclado portugues ABNT2");lv_dropdown_set_selected(keymap,s.settings.keyboard_layout);lv_obj_set_width(keymap,lv_pct(100));lv_obj_add_event_cb(keymap,[](lv_event_t* e){static_cast<Ui*>(lv_event_get_user_data(e))->post({Action::KeyboardLayout,int(lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))))});},LV_EVENT_VALUE_CHANGED,this);
    auto* output=lv_dropdown_create(overlay_);lv_dropdown_set_options(output,"Alto-falante (downmix mono)\nFones (estereo)");lv_dropdown_set_selected(output,s.settings.output);lv_obj_set_width(output,lv_pct(100));lv_obj_add_event_cb(output,[](lv_event_t* e){static_cast<Ui*>(lv_event_get_user_data(e))->post({Action::Output,int(lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))))});},LV_EVENT_VALUE_CHANGED,this);
    button(overlay_,s.settings.visualize?"Desligar visualizador":"Ligar visualizador",0,0,360,[this]{post({Action::Visualize});close_dialog();});button(overlay_,"Tentar montar SD",0,0,360,[this]{post({Action::RetryStorage});close_dialog();});button(overlay_,"Fechar",0,0,180,[this]{close_dialog();});
}
void Ui::close_dialog(){if(overlay_)lv_obj_delete(overlay_);overlay_=entry_=keyboard_=nullptr;browser_mode_=false;dead_key_=0;}
void Ui::prompt(const char* title,Action action,const std::string& value){
    close_dialog();pending_=action;overlay_=lv_obj_create(root_);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);lv_obj_set_style_radius(overlay_,0,0);lv_obj_set_pos(overlay_,16,16);lv_obj_set_size(overlay_,lv_display_get_horizontal_resolution(display_)-32,lv_display_get_vertical_resolution(display_)-32);
    label(overlay_,title,4,4,landscape_?1100:620);
    entry_=lv_textarea_create(overlay_);lv_obj_set_pos(entry_,4,66);lv_obj_set_width(entry_,landscape_?1180:620);lv_obj_set_height(entry_,120);lv_textarea_set_max_length(entry_,2048);lv_textarea_set_text(entry_,value.c_str());
    keyboard_=lv_keyboard_create(overlay_);configure_keyboard();lv_obj_set_width(keyboard_,lv_pct(100));lv_obj_set_height(keyboard_,landscape_?240:380);lv_obj_align(keyboard_,LV_ALIGN_BOTTOM_MID,0,0);lv_keyboard_set_textarea(keyboard_,entry_);configure_keyboard();
    button(overlay_,"Confirmar",4,200,160,[this]{auto text=std::string(lv_textarea_get_text(entry_));post({pending_,0,0,text});close_dialog();});button(overlay_,"Cancelar",180,200,140,[this]{close_dialog();});
}
void Ui::confirm(const char* text,std::function<void()> fn){
    close_dialog();overlay_=lv_obj_create(root_);lv_obj_set_size(overlay_,landscape_?900:650,260);lv_obj_center(overlay_);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);label(overlay_,text,4,4,landscape_?840:590);
    button(overlay_,"Confirmar",4,150,160,[this,fn]{close_dialog();fn();});button(overlay_,"Cancelar",180,150,140,[this]{close_dialog();});
}
void Ui::message(const std::string& text){confirm(text.c_str(),[]{});}
void Ui::details(){
    auto s=engine_.snapshot();close_dialog();overlay_=lv_obj_create(root_);lv_obj_set_size(overlay_,landscape_?1100:688,landscape_?660:1248);lv_obj_center(overlay_);lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x242439),0);lv_obj_set_flex_flow(overlay_,LV_FLEX_FLOW_COLUMN);
    std::string text=s.media.title+"\nArtista: "+s.media.artist+"\nAlbum: "+s.media.album+"\nArtista do album: "+s.media.album_artist+"\nFaixa/disco: "+s.media.track+" / "+s.media.disc+"\nData: "+s.media.date+" | Genero: "+s.media.genre+"\n"+s.media.codec+" | "+std::to_string(s.media.rate)+" Hz | "+std::to_string(s.media.bitrate/1000)+" kbps | "+std::to_string(s.media.channels)+" canais\nSaturacoes pos-EQ: "+std::to_string(s.clips)+" | Underruns: "+std::to_string(s.underruns);
    auto* caption=label(overlay_,text.c_str(),0,0,lv_pct(100));lv_label_set_long_mode(caption,LV_LABEL_LONG_WRAP);
    if(!s.media.artwork.empty()){auto* image=lv_image_create(overlay_);lv_image_set_src(image,("S:"+s.media.artwork).c_str());lv_obj_set_size(image,240,240);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);}
    button(overlay_,"Fechar",0,0,180,[this]{close_dialog();});
}
bool Ui::input(uint8_t token,bool down,uint64_t now){bool before=screen.on;bool pass=screen.input(token,down,now);if(!before&&screen.on){engine_.set_visual(engine_.snapshot().settings.visualize);lv_display_enable_invalidation(display_,true);lv_obj_invalidate(root_);if(backlight)backlight(engine_.snapshot().settings.brightness);lv_label_set_long_mode(title_,LV_LABEL_LONG_DOT);lv_label_set_long_mode(title_,LV_LABEL_LONG_SCROLL_CIRCULAR);}return pass;}
bool Ui::key(int key,bool ctrl,bool shift,bool down,uint8_t token,uint64_t now){
    if(!input(token,down,now))return false;
    if(!down)return false;
    if(key==0x110100){caps_lock_=!caps_lock_;return false;}
    if(key>=MediaPlayPause&&key<=MediaUnknown){
        if(key==MediaVolumeUp||key==MediaVolumeDown)post({Action::Volume,engine_.snapshot().settings.volume+(key==MediaVolumeUp?2:-2)});
        else if(key!=MediaUnknown){Action actions[]={Action::PlayPause,Action::Play,Action::Pause,Action::Stop,Action::Next,Action::Previous,Action::Mute};post({actions[key-MediaPlayPause]});}
        return false;
    }
    if(overlay_){if(key=='\t'){if(shift)lv_group_focus_prev(focus_);else lv_group_focus_next(focus_);return false;}if(key==27){close_dialog();return false;}
        if(entry_){
            if(key>=0x110001&&key<=0x110005){dead_key_=key;return false;}
            if(dead_key_&&key>=32&&key<=0xff){const char* plain="aeiouAEIOU";const char* match=strchr(plain,char(key));static const uint32_t accents[5][10]={{0xe1,0xe9,0xed,0xf3,0xfa,0xc1,0xc9,0xcd,0xd3,0xda},{0xe0,0xe8,0xec,0xf2,0xf9,0xc0,0xc8,0xcc,0xd2,0xd9},{0xe2,0xea,0xee,0xf4,0xfb,0xc2,0xca,0xce,0xd4,0xdb},{0xe3,0,0,0xf5,0,0xc3,0,0,0xd5,0},{0xe4,0xeb,0xef,0xf6,0xfc,0xc4,0xcb,0xcf,0xd6,0xdc}};if(match&&accents[dead_key_-0x110001][match-plain])key=accents[dead_key_-0x110001][match-plain];else if(dead_key_==0x110004&&(key=='n'||key=='N'))key=key=='n'?0xf1:0xd1;else if(key==' ')key=dead_key_==0x110001?0xb4:dead_key_==0x110002?'`':dead_key_==0x110003?'^':dead_key_==0x110004?'~':0xa8;else insert_character(entry_,dead_key_==0x110001?0xb4:dead_key_==0x110002?'`':dead_key_==0x110003?'^':dead_key_==0x110004?'~':0xa8);dead_key_=0;}
            if(key==8||key==127)lv_textarea_delete_char(entry_);else if(key>=32&&key<=0x10ffff&&!ctrl)insert_character(entry_,key);}
        return false;}
    std::string name;
    if(key==' ')name="Space";else if(key==LV_KEY_LEFT)name="Left";else if(key==LV_KEY_RIGHT)name="Right";else if(key==LV_KEY_UP)name="Up";else if(key==LV_KEY_DOWN)name="Down";else if(key==127)name="Delete";else if(key=='\r'||key=='\n')name="Enter";else if(key=='\t')name="Tab";else if(key==27)name="Esc";else if(key>='a'&&key<='z')name=char(key-'a'+'A');else if(key>='A'&&key<='Z')name=char(key);
    if(shift&&name=="Tab")name="Shift+"+name;
    if(ctrl)name="Ctrl+"+name;
    auto found=keymap_.find(name);if(found==keymap_.end())return true;auto action=found->second;
    if(action=="open")lv_obj_send_event(controls_["open"],LV_EVENT_CLICKED,nullptr);
    else if(action=="url")prompt("Adicionar URL de audio ou playlist",Action::Add,"https://");
    else if(action=="save")post({Action::SavePlaylist});
    else if(action=="search")lv_obj_send_event(controls_["search"],LV_EVENT_CLICKED,nullptr);
    else if(action=="focus_next")lv_group_focus_next(focus_);
    else if(action=="focus_previous")lv_group_focus_prev(focus_);
    else if(action=="playlist"){playlist_focus_=true;lv_group_focus_obj(rows_[0]);}
    else if(action=="equalizer")lv_group_focus_obj(eqs_[0]);
    else if(action=="confirm"){auto* focused=lv_group_get_focused(focus_);if(focused)lv_obj_send_event(focused,LV_EVENT_CLICKED,nullptr);else post({Action::Select,selected_});}
    else if(action=="remove")post({Action::Remove,selected_});
    else if(action=="seek_back")post({Action::Seek,-5000});
    else if(action=="seek_forward")post({Action::Seek,5000});
    else if(action=="volume_up"||action=="volume_down"){
        bool up=action=="volume_up";
        if(playlist_focus_){selected_=std::clamp(selected_+(up?-1:1),0,std::max(0,int(engine_.snapshot().count)-1));first_=selected_/(landscape_?9:6)*(landscape_?9:6);}
        else post({Action::Volume,engine_.snapshot().settings.volume+(up?2:-2)});
    }else{
        const std::map<std::string,Action> actions={{"play_pause",Action::PlayPause},{"previous",Action::Previous},{"play",Action::Play},{"pause",Action::Pause},{"stop",Action::Stop},{"next",Action::Next},{"mute",Action::Mute},{"shuffle",Action::Shuffle},{"repeat",Action::Repeat}};
        auto entry=actions.find(action);if(entry!=actions.end())post({entry->second});
    }
    return false;
}
void Ui::refresh(uint64_t now){
    now_=now;screen.tick(now);if(was_on_&&!screen.on){engine_.set_visual(false);if(backlight)backlight(0);previous_brightness_=-1;lv_anim_delete_all();lv_display_enable_invalidation(display_,false);}was_on_=screen.on;if(!screen.on)return;
    if(browser_mode_){size_t total;std::string path;auto items=engine_.directory_rows(directory_first_,9,total,path);text_if_changed(directory_title_,path,previous_directory_path_);for(int i=0;i<9;i++){auto* l=lv_obj_get_child(directory_rows_[i],0);std::string text;if(size_t(i)<items.size())text=std::string(items[i].directory?LV_SYMBOL_DIRECTORY:LV_SYMBOL_AUDIO)+" "+items[i].name;text_if_changed(l,text,previous_directory_rows_[i]);}}
    auto s=engine_.snapshot();if(s.settings.orientation_mode&&s.settings.landscape!=landscape_)orientation(s.settings.landscape);if(pending_orientation_>=0&&s.settings.orientation_mode==0&&bool(pending_orientation_)!=landscape_)orientation(bool(pending_orientation_));if(backlight&&previous_brightness_!=s.settings.brightness){backlight(s.settings.brightness);previous_brightness_=s.settings.brightness;}auto title=s.media.title.empty()?"Adicione uma faixa":s.media.artist.empty()?s.media.title:s.media.artist+" - "+s.media.title;
    if(title!=previous_title_){lv_label_set_text(title_,title.c_str());previous_title_=title;}
    auto status=s.message.empty()?std::string(state_name(s.state))+" | "+s.media.codec+(s.media.live?" | LIVE":""):s.message;
    if(status!=previous_status_){lv_label_set_text(status_,status.c_str());previous_status_=status;}
    int64_t shown=s.settings.remaining&&s.media.duration_ms>=0?std::max<int64_t>(0,s.media.duration_ms-s.position_ms):s.position_ms;
    char text[160];snprintf(text,sizeof(text),"%s%02u:%02u",s.settings.remaining&&s.media.duration_ms>=0?"-":"",unsigned(shown/60000),unsigned(shown/1000%60));text_if_changed(time_,text,previous_time_);
    if(!lv_obj_has_state(volume_,LV_STATE_PRESSED))lv_slider_set_value(volume_,s.settings.volume,LV_ANIM_OFF);
    if(s.media.seekable&&s.media.duration_ms>0){lv_obj_remove_state(seek_,LV_STATE_DISABLED);if(!lv_obj_has_state(seek_,LV_STATE_PRESSED))lv_slider_set_value(seek_,int(s.position_ms*1000/s.media.duration_ms),LV_ANIM_OFF);}else lv_obj_add_state(seek_,LV_STATE_DISABLED);
    if(!lv_obj_has_state(preamp_,LV_STATE_PRESSED))lv_slider_set_value(preamp_,int(s.settings.eq.preamp),LV_ANIM_OFF);
    text_if_changed(lv_obj_get_child(controls_["eq"],0),s.settings.eq.enabled?"EQ: ON":"EQ: OFF");
    background_if_changed(controls_["shuffle"],s.settings.shuffle?0x86b95a:0x8997a9);background_if_changed(controls_["repeat"],s.settings.repeat?0x86b95a:0x8997a9);
    for(int i=0;i<10;i++){if(frequencies[i]>=(s.media.rate?s.media.rate:48000)*.45f)lv_obj_add_state(eqs_[i],LV_STATE_DISABLED);else lv_obj_remove_state(eqs_[i],LV_STATE_DISABLED);}
    uint32_t curve_rate=s.media.rate?s.media.rate:48000;
    if(curve_rate_!=curve_rate||curve_settings_.db!=s.settings.eq.db||curve_settings_.preamp!=s.settings.eq.preamp||curve_settings_.enabled!=s.settings.eq.enabled){Equalizer response;response.configure(curve_rate,s.settings.eq);for(size_t i=0;i<curve_points_.size();i++){float hz=40*std::pow(std::min(20000.f,curve_rate*.45f)/40,float(i)/(curve_points_.size()-1));curve_points_[i].x=i*210.f/(curve_points_.size()-1);curve_points_[i].y=std::clamp(12-response.response_db(hz,curve_rate)*.5f,0.f,24.f);}lv_line_set_points(curve_,curve_points_.data(),curve_points_.size());curve_settings_=s.settings.eq;curve_rate_=curve_rate;}
    for(int i=0;i<10;i++)if(!lv_obj_has_state(eqs_[i],LV_STATE_PRESSED))lv_slider_set_value(eqs_[i],int(s.settings.eq.db[i]),LV_ANIM_OFF);
    if(first_>=s.count)first_=0;
    auto list=engine_.rows(first_,9);
    for(int i=0;i<9;i++){auto* l=lv_obj_get_child(rows_[i],0);std::string row;if(size_t(i)<list.size())row=std::string(int(first_+i)==s.index?">":" ")+" "+std::to_string(first_+i+1)+". "+list[i].title;text_if_changed(l,row,previous_rows_[i]);background_if_changed(rows_[i],(int(first_+i)==selected_||selections_.count(int(first_+i)))?0x172e8c:0x080b08);}
    snprintf(text,sizeof(text),"%u faixas%s | %u:%02u%s %s",unsigned(s.count),s.dirty?" *":"",unsigned(s.known_duration_ms/60000),unsigned(s.known_duration_ms/1000%60),s.unknown_durations?" +?":"",s.scan_active?"INDEXANDO":s.settings.mute?"MUTE":"");text_if_changed(info_,text,previous_info_);
    // Audio analysis and pointer polling keep their own cadence. Redraw the
    // visualizer at 20 Hz, only where quantized pixel geometry actually changes.
    auto invalidate_spectrum=[this]{lv_area_t area;lv_obj_get_content_coords(player_,&area);area.x1+=8;area.x2=area.x1+23*24+17;area.y1+=109;area.y2=area.y1+49;lv_obj_invalidate_area(player_,&area);};
    if(lv_obj_has_flag(bars_[0],LV_OBJ_FLAG_HIDDEN)==s.settings.visualize){
        invalidate_spectrum();
        for(int i=0;i<24;i++){if(s.settings.visualize){lv_obj_remove_flag(bars_[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(peaks_[i],LV_OBJ_FLAG_HIDDEN);}else{lv_obj_add_flag(bars_[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(peaks_[i],LV_OBJ_FLAG_HIDDEN);}}
    }
    if(s.settings.visualize&&!overlay_&&now>=next_spectrum_){
        next_spectrum_=now+50;std::array<int,24> heights,peak_y;bool changed=false;
        for(int i=0;i<24;i++){heights[i]=std::max(1,int(s.spectrum[i]*48));peak_y[i]=157-int(s.peaks[i]*48);changed|=lv_obj_get_style_height(bars_[i],0)!=heights[i]||lv_obj_get_style_y(bars_[i],0)!=157-heights[i]||lv_obj_get_style_y(peaks_[i],0)!=peak_y[i];}
        if(changed){
            // Pre-register one region containing every old/new bar and peak.
            // Otherwise their individual invalidations overflow LVGL's damage
            // list and promote this small animation to a full-screen redraw.
            invalidate_spectrum();
            for(int i=0;i<24;i++){int h=heights[i];lv_obj_set_y(bars_[i],157-h);lv_obj_set_height(bars_[i],h);background_if_changed(bars_[i],h>40?0xff5b42:h>30?0xf2cc4c:0x65e545);lv_obj_set_y(peaks_[i],peak_y[i]);}
        }
    }
}
lv_obj_t* Ui::control(const std::string& name)const{auto it=controls_.find(name);return it==controls_.end()?nullptr:it->second;}
}
