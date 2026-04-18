#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

//
// Event handlers
//

lv_obj_t* tick_value_change_obj;

//
// Screens
//

void create_screen_bootingscreen()
{
    lv_obj_t* obj = lv_obj_create(0);
    objects.bootingscreen = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 1280, 720);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Use a solid black background (disable gradient) to avoid any
     * perceived flash or color shift during boot. */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t* parent_obj = obj;
        {
            // booting title — larger and centered
            lv_obj_t* obj = lv_label_create(parent_obj);
            objects.booting_title = obj;
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Rally Box");
            lv_obj_align(obj, LV_ALIGN_CENTER, 0, -20);
        }
        {
            // booting label — centered below title
            lv_obj_t* obj = lv_label_create(parent_obj);
            objects.booting_label = obj;
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Booting");
            lv_obj_align(obj, LV_ALIGN_CENTER, 0, 10);
        }
        {
            // booting spinner — center to the right of the label
            lv_obj_t* obj = lv_spinner_create(parent_obj);
            objects.booting_spinner = obj;
            lv_obj_set_size(obj, 20, 26);
            lv_obj_set_style_arc_width(obj, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(obj, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            /* Place spinner close to the booting label */
            lv_obj_align_to(obj, objects.booting_label, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
        }
    }

    tick_screen_bootingscreen();
}

void tick_screen_bootingscreen()
{
}

void create_screen_dashboard()
{
    lv_obj_t* obj = lv_obj_create(0);
    objects.dashboard = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 1280, 720);
    {
        lv_obj_t* parent_obj = obj;
        {
            lv_obj_t* obj = lv_tabview_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 1280, 720);
            lv_tabview_set_tab_bar_position(obj, LV_DIR_LEFT);
            lv_tabview_set_tab_bar_size(obj, 80);
            lv_tabview_set_active(obj, 1, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffe57373), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t* parent_obj = obj;
                {
                    lv_obj_t* obj = lv_tabview_get_tab_bar(parent_obj);
                    objects.obj1 = obj;
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff424242), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffe9e9e9), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_RIGHT, LV_PART_ITEMS | LV_STATE_CHECKED);
                }
                {
                    lv_obj_t* obj = lv_tabview_get_content(parent_obj);
                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    {
                        lv_obj_t* parent_obj = obj;
                        {
                            // menu
                            lv_obj_t* obj = lv_tabview_add_tab(lv_obj_get_parent(parent_obj), "Menu");
                            objects.menu = obj;
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            {
                                lv_obj_t* parent_obj = obj;
                                {
                                    // subtitle dashbaord_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.subtitle_dashbaord_1 = obj;
                                    lv_obj_set_pos(obj, 11, 63);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "System Dashboard");
                                }
                                {
                                    // dashboard title_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.dashboard_title_1 = obj;
                                    lv_obj_set_pos(obj, 11, 11);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Rally Box");
                                }
                                {
                                    lv_obj_t* obj = lv_obj_create(parent_obj);
                                    objects.obj2 = obj;
                                    lv_obj_set_pos(obj, 11, 97);
                                    lv_obj_set_size(obj, 389, 156);
                                    lv_obj_set_style_outline_color(obj, lv_color_hex(0xfff8f4f4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_outline_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    lv_obj_t* obj = lv_obj_create(parent_obj);
                                    objects.obj3 = obj;
                                    lv_obj_set_pos(obj, 11, 273);
                                    lv_obj_set_size(obj, 389, 156);
                                    lv_obj_set_style_outline_color(obj, lv_color_hex(0xfff8f4f4), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_outline_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // sd card icon 2_2
                                    lv_obj_t* obj = lv_image_create(parent_obj);
                                    objects.sd_card_icon_2_2 = obj;
                                    lv_obj_set_pos(obj, -13, 290);
                                    lv_obj_set_size(obj, 125, 139);
                                    lv_image_set_src(obj, &img_sdcard);
                                    lv_image_set_scale(obj, 40);
                                    lv_obj_set_style_image_recolor_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_image_recolor(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // sd card title 2_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.sd_card_title_2_1 = obj;
                                    lv_obj_set_pos(obj, 88, 314);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Storage 2");
                                }
                                {
                                    // readWrite label 2_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.read_write_label_2_1 = obj;
                                    lv_obj_set_pos(obj, 86, 375);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff0ad757), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Writing...");
                                }
                                {
                                    // active checkbox sd card 2_1
                                    lv_obj_t* obj = lv_checkbox_create(parent_obj);
                                    objects.active_checkbox_sd_card_2_1 = obj;
                                    lv_obj_set_pos(obj, 258, 314);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_checkbox_set_text(obj, "Active");
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
                                    lv_obj_add_state(obj, LV_STATE_CHECKED);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xfff7efef), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // sdcard label 2_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.sdcard_label_2_1 = obj;
                                    lv_obj_set_pos(obj, 88, 348);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "SD CARD");
                                }
                                {
                                    // active checkbox sd card 1_1
                                    lv_obj_t* obj = lv_checkbox_create(parent_obj);
                                    objects.active_checkbox_sd_card_1_1 = obj;
                                    lv_obj_set_pos(obj, 258, 126);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_checkbox_set_text(obj, "Active");
                                    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
                                    lv_obj_add_state(obj, LV_STATE_CHECKED);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xfff7efef), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // readWrite label 1_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.read_write_label_1_1 = obj;
                                    lv_obj_set_pos(obj, 86, 187);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff0ad757), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Reading...");
                                }
                                {
                                    // sdcard label 1_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.sdcard_label_1_1 = obj;
                                    lv_obj_set_pos(obj, 88, 160);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "SD CARD");
                                }
                                {
                                    // sdcard storage 1_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.sdcard_storage_1_1 = obj;
                                    lv_obj_set_pos(obj, 88, 126);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Storage 1");
                                }
                                {
                                    // sd card icon 2_3
                                    lv_obj_t* obj = lv_image_create(parent_obj);
                                    objects.sd_card_icon_2_3 = obj;
                                    lv_obj_set_pos(obj, -13, 102);
                                    lv_obj_set_size(obj, 125, 139);
                                    lv_image_set_src(obj, &img_sdcard);
                                    lv_image_set_scale(obj, 40);
                                    lv_obj_set_style_image_recolor_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_image_recolor(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // dashboard uptime_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.dashboard_uptime_1 = obj;
                                    lv_obj_set_pos(obj, 438, 15);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Uptime");
                                }
                                {
                                    // system status top bar title_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.system_status_top_bar_title_1 = obj;
                                    lv_obj_set_pos(obj, 603, 18);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "SYSTEM STATUS");
                                }
                                {
                                    // wifi connection
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.wifi_connection = obj;
                                    lv_obj_set_pos(obj, 604, 41);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff00c11d), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Connected");
                                }
                                {
                                    // system load status_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.system_load_status_1 = obj;
                                    lv_obj_set_pos(obj, 1031, 19);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff0ad757), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Stable");
                                }
                                {
                                    // dashbaord wifi icon_1
                                    lv_obj_t* obj = lv_image_create(parent_obj);
                                    objects.dashbaord_wifi_icon_1 = obj;
                                    lv_obj_set_pos(obj, 535, 11);
                                    lv_obj_set_size(obj, 68, 57);
                                    lv_image_set_src(obj, &img_wifi_icon);
                                    lv_image_set_scale(obj, 20);
                                }
                                {
                                    // system load title_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.system_load_title_1 = obj;
                                    lv_obj_set_pos(obj, 826, 15);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "System Load:");
                                }
                                {
                                    // title_4
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.title_4 = obj;
                                    lv_obj_set_pos(obj, 439, 42);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "00:00:01");
                                }
                                {
                                    // error title_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.error_title_1 = obj;
                                    lv_obj_set_pos(obj, 826, 49);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Errors           : ");
                                }
                                {
                                    // error status_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.error_status_1 = obj;
                                    lv_obj_set_pos(obj, 1031, 50);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "0");
                                }
                                {
                                    // format sd card2
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.format_sd_card2 = obj;
                                    lv_obj_set_pos(obj, 674, 216);
                                    lv_obj_set_size(obj, 120, 50);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            // format sdcard 2 text label
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.format_sdcard_2_text_label = obj;
                                            lv_obj_set_pos(obj, 7, 0);
                                            lv_obj_set_size(obj, 125, LV_SIZE_CONTENT);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Fmt SD Card 2");
                                        }
                                    }
                                }
                                {
                                    // format sd card1
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.format_sd_card1 = obj;
                                    lv_obj_set_pos(obj, 520, 216);
                                    lv_obj_set_size(obj, 120, 50);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            // format sdcard 1 text label_1
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.format_sdcard_1_text_label_1 = obj;
                                            lv_obj_set_pos(obj, 7, 0);
                                            lv_obj_set_size(obj, 125, LV_SIZE_CONTENT);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Fmt SD Card 1");
                                        }
                                    }
                                }
                                {
                                    // test sd card1 mount_2
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.test_sd_card1_mount_2 = obj;
                                    lv_obj_set_pos(obj, 669, 124);
                                    lv_obj_set_size(obj, 120, 50);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            // test sdcard 2 text label
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.test_sdcard_2_text_label = obj;
                                            lv_obj_set_pos(obj, 5, 0);
                                            lv_obj_set_size(obj, 120, LV_SIZE_CONTENT);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Test SD Card 2");
                                        }
                                    }
                                }
                                {
                                    // test sd card1 mount
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.test_sd_card1_mount = obj;
                                    lv_obj_set_pos(obj, 520, 122);
                                    lv_obj_set_size(obj, 120, 50);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            // test sdcard 1 text label
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.test_sdcard_1_text_label = obj;
                                            lv_obj_set_pos(obj, 5, 0);
                                            lv_obj_set_size(obj, 120, LV_SIZE_CONTENT);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Test SD Card 1");
                                        }
                                    }
                                }
                            }
                        }
                        {
                            // wifi page
                            lv_obj_t* obj = lv_tabview_add_tab(lv_obj_get_parent(parent_obj), "Wi-Fi");
                            objects.wifi_page = obj;
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            /* Avoid bright yellow press highlight on page background; keep dark
                             * background even while pressed to prevent abrupt color flash. */
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            {
                                lv_obj_t* parent_obj = obj;
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.obj4 = obj;
                                    lv_obj_set_pos(obj, 532, 429);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Connecting....");
                                }
                                {
                                    // WIFI CONENCT BUTTON_1
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.wifi_conenct_button_1 = obj;
                                    lv_obj_set_pos(obj, 520, 370);
                                    lv_obj_set_size(obj, 120, 46);
                                    lv_obj_add_event_cb(obj, action_connect_wifi, LV_EVENT_CLICKED, (void*)0);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff00e5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff0077ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_grad_stop(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            lv_obj_set_pos(obj, 0, 0);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "CONNECT");
                                        }
                                    }
                                }
                                {
                                    // input box_1
                                    lv_obj_t* obj = lv_obj_create(parent_obj);
                                    objects.input_box_1 = obj;
                                    lv_obj_set_pos(obj, 11, 85);
                                    lv_obj_set_size(obj, 1134, 264);
                                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0f1a2e), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_row(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_pad_column(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_outline_pad(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_arc_rounded(obj, true, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            // wifi password_1
                                            lv_obj_t* obj = lv_textarea_create(parent_obj);
                                            objects.wifi_password_1 = obj;
                                            lv_obj_set_pos(obj, 0, 167);
                                            lv_obj_set_size(obj, LV_PCT(100), 70);
                                            lv_textarea_set_max_length(obj, 128);
                                            lv_textarea_set_placeholder_text(obj, "Enter Password");
                                            lv_textarea_set_one_line(obj, false);
                                            lv_textarea_set_password_mode(obj, true);
                                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // wifi ssid_1
                                            lv_obj_t* obj = lv_textarea_create(parent_obj);
                                            objects.wifi_ssid_1 = obj;
                                            lv_obj_set_pos(obj, 1, 49);
                                            lv_obj_set_size(obj, LV_PCT(100), 70);
                                            lv_textarea_set_max_length(obj, 128);
                                            lv_textarea_set_placeholder_text(obj, "Enter WiFi Name");
                                            lv_textarea_set_one_line(obj, false);
                                            lv_textarea_set_password_mode(obj, false);
                                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                        }
                                        {
                                            // SSID_1
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.ssid_1 = obj;
                                            lv_obj_set_pos(obj, 1, 13);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffb0bec5), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "SSID");
                                        }
                                        {
                                            // wifi-subtitle_1
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            objects.wifi_subtitle_1 = obj;
                                            lv_obj_set_pos(obj, 1, 132);
                                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                            lv_obj_set_style_text_color(obj, lv_color_hex(0xffb0bec5), LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "PASSWORD");
                                        }
                                    }
                                }
                                {
                                    // ui keyboard_1
                                    lv_obj_t* obj = lv_keyboard_create(parent_obj);
                                    objects.ui_keyboard_1 = obj;
                                    lv_obj_set_pos(obj, 1, 1153);
                                    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(40));
                                    lv_obj_add_event_cb(obj, action_password_ssid_keyboad, LV_EVENT_FOCUSED, (void*)1);
                                    lv_obj_add_event_cb(obj, action_password_ssid_keyboad, LV_EVENT_PRESSED, (void*)2);
                                    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
                                    lv_obj_set_style_align(obj, LV_ALIGN_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                {
                                    // titile wifi screen_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.titile_wifi_screen_1 = obj;
                                    lv_obj_set_pos(obj, 9, 53);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "CONNECT TO WIFI");
                                }
                                {
                                    // brand name wifi screen_1
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.brand_name_wifi_screen_1 = obj;
                                    lv_obj_set_pos(obj, 2, 1);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Rally Box");
                                }
                                {
                                    // wifi icon wifi screen_1
                                    lv_obj_t* obj = lv_image_create(parent_obj);
                                    objects.wifi_icon_wifi_screen_1 = obj;
                                    lv_obj_set_pos(obj, 225, 0);
                                    lv_obj_set_size(obj, 87, 85);
                                    lv_image_set_src(obj, &img_wifi_icon);
                                    lv_image_set_scale(obj, 20);
                                }
                            }
                        }
                        {
                            // BlueTooth page
                            lv_obj_t* obj = lv_tabview_add_tab(lv_obj_get_parent(parent_obj), "BlueTooth");
                            objects.bluetooth_page = obj;
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            {
                                lv_obj_t* parent_obj = obj;
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.bluetooth_title = obj;
                                    lv_obj_set_pos(obj, 12, 10);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Rally Box");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 12, 63);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_letter_space(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "BlueTooth");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.bluetooth_status_label = obj;
                                    lv_obj_set_pos(obj, 12, 110);
                                    lv_obj_set_size(obj, 1120, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff9fadb8), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
                                    lv_label_set_text(obj, "Bluetooth status will appear here");
                                }
                                {
                                    lv_obj_t* obj = lv_dropdown_create(parent_obj);
                                    objects.bluetooth_device_dropdown = obj;
                                    lv_obj_set_pos(obj, 12, 170);
                                    lv_obj_set_size(obj, 860, 54);
                                    lv_dropdown_set_options(obj, "No devices found");
                                }
                                {
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.bluetooth_refresh_button = obj;
                                    lv_obj_set_pos(obj, 888, 170);
                                    lv_obj_set_size(obj, 120, 54);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Refresh");
                                        }
                                    }
                                }
                                {
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.bluetooth_connect_button = obj;
                                    lv_obj_set_pos(obj, 1024, 170);
                                    lv_obj_set_size(obj, 120, 54);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "Connect");
                                        }
                                    }
                                }
                            }
                        }
                        {
                            // GNSS page
                            lv_obj_t* obj = lv_tabview_add_tab(lv_obj_get_parent(parent_obj), "GNSS");
                            objects.gnss_page = obj;
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0a0a0a), LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
                            {
                                lv_obj_t* parent_obj = obj;
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.gnss_title = obj;
                                    lv_obj_set_pos(obj, 12, 10);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffeaeaea), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "GNSS UART Monitor");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    objects.gnss_status_label = obj;
                                    lv_obj_set_pos(obj, 12, 58);
                                    lv_obj_set_size(obj, 1120, LV_SIZE_CONTENT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xff9fadb8), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Idle. Enter UART settings, then press START");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 12, 98);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffb0bec5), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "Baud");
                                }
                                {
                                    lv_obj_t* obj = lv_textarea_create(parent_obj);
                                    objects.gnss_baud_input = obj;
                                    lv_obj_set_pos(obj, 12, 122);
                                    lv_obj_set_size(obj, 180, 44);
                                    lv_textarea_set_one_line(obj, true);
                                    lv_textarea_set_text(obj, "115200");
                                    lv_textarea_set_placeholder_text(obj, "baud");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 210, 98);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffb0bec5), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "TX GPIO");
                                }
                                {
                                    lv_obj_t* obj = lv_textarea_create(parent_obj);
                                    objects.gnss_tx_input = obj;
                                    lv_obj_set_pos(obj, 210, 122);
                                    lv_obj_set_size(obj, 140, 44);
                                    lv_textarea_set_one_line(obj, true);
                                    lv_textarea_set_text(obj, "51");
                                    lv_textarea_set_placeholder_text(obj, "tx");
                                }
                                {
                                    lv_obj_t* obj = lv_label_create(parent_obj);
                                    lv_obj_set_pos(obj, 368, 98);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffb0bec5), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_label_set_text(obj, "RX GPIO");
                                }
                                {
                                    lv_obj_t* obj = lv_textarea_create(parent_obj);
                                    objects.gnss_rx_input = obj;
                                    lv_obj_set_pos(obj, 368, 122);
                                    lv_obj_set_size(obj, 140, 44);
                                    lv_textarea_set_one_line(obj, true);
                                    lv_textarea_set_text(obj, "50");
                                    lv_textarea_set_placeholder_text(obj, "rx");
                                }
                                {
                                    lv_obj_t* obj = lv_button_create(parent_obj);
                                    objects.gnss_start_stop_button = obj;
                                    lv_obj_set_pos(obj, 526, 122);
                                    lv_obj_set_size(obj, 160, 44);
                                    {
                                        lv_obj_t* parent_obj = obj;
                                        {
                                            lv_obj_t* obj = lv_label_create(parent_obj);
                                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                            lv_label_set_text(obj, "START");
                                        }
                                    }
                                }
                                {
                                    lv_obj_t* obj = lv_textarea_create(parent_obj);
                                    objects.gnss_dump_panel = obj;
                                    lv_obj_set_pos(obj, 12, 186);
                                    lv_obj_set_size(obj, 1132, 500);
                                    lv_textarea_set_one_line(obj, false);
                                    lv_textarea_set_max_length(obj, 16384);
                                    lv_textarea_set_placeholder_text(obj, "GNSS UART dump will appear here...");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    tick_screen_dashboard();
}

void tick_screen_dashboard()
{
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_bootingscreen,
    tick_screen_dashboard,
};
void tick_screen(int screen_index)
{
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId)
{
    tick_screen_funcs[screenId - 1]();
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens()
{

    // Set default LVGL theme
    lv_display_t* dispp = lv_display_get_default();
    lv_theme_t* theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);

    // Initialize screens
    // Create screens
    create_screen_bootingscreen();
    create_screen_dashboard();
}