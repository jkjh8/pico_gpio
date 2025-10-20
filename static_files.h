#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 임베드된 파일 구조체
typedef struct {
    const char* path;
    const char* content_type;
    const uint8_t* data;
    size_t size;
    size_t original_size;
    bool is_compressed;
} embedded_file_t;

extern const uint8_t favicon_ico_data[];
extern const size_t favicon_ico_size;
extern const size_t favicon_ico_original_size;
extern const bool favicon_ico_is_compressed;
extern const uint8_t icon_svg_data[];
extern const size_t icon_svg_size;
extern const size_t icon_svg_original_size;
extern const bool icon_svg_is_compressed;
extern const uint8_t index_html_data[];
extern const size_t index_html_size;
extern const size_t index_html_original_size;
extern const bool index_html_is_compressed;
extern const uint8_t assets_CommPage_CXTr9LKL_js_data[];
extern const size_t assets_CommPage_CXTr9LKL_js_size;
extern const size_t assets_CommPage_CXTr9LKL_js_original_size;
extern const bool assets_CommPage_CXTr9LKL_js_is_compressed;
extern const uint8_t assets_ErrorNotFound_B11vek2x_js_data[];
extern const size_t assets_ErrorNotFound_B11vek2x_js_size;
extern const size_t assets_ErrorNotFound_B11vek2x_js_original_size;
extern const bool assets_ErrorNotFound_B11vek2x_js_is_compressed;
extern const uint8_t assets_format_M0vUyom3_js_data[];
extern const size_t assets_format_M0vUyom3_js_size;
extern const size_t assets_format_M0vUyom3_js_original_size;
extern const bool assets_format_M0vUyom3_js_is_compressed;
extern const uint8_t assets_GpioPage_Dska8rp__js_data[];
extern const size_t assets_GpioPage_Dska8rp__js_size;
extern const size_t assets_GpioPage_Dska8rp__js_original_size;
extern const bool assets_GpioPage_Dska8rp__js_is_compressed;
extern const uint8_t assets_index_C_OMDwCs_js_data[];
extern const size_t assets_index_C_OMDwCs_js_size;
extern const size_t assets_index_C_OMDwCs_js_original_size;
extern const bool assets_index_C_OMDwCs_js_is_compressed;
extern const uint8_t assets_index_DwWt3dB1_css_data[];
extern const size_t assets_index_DwWt3dB1_css_size;
extern const size_t assets_index_DwWt3dB1_css_original_size;
extern const bool assets_index_DwWt3dB1_css_is_compressed;
extern const uint8_t assets_IndexPage_BNUAGjwB_js_data[];
extern const size_t assets_IndexPage_BNUAGjwB_js_size;
extern const size_t assets_IndexPage_BNUAGjwB_js_original_size;
extern const bool assets_IndexPage_BNUAGjwB_js_is_compressed;
extern const uint8_t assets_MainLayout_D4Rwqm_n_js_data[];
extern const size_t assets_MainLayout_D4Rwqm_n_js_size;
extern const size_t assets_MainLayout_D4Rwqm_n_js_original_size;
extern const bool assets_MainLayout_D4Rwqm_n_js_is_compressed;
extern const uint8_t assets_QSelect_Den2vTL__js_data[];
extern const size_t assets_QSelect_Den2vTL__js_size;
extern const size_t assets_QSelect_Den2vTL__js_original_size;
extern const bool assets_QSelect_Den2vTL__js_is_compressed;
extern const uint8_t assets_selection_C_doQ2rA_js_data[];
extern const size_t assets_selection_C_doQ2rA_js_size;
extern const size_t assets_selection_C_doQ2rA_js_original_size;
extern const bool assets_selection_C_doQ2rA_js_is_compressed;
extern const uint8_t assets_SettingsPage_Cx_yp66t_js_data[];
extern const size_t assets_SettingsPage_Cx_yp66t_js_size;
extern const size_t assets_SettingsPage_Cx_yp66t_js_original_size;
extern const bool assets_SettingsPage_Cx_yp66t_js_is_compressed;
extern const uint8_t assets_use_quasar_CTZkVEQf_js_data[];
extern const size_t assets_use_quasar_CTZkVEQf_js_size;
extern const size_t assets_use_quasar_CTZkVEQf_js_original_size;
extern const bool assets_use_quasar_CTZkVEQf_js_is_compressed;
extern const uint8_t assets_useApi_ByS7HFL8_js_data[];
extern const size_t assets_useApi_ByS7HFL8_js_size;
extern const size_t assets_useApi_ByS7HFL8_js_original_size;
extern const bool assets_useApi_ByS7HFL8_js_is_compressed;
extern const uint8_t assets_useRules_DNY5au38_js_data[];
extern const size_t assets_useRules_DNY5au38_js_size;
extern const size_t assets_useRules_DNY5au38_js_original_size;
extern const bool assets_useRules_DNY5au38_js_is_compressed;

// 임베드된 파일 테이블
extern const embedded_file_t embedded_files[];
extern const size_t embedded_files_count;

// 함수 선언
const char* get_content_type(const char* file_path);
const char* get_embedded_file_with_content_type(const char* path, size_t* file_size, bool* is_compressed, size_t* original_size, const char** content_type);

#endif // STATIC_FILES_H
